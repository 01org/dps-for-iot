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

#ifndef _DPS_H
#define _DPS_H

#include <stdint.h>
#include <stddef.h>
#include <dps/err.h>
#include <dps/uuid.h>
#include <safe_lib.h>
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

#define A_SIZEOF(a)  (sizeof(a) / sizeof((a)[0]))

#define DPS_TRUE  1
#define DPS_FALSE 0

/**
 * @defgroup nodeaddress Node Address
 * Remote node addresses.
 * @{
 */

/**
 * Opaque type for a remote node address.
 */
typedef struct _DPS_NodeAddress DPS_NodeAddress;

/**
 * Get text representation of an address. This function uses a static string buffer so is not thread safe.
 *
 * @param addr to get the text for
 *
 * @return  A text string for the address
 */
const char* DPS_NodeAddrToString(const DPS_NodeAddress* addr);

/**
 * Creates a node address.
 */
DPS_NodeAddress* DPS_CreateAddress();

/**
 * Set a node address
 *
 * @param addr  The address to set
 * @param sa    The value to set
 *
 * @return The addr passed in.
 */
DPS_NodeAddress* DPS_SetAddress(DPS_NodeAddress* addr, const struct sockaddr* sa);

/**
 * Copy a node address
 */
void DPS_CopyAddress(DPS_NodeAddress* dest, const DPS_NodeAddress* src);

/**
 * Frees resources associated with an address
 */
void DPS_DestroyAddress(DPS_NodeAddress* addr);

/** @} */ // end of nodeaddress group

/**
 * @defgroup keystore Key Store
 * Key stores provide key data for protecting messages and the
 * network.
 * @{
 */

/**
 * @name KeyStore
 * Hooks for implementating an application-defined key store.
 * @{
 */

typedef enum {
    DPS_KEY_SYMMETRIC,          /**< struct _DPS_KeySymmetric */
    DPS_KEY_EC,                 /**< struct _DPS_KeyEC */
    DPS_KEY_EC_CERT             /**< struct _DPS_KeyCert */
} DPS_KeyType;

/**
 * Symmetric key data
 *
 * @note need to define this outside of DPS_Key to satisfy SWIG.
 */
struct _DPS_KeySymmetric {
    const uint8_t* key;         /**< Key data */
    size_t len;                 /**< Size of key data */
};

/**
 * Allowed elliptic curves
 */
typedef enum {
    DPS_EC_CURVE_RESERVED = 0,
    DPS_EC_CURVE_P256 = 1, /**< NIST P-256 also known as secp256r1 */
    DPS_EC_CURVE_P384 = 2, /**< NIST P-384 also known as secp384r1 */
    DPS_EC_CURVE_P521 = 3  /**< NIST P-521 also known as secp521r1 */
} DPS_ECCurve;

/**
 * Elliptic curve key data.
 *
 * Only @p x and @p y are needed for a public key.  Similarly, only @p
 * d is needed for a private key.
 *
 * @note need to define this outside of DPS_Key to satisfy SWIG.
 */
struct _DPS_KeyEC {
    DPS_ECCurve curve; /**< The named curve */
    const uint8_t* x; /**< X coordinate */
    const uint8_t* y; /**< Y coordinate */
    const uint8_t* d; /**< D coordinate */
};

/**
 * Certificate key data.
 *
 * @note need to define this outside of DPS_Key to satisfy SWIG.
 */
struct _DPS_KeyCert {
    const char *cert;           /**< The certificate in PEM format */
    const char *privateKey;     /**< The optional private key in PEM format */
    const char *password;       /**< The optional password protecting the key */
};

/**
 * Union of supported key types.
 */
typedef struct _DPS_Key {
    DPS_KeyType type; /**< Type of key */
    union {
        struct _DPS_KeySymmetric symmetric; /**< DPS_KEY_SYMMETRIC */
        struct _DPS_KeyEC ec;               /**< DPS_KEY_EC */
        struct _DPS_KeyCert cert;           /**< DPS_KEY_EC_CERT */
    };
} DPS_Key;

/**
 * An identifier of a key in a key store.
 */
typedef struct _DPS_KeyId {
    const uint8_t* id; /**< the identifier of the key */
    size_t len; /**< the length of the identifier, in bytes */
} DPS_KeyId;

/**
 * Opaque type for a key store.
 */
typedef struct _DPS_KeyStore DPS_KeyStore;

/**
 * Opaque type for a key store request.
 */
typedef struct _DPS_KeyStoreRequest DPS_KeyStoreRequest;

/**
 * Function prototype for a key store handler called when a key and
 * key identifier is requested.
 *
 * DPS_SetKeyAndIdentity() should be called to provide the key and
 * identifier to the caller.
 *
 * @param request The request, only valid with the body of this
 *                callback function.
 *
 * @return
 * - DPS_OK when DPS_SetKeyAndIdentity() succeeds
 * - DPS_ERR_MISSING when no key is configured for this host
 * - error otherwise
 */
typedef DPS_Status (*DPS_KeyAndIdentityHandler)(DPS_KeyStoreRequest* request);

/**
 * Function prototype for a key store handler called when a key with the provided
 * key identifier is requested.
 *
 * DPS_SetKey() should be called to provide the key to the caller.
 *
 * @param request The request, only valid with the body of this
 *                callback function.
 * @param keyId The identifier of the key to provide.
 *
 * @return
 * - DPS_OK when DPS_SetKey() succeeds
 * - DPS_ERR_MISSING when no key is located
 * - error otherwise
 */
typedef DPS_Status (*DPS_KeyHandler)(DPS_KeyStoreRequest* request, const DPS_KeyId* keyId);

/**
 * Function prototype for a key store handler called when an ephemeral key with the
 * provided type is requested.
 *
 * DPS_SetKey() should be called to provide the ephemeral key to the caller.
 *
 * @param request The request, only valid with the body of this
 *                callback function.
 * @param key The requested key type and parameters (e.g. key->type is
 *            DPS_KEY_EC and key->ec.curve is DPS_EC_CURVE_P256).
 *
 * @return
 * - DPS_OK when DPS_SetKey() succeeds
 * - DPS_ERR_MISSING when no key is located
 * - error otherwise
 */
typedef DPS_Status (*DPS_EphemeralKeyHandler)(DPS_KeyStoreRequest* request, const DPS_Key* key);

/**
 * Function prototype for a key store handler called when the trusted
 * CA chain is requested.
 *
 * DPS_SetCA() should be called to provide the CA chain to the caller.
 *
 * @param request The request, only valid with the body of this
 *                callback function.
 *
 * @return
 * - DPS_OK when DPS_SetCA() succeeds
 * - DPS_ERR_MISSING when no CA chain is configured
 * - error otherwise
 */
typedef DPS_Status (*DPS_CAHandler)(DPS_KeyStoreRequest* request);

/**
 * Provide a key and key identifier to a key store request.
 *
 * @param request The @p request parameter of the handler
 * @param key The key
 * @param keyId The identifier of the key to provide
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetKeyAndIdentity(DPS_KeyStoreRequest* request, const DPS_Key* key, const DPS_KeyId* keyId);

/**
 * Provide a key to a key store request.
 *
 * @param request The @p request parameter of the handler
 * @param key The key
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetKey(DPS_KeyStoreRequest* request, const DPS_Key* key);

/**
 * Provide a trusted CA chain to a key store request.
 *
 * @param request The @p request parameter of the handler
 * @param ca The CA chain in PEM format
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetCA(DPS_KeyStoreRequest* request, const char* ca);

/**
 * Returns the @p DPS_KeyStore* of a key store request.
 *
 * @param request A key store request
 *
 * @return The DPS_KeyStore* or NULL
 */
DPS_KeyStore* DPS_KeyStoreHandle(DPS_KeyStoreRequest* request);

/**
 * Creates a key store.
 *
 * @param keyAndIdentityHandler Optional handler for receiving key and
 *                              key identifier requests
 * @param keyHandler Optional handler for receiving key requests
 * @param ephemeralKeyHandler Optional handler for receiving ephemeral key requests
 * @param caHandler Optional handler for receiving CA chain requests
 *
 * @return A pointer to the key store or NULL if there were no resources.
 */
DPS_KeyStore* DPS_CreateKeyStore(DPS_KeyAndIdentityHandler keyAndIdentityHandler, DPS_KeyHandler keyHandler,
                                 DPS_EphemeralKeyHandler ephemeralKeyHandler, DPS_CAHandler caHandler);

/**
 * Destroys a previously created key store.
 *
 * @param keyStore The key store
 */
void DPS_DestroyKeyStore(DPS_KeyStore* keyStore);

/**
 * Store a pointer to application data in a key store.
 *
 * @param keyStore The key store
 * @param data The data pointer to store
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetKeyStoreData(DPS_KeyStore* keyStore, void* data);

/**
 * Get application data pointer previously set by DPS_SetKeyStoreData().
 *
 * @param keyStore The keyStore
 *
 * @return  A pointer to the data or NULL if the key store is invalid
 */
void* DPS_GetKeyStoreData(const DPS_KeyStore* keyStore);

/** @} */ // end of KeyStore subgroup

/**
 * @name In-memory Key Store
 * The implementation of an in-memory key store.
 * @{
 */

/**
 * Opaque type for an in-memory key store.
 */
typedef struct _DPS_MemoryKeyStore DPS_MemoryKeyStore;

/**
 * Creates an in-memory key store.
 *
 * @return A pointer to the key store or NULL if there were no resources.
 */
DPS_MemoryKeyStore* DPS_CreateMemoryKeyStore();

/**
 * Destroys a previously created in-memory key store.
 */
void DPS_DestroyMemoryKeyStore(DPS_MemoryKeyStore* keyStore);

/**
 * Create or replace a key with the specified key identifier in the key store.
 *
 * Specify a previously set key identifier and a NULL key to remove a key from the key store.
 *
 * @param keyStore An in-memory key store
 * @param keyId The identifier of the key to create, replace, or remove
 * @param key The key
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetContentKey(DPS_MemoryKeyStore* keyStore, const DPS_KeyId* keyId, const DPS_Key* key);

/**
 * Create or replace the network key in the key store.
 *
 * @param keyStore An in-memory key store
 * @param keyId The identifier of the key to create
 * @param key The key
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetNetworkKey(DPS_MemoryKeyStore* keyStore, const DPS_KeyId* keyId, const DPS_Key* key);

/**
 * Create or replace the trusted CA(s) in the key store.
 *
 * @param mks An in-memory key store
 * @param ca The CA chain in PEM format
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetTrustedCA(DPS_MemoryKeyStore* mks, const char* ca);

/**
 * Create or replace a certificate in the key store.
 *
 * @param mks An in-memory key store
 * @param cert The certificate in PEM format
 * @param key The optional private key in PEM format
 * @param password The optional password protecting the key, may be
 *                 NULL
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetCertificate(DPS_MemoryKeyStore* mks, const char* cert, const char* key,
                              const char* password);

/**
 * Returns the @p DPS_KeyStore* of an in-memory key store.
 *
 * @param keyStore An in-memory key store
 *
 * @return The DPS_KeyStore* or NULL
 */
DPS_KeyStore* DPS_MemoryKeyStoreHandle(DPS_MemoryKeyStore* keyStore);

/** @} */ // end of MemoryKeyStore subgroup

/** @} */ // end of keystore group

/**
 * @defgroup permstore Permission Store
 * Permission stores provide access control for sending and receiving
 * messages.
 * @{
 */

/**
 * @name PermissionStore
 * Hooks for implementating an application-defined permission store.
 * @{
 */

typedef enum {
    DPS_PERM_PUB = (1 << 0),    /**< Publication permission bit */
    DPS_PERM_SUB = (1 << 1),    /**< Subscription permission bit */
    DPS_PERM_ACK = (1 << 2),    /**< End-to-end publication acknowledgement permission bit */
    /**
     * Forward other permission bits.  This is never requested by DPS
     * but is available to be used by applications to indicate whether
     * the network or end-to-end ID should be checked.
     */
    DPS_PERM_FORWARD = (1 << 3)
} DPS_Permission;

/**
 * Opaque type for a permission store.
 */
typedef struct _DPS_PermissionStore DPS_PermissionStore;

/**
 * Opaque type for a permission store request.
 */
typedef struct _DPS_PermissionStoreRequest DPS_PermissionStoreRequest;

/**
 * Returns the network ID of a permission request
 *
 * @param request The @p request parameter of the handler
 *
 * @return the ID of the request or NULL if unknown
 */
const DPS_KeyId* DPS_GetNetworkId(DPS_PermissionStoreRequest* request);

/**
 * Returns the end-to-end ID of a permission request
 *
 * @param request The @p request parameter of the handler
 *
 * @return the ID of the request or NULL if unknown
 */
const DPS_KeyId* DPS_GetEndToEndId(DPS_PermissionStoreRequest* request);

/**
 * Returns the requested permission(s)
 *
 * @param request The @p request parameter of the handler
 *
 * @return the permission(s) requested
 */
DPS_Permission DPS_GetPermission(DPS_PermissionStoreRequest* request);

/**
 * Indicates whether the permission request includes the provided
 * topics.
 *
 * @param request The @p request parameter of the handler
 * @param topics The topic strings to check
 * @param numTopics The number of topic strings to check - must be >= 1
 *
 * @return
 * - DPS_OK if the request includes topic information and the provided
 *          topics are present
 * - DPS_ERR_FAILURE if the request includes topic information and the
 *                   provided topics are not present
 * - DPS_ERR_MISSING if request does not include topic information
 */
DPS_Status DPS_IncludesTopics(DPS_PermissionStoreRequest* request, const char** topics, size_t numTopics);

/**
 * Returns the @p DPS_PermissionStore* of a permission store request.
 *
 * @param request A permission store request
 *
 * @return The DPS_PermissionStore* or NULL
 */
DPS_PermissionStore* DPS_PermissionStoreHandle(DPS_PermissionStoreRequest* request);

/**
 * Function prototype for a permission store handler called when
 * permission is requested.
 *
 * @param request The request, only valid with the body of this
 *                callback function.
 *
 * @return
 * - DPS_TRUE if permission is granted
 * - DPS_FALSE if permission is denied
 */
typedef int (*DPS_PermissionHandler)(DPS_PermissionStoreRequest* request);

/**
 * Creates a permission store.
 *
 * @param handler handler for permission requests
 *
 * @return A pointer to the permission store or NULL if there were no resources.
 */
DPS_PermissionStore* DPS_CreatePermissionStore(DPS_PermissionHandler handler);

/**
 * Destroys a previously created permission store.
 *
 * @param permStore The permission store
 */
void DPS_DestroyPermissionStore(DPS_PermissionStore* permStore);

/**
 * Store a pointer to application data in a permission store.
 *
 * @param permStore The permission store
 * @param data The data pointer to store
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetPermissionStoreData(DPS_PermissionStore* permStore, void* data);

/**
 * Get application data pointer previously set by DPS_SetPermissionStoreData().
 *
 * @param permStore The permission store
 *
 * @return  A pointer to the data or NULL if the permission store is invalid
 */
void* DPS_GetPermissionStoreData(const DPS_PermissionStore* permStore);

/** @} */ // end of PermissionStore subgroup

/**
 * @name In-memory Permission Store
 * The implementation of an in-memory permission store.
 * @{
 */

/**
 * Opaque type for an in-memory permission store.
 */
typedef struct _DPS_MemoryPermissionStore DPS_MemoryPermissionStore;

/**
 * Creates an in-memory permission store.
 *
 * @return A pointer to the permission store or NULL if there were no resources.
 */
DPS_MemoryPermissionStore* DPS_CreateMemoryPermissionStore();

/**
 * Destroys a previously created in-memory permission store.
 */
void DPS_DestroyMemoryPermissionStore(DPS_MemoryPermissionStore* permStore);

/**
 * Create or replace permissions in the permission store.
 *
 * When NULL or 0 is provided as a parameter, that indicates wildcard
 * matching.
 *
 * @param permStore An in-memory permission store
 * @param topics The topic strings to check, may be NULL
 * @param numTopics The number of topic strings to check, may be 0
 * @param keyId The key identifier to check, may be NULL
 * @param perms A combination of DPS_Permission flags or 0
 *
 * @return DPS_OK if permission entry was succesfully created or
 * replaced, error otherwise
 */
DPS_Status DPS_SetPermission(DPS_MemoryPermissionStore* permStore, const char** topics, size_t numTopics,
                             const DPS_KeyId* keyId, DPS_Permission perms);

/**
 * Returns the @p DPS_PermissionStore* of an in-memory permission store.
 *
 * @param permStore An in-memory permission store
 *
 * @return The DPS_PermissionStore* or NULL
 */
DPS_PermissionStore* DPS_MemoryPermissionStoreHandle(DPS_MemoryPermissionStore* permStore);

/** @} */ // end of MemoryPermissionStore subgroup

/** @} */ // end of permstore group

/**
 * @defgroup node Node
 * Entities in the DPS network.
 * @{
 */

/**
 * Opaque type for a node.
 */
typedef struct _DPS_Node DPS_Node;

/**
 * Allocates space for a local DPS node.
 *
 * @param separators    The separator characters to use for topic matching, if NULL defaults to "/"
 * @param keyStore      The key store to use for this node
 * @param keyId         The key identifier of this node
 *
 * @return A pointer to the uninitialized node or NULL if there were no resources for the node.
 */
DPS_Node* DPS_CreateNode(const char* separators, DPS_KeyStore* keyStore, const DPS_KeyId* keyId);

/**
 * Set the permission store to use for this node.
 *
 * @param node       The node
 * @param permStore  The permission store
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetPermissionStore(DPS_Node* node, DPS_PermissionStore* permStore);

/**
 * Store a pointer to application data in a node.
 *
 * @param node   The node
 * @param data  The data pointer to store
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetNodeData(DPS_Node* node, void* data);

/**
 * Get application data pointer previously set by DPS_SetNodeData()
 *
 * @param node   The node
 *
 * @return  A pointer to the data or NULL if the node is invalid
 */
void* DPS_GetNodeData(const DPS_Node* node);

/**
 * Disable multicast send and receive on the node.  See @p mcastPub of DPS_StartNode().
 */
#define DPS_MCAST_PUB_DISABLED       0

/**
 * Enable multicast send on the node.  See @p mcastPub of DPS_StartNode().
 */
#define DPS_MCAST_PUB_ENABLE_SEND    1

/**
 * Enable multicast receive on the node.  See @p mcastPub of DPS_StartNode().
 */
#define DPS_MCAST_PUB_ENABLE_RECV    2

/**
 * Initialized and starts running a local node. Node can only be started once.
 *
 * @param node         The node
 * @param mcastPub     Indicates if this node sends or listens for multicast publications
 * @param listenPort   If non-zero identifies specific port to listen on
 *
 * @return DPS_OK or various error status codes
 */
DPS_Status DPS_StartNode(DPS_Node* node, int mcastPub, int listenPort);

/**
 * Function prototype for callback function called when a node is destroyed.
 *
 * @param node   The node that was destroyed. This pointer is valid during
 *               the callback.
 * @param data   Data pointer passed to DPS_DestroyNode()
 *
 */
typedef void (*DPS_OnNodeDestroyed)(DPS_Node* node, void* data);

/**
 * Destroys a node and free any resources.
 *
 * @param node   The node to destroy
 * @param cb     Callback function to be called when the node is destroyed
 * @param data   Data to be passed to the callback function
 *
 * @return
 * - DPS_OK if the node will be destroyed and the callback called
 * - DPS_ERR_NULL node or cb was a null pointer
 * - Or an error status code in which case the callback will not be called.
 */
DPS_Status DPS_DestroyNode(DPS_Node* node, DPS_OnNodeDestroyed cb, void* data);

/**
 * The default maximum rate (in msecs) to compute and send out subscription updates.
 */
#define DPS_SUBSCRIPTION_UPDATE_RATE 1000

/**
 * Specify the time delay (in msecs) between subscription updates.
 *
 * @param node           The node
 * @param subsRateMsecs  The time delay (in msecs) between updates
 */
void DPS_SetNodeSubscriptionUpdateDelay(DPS_Node* node, uint32_t subsRateMsecs);

/**
 * Get the uv event loop for this node. The only thing that is safe to do with the node
 * is to create an async callback. Other libuv APIs can then be called from within the
 * async callback.
 *
 * @param node     The local node to use
 */
uv_loop_t* DPS_GetLoop(DPS_Node* node);

/**
 * Get the port number this node is listening for connections on
 *
 * @param node     The local node to use
 */
uint16_t DPS_GetPortNumber(DPS_Node* node);

/**
 * Function prototype for function called when a DPS_Link() completes.
 *
 * @param node   The local node to use
 * @param addr   The address of the remote node that was linked
 * @param status Indicates if the link completed or failed
 * @param data   Application data passed in the call to DPS_Link()
 */
typedef void (*DPS_OnLinkComplete)(DPS_Node* node, DPS_NodeAddress* addr, DPS_Status status, void* data);

/**
 * Link the local node to a remote node
 *
 * @param node   The local node to use
 * @param addr   The address of the remote node to link to
 * @param cb     The callback function to call on completion, can be NULL which case the function is synchronous
 * @param data   Application data to be passed to the callback

 * @return DPS_OK or an error status. If an error status is returned the callback function will not be called.
 */
DPS_Status DPS_Link(DPS_Node* node, DPS_NodeAddress* addr, DPS_OnLinkComplete cb, void* data);

/**
 * Function prototype for function called when a DPS_Unlink() completes.
 *
 * @param node   The local node to use
 * @param addr   The address of the remote node that was unlinked
 * @param data   Application data passed in the call to DPS_Link()
 */
typedef void (*DPS_OnUnlinkComplete)(DPS_Node* node, DPS_NodeAddress* addr, void* data);

/**
 * Unlink the local node from a remote node
 *
 * @param node   The local node to use
 * @param addr   The address of the remote node to unlink from
 * @param cb     The callback function to call on completion, can be NULL which case the function is synchronous
 * @param data   Application data to be passed to the callback
 *
 * @return DPS_OK or an error status. If an error status is returned the callback function will not be called.
 */
DPS_Status DPS_Unlink(DPS_Node* node, DPS_NodeAddress* addr, DPS_OnUnlinkComplete cb, void* data);

/**
 * Function prototype for function called when a DPS_ResolveAddress() completes.
 *
 * @param node   The local node to use
 * @param addr   The resolved address or NULL if the address could not be resolved
 * @param data   Application data passed in the call to DPS_ResolveAddress()
 */
typedef void (*DPS_OnResolveAddressComplete)(DPS_Node* node, DPS_NodeAddress* addr, void* data);

/**
 * Resolve a host name or IP address and service name or port number.
 *
 * @param node     The local node to use
 * @param host     The host name or IP address to resolve
 * @param service  The port or service name to resolve
 * @param cb       The callback function to call on completion
 * @param data     Application data to be passed to the callback
 *
 * @return DPS_OK or an error status. If an error status is returned the callback function will not be called.
 */
DPS_Status DPS_ResolveAddress(DPS_Node* node, const char* host, const char* service, DPS_OnResolveAddressComplete cb, void* data);

/** @} */ // end of node group

/**
 * @defgroup publication Publication
 * Publications.
 * @{
 */

/**
 * Opaque type for a publication
 */
typedef struct _DPS_Publication DPS_Publication;

/**
 * Get the UUID for a publication
 *
 * @param pub   The publication
 */
const DPS_UUID* DPS_PublicationGetUUID(const DPS_Publication* pub);

/**
 * Get the sequence number for a publication. Serial numbers are always > 0.
 *
 * @param pub   The publication
 *
 * @return The sequence number or zero if the publication is invalid.
 */
uint32_t DPS_PublicationGetSequenceNum(const DPS_Publication* pub);

/**
 * Get a topic for a publication
 *
 * @param pub   The publication
 * @param index The topic index
 *
 * @return The topic string or NULL if the publication or index is invalid.
 */
const char* DPS_PublicationGetTopic(const DPS_Publication* pub, size_t index);

/**
 * Get the number of topics in a publication
 *
 * @param pub   The publication
 */
size_t DPS_PublicationGetNumTopics(const DPS_Publication* pub);

/**
 * Check if an acknowledgement was requested for a publication.
 *
 * @param pub   The publication
 *
 * @return Returns 1 if an acknowledgement was requested, otherwise 0.
 */
int DPS_PublicationIsAckRequested(const DPS_Publication* pub);

/**
 * Get the local node associated with a publication
 *
 * @param pub   The publication
 *
 * @return  A pointer to the node or NULL if the publication is invalid
 */
DPS_Node* DPS_PublicationGetNode(const DPS_Publication* pub);

/**
 * Allocates storage for a publication
 *
 * @param node         The local node to use
 */
DPS_Publication* DPS_CreatePublication(DPS_Node* node);

/**
 * Creates a partial copy of a publication that can be used to acknowledge the publication.
 * The copy is not useful for anything other than in a call to DPS_AckPublication() and should
 * be freed by calling DPS_DestroyPublcation() when no longer needed.
 *
 * The partial copy can be used with DPS_PublicationGetUUID() and DPS_PublicationGetSequenceNum()
 *
 * @param pub  The publication to copy
 *
 * @return A partial copy of the publication or NULL if the publication could not be copied.
 */
DPS_Publication* DPS_CopyPublication(const DPS_Publication* pub);

/**
 * Store a pointer to application data in a publication.
 *
 * @param pub   The publication
 * @param data  The data pointer to store
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetPublicationData(DPS_Publication* pub, void* data);

/**
 * Get application data pointer previously set by DPS_SetPublicationData()
 *
 * @param pub   The publication
 *
 * @return  A pointer to the data or NULL if the publication is invalid
 */
void* DPS_GetPublicationData(const DPS_Publication* pub);

/**
 * Function prototype for a publication acknowledgment handler called when an acknowledgement
 * for a publication is received from a remote subscriber. The handler is called for each
 * subscriber that generates an acknowledgement so may be called numerous times for same
 * publication.
 *
 * @param pub      Opaque handle for the publication that was received
 * @param payload  Payload accompanying the acknowledgement if any
 * @param len   Length of the payload
 */
typedef void (*DPS_AcknowledgementHandler)(DPS_Publication* pub, uint8_t* payload, size_t len);

/**
 * Initializes a newly created publication with a set of topics. Each publication has a UUID and a
 * sequence number. The sequence number is incremented each time the publication is published. This
 * allows subscriber to determine that publications received form a series. The acknowledgment
 * handler is optional, if present the publication is marked as requesting acknowledgment and that
 * information is provided to the subscribers.
 *
 * Call the accessor function DPS_PublicationGetUUID() to get the UUID for this publication.
 *
 * @param pub         The the publication to initialize
 * @param topics      The topic strings to publish
 * @param numTopics   The number of topic strings to publish - must be >= 1
 * @param noWildCard  If TRUE the publication will not match wildcard subscriptions
 * @param keyId       Optional key identifier to use for encrypted publications
 * @param handler     Optional handler for receiving acknowledgments
 */
DPS_Status DPS_InitPublication(DPS_Publication* pub,
                               const char** topics,
                               size_t numTopics,
                               int noWildCard,
                               const DPS_KeyId* keyId,
                               DPS_AcknowledgementHandler handler);

/**
 * Adds a key identifier to use for encrypted publications.
 *
 * @param pub         The the publication to initialize
 * @param keyId       Key identifier to use for encrypted publications
 */
DPS_Status DPS_PublicationAddKeyId(DPS_Publication* pub, const DPS_KeyId* keyId);

/**
 * Removes a key identifier to use for encrypted publications.
 *
 * @param pub         The the publication to initialize
 * @param keyId       Key identifier to remove
 */
void DPS_PublicationRemoveKeyId(DPS_Publication* pub, const DPS_KeyId* keyId);

/**
 * Publish a set of topics along with an optional payload. The topics will be published immediately
 * to matching subscribers and then re-published whenever a new matching subscription is received.
 *
 * Call the accessor function DPS_PublicationGetUUID() to get the UUID for this publication.  Call
 * the accessor function DPS_PublicationGetSequenceNum() to get the current sequence number for this
 * publication. The sequence number is incremented each time DPS_Publish() is called for the same
 * publication.
 *
 * @param pub          The publication to send
 * @param pubPayload   Optional payload
 * @param len          Length of the payload
 * @param ttl          Time to live in seconds - maximum TTL is about 9 hours
 *
 * @return DPS_OK if the topics were succesfully published
 */
DPS_Status DPS_Publish(DPS_Publication* pub, const uint8_t* pubPayload, size_t len, int16_t ttl);

/**
 * Delete a publication and frees any resources allocated. This does not cancel retained publications
 * that have an unexpired TTL. To expire a retained publication call DPS_Publish() with a zero TTL.
 *
 * This function should only be called for publications created by DPS_CreatePublication() or
 * DPS_CopyPublication().
 *
 * @param pub         The publication to destroy
 */
DPS_Status DPS_DestroyPublication(DPS_Publication* pub);

/**
 * Acknowledge a publication. A publication should be acknowledged as soon as possible after receipt,
 * ideally from within the publication handler callback function. If the publication cannot be
 * acknowledged immediately in the publication handler callback, call DPS_CopyPublication() to make a
 * partial copy of the publication that can be passed to this function at a later time.
 *
 * @param pub           The publication to acknowledge
 * @param ackPayload    Optional payload to accompany the aknowledgment
 * @param len           The length of the payload
 */
DPS_Status DPS_AckPublication(const DPS_Publication* pub, const uint8_t* ackPayload, size_t len);

/**
 * Get the local node associated with a publication
 *
 * @param pub   A publication
 *
 * @return  Returns the local node associated with a publication
 */
DPS_Node* DPS_GetPublicationNode(const DPS_Publication* pub);

/** @} */ // end of publication group

/**
 * @defgroup subscription Subscription
 * Subscriptions.
 * @{
 */

/**
 * Opaque type for a subscription.
 */
typedef struct _DPS_Subscription DPS_Subscription;

/**
 * Get a topic for an active subscription
 *
 * @param sub   The subscription
 * @param index The topic index
 *
 * @return The topic string or NULL if the subscription or index is invalid.
 */
const char* DPS_SubscriptionGetTopic(const DPS_Subscription* sub, size_t index);

/**
 * Get the number of topics registered with an active subscription
 */
size_t DPS_SubscriptionGetNumTopics(const DPS_Subscription* sub);

/**
 * Allocate memory for a subscription and initialize topics
 *
 * @param node         The local node to use
 * @param topics       The topic strings to match
 * @param numTopics    The number of topic strings to match - must be >= 1
 *
 * @return   Returns a pointer to the newly created subscription or NULL if resources
 *           could not be allocated or the arguments were invalid
 */
DPS_Subscription* DPS_CreateSubscription(DPS_Node* node, const char** topics, size_t numTopics);

/**
 * Store a pointer to application data in a subscription.
 *
 * @param sub   The subscription
 * @param data  The data pointer to store
 *
 * @return DPS_OK or an error
 */
DPS_Status DPS_SetSubscriptionData(DPS_Subscription* sub, void* data);

/**
 * Get application data pointer previously set by DPS_SetSubscriptionData()
 *
 * @param sub   The subscription
 *
 * @return  A pointer to the data or NULL if the subscription is invalid
 */
void* DPS_GetSubscriptionData(DPS_Subscription* sub);

/**
 * Get the local node associated with a subscription
 *
 * @param sub   The subscription
 *
 * @return  A pointer to the node or NULL if the subscription is invalid
 */
DPS_Node* DPS_SubscriptionGetNode(const DPS_Subscription* sub);

/**
 * Function prototype for a publication handler called when a publication is received that
 * matches a subscription. Note that there is a possibilitly of false-positive matches.
 *
 * The publication handle is only valid within the body of this callback function.
 * DPS_CopyPublication() will make a partial copy of the publication that can be used later for
 * example to call DPS_AckPublication().
 *
 * The accessor functions DPS_PublicationGetUUID() and DPS_PublicationGetSequenceNum()
 * return information about the received publication.
 *
 * The accessor functions DPS_SubscriptionGetNumTopics() and DPS_SubscriptionGetTopic()
 * return information about the subscription that was matched.
 *
 * @param sub      Opaque handle for the subscription that was matched
 * @param pub      Opaque handle for the publication that was received
 * @param payload  Payload from the publication if any
 * @param len      Length of the payload
 */
typedef void (*DPS_PublicationHandler)(DPS_Subscription* sub, const DPS_Publication* pub, uint8_t* payload, size_t len);

/**
 * Start subscribing to a set of topics
 *
 * @param sub          The subscription to start
 * @param handler      Callback function to be called with topic matches
 */
DPS_Status DPS_Subscribe(DPS_Subscription* sub, DPS_PublicationHandler handler);

/**
 * Stop subscribing to the subscription topic and free resources allocated for the subscription
 *
 * @param sub   The subscription to cancel
 */
DPS_Status DPS_DestroySubscription(DPS_Subscription* sub);

/** @} */ // end of subscription group

#ifdef __cplusplus
}
#endif

#endif
