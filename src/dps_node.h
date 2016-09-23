#ifndef _DPS_NODE_H
#define _DPS_NODE_H

#include <dps/bitvec.h>
#include <dps/network.h>
#include <dps/dps_history.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _BackgroundHandler BackgroundHandler;

typedef enum { LINK_OP, UNLINK_OP } OpType;

typedef struct {
    OpType op;
    void* data;
    uint64_t timeout;
    uv_mutex_t mutex;
    union {
        DPS_OnLinkComplete link;
        DPS_OnUnlinkComplete unlink;
        void* cb;
    } on;
} OnOpCompletion;

typedef struct _RemoteNode {
    OnOpCompletion* completion;
    uint8_t linked;                    /* True if this is a node that was explicitly linked */
    struct {
        uint8_t sync;                  /* If TRUE request remote to synchronize interests */
        uint8_t updates;               /* TRUE if updates have been received but not acted on */
        DPS_BitVector* needs;          /* Bit vector of needs received from  this remote node */
        DPS_BitVector* interests;      /* Bit vector of interests received from  this remote node */
    } inbound;
    struct {
        uint8_t sync;                  /* If TRUE synchronize outbound interests with remote node (no deltas) */
        uint8_t checkForUpdates;       /* TRUE if there may be updated interests to send to this remote */
        DPS_BitVector* needs;          /* Needs bit vector sent outbound to this remote node */
        DPS_BitVector* interests;      /* Interests bit vector sent outbound to this remote node */
    } outbound;
    DPS_NodeAddress addr;
    uint64_t expires;
    /*
     * Remote nodes are doubly linked into a ring
     */
    struct _RemoteNode* prev;
    struct _RemoteNode* next;
} RemoteNode;

/*
 * Acknowledgment packet queued to be sent on node loop
 */
typedef struct _PublicationAck {
    uv_buf_t bufs[3];
    DPS_NodeAddress destAddr;
    uint32_t sequenceNum;
    DPS_UUID pubId;
    struct _PublicationAck* next;
} PublicationAck;

typedef struct _DPS_Node {
    void* userData;

    uint16_t tasks;                        /* Background tasks that have been scheduled */
    uint16_t port;
    char separators[13];                  /* List of separator characters */

    uv_thread_t thread;                   /* Thread for the event loop */
    uv_loop_t* loop;                      /* uv lib event loop */
    uv_mutex_t nodeMutex;                 /* Mutex to protect this node */
    uv_mutex_t condMutex;                 /* Mutex for use wih condition variables */
#ifndef NDEBUG
    int lockCount;                        /* Detect recursive locks */
#endif
    BackgroundHandler* bgHandler;         /* Async handler for background tasks */

    uint64_t ttlBasis;                    /* basis time for expiring retained messages */

    struct {
        PublicationAck* first;
        PublicationAck* last;
    } ackQueue;                           /* Queued acknowledgment packets */

    RemoteNode* remoteNodes;              /* Linked list of remote nodes */

    struct {
        DPS_BitVector* needs;             /* Preallocated needs bit vector */
        DPS_BitVector* interests;         /* Preallocated interests bit vector */
    } scratch;

    DPS_CountVector* interests;           /* Tracks all interests for this node */
    DPS_CountVector* needs;               /* Tracks all needs for this node */

    DPS_History history;                  /* History of recently sent publications */

    DPS_Publication* publications;        /* Linked list of local and retained publications */
    DPS_Subscription* subscriptions;      /* Linked list of local subscriptions */

    DPS_MulticastReceiver* mcastReceiver;
    DPS_MulticastSender* mcastSender;

    DPS_NetContext* netCtx;               /* Network context */

} DPS_Node;

/*
 * Create a background handler to run a function on the node's event
 * loop thread when scheduled.
 *
 * This should be called prior to running the node's event loop thread.
 *
 * @param node   The node whose thread the function will be called on
 * @param run    The function called when the handler runs
 *
 * @return The background handler
 */
BackgroundHandler* DPS_BackgroundCreate(DPS_Node* node, void (*run)(DPS_Node*));

/*
 * Release the background handler resources.
 *
 * This must be called from the node's event loop thread, and the event loop
 * must run after calling to ensure all cleanup is completed.
 */
void DPS_BackgroundClose(BackgroundHandler* bg);

/*
 * Request the background handler to run.
 *
 * This is safe to call from any thread.
 *
 * @param bg  The background handler
 */
void DPS_BackgroundScheduleNow(BackgroundHandler* bg);

/*
 * Request the background handler to call a function from the node's event
 * loop thread at some future time.
 *
 * This must be called from the node's thread.
 *
 * @param bg            The background handler
 * @param run           The function called when the handler runs
 * @param delayMsecs    Milliseconds from now when to call the function
 */
void DPS_BackgroundSchedule(BackgroundHandler* bg, void (*run)(DPS_Node*), uint64_t delayMsecs);

#ifdef __cplusplus
}
#endif

#endif

