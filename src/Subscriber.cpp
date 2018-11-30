// -*- mode: C++; c-basic-offset: 2; -*-
// Copyright 2018 Intel Corporation All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <dps/dps.h>
#include <dps/private/loop.h>
#include <dps/Publisher.hpp>
#include <dps/Subscriber.hpp>

using namespace dps;

class ReliableSubscriber::Publisher : public ReliablePublisher
{
public:
  ReliableSubscriber * subscriber_;
  RemoteReliablePublisher * remote_;

  Publisher(const QoS & qos, ReliableSubscriber * subscriber, RemoteReliablePublisher * remote);
  virtual ~Publisher() {}
  virtual DPS_Status initialize(DPS_Node * node, const std::vector<std::string> & topics);
  virtual void ackHandler(DPS_Publication * pub, const AckHeader & header, RxStream & rxBuf);
  virtual TxStream heartbeat();
};

Subscriber::Subscriber(const QoS & qos, SubscriberListener * listener)
: qos_(qos), listener_(listener), sub_(nullptr), cache_(new Cache<RxStream>(qos.depth))
{
  DPS_GenerateUUID(&uuid_);
}

Subscriber::Subscriber(const QoS & qos, SubscriberListener * listener, const DPS_UUID * uuid)
: qos_(qos), listener_(listener), sub_(nullptr), cache_(new Cache<RxStream>(qos.depth))
{
  memcpy(&uuid_, uuid, sizeof(DPS_UUID));
}

Subscriber::~Subscriber()
{
  for (auto it = remote_.begin(); it != remote_.end(); ++it) {
    delete it->second.pub_;
  }
  delete cache_;
}

DPS_Status Subscriber::initialize(DPS_Node * node, const std::vector<std::string> & topics)
{
  std::lock_guard<std::recursive_mutex> lock(internalMutex_);
  std::vector<const char *> ctopics;
  DPS_Status ret = DPS_OK;
  std::transform(topics.begin(), topics.end(), std::back_inserter(ctopics),
                 [](const std::string & s) { return s.c_str(); });
  sub_ = DPS_CreateSubscription(node, ctopics.data(), ctopics.size());
  if (!sub_) {
    ret = DPS_ERR_RESOURCES;
    goto Exit;
  }
  ret = DPS_SetSubscriptionData(sub_, this);
  if (ret != DPS_OK) {
    goto Exit;
  }
  ret = DPS_Subscribe(sub_, pubHandler_);
  if (ret != DPS_OK) {
    goto Exit;
  }
 Exit:
  if (ret != DPS_OK) {
    close();
  }
  return ret;
}

DPS_Status Subscriber::close()
{
  std::lock_guard<std::recursive_mutex> lock(internalMutex_);
  for (auto it = remote_.begin(); it != remote_.end(); ++it) {
    if (it->second.pub_) {
      DPS_Status ret = it->second.pub_->close();
      if (ret != DPS_OK) {
        DPS_ERRPRINT("close publisher failed: %s\n", DPS_ErrTxt(ret));
      }
    }
  }
  DPS_Status ret = DPS_DestroySubscription(sub_);
  if (ret != DPS_OK) {
    return ret;
  }
  sub_ = nullptr;
  return ret;
}

const DPS_UUID * Subscriber::uuid() const
{
  return &uuid_;
}

size_t Subscriber::unreadCount()
{
  return cache_->size();
}

bool Subscriber::takeNextData(RxStream & buf, PublicationInfo & info)
{
  std::lock_guard<std::recursive_mutex> lock(internalMutex_);
  if (cache_->empty()) {
    return false;
  }
  takeFromCache(buf, info);
  return true;
}

DPS_Status Subscriber::ack(TxStream && payload, const DPS_UUID * uuid, uint32_t sn)
{
  std::lock_guard<std::recursive_mutex> lock(internalMutex_);
  RemotePublisher & remote = remote_[*uuid];
  DPS_Status ret = remote.initialize(uuid, this);
  if (ret != DPS_OK) {
    return ret;
  }
  TxStream txBuf;
  txBuf << uuid_ << sn << payload;
  return remote.pub_->publish(std::move(txBuf));
}

void Subscriber::dump()
{
  DPS_PRINT("Publication (inbound)\n");
  for (auto it = cache_->begin(); it != cache_->end(); ++it) {
    DPS_PRINT("  %s(%d)\n", DPS_UUIDToString(DPS_PublicationGetUUID(it->pub_.get())), it->sn_);
  }
  DPS_PRINT("Publisher\n");
  for (auto it = remote_.begin(); it != remote_.end(); ++it) {
    DPS_PRINT("  %s\n", DPS_UUIDToString(&it->first));
    for (auto jt = it->second.pub_->cache_->begin(); jt != it->second.pub_->cache_->end(); ++jt) {
      DPS_PRINT("    %s(%d)\n", DPS_UUIDToString(DPS_PublicationGetUUID(jt->pub_.get())), jt->sn_);
    }
  }
}

void Subscriber::pubHandler_(DPS_Subscription * sub, const DPS_Publication * pub,
                             uint8_t * data, size_t dataLen)
{
  Subscriber * subscriber = static_cast<Subscriber *>(DPS_GetSubscriptionData(sub));
  RxStream rxBuf(data, dataLen);
  std::lock_guard<std::recursive_mutex> lock(subscriber->internalMutex_);
  PublicationHeader header;
  rxBuf >> header;
  subscriber->pubHandler(pub, header, rxBuf);
}

void Subscriber::pubHandler(const DPS_Publication * pub, PublicationHeader & header, RxStream & rxBuf)
{
  const DPS_UUID * uuid = DPS_PublicationGetUUID(pub);

  if (header.type_ != QOS_DATA) {
    return;
  }
  DPS_PRINT("DATA %s(%d) [%d,%d]\n", DPS_UUIDToString(uuid), header.sn_, header.range_.first,
            header.range_.second);
  if (cache_->full()) {
    cache_->removeData(cache_->begin());
  }
  addToCache(pub, header, std::move(rxBuf));
}

void Subscriber::addToCache(const DPS_Publication * pub, const PublicationHeader & header, RxStream && buf)
{
  Cache<RxStream>::Data data =
    { Publication(DPS_CopyPublication(pub)), header.sn_, std::move(buf) };
  cache_->addData(std::move(data));
  listener_->onNewPublication(this);
}

void Subscriber::takeFromCache(RxStream & buf, PublicationInfo & info)
{
  Cache<RxStream>::Data data;
  cache_->takeNextData(data);
  memcpy(&info.uuid, DPS_PublicationGetUUID(data.pub_.get()), sizeof(DPS_UUID));
  info.sn = data.sn_;
  buf = std::move(data.buf_);
}

DPS_Status Subscriber::RemotePublisher::initialize(const DPS_UUID * uuid, Subscriber * subscriber)
{
  if (!pub_) {
    pub_ = new Publisher(subscriber->qos_, nullptr);
    std::vector<std::string> topic = { DPS_UUIDToString(uuid) };
    DPS_Status ret = pub_->initialize(DPS_SubscriptionGetNode(subscriber->sub_), topic);
    if (ret != DPS_OK) {
      delete pub_;
      pub_ = nullptr;
      return ret;
    }
  }
  return DPS_OK;
}

ReliableSubscriber::ReliableSubscriber(const QoS & qos, SubscriberListener * listener)
: Subscriber(qos, listener)
{
}

ReliableSubscriber::ReliableSubscriber(const QoS & qos, SubscriberListener * listener, const DPS_UUID * uuid)
: Subscriber(qos, listener, uuid)
{
}

ReliableSubscriber::~ReliableSubscriber()
{
  for (auto it = remote_.begin(); it != remote_.end(); ++it) {
    delete it->second.pub_;
  }
}

DPS_Status ReliableSubscriber::close()
{
  std::lock_guard<std::recursive_mutex> lock(internalMutex_);
  for (auto it = remote_.begin(); it != remote_.end(); ++it) {
    if (it->second.pub_) {
      DPS_Status ret = it->second.pub_->close();
      if (ret != DPS_OK) {
        DPS_ERRPRINT("close publisher failed: %s\n", DPS_ErrTxt(ret));
      }
    }
  }
  return Subscriber::close();
}

DPS_Status ReliableSubscriber::ack(TxStream && payload, const DPS_UUID * uuid, uint32_t sn)
{
  std::lock_guard<std::recursive_mutex> lock(internalMutex_);
  RemoteReliablePublisher & remote = remote_[*uuid];
  DPS_Status ret = remote.initialize(uuid, this);
  if (ret != DPS_OK) {
    return ret;
  }
  TxStream txBuf;
  txBuf << uuid_ << sn << payload;
  return remote.pub_->publish(std::move(txBuf));
}

void ReliableSubscriber::dump()
{
  DPS_PRINT("Publication (inbound)\n");
  for (auto it = cache_->begin(); it != cache_->end(); ++it) {
    DPS_PRINT("  %s(%d)\n", DPS_UUIDToString(DPS_PublicationGetUUID(it->pub_.get())), it->sn_);
  }
  DPS_PRINT("Publisher\n");
  for (auto it = remote_.begin(); it != remote_.end(); ++it) {
    DPS_PRINT("  %s [%d,%d]\n", DPS_UUIDToString(&it->first), it->second.range_.first, it->second.range_.second);
    for (auto jt = it->second.pub_->cache_->begin(); jt != it->second.pub_->cache_->end(); ++jt) {
      DPS_PRINT("    %s(%d)\n", DPS_UUIDToString(DPS_PublicationGetUUID(jt->pub_.get())), jt->sn_);
    }
  }
}

void ReliableSubscriber::pubHandler(const DPS_Publication * pub, PublicationHeader & header, RxStream & rxBuf)
{
  const DPS_UUID * uuid = DPS_PublicationGetUUID(pub);
  RemoteReliablePublisher & remote = remote_[*uuid];

  if (header.type_ == QOS_DATA) {
    DPS_PRINT("DATA %s(%d) [%d,%d]\n", DPS_UUIDToString(uuid), header.sn_, header.range_.first, header.range_.second);
  } else if (header.type_ == QOS_HEARTBEAT) {
    DPS_PRINT("HEARTBEAT %s [%d,%d]\n", DPS_UUIDToString(uuid), header.range_.first, header.range_.second);
  }

  DPS_Status ret = remote.initialize(uuid, this);
  if (ret == DPS_ERR_BUSY) {
    // waiting for registration ack from publisher
    return;
  } else if (ret != DPS_OK) {
    DPS_ERRPRINT("initialize publisher failed: %s\n", DPS_ErrTxt(ret));
    return;
  }

  remote.range_ = header.range_;

  if (header.type_ == QOS_DATA && !remote.received(header.sn_)) {
    // ensure enough space is available in cache to receive
    // missing publications before adding this one
    size_t need = 1;
    for (uint32_t n = header.range_.first; n < header.sn_; ++n) {
      if (!remote.received(n)) {
        ++need;
      }
    }
    if (need <= cache_->avail()) {
      addToCache(pub, header, std::move(rxBuf));
      remote.setReceived(header.sn_, header.range_.first);
    }
  }
  ackPublication(pub);
}

void ReliableSubscriber::ackPublication(const DPS_Publication * pub, const TxStream & payload)
{
  const DPS_UUID* uuid = DPS_PublicationGetUUID(pub);
  SNSet sns = missing(uuid, cache_->avail());
  TxStream buf;
  AckHeader header = { uuid_, sns };
  buf << header << payload;
  DPS_Status ret = DPS_AckPublication(pub, buf.data(), buf.size());
  if (ret != DPS_OK) {
    DPS_ERRPRINT("Ack failed: %s\n", DPS_ErrTxt(ret));
  }
}

void ReliableSubscriber::ackPublication(const DPS_Publication * pub)
{
  TxStream payload(0);
  ackPublication(pub, payload);
}

SNSet ReliableSubscriber::missing(const DPS_UUID * uuid, size_t avail)
{
  RemoteReliablePublisher & remote = remote_[*uuid];
  SNSet & received = remote.received_;
  Range & range = remote.range_;
  SNSet complement;

  // messages prior to range.first are considered lost if not already received
  received.shrink(range.first);

  complement.base_ = received.base_;
  while (received.test(complement.base_) && complement.base_ <= range.second) {
    ++complement.base_;
  }
  for (uint32_t sn = complement.base_; avail && sn <= range.second; ++sn) {
    if (!received.test(sn)) {
      complement.set(sn);
      --avail;
    }
  }
  return complement;
}

DPS_Status ReliableSubscriber::RemoteReliablePublisher::initialize(const DPS_UUID * uuid,
                                                                   ReliableSubscriber * subscriber)
{
  DPS_Status ret = DPS_OK;
  if (!pub_) {
    pub_ = new ReliableSubscriber::Publisher(subscriber->qos_, subscriber, this);
    std::vector<std::string> topic = { DPS_UUIDToString(uuid) };
    DPS_Status ret = pub_->initialize(DPS_SubscriptionGetNode(subscriber->sub_), topic);
    if (ret == DPS_OK) {
      busy_ = true;
    } else {
      delete pub_;
      pub_ = nullptr;
    }
  }
  if (ret == DPS_OK && busy_) {
    ret = DPS_ERR_BUSY;
  }
  return ret;
}

ReliableSubscriber::Publisher::Publisher(const QoS & qos, ReliableSubscriber * subscriber,
                                         RemoteReliablePublisher * remote)
  : ReliablePublisher(qos, nullptr), subscriber_(subscriber), remote_(remote)
{
}

DPS_Status ReliableSubscriber::Publisher::initialize(DPS_Node * node, const std::vector<std::string> & topics)
{
  std::vector<std::string> thisTopic;
  DPS_Status ret;
  int err;
  std::lock_guard<std::recursive_mutex> lock(internalMutex_);
  ret = dps::Publisher::initialize(node, topics, ackHandler_);
  if (ret != DPS_OK) {
    goto Exit;
  }
  err = uv_async_init(DPS_GetLoop(node), &async_, onAsync_);
  if (err) {
    ret = DPS_ERR_FAILURE;
    goto Exit;
  }
  async_.data = this;
  err = uv_timer_init(DPS_GetLoop(node), &timer_);
  if (err) {
    ret = DPS_ERR_FAILURE;
    goto Exit;
  }
  timer_.data = this;
  err = uv_async_send(&async_);
  if (err) {
    ret = DPS_ERR_FAILURE;
    goto Exit;
  }
Exit:
  if (ret != DPS_OK) {
    close();
  }
  return ret;
}

TxStream ReliableSubscriber::Publisher::heartbeat()
{
  if (remote_->busy_) {
    Range range;
    if (cache_->empty()) {
      range = { sn_, sn_ };
    } else {
      range = { cache_->minSN(), cache_->maxSN() };
    }
    PublicationHeader header = { QOS_ADD, range };
    TxStream txBuf;
    txBuf << header << subscriber_->uuid_;
    return txBuf;
  } else {
    return ReliablePublisher::heartbeat();
  }
}

void ReliableSubscriber::Publisher::ackHandler(DPS_Publication * pub, const AckHeader & header,
                                               RxStream & rxBuf)
{
  if (!rxBuf.eof()) {
    Range range;
    rxBuf >> range;
    remote_->busy_ = false;
    remote_->range_ = range;
    // TODO this assumes VOLATILE for now, for TRANSIENT, use range.first - 1
    remote_->setReceived(range.second, range.second);
    heartbeatPolicy_ = HEARTBEAT_UNACKNOWLEDGED;
  }
  ReliablePublisher::ackHandler(pub, header, rxBuf);
}
