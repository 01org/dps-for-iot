#ifndef _NETWORK_H
#define _NETWORK_H

#include <stdint.h>
#include <uv.h>
#include <dps/private/dps.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque data structure for network-specific state
 */
typedef struct _DPS_NetContext DPS_NetContext;

/**
 * Opaque type for managing connection state for connection-oriented transports
 */
typedef struct _DPS_NetConnection DPS_NetConnection;

/**
 * Type for a remote network endpoint. This provides an abstraction connectionless and
 * connection-oriented network layers.
 */
typedef struct {
    DPS_NodeAddress addr;   /**< The endpoint address */
    DPS_NetConnection* cn;  /**< The connection state or NULL for connectionless networ layers */
} DPS_NetEndpoint;

typedef struct _DPS_MulticastReceiver DPS_MulticastReceiver;

/**
 * Function prototype for handler to be called on receiving data from a remote node
 *
 * @param node      The node that received the data
 * @param endpoint  The endpoint that received the data
 * @param status    Indicates if the receive was successful or there was a network layer error
 * @param data      The raw data 
 * @param len       Length of the raw data
 *
 * @return  If positive the minimum number of bytes still to be read
 *          If negative indicates the data was not succesfully decoded
 */
typedef ssize_t (*DPS_OnReceive)(DPS_Node* node, DPS_NetEndpoint* endpoint, DPS_Status status, const uint8_t* data, size_t len);

/*
 * Start receiving multicast data
 */
DPS_MulticastReceiver* DPS_MulticastStartReceive(DPS_Node* node, DPS_OnReceive cb);

/*
 * Stop receiving multicast data
 */
void DPS_MulticastStopReceive(DPS_MulticastReceiver* receiver);


typedef struct _DPS_MulticastSender DPS_MulticastSender;

/*
 * Setup to enable sending multicast data
 *
 * @param node     Opaque pointer to the DPS node 
 *
 * @return   An opaque pointer to a struct holding the state of the multicast sender.
 */
DPS_MulticastSender* DPS_MulticastStartSend(DPS_Node* node);

/*
 * Free resources used for sending multicast data
 *
 * @param node     Opaque pointer to the DPS node 
 * @param sender   An opaque pointer to a struct holding the state of the multicast sender.
 *                 This will be free after this call and the pointer will no longer be valid.
 */
void DPS_MulticastStopSend(DPS_MulticastSender* sender);

/*
 * Prototype for function called when a send completes 
 *
 * @param node     Opaque pointer to the DPS node 
 * @param endpoint The endpoint for which the send was complete
 * @param bufs     Array holding pointers to the buffers passed in the send API call. The data in these buffers
 *                 can now be freed. 
 * @param          The length of the bufs array
 * @param status   Indicates if the send was successful or not
 */
typedef void (*DPS_NetSendComplete)(DPS_Node* node, DPS_NetEndpoint* endpoint, uv_buf_t* bufs, size_t numBufs, DPS_Status status);

/*
 * Multicast some data immediately
 *
 * @param node     Opaque pointer to the DPS node 
 * @param bufs     Data buffers to send
 * @param numBufs  Number of buffers to send
 */
DPS_Status DPS_MulticastSend(DPS_MulticastSender* sender, uv_buf_t* bufs, size_t numBufs);

/*
 * Start listening and receiving data
 *
 * @param node  Opaque pointer to the DPS node 
 * @param port  If non-zero the port number to listen on, if zero use an ephemeral port
 * @param cb    Function to call when data is received
 *
 * @return   Returns a pointer to an opaque data structure that holds the state of the netCtx.
 */
DPS_NetContext* DPS_NetStart(DPS_Node* node, int port, DPS_OnReceive cb);

/*
 * Get the port the netCtx is listening on
 *
 * @param netCtx  Pointer to an opaque data structure that holds the state of the netCtx.
 */
uint16_t DPS_NetGetListenerPort(DPS_NetContext* netCtx);

/*
 * Stop listening for data
 *
 * @param netCtx  Pointer to an opaque data structure that holds the network state.
 *                The netCtx will be freed and this pointer will be invalid after this call.
 */
void DPS_NetStop(DPS_NetContext* netCtx);

/*
 * Send data to a specific endpoint.
 *
 * @param node            Opaque pointer to the DPS node
 * @param endpoint        The endpoint to send to - note this may be updated with connection state
 *                        information.
 * @param bufs            Data buffers to send, the data in the buffers must be live until the send completes.
 * @param numBufs         Number of buffers to send
 * @param addr            Destination address
 * @param sendCompleteCB  Function called when the send is completeso the content of the data buffers can be freed.
 */
DPS_Status DPS_NetSend(DPS_Node* node, DPS_NetEndpoint* endpoint, uv_buf_t* bufs, size_t numBufs, DPS_NetSendComplete sendCompleteCB);

/*
 * Increment the reference count to potentialy keeping a underlying connection alive. This is only
 * meaningful for connection-oriented transports.
 *
 * @param cn    Connection to be add'refd
 */
void DPS_NetConnectionAddRef(DPS_NetConnection* cn);

/*
 * Decrement the reference count on a connection potentially allowing an underlying connection to be
 * dropped. This is only meaningful for connection-oriented transports.
 *
 * @param cn    Connection to be dec'refd
 */
void DPS_NetConnectionDecRef(DPS_NetConnection* cn);

/*
 * Compare two addresses. This comparison handles the case of ipv6 mapped ipv4 address
 *
 * @param addr1  The address to compare against
 * @param addr2  The address to compare with addr1
 *
 * @return Returns zero if the addresses are different non-zero if they are the same
 */
int DPS_SameAddr(DPS_NodeAddress* addr1, DPS_NodeAddress* addr2);

/*
 * Generates text for an address
 *
 * @param addr  The address to stringify.
 *
 * This function uses a static string internally so is not thread-safe
 */
const char* DPS_NetAddrText(const struct sockaddr* addr);

#ifdef __cplusplus
}
#endif

#endif
