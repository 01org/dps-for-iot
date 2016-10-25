#include <assert.h>
#include <string.h>
#include <malloc.h>
#include <uv.h>
#include <dps/dbg.h>
#include <dps/dps.h>
#include <dps/private/network.h>
#include "../node.h"

/*
 * Debug control for this module
 */
DPS_DEBUG_CONTROL(DPS_DEBUG_ON);


typedef struct _WriteRequest {
    DPS_NetConnection* cn;
    uv_write_t writeReq;
    DPS_NetSendComplete onSendComplete;
    struct _WriteRequest* next;
    size_t numBufs;
    uv_buf_t bufs[1];
} WriteRequest;

typedef struct _DPS_NetConnection {
    DPS_Node* node;
    uv_tcp_t socket;
    DPS_NetEndpoint peerEp;
    int refCount;
    uv_shutdown_t shutdownReq;
    /* Rx side */
    uint16_t readLen; /* how much data has already been read */
    uint16_t bufLen;  /* how big the buffer is */
    char* buffer;
    /* Tx side */
    uv_connect_t connectReq;
    WriteRequest* pendingWrites;
} DPS_NetConnection;

struct _DPS_NetContext {
    uv_tcp_t socket;   /* the listen socket */
    DPS_Node* node;
    DPS_OnReceive receiveCB;
};

#define MIN_BUF_ALLOC_SIZE   512
#define MIN_READ_SIZE         16

static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf)
{
    DPS_NetConnection* cn = (DPS_NetConnection*)handle->data;

    if (!cn->buffer) {
        cn->buffer = malloc(MIN_BUF_ALLOC_SIZE);
        if (!cn->buffer) {
            cn->bufLen = 0;
            cn->readLen = 0;
            buf->len = 0;
            buf->base = NULL;
            return;
        }
        cn->bufLen = MIN_BUF_ALLOC_SIZE;
        cn->readLen = 0;
    }
    if (cn->readLen) {
        buf->len = cn->bufLen - cn->readLen;
        buf->base = cn->buffer + cn->readLen;
    } else {
        buf->len = MIN_READ_SIZE;
        buf->base = cn->buffer;
    }
}

static void SocketClosed(uv_handle_t* handle)
{
    DPS_DBGPRINT("Closed handle %p\n", handle);
    free(handle->data);
}

static void StreamClosed(uv_handle_t* handle)
{
    DPS_NetConnection* cn = (DPS_NetConnection*)handle->data;

    DPS_DBGPRINT("Closed stream handle %p\n", handle);
    if (cn->buffer) {
        free(cn->buffer);
    }
    free(cn);
}

static void OnShutdownComplete(uv_shutdown_t* req, int status)
{
    DPS_DBGPRINT("Shutdown complete handle %p\n", req->handle);
    if (!uv_is_closing((uv_handle_t*)req->handle)) {
        req->handle->data = req->data;
        uv_close((uv_handle_t*)req->handle, StreamClosed);
    }
}

static void CancelPendingWrites(DPS_NetConnection* cn)
{
    while (cn->pendingWrites) {
        WriteRequest* wr = cn->pendingWrites;
        cn->pendingWrites = wr->next;
        wr->onSendComplete(cn->node, &cn->peerEp, wr->bufs, wr->numBufs, DPS_ERR_NETWORK);
        free(wr);
    }
}


static void Shutdown(DPS_NetConnection* cn)
{
    assert(cn->refCount == 0);
    uv_read_stop((uv_stream_t*)&cn->socket);
    cn->shutdownReq.data = cn;
    CancelPendingWrites(cn);
    uv_shutdown(&cn->shutdownReq, (uv_stream_t*)&cn->socket, OnShutdownComplete);
}

static void OnData(uv_stream_t* socket, ssize_t nread, const uv_buf_t* buf)
{
    DPS_NetConnection* cn = (DPS_NetConnection*)socket->data;
    DPS_NetContext* netCtx = cn->node->netCtx;
    ssize_t toRead;

    DPS_DBGTRACE();
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
        uv_read_stop(socket);
        netCtx->receiveCB(cn->node, &cn->peerEp, nread == UV_EOF ? DPS_ERR_EOF : DPS_ERR_NETWORK, NULL, 0);
        return;
    }
    assert(socket == (uv_stream_t*)&cn->socket);

    cn->readLen += (uint16_t)nread;
    toRead = netCtx->receiveCB(cn->node, &cn->peerEp, DPS_OK, (uint8_t*)cn->buffer, cn->readLen);
    /*
     * All done if there is nothing to read or the data was bad
     */
    if (toRead <= 0) {
        free(cn->buffer);
        cn->buffer = NULL;
        cn->readLen = 0;
        /*
         * Stop reading if we got an error
         */
        if (toRead < 0) {
            uv_read_stop(socket);
        }
        /*
         * Shutdown the connect if  the upper layer didn't AddRef to keep it alive
         */
        if (cn->refCount == 0) {
            Shutdown(cn);
        }
        return;
    }
    /*
     * Keep reading if we don't have enough data to parse the header
     */
    if (cn->readLen < MIN_READ_SIZE) {
        return;
    }
    /*
     * May need to allocate a larger buffer to complete the read.
     */
    if (cn->bufLen < (toRead + cn->readLen)) {
        cn->buffer = realloc(cn->buffer, cn->bufLen);
    }
    /*
     * Clamp bufLen to the total packet size
     */
    cn->bufLen = toRead + cn->readLen;
}

static void OnIncomingConnection(uv_stream_t* stream, int status)
{
    int ret;
    DPS_NetContext* netCtx = (DPS_NetContext*)stream->data;
    DPS_NetConnection* cn;
    int sz = sizeof(cn->peerEp.addr.inaddr);

    DPS_DBGTRACE();

    if (status < 0) {
        DPS_ERRPRINT("OnIncomingConnection %s\n", uv_strerror(status));
        return;
    }
    cn = calloc(1, sizeof(*cn));
    if (!cn) {
        DPS_ERRPRINT("OnIncomingConnection malloc failed\n");
        return;
    }
    ret = uv_tcp_init(stream->loop, &cn->socket);
    if (ret) {
        DPS_ERRPRINT("uv_tcp_init error=%s\n", uv_err_name(ret));
        free(cn);
        return;
    }

    cn->node = netCtx->node;
    cn->socket.data = cn;
    cn->peerEp.cn = cn;

    ret = uv_accept(stream, (uv_stream_t*)&cn->socket);
    if (ret) {
        DPS_ERRPRINT("OnIncomingConnection accept %s\n", uv_strerror(ret));
        stream->data = cn;
        uv_close((uv_handle_t*)stream, StreamClosed);
        return;
    }
    uv_tcp_getpeername((uv_tcp_t*)&cn->socket, (struct sockaddr*)&cn->peerEp.addr.inaddr, &sz);
    ret = uv_read_start((uv_stream_t*)&cn->socket, AllocBuffer, OnData);
    if (ret) {
        DPS_ERRPRINT("OnIncomingConnection read start %s\n", uv_strerror(ret));
        Shutdown(cn);
    }
}

static int GetLocalScopeId()
{
    static int localScope = 0;

    if (!localScope) {
        uv_interface_address_t* ifsAddrs;
        int numIfs;
        int i;
        uv_interface_addresses(&ifsAddrs, &numIfs);
        for (i = 0; i < numIfs; ++i) {
            uv_interface_address_t* ifn = &ifsAddrs[i];
            if ((ifn->address.address4.sin_family == AF_INET6) && (strcmp(ifn->name, "lo") == 0)) {
                localScope = i;
            }
        }
        uv_free_interface_addresses(ifsAddrs, numIfs);
    }
    return localScope;
}


#define LISTEN_BACKLOG  2

DPS_NetContext* DPS_NetStart(DPS_Node* node, int port, DPS_OnReceive cb)
{
    int ret;
    DPS_NetContext* netCtx;
    struct sockaddr_in6 addr;

    netCtx = calloc(1, sizeof(*netCtx));
    if (!netCtx) {
        return NULL;
    }
    ret = uv_tcp_init(DPS_GetLoop(node), &netCtx->socket);
    if (ret) {
        DPS_ERRPRINT("uv_tcp_init error=%s\n", uv_err_name(ret));
        free(netCtx);
        return NULL;
    }
    netCtx->node = node;
    netCtx->receiveCB = cb;
    ret = uv_ip6_addr("::", port, &addr);
    if (ret) {
        goto ErrorExit;
    }
    ret = uv_tcp_bind(&netCtx->socket, (const struct sockaddr*)&addr, 0);
    if (ret) {
        goto ErrorExit;
    }
    netCtx->socket.data = netCtx;
    ret = uv_listen((uv_stream_t*)&netCtx->socket, LISTEN_BACKLOG, OnIncomingConnection);
    if (ret) {
        goto ErrorExit;
    }
    return netCtx;

ErrorExit:

    DPS_ERRPRINT("Failed to start net netCtx: error=%s\n", uv_err_name(ret));
    netCtx->socket.data = netCtx;
    uv_close((uv_handle_t*)&netCtx->socket, SocketClosed);
    return NULL;
}

uint16_t DPS_NetGetListenerPort(DPS_NetContext* netCtx)
{
    struct sockaddr_in6 addr;
    int len = sizeof(addr);

    if (!netCtx) {
        return 0;
    }
    if (uv_tcp_getsockname(&netCtx->socket, (struct sockaddr*)&addr, &len)) {
        return 0;
    }
    DPS_DBGPRINT("Listener port = %d\n", ntohs(addr.sin6_port));
    return ntohs(addr.sin6_port);
}

void DPS_NetStop(DPS_NetContext* netCtx)
{
    if (netCtx) {
        netCtx->socket.data = netCtx;
        uv_close((uv_handle_t*)&netCtx->socket, SocketClosed);
    }
}

static void OnWriteComplete(uv_write_t* req, int status)
{
    WriteRequest* wr = (WriteRequest*)req->data;
    DPS_Status dpsRet = DPS_OK;

    if (status) {
        DPS_DBGPRINT("OnWriteComplete status=%s\n", uv_err_name(status));
        dpsRet = DPS_ERR_NETWORK;
    }
    wr->onSendComplete(wr->cn->node, &wr->cn->peerEp, wr->bufs, wr->numBufs, dpsRet);
    DPS_NetConnectionDecRef(wr->cn);
    free(wr);
}

static DPS_Status DoWrite(DPS_NetConnection* cn)
{
    int r = 0;

    while (cn->pendingWrites) {
        WriteRequest* wr = cn->pendingWrites;
        wr->writeReq.data = wr;
        r = uv_write(&wr->writeReq, (uv_stream_t*)&cn->socket, wr->bufs, (uint32_t)wr->numBufs, OnWriteComplete);
        if (r != 0) {
            break;
        }
        cn->pendingWrites = wr->next;
        DPS_NetConnectionAddRef(cn);
    }
    if (r) {
        DPS_ERRPRINT("OnOutgoingConnection - write failed: %s\n", uv_err_name(r));
        return DPS_ERR_NETWORK;
    } else {
        return DPS_OK;
    }
}

static void OnOutgoingConnection(uv_connect_t *req, int status)
{
    DPS_NetConnection* cn = (DPS_NetConnection*)req->data;
    if (status == 0) {
        cn->socket.data = cn;
        status = uv_read_start((uv_stream_t*)&cn->socket, AllocBuffer, OnData);
    }
    if (status == 0) {
        DoWrite(cn);
    } else {
        DPS_ERRPRINT("OnOutgoingConnection - connect %s failed: %s\n", DPS_NodeAddrToString(&cn->peerEp.addr), uv_err_name(status));
        CancelPendingWrites(cn);
    }
}

DPS_Status DPS_NetSend(DPS_Node* node, DPS_NetEndpoint* ep, uv_buf_t* bufs, size_t numBufs, DPS_NetSendComplete sendCompleteCB)
{
    WriteRequest* wr;
    int r;

#ifndef NDEBUG
    {
        size_t i;
        size_t len = 0;
        for (i = 0; i < numBufs; ++i) {
            len += bufs[i].len;
        }
        DPS_DBGPRINT("DPS_NetSend total %zu bytes to %s\n", len, DPS_NodeAddrToString(&ep->addr));
    }
#endif

    wr = malloc(sizeof(WriteRequest) + (numBufs - 1) * sizeof(uv_buf_t));
    if (!wr) {
        return DPS_ERR_RESOURCES;
    }
    memcpy(wr->bufs, bufs, numBufs * sizeof(uv_buf_t));
    wr->numBufs = numBufs;
    wr->onSendComplete = sendCompleteCB;
    wr->next = NULL;
    /*
     * See if we already have a connection
     */
    if (ep->cn) {
        wr->cn = ep->cn;
        /*
         * If there are pending writes the connection is not up yet
         */
        if (ep->cn->pendingWrites) {
            WriteRequest* last = ep->cn->pendingWrites;
            while (last->next) {
                last = last->next;
            }
            last->next = wr;
            return DPS_OK;
        }
        ep->cn->pendingWrites = wr;
        return DoWrite(ep->cn);
    }
    ep->cn = calloc(1, sizeof(DPS_NetConnection));
    if (!ep->cn) {
        goto ErrExit;
    }
    r = uv_tcp_init(DPS_GetLoop(node), &ep->cn->socket);
    if (r) {
        goto ErrExit;
    }
    ep->cn->peerEp.addr = ep->addr;
    ep->cn->node = node;

    if (ep->addr.inaddr.ss_family == AF_INET6) {
        struct sockaddr_in6* in6 = (struct sockaddr_in6*)&ep->addr.inaddr;
        if (!in6->sin6_scope_id) {
            in6->sin6_scope_id = GetLocalScopeId();
        }
    }
    ep->cn->connectReq.data = ep->cn;
    r = uv_tcp_connect(&ep->cn->connectReq, &ep->cn->socket, (struct sockaddr*)&ep->addr.inaddr, OnOutgoingConnection);
    if (r) {
        DPS_ERRPRINT("uv_tcp_connect %s error=%s\n", DPS_NodeAddrToString(&ep->addr), uv_err_name(r));
        ep->cn->socket.data = ep->cn;
        uv_close((uv_handle_t*)&ep->cn->socket, StreamClosed);
        goto ErrExit;
    }
    ep->cn->peerEp.cn = ep->cn;
    ep->cn->pendingWrites = wr;
    wr->cn = ep->cn;
    DPS_NetConnectionAddRef(ep->cn);
    return DPS_OK;

ErrExit:

    if (wr) {
        free(wr);
    }
    if (ep->cn) {
        free(ep->cn);
    }
    ep->cn = NULL;
    return DPS_ERR_NETWORK;
}

void DPS_NetConnectionAddRef(DPS_NetConnection* cn)
{
    if (cn) {
        DPS_DBGTRACE();
        ++cn->refCount;
    }
}

void DPS_NetConnectionDecRef(DPS_NetConnection* cn)
{
    if (cn) {
        DPS_DBGTRACE();
        assert(cn->refCount > 0);
        if (--cn->refCount == 0) {
            Shutdown(cn);
        }
    }
}