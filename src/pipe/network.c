/*
 *******************************************************************
 *
 * Copyright 2016 Intel Corporation All rights reserved.
 *
 *-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 */

#include <safe_lib.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <dps/dbg.h>
#include <dps/dps.h>
#include <dps/private/network.h>
#include <dps/private/cbor.h>
#include "../node.h"
#include "../queue.h"

#ifdef _WIN32
#include <netioapi.h>
#define PATH_SEP "\\"
#else
#include <net/if.h>
#include <sys/un.h>
#define PATH_SEP "/"
#endif

/*
 * Debug control for this module
 */
DPS_DEBUG_CONTROL(DPS_DEBUG_ON);

typedef struct _DPS_NetPipeConnection DPS_NetPipeConnection;

#define SIZEOF_HEADER CBOR_SIZEOF(uint32_t) +                   \
    CBOR_SIZEOF_STRING_AND_LENGTH(DPS_NODE_ADDRESS_PATH_MAX)

typedef struct _SendRequest {
    DPS_Queue queue;
    DPS_NetPipeConnection* cn;
    uv_write_t writeReq;
    DPS_NetSendComplete onSendComplete;
    void* appCtx;
    DPS_Status status;
    size_t numBufs;
    uint8_t hdrBuf[SIZEOF_HEADER]; /* pre-allocated buffer for serializing message header */
    uv_buf_t bufs[1];
} SendRequest;

typedef struct _DPS_NetPipeConnection {
    DPS_NetConnection cn;
    DPS_Node* node;
    uv_pipe_t socket;
    DPS_NetEndpoint peerEp;
    int refCount;
    uv_shutdown_t shutdownReq;
    /* Rx side */
    uint8_t hdrBuf[SIZEOF_HEADER]; /* pre-allocated buffer for deserializing message header */
    size_t readLen; /* how much data has already been read */
    DPS_NetRxBuffer* msgBuf;
    /* Tx side */
    uv_connect_t connectReq;
    DPS_Queue sendQueue;
    DPS_Queue sendCompletedQueue;
    uv_idle_t idle;
} DPS_NetPipeConnection;

typedef struct _DPS_NetPipeContext {
    DPS_NetContext ctx;
    uv_pipe_t socket;   /* the listen socket */
    DPS_Node* node;
    DPS_OnReceive receiveCB;
} DPS_NetPipeContext;

DPS_NodeAddress* DPS_NetPipeGetListenAddress(DPS_NodeAddress* addr, DPS_NetContext* netCtx);
void DPS_NetPipeStop(DPS_NetContext* netCtx);
DPS_Status DPS_NetPipeSend(DPS_Node* node, void* appCtx, DPS_NetEndpoint* ep, uv_buf_t* bufs, size_t numBufs,
                           DPS_NetSendComplete sendCompleteCB);
void DPS_NetPipeConnectionIncRef(DPS_NetConnection* cn);
void DPS_NetPipeConnectionDecRef(DPS_NetConnection* cn);
static void Shutdown(DPS_NetPipeConnection* cn);

static void ConnectionIncRef(DPS_NetPipeConnection* cn)
{
    if (cn) {
        DPS_DBGTRACE();
        ++cn->refCount;
    }
}

static void ConnectionDecRef(DPS_NetPipeConnection* cn)
{
    if (cn) {
        DPS_DBGTRACE();
        assert(cn->refCount > 0);
        if (--cn->refCount == 0) {
            Shutdown(cn);
        }
    }
}

static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
{
    DPS_NetPipeConnection* cn = (DPS_NetPipeConnection*)handle->data;

    if (cn->msgBuf) {
        buf->len = DPS_RxBufferAvail(&cn->msgBuf->rx);
        buf->base = (char*)cn->msgBuf->rx.rxPos;
    } else {
        buf->len = (uint32_t)(sizeof(cn->hdrBuf) - cn->readLen);
        buf->base = (char*)(cn->hdrBuf + cn->readLen);
    }
}

static void ListenSocketClosed(uv_handle_t* handle)
{
    DPS_DBGPRINT("Closed handle %p\n", handle);
    free(handle->data);
}

static void CancelPendingSends(DPS_NetPipeConnection* cn)
{
    while (!DPS_QueueEmpty(&cn->sendQueue)) {
        SendRequest* req = (SendRequest*)DPS_QueueFront(&cn->sendQueue);
        DPS_QueueRemove(&req->queue);
        DPS_DBGPRINT("Canceling SendRequest=%p\n", req);
        req->status = DPS_ERR_NETWORK;
        DPS_QueuePushBack(&cn->sendCompletedQueue, &req->queue);
    }
}

static void SendCompleted(DPS_NetPipeConnection* cn)
{
    while (!DPS_QueueEmpty(&cn->sendCompletedQueue)) {
        SendRequest* req = (SendRequest*)DPS_QueueFront(&cn->sendCompletedQueue);
        DPS_QueueRemove(&req->queue);
        req->onSendComplete(req->cn->node, req->appCtx, &req->cn->peerEp, req->bufs + 1,
                            req->numBufs - 1, req->status);
        free(req);
    }
}

static void SendCompletedTask(uv_idle_t* idle)
{
    DPS_NetPipeConnection* cn = idle->data;
    SendCompleted(cn);
    uv_idle_stop(idle);
}

static void FreeConnection(DPS_NetPipeConnection* cn)
{
    /*
     * Free memory for any pending sends
     */
    CancelPendingSends(cn);
    SendCompleted(cn);
    DPS_NetRxBufferDecRef(cn->msgBuf);
    cn->msgBuf = NULL;
    free(cn);
}

static void IdleClosed(uv_handle_t* handle)
{
    DPS_NetPipeConnection* cn = handle->data;
    FreeConnection(cn);
}

static void StreamClosed(uv_handle_t* handle)
{
    DPS_NetPipeConnection* cn = (DPS_NetPipeConnection*)handle->data;

    DPS_DBGPRINT("Closed stream handle %p\n", handle);
    if (!uv_is_closing((uv_handle_t*)&cn->idle)) {
        uv_close((uv_handle_t*)&cn->idle, IdleClosed);
    }
}

static void OnShutdownComplete(uv_shutdown_t* req, int status)
{
    DPS_NetPipeConnection* cn = (DPS_NetPipeConnection*)req->data;

    DPS_DBGPRINT("Shutdown complete handle %p\n", req->handle);
    if (!uv_is_closing((uv_handle_t*)req->handle)) {
        req->handle->data = cn;
        uv_close((uv_handle_t*)req->handle, StreamClosed);
    }
}

static void Shutdown(DPS_NetPipeConnection* cn)
{
    if (!cn->shutdownReq.data) {
        int r;
        assert(cn->refCount == 0);
        uv_read_stop((uv_stream_t*)&cn->socket);
        cn->shutdownReq.data = cn;
        r = uv_shutdown(&cn->shutdownReq, (uv_stream_t*)&cn->socket, OnShutdownComplete);
        if (r) {
            DPS_ERRPRINT("Shutdown failed %s - closing\n", uv_err_name(r));
            if (!uv_is_closing((uv_handle_t*)&cn->socket)) {
                cn->socket.data = cn;
                uv_close((uv_handle_t*)&cn->socket, StreamClosed);
            }
        }
    }
}

static void OnData(uv_stream_t* socket, ssize_t nread, const uv_buf_t* buf)
{
    DPS_Status ret = DPS_OK;
    DPS_NetPipeConnection* cn = (DPS_NetPipeConnection*)socket->data;
    DPS_NetPipeContext* netCtx = (DPS_NetPipeContext*)cn->node->netCtx;
    DPS_RxBuffer hdrBuf;

    DPS_DBGTRACE();

    DPS_RxBufferClear(&hdrBuf);
    /*
     * netCtx will be null if we are shutting down
     */
    if (!netCtx) {
        return;
    }
    /*
     * libuv does this...
     */
    if (nread == 0) {
        return;
    }
    if (nread < 0) {
        ret = nread == UV_EOF ? DPS_ERR_EOF : DPS_ERR_NETWORK;
        goto Done;
    }
    assert(socket == (uv_stream_t*)&cn->socket);

    while (nread && (ret == DPS_OK)) {
        /*
         * Parse out the message length
         */
        if (!cn->msgBuf) {
            uint32_t msgLen;
            uint8_t* pos;
            char* path;
            size_t size;
            cn->readLen += nread;
            DPS_RxBufferInit(&hdrBuf, cn->hdrBuf, cn->readLen);
            ret = CBOR_DecodeUint32(&hdrBuf, &msgLen);
            if (ret == DPS_ERR_EOD) {
                /*
                 * Keep reading if we don't have enough data to parse the length
                 */
                return;
            } else if (ret != DPS_OK) {
                goto Done;
            }
            pos = hdrBuf.rxPos;
            ret = CBOR_DecodeString(&hdrBuf, &path, &size);
            if (ret == DPS_ERR_EOD) {
                return;
            } else if (ret != DPS_OK) {
                goto Done;
            }
            if (memcpy_s(cn->peerEp.addr.u.path, DPS_NODE_ADDRESS_PATH_MAX, path, size) != EOK) {
                ret = DPS_ERR_INVALID;
                goto Done;
            }
            cn->peerEp.addr.type = DPS_PIPE;
            msgLen -= hdrBuf.rxPos - pos;
            cn->msgBuf = DPS_CreateNetRxBuffer(msgLen);
            if (cn->msgBuf) {
                /*
                 * Copy message bytes if any
                 */
                size = (msgLen < DPS_RxBufferAvail(&hdrBuf)) ? msgLen : DPS_RxBufferAvail(&hdrBuf);
                memcpy(cn->msgBuf->rx.rxPos, hdrBuf.rxPos, size);
                cn->msgBuf->rx.rxPos += size;
                hdrBuf.rxPos += size;
            } else {
                ret = DPS_ERR_RESOURCES;
            }
        } else {
            cn->msgBuf->rx.rxPos += nread;
        }
        if (cn->msgBuf) {
            /*
             * Keep reading if we don't have a complete message
             */
            if (DPS_RxBufferAvail(&cn->msgBuf->rx)) {
                return;
            }
            DPS_DBGPRINT("Received message of length %zd\n", cn->msgBuf->rx.eod - cn->msgBuf->rx.base);
            cn->msgBuf->rx.rxPos = cn->msgBuf->rx.base;
        }
    Done:
        /*
         * Issue callback if we've received enough to have a valid
         * peer endpoint.
         */
        if (cn->peerEp.addr.type == DPS_PIPE) {
            netCtx->receiveCB(cn->node, &cn->peerEp, ret, cn->msgBuf);
        }
        DPS_NetRxBufferDecRef(cn->msgBuf);
        cn->msgBuf = NULL;
        cn->readLen = 0;
        /*
         * Stop reading if we got an error
         */
        if (ret != DPS_OK) {
            uv_read_stop(socket);
        }
        /*
         * Shutdown the connection if the upper layer didn't IncRef to keep it alive
         */
        if (cn->refCount == 0) {
            Shutdown(cn);
        }

        /*
         * If there's leftover data in the hdrBuf, we'll loop back and consume it
         */
        nread = DPS_RxBufferAvail(&hdrBuf);
        if (nread) {
            memmove(hdrBuf.base, hdrBuf.rxPos, nread);
        }
    }
}

static void OnIncomingConnection(uv_stream_t* stream, int status)
{
    int ret;
    DPS_NetPipeContext* netCtx = (DPS_NetPipeContext*)stream->data;
    DPS_NetPipeConnection* cn;

    DPS_DBGTRACE();

    if (netCtx->node->state != DPS_NODE_RUNNING) {
        return;
    }
    if (status < 0) {
        DPS_ERRPRINT("OnIncomingConnection %s\n", uv_strerror(status));
        goto FailConnection;
    }

    cn = calloc(1, sizeof(DPS_NetPipeConnection));
    if (!cn) {
        DPS_ERRPRINT("OnIncomingConnection malloc failed\n");
        goto FailConnection;
    }
    cn->cn.incRef = DPS_NetPipeConnectionIncRef;
    cn->cn.decRef = DPS_NetPipeConnectionDecRef;
    ret = uv_pipe_init(stream->loop, &cn->socket, 0);
    if (ret) {
        DPS_ERRPRINT("uv_pipe_init error=%s\n", uv_err_name(ret));
        free(cn);
        goto FailConnection;
    }
    cn->node = netCtx->node;
    cn->socket.data = cn;
    cn->peerEp.cn = (DPS_NetConnection*)cn;
    DPS_QueueInit(&cn->sendQueue);
    DPS_QueueInit(&cn->sendCompletedQueue);
    uv_idle_init(stream->loop, &cn->idle);
    cn->idle.data = cn;

    ret = uv_accept(stream, (uv_stream_t*)&cn->socket);
    if (ret) {
        DPS_ERRPRINT("OnIncomingConnection accept %s\n", uv_strerror(ret));
        goto FailConnection;
    }
    ret = uv_read_start((uv_stream_t*)&cn->socket, AllocBuffer, OnData);
    if (ret) {
        DPS_ERRPRINT("OnIncomingConnection read start %s\n", uv_strerror(ret));
        Shutdown(cn);
    }
    return;

FailConnection:

    uv_close((uv_handle_t*)stream, NULL);
}

/*
 * The scope id must be set for link local addresses.
 *
 * TODO - how to handle case where there are multiple interfaces with link local addresses.
 */
static int GetScopeId(struct sockaddr_in6* addr)
{
    if (IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) {
        static int linkLocalScope = 0;
        if (!linkLocalScope) {
            uv_interface_address_t* ifsAddrs = NULL;
            int numIfs = 0;
            int i;
            uv_interface_addresses(&ifsAddrs, &numIfs);
            for (i = 0; i < numIfs; ++i) {
                uv_interface_address_t* ifn = &ifsAddrs[i];
                if (ifn->is_internal || (ifn->address.address6.sin6_family != AF_INET6)) {
                    continue;
                }
                if (IN6_IS_ADDR_LINKLOCAL(&ifn->address.address6.sin6_addr)) {
                    linkLocalScope = if_nametoindex(ifn->name);
                    break;
                }
            }
            uv_free_interface_addresses(ifsAddrs, numIfs);
        }
        return linkLocalScope;
    }
    return 0;
}

#define LISTEN_BACKLOG  2

DPS_NetContext* DPS_NetPipeStart(DPS_Node* node, const DPS_NodeAddress* addr, DPS_OnReceive cb)
{
    char path[DPS_NODE_ADDRESS_PATH_MAX] = { 0 };
    DPS_NetPipeContext* netCtx = NULL;
    DPS_UUID uuid;
    int ret;

    netCtx = calloc(1, sizeof(DPS_NetPipeContext));
    if (!netCtx) {
        return NULL;
    }
    netCtx->ctx.getListenAddress = DPS_NetPipeGetListenAddress;
    netCtx->ctx.stop = DPS_NetPipeStop;
    netCtx->ctx.send = DPS_NetPipeSend;
    ret = uv_pipe_init(node->loop, &netCtx->socket, 0);
    if (ret) {
        DPS_ERRPRINT("uv_pipe_init error=%s\n", uv_err_name(ret));
        free(netCtx);
        return NULL;
    }
    netCtx->socket.data = netCtx;
    netCtx->node = node;
    netCtx->receiveCB = cb;
    if (addr && addr->u.path[0]) {
        ret = uv_pipe_bind(&netCtx->socket, addr->u.path);
    } else {
        /*
         * Create a unique temporary path
         */
        do {
#ifdef _WIN32
            ret = strcat_s(path, sizeof(path), "\\\\.\\pipe");
#else
            size_t len = sizeof(path);
            ret = uv_os_tmpdir(path, &len);
#endif
            if (ret) {
                goto ErrorExit;
            }
            DPS_GenerateUUID(&uuid);
            ret = strcat_s(path, sizeof(path), PATH_SEP);
            if (ret != EOK) {
                goto ErrorExit;
            }
            ret = strcat_s(path, sizeof(path), DPS_UUIDToString(&uuid));
            if (ret != EOK) {
                goto ErrorExit;
            }
            ret = uv_pipe_bind(&netCtx->socket, path);
        } while (ret == EADDRINUSE);
    }
    if (ret) {
        goto ErrorExit;
    }
    ret = uv_listen((uv_stream_t*)&netCtx->socket, LISTEN_BACKLOG, OnIncomingConnection);
    if (ret) {
        goto ErrorExit;
    }
    DPS_DBGPRINT("Listening on socket %p\n", &netCtx->socket);
#ifndef _WIN32
    /*
     * libuv does not ignore SIGPIPE on Linux
     */
    signal(SIGPIPE, SIG_IGN);
#endif
    return (DPS_NetContext*)netCtx;

ErrorExit:
    DPS_ERRPRINT("Failed to start net netCtx: error=%s\n", uv_err_name(ret));
    uv_close((uv_handle_t*)&netCtx->socket, ListenSocketClosed);
    return NULL;
}

DPS_NodeAddress* DPS_NetPipeGetListenAddress(DPS_NodeAddress* addr, DPS_NetContext* ctx)
{
    DPS_NetPipeContext* netCtx = (DPS_NetPipeContext*)ctx;
    size_t len;

    DPS_DBGTRACEA("netCtx=%p\n", netCtx);

    memzero_s(addr, sizeof(DPS_NodeAddress));
    if (!netCtx) {
        return addr;
    }
    addr->type = DPS_PIPE;
    len = sizeof(addr->u.path);
    if (uv_pipe_getsockname(&netCtx->socket, addr->u.path, &len)) {
        return addr;
    }
#ifdef _WIN32
    if (!strncmp(addr->u.path, "\\\\?", 3)) {
        addr->u.path[2] = '.';
    }
#endif
    DPS_DBGPRINT("Listener address = %s\n", addr->u.path);
    return addr;
}

void DPS_NetPipeStop(DPS_NetContext* ctx)
{
    DPS_NetPipeContext* netCtx = (DPS_NetPipeContext*)ctx;

    if (netCtx) {
        netCtx->socket.data = netCtx;
        uv_close((uv_handle_t*)&netCtx->socket, ListenSocketClosed);
    }
}

static void OnWriteComplete(uv_write_t* writeReq, int status)
{
    SendRequest* req = (SendRequest*)writeReq->data;
    DPS_NetPipeConnection* cn = req->cn;

    if (status) {
        DPS_DBGPRINT("OnWriteComplete status=%s\n", uv_err_name(status));
        req->status = DPS_ERR_NETWORK;
    } else {
        req->status = DPS_OK;
    }
    DPS_QueuePushBack(&cn->sendCompletedQueue, &req->queue);
    SendCompleted(cn);
    ConnectionDecRef(cn);
}

static void DoSend(DPS_NetPipeConnection* cn)
{
    while (!DPS_QueueEmpty(&cn->sendQueue)) {
        SendRequest* req = (SendRequest*)DPS_QueueFront(&cn->sendQueue);
        DPS_QueueRemove(&req->queue);
        req->writeReq.data = req;
        int r = uv_write(&req->writeReq, (uv_stream_t*)&cn->socket, req->bufs, (uint32_t)req->numBufs,
                         OnWriteComplete);
        if (r == 0) {
            ConnectionIncRef(cn);
        } else {
            DPS_ERRPRINT("DoSend - write failed: %s\n", uv_err_name(r));
            req->status = DPS_ERR_NETWORK;
            DPS_QueuePushBack(&cn->sendCompletedQueue, &req->queue);
        }
    }
}

static void OnOutgoingConnection(uv_connect_t *req, int status)
{
    DPS_NetPipeConnection* cn = (DPS_NetPipeConnection*)req->data;
    if (status == 0) {
        cn->socket.data = cn;
        status = uv_read_start((uv_stream_t*)&cn->socket, AllocBuffer, OnData);
    }
    if (status == 0) {
        DoSend(cn);
    } else {
        DPS_ERRPRINT("OnOutgoingConnection - connect %s failed: %s\n",
                     DPS_NodeAddrToString(&cn->peerEp.addr), uv_err_name(status));
        assert(!DPS_QueueEmpty(&cn->sendQueue));
        CancelPendingSends(cn);
    }
    SendCompleted(cn);
}

DPS_Status DPS_NetPipeSend(DPS_Node* node, void* appCtx, DPS_NetEndpoint* ep, uv_buf_t* bufs,
                           size_t numBufs, DPS_NetSendComplete sendCompleteCB)
{
    DPS_Status ret;
    DPS_TxBuffer hdrBuf;
    SendRequest* req;
    DPS_NetPipeConnection* cn = NULL;
    uv_handle_t* socket = NULL;
    int r;
    size_t i;
    size_t len = 0;

    len += CBOR_SIZEOF_STRING(node->addr.u.path);
    for (i = 0; i < numBufs; ++i) {
        len += bufs[i].len;
    }
    if (len > UINT32_MAX) {
        return DPS_ERR_RESOURCES;
    }

    DPS_DBGPRINT("DPS_NetSend total %zu bytes to %s\n", len, DPS_NodeAddrToString(&ep->addr));

    req = malloc(sizeof(SendRequest) + numBufs * sizeof(uv_buf_t));
    if (!req) {
        return DPS_ERR_RESOURCES;
    }
    /*
     * Write message header
     */
    DPS_TxBufferInit(&hdrBuf, req->hdrBuf, sizeof(req->hdrBuf));
    ret = CBOR_EncodeUint32(&hdrBuf, len);
    if (ret != DPS_OK) {
        goto ErrExit;
    }
    /*
     * Include our listening address, otherwise all clients will look
     * the same to the server since the socket peername is unnamed
     */
    ret = CBOR_EncodeString(&hdrBuf, node->addr.u.path);
    if (ret != DPS_OK) {
        goto ErrExit;
    }
    req->bufs[0].base = (char*)req->hdrBuf;
    req->bufs[0].len = DPS_TxBufferUsed(&hdrBuf);
    /*
     * Copy other uvbufs into the send request
     */
    for (i = 0; i < numBufs; ++i) {
        req->bufs[i + 1] = bufs[i];
    }
    req->numBufs = numBufs + 1;
    req->onSendComplete = sendCompleteCB;
    req->appCtx = appCtx;
    /*
     * See if we already have a connection
     */
    if (ep->cn) {
        cn = (DPS_NetPipeConnection*)ep->cn;
        req->cn = cn;
        /*
         * If there are pending sends the connection is not up yet
         */
        if (!DPS_QueueEmpty(&cn->sendQueue)) {
            DPS_QueuePushBack(&cn->sendQueue, &req->queue);
            return DPS_OK;
        }
        DPS_QueuePushBack(&cn->sendQueue, &req->queue);
        DoSend(cn);
        uv_idle_start(&cn->idle, SendCompletedTask);
        return DPS_OK;
    }

    cn = calloc(1, sizeof(DPS_NetPipeConnection));
    if (!cn) {
        goto ErrExit;
    }
    cn->cn.incRef = DPS_NetPipeConnectionIncRef;
    cn->cn.decRef = DPS_NetPipeConnectionDecRef;
    r = uv_pipe_init(node->loop, &cn->socket, 0);
    if (r) {
        goto ErrExit;
    }
    cn->peerEp.addr = ep->addr;
    cn->node = node;
    DPS_QueueInit(&cn->sendQueue);
    DPS_QueueInit(&cn->sendCompletedQueue);
    uv_idle_init(node->loop, &cn->idle);
    cn->idle.data = cn;
    socket = (uv_handle_t*)&cn->socket;

    cn->connectReq.data = cn;
    uv_pipe_connect(&cn->connectReq, &cn->socket, ep->addr.u.path, OnOutgoingConnection);
    cn->peerEp.cn = (DPS_NetConnection*)cn;
    DPS_QueuePushBack(&cn->sendQueue, &req->queue);
    req->cn = cn;
    ConnectionIncRef(cn);
    ep->cn = (DPS_NetConnection*)cn;
    return DPS_OK;

ErrExit:

    if (req) {
        free(req);
    }
    if (socket) {
        socket->data = cn;
        uv_close(socket, StreamClosed);
    } else {
        if (cn) {
            free(cn);
        }
    }
    ep->cn = NULL;
    return DPS_ERR_NETWORK;
}

void DPS_NetPipeConnectionIncRef(DPS_NetConnection* cn)
{
    ConnectionIncRef((DPS_NetPipeConnection*)cn);
}

void DPS_NetPipeConnectionDecRef(DPS_NetConnection* cn)
{
    ConnectionDecRef((DPS_NetPipeConnection*)cn);
}

DPS_NetTransport DPS_NetPipeTransport = {
    DPS_PIPE,
    DPS_NetPipeStart
};
