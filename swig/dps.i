/*
 *******************************************************************
 *
 * Copyright 2018 Intel Corporation All rights reserved.
 *
 *-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 *
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

%module(docstring="Distributed Publish & Subscribe for IoT") dps

/*
 * Ignore this warning generated by the nested union inside DPS_Key.
 * Suppressing it with warnfilter doesn't appear to work.
 */
#pragma SWIG nowarn=312

%ignore DPS_CBOR2JSON;
%ignore DPS_DestroyKeyStore;
%ignore DPS_DestroyPublication;
%ignore DPS_DestroySubscription;
%ignore DPS_GetKeyStoreData;
%ignore DPS_GetLoop;
%ignore DPS_GetNodeData;
%ignore DPS_GetPublicationData;
%ignore DPS_GetSubscriptionData;
%ignore DPS_JSON2CBOR;
%ignore DPS_KeyStoreHandle;
%ignore DPS_MemoryKeyStoreHandle;
%ignore DPS_NodeAddrToString;
%ignore DPS_PublicationGetNumTopics;
%ignore DPS_PublicationGetTopic;
%ignore DPS_SetKeyStoreData;
%ignore DPS_SetNodeData;
%ignore DPS_SetPublicationData;
%ignore DPS_SetSubscriptionData;
%ignore DPS_SubscriptionGetNumTopics;
%ignore DPS_SubscriptionGetTopic;
%ignore DPS_UUIDToString;
%ignore _DPS_Key;
%ignore _DPS_KeyId;

/*
 * Declarations that are not relevant
 */
%ignore DPS_TRUE;
%ignore DPS_FALSE;

/*
 * These are helpers to override the string conversion of node
 * addresses and UUIDs.
 */
%typemap(in) int depth { $1 = 0; }
%typemap(in) void* opts { $1 = NULL; }

%{
#include <safe_lib.h>
#include <uv.h>
#include <dps/dbg.h>
#include <dps/dps.h>
#include <dps/err.h>
#include <dps/event.h>
#include <dps/json.h>
#include <dps/synchronous.h>
#include <dps/uuid.h>

static const char* NodeAddrToString(const DPS_NodeAddress* addr, int depth = 0, void* opts = NULL)
{
    return DPS_NodeAddrToString(addr);
}

static const char* UUIDToString(const DPS_UUID* uuid, int depth = 0, void* opts = NULL)
{
    return DPS_UUIDToString(uuid);
}
%}

%nodefaultctor _DPS_NodeAddress;
%nodefaultdtor _DPS_NodeAddress;
struct _DPS_NodeAddress { };

%include "dps_map.i"

%{
class KeyStore {
public:
    KeyStore(Handler* keyAndIdHandler, Handler* keyHandler, Handler* ephemeralKeyHandler,
             Handler* caHandler) :
        m_keyAndIdHandler(keyAndIdHandler),
        m_keyHandler(keyHandler),
        m_ephemeralKeyHandler(ephemeralKeyHandler),
        m_caHandler(caHandler) {
    }
    ~KeyStore() {
        delete m_keyAndIdHandler;
        delete m_keyHandler;
        delete m_ephemeralKeyHandler;
        delete m_caHandler;
    }
    Handler* m_keyAndIdHandler;
    Handler* m_keyHandler;
    Handler* m_ephemeralKeyHandler;
    Handler* m_caHandler;
};

static int AsVal_bytes(Handle obj, uint8_t** bytes, size_t* len);
static Handle From_bytes(const uint8_t* bytes, size_t len);
static Handle From_topics(const char** topics, size_t len);

static DPS_Status KeyAndIdHandler(DPS_KeyStoreRequest* request);
static DPS_Status KeyHandler(DPS_KeyStoreRequest* request, const DPS_KeyId* keyId);
static DPS_Status EphemeralKeyHandler(DPS_KeyStoreRequest* request, const DPS_Key* key);
static DPS_Status CAHandler(DPS_KeyStoreRequest* request);

static void OnNodeDestroyed(DPS_Node* node, void* data);
static void OnLinkComplete(DPS_Node* node, DPS_NodeAddress* addr, DPS_Status status, void* data);
static void OnNodeAddressComplete(DPS_Node* node, DPS_NodeAddress* addr, void* data);

static void AcknowledgementHandler(DPS_Publication* pub, uint8_t* payload, size_t len);

static void PublicationHandler(DPS_Subscription* sub, const DPS_Publication* pub, uint8_t* payload, size_t len);

static void InitializeModule();
%}

%typemap(in) int8_t = char;
%typemap(out) int8_t = char;
%typemap(in) uint8_t = unsigned char;
%typemap(out) uint8_t = unsigned char;
%typemap(in) int16_t = int;
%typemap(out) int16_t = int;
%typemap(in) uint16_t = unsigned int;
%typemap(out) uint16_t = unsigned int;
%typemap(in) int32_t = long;
%typemap(out) int32_t = long;
%typemap(in) uint32_t = unsigned long;
%typemap(out) uint32_t = unsigned long;

%immutable _DPS_KeySymmetric::key;
%immutable _DPS_KeySymmetric::len;
%immutable _DPS_KeyEC::curve;
%immutable _DPS_KeyEC::x;
%immutable _DPS_KeyEC::y;
%immutable _DPS_KeyEC::d;
%immutable _DPS_KeyCert::cert;
%immutable _DPS_KeyCert::privateKey;
%immutable _DPS_KeyCert::password;

%typemap(in) const DPS_Key* (DPS_Key k, uint8_t* bytes = NULL, int res = 0) {
    void* argp;
    if (SWIG_IsOK((res = SWIG_ConvertPtr($input, &argp, SWIGTYPE_p__DPS_Key, 0)))) {
        $1 = (DPS_Key*)argp;
    } else if (SWIG_IsOK((res = SWIG_ConvertPtr($input, &argp, SWIGTYPE_p__DPS_KeySymmetric, 0)))) {
        k.type = DPS_KEY_SYMMETRIC;
        memcpy(&k.symmetric, argp, sizeof(DPS_KeySymmetric));
        $1 = &k;
    } else if (SWIG_IsOK((res = SWIG_ConvertPtr($input, &argp, SWIGTYPE_p__DPS_KeyEC, 0)))) {
        k.type = DPS_KEY_EC;
        memcpy(&k.ec, argp, sizeof(DPS_KeyEC));
        $1 = &k;
    } else if (SWIG_IsOK((res = SWIG_ConvertPtr($input, &argp, SWIGTYPE_p__DPS_KeyCert, 0)))) {
        k.type = DPS_KEY_EC_CERT;
        memcpy(&k.ec, argp, sizeof(DPS_KeyCert));
        $1 = &k;
    } else if (SWIG_IsOK((res = AsVal_bytes($input, &bytes, &k.symmetric.len)))) {
        k.type = DPS_KEY_SYMMETRIC;
        k.symmetric.key = bytes;
        $1 = &k;
    } else {
        res = SWIG_TypeError;
        SWIG_exception_fail(SWIG_ArgError(res), "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
    }
}
%typemap(freearg) const DPS_Key* {
    if (SWIG_IsNewObj(res$argnum)) {
        delete[] bytes$argnum;
    }
}

%extend _DPS_KeySymmetric {
    const DPS_KeyType type;
    _DPS_KeySymmetric(const uint8_t* bytes, size_t n) {
        DPS_KeySymmetric* key = (DPS_KeySymmetric*)calloc(1, sizeof(DPS_KeySymmetric));
        if (bytes) {
            key->key = (uint8_t*)malloc(n);
            memcpy((uint8_t*)key->key, bytes, n);
            key->len = n;
        }
        return key;
    }
    ~_DPS_KeySymmetric() {
        if ($self) {
            if ($self->key) free((uint8_t*)$self->key);
            free($self);
        }
    }
}

%typemap(in) const uint8_t* (int res = 0) {
    size_t unused;
    res = AsVal_bytes($input, (uint8_t**)&$1, &unused);
    if (!SWIG_IsOK(res)) {
        SWIG_exception_fail(SWIG_ArgError(res), "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
    }
}
%typemap(freearg) const uint8_t* {
    if (SWIG_IsNewObj(res$argnum)) {
        delete[] $1;
    }
}

%extend _DPS_KeyEC {
    const DPS_KeyType type;
    _DPS_KeyEC(DPS_ECCurve curve, const uint8_t* x, const uint8_t* y, const uint8_t* d) {
        DPS_KeyEC* key;
        size_t n;
        switch (curve) {
        case DPS_EC_CURVE_P384: n = 48; break;
        case DPS_EC_CURVE_P521: n = 66; break;
        default: return NULL;
        }
        key = (DPS_KeyEC*)calloc(1, sizeof(DPS_KeyEC));
        key->curve = curve;
        if (x) {
            key->x = (uint8_t*)malloc(n);
            memcpy((uint8_t*)key->x, x, n);
        }
        if (y) {
            key->y = (uint8_t*)malloc(n);
            memcpy((uint8_t*)key->y, y, n);
        }
        if (d) {
            key->d = (uint8_t*)malloc(n);
            memcpy((uint8_t*)key->d, d, n);
        }
        return key;
    }
    ~_DPS_KeyEC() {
        if ($self) {
            if ($self->x) free((uint8_t*)$self->x);
            if ($self->y) free((uint8_t*)$self->y);
            if ($self->d) free((uint8_t*)$self->d);
            free($self);
        }
    }
}

%extend _DPS_KeyCert {
    const DPS_KeyType type;
    _DPS_KeyCert(const char* cert, const char* privateKey, const char* password) {
        DPS_KeyCert* key = (DPS_KeyCert*)calloc(1, sizeof(DPS_KeyCert));
        if (cert) {
            key->cert = strdup(cert);
        }
        if (privateKey) {
            key->privateKey = strdup(privateKey);
        }
        if (password) {
            key->password = strdup(password);
        }
        return key;
    }
    _DPS_KeyCert(const char* cert) {
        DPS_KeyCert* key = (DPS_KeyCert*)calloc(1, sizeof(DPS_KeyCert));
        if (cert) {
            key->cert = strdup(cert);
        }
        return key;
    }
    ~_DPS_KeyCert() {
        if ($self) {
            if ($self->cert) free((char*)$self->cert);
            if ($self->privateKey) free((char*)$self->privateKey);
            if ($self->password) free((char*)$self->password);
            free($self);
        }
    }
}

%{
const DPS_KeyType _DPS_KeySymmetric_type_get(DPS_KeySymmetric*) { return DPS_KEY_SYMMETRIC; }
const DPS_KeyType _DPS_KeyEC_type_get(DPS_KeyEC*) { return DPS_KEY_EC; }
const DPS_KeyType _DPS_KeyCert_type_get(DPS_KeyCert*) { return DPS_KEY_EC_CERT; }
%}

%typemap(in) const DPS_KeyId* (DPS_KeyId keyId, int res = 0) {
    res = AsVal_bytes($input, (uint8_t**)&keyId.id, &keyId.len);
    if (!SWIG_IsOK(res)) {
        SWIG_exception_fail(SWIG_ArgError(res), "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
    }
    if (keyId.len) {
        $1 = &keyId;
    }
}
%typemap(freearg) const DPS_KeyId* {
    if (SWIG_IsNewObj(res$argnum) && $1) {
        delete[] $1->id;
    }
}

%typemap(in) DPS_KeyAndIdHandler (Handler* handler = NULL) {
    handler = new Handler($input);
    $1 = KeyAndIdHandler;
}
%typemap(in) DPS_KeyHandler (Handler* handler = NULL) {
    handler = new Handler($input);
    $1 = KeyHandler;
}
%typemap(in) DPS_EphemeralKeyHandler (Handler* handler = NULL) {
    handler = new Handler($input);
    $1 = EphemeralKeyHandler;
}
%typemap(in) DPS_CAHandler (Handler* handler = NULL) {
    handler = new Handler($input);
    $1 = CAHandler;
}

%typemap(out) DPS_KeyStore* {
    KeyStore* keyStore = new KeyStore(handler1, handler2, handler3, handler4);
    DPS_SetKeyStoreData(result, keyStore);
    $result = SWIG_NewPointerObj(SWIG_as_voidptr(result), $1_descriptor, 0 |  0 );
}

%{
void DestroyKeyStore(DPS_KeyStore* keyStore)
{
    KeyStore* ks = (KeyStore*)DPS_GetKeyStoreData(keyStore);
    delete ks;
    DPS_DestroyKeyStore(keyStore);
}
%}
void DestroyKeyStore(DPS_KeyStore* keyStore);

%{
DPS_Status SetCertificate(DPS_MemoryKeyStore* mks, const char* cert)
{
    return DPS_SetCertificate(mks, cert, NULL, NULL);
}
%}
DPS_Status SetCertificate(DPS_MemoryKeyStore* mks, const char* cert);

%{
DPS_Node* CreateNode(const char* separators)
{
    return DPS_CreateNode(separators, NULL, NULL);
}
DPS_Node* CreateNode(const char* separators, DPS_MemoryKeyStore* keyStore, const DPS_KeyId* keyId)
{
    return DPS_CreateNode(separators, DPS_MemoryKeyStoreHandle(keyStore), keyId);
}
%}
DPS_Node* CreateNode(const char* separators);
DPS_Node* CreateNode(const char* separators, DPS_MemoryKeyStore* keyStore, const DPS_KeyId* keyId);

%typemap(in) (DPS_OnNodeDestroyed cb, void* data) {
    $1 = OnNodeDestroyed;
    $2 = new Handler($input);
}

%typemap(in) (DPS_OnLinkComplete cb, void* data) {
    $1 = OnLinkComplete;
    $2 = new Handler($input);
}

%typemap(in) (DPS_OnUnlinkComplete cb, void* data) {
    $1 = OnNodeAddressComplete;
    $2 = new Handler($input);
}

%typemap(in) (DPS_OnResolveAddressComplete cb, void* data) {
    $1 = OnNodeAddressComplete;
    $2 = new Handler($input);
}

/*
 * Matching on multiple arguments requires the name unfortunately.
 */
%typemap(default) (const uint8_t* pubPayload, size_t len) {
    $1 = NULL;
    $2 = 0;
}
%typemap(in) (const uint8_t* pubPayload, size_t len) (int res = 0) {
    res = AsVal_bytes($input, &$1, &$2);
    if (!SWIG_IsOK(res)) {
        SWIG_exception_fail(SWIG_ArgError(res), "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
    }
}
%typemap(freearg) (const uint8_t* pubPayload, size_t len) {
    if (SWIG_IsNewObj(res$argnum)) {
        delete[] $1;
    }
}

%typemap(default) (const uint8_t* ackPayload, size_t len) {
    $1 = NULL;
    $2 = 0;
}
%typemap(in) (const uint8_t* ackPayload, size_t len) (int res = 0) {
    res = AsVal_bytes($input, &$1, &$2);
    if (!SWIG_IsOK(res)) {
        SWIG_exception_fail(SWIG_ArgError(res), "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
    }
}
%typemap(freearg) (const uint8_t* ackPayload, size_t len) {
    if (SWIG_IsNewObj(res$argnum)) {
        delete[] $1;
    }
}

%typemap(in,numinputs=0,noblock=1) size_t* n {
    size_t sz;
    $1 = &sz;
}

%typemap(out) const char** PublicationGetTopics {
    $result = From_topics((const char**)$1, sz);
    free($1);
}

%{
const char** PublicationGetTopics(const DPS_Publication* pub, size_t* n)
{
    *n = DPS_PublicationGetNumTopics(pub);
    const char** topics = (const char**)calloc(*n, sizeof(const char *));
    for (size_t i = 0; i < *n; ++i) {
        topics[i] = DPS_PublicationGetTopic(pub, i);
    }
    return topics;
}
%}
const char** PublicationGetTopics(const DPS_Publication* pub, size_t* n);

%typemap(in) DPS_AcknowledgementHandler {
    $1 = AcknowledgementHandler;
    DPS_SetPublicationData(arg1, new Handler($input));
}

%{
void DestroyPublication(DPS_Publication* pub)
{
    Handler* handler = (Handler*)DPS_GetPublicationData(pub);
    delete handler;
    DPS_DestroyPublication(pub);
}
%}
void DestroyPublication(DPS_Publication* pub);

%typemap(in) DPS_PublicationHandler {
    $1 = PublicationHandler;
    DPS_SetSubscriptionData(arg1, new Handler($input));
}

%typemap(out) const char** SubscriptionGetTopics {
    $result = From_topics((const char**)$1, sz);
    free($1);
}

%{
const char** SubscriptionGetTopics(const DPS_Subscription* sub, size_t* n)
{
    *n = DPS_SubscriptionGetNumTopics(sub);
    const char** topics = (const char**)calloc(*n, sizeof(const char *));
    for (size_t i = 0; i < *n; ++i) {
        topics[i] = DPS_SubscriptionGetTopic(sub, i);
    }
    return topics;
}
%}
const char** SubscriptionGetTopics(const DPS_Subscription* sub, size_t* n);

%{
void DestroySubscription(DPS_Subscription* sub)
{
    Handler* handler = (Handler*)DPS_GetSubscriptionData(sub);
    delete handler;
    DPS_DestroySubscription(sub);
}
%}
void DestroySubscription(DPS_Subscription* sub);

%typemap(in,numinputs=0,noblock=1) uint8_t** cbor {
    uint8_t* cbor;
    cbor = NULL;
    $1 = &cbor;
}
%typemap(out) DPS_Status JSON2CBOR {
    if ($1 == DPS_OK) {
        $result = From_bytes(cbor, sz);
    }
    delete[] cbor;
    if ($1 != DPS_OK) {
        SWIG_exception_fail(SWIG_ERROR, "in method '" "$symname" "'");
    }
}

%typemap(default) (const uint8_t* cbor, size_t len) {
    $1 = NULL;
    $2 = 0;
}
%typemap(default) (int pretty) {
    $1 = DPS_FALSE;
}
%typemap(in) (const uint8_t* cbor, size_t len, int res) {
    res = AsVal_bytes($input, &$1, &$2);
    if (!SWIG_IsOK(res)) {
        SWIG_exception_fail(SWIG_ArgError(res), "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
    }
}
%typemap(in,numinputs=0,noblock=1) char** json {
    char* json;
    json = NULL;
    $1 = &json;
}
%typemap(freearg) (const uint8_t* cbor, size_t len) {
    if (SWIG_IsNewObj(res$argnum)) {
        delete[] $1;
    }
}
%typemap(out) DPS_Status CBOR2JSON {
    if ($1 == DPS_OK) {
        $result = SWIG_FromCharPtr(json);
    }
    delete[] json;
    if ($1 != DPS_OK) {
        SWIG_exception_fail(SWIG_ERROR, "in method '" "$symname" "'");
    }
}

%{
DPS_Status JSON2CBOR(const char* json, uint8_t** cbor, size_t* n)
{
    DPS_Status ret;
    size_t sz = 256;

    do {
        delete[] *cbor;
        sz *= 2;
        *cbor = new uint8_t[sz];
        ret = DPS_JSON2CBOR(json, *cbor, sz, n);
    } while (ret == DPS_ERR_OVERFLOW);
    if (ret != DPS_OK) {
        delete[] *cbor;
        *cbor = NULL;
        *n = 0;
    }
    return ret;
}

/*
 * Note reordering of json to last argument: numinputs=0 doesn't
 * appear to work correctly for JavaScript generation when argument is
 * not last.
 */
DPS_Status CBOR2JSON(const uint8_t* cbor, size_t len, int pretty, char** json)
{
    DPS_Status ret;
    size_t sz = 256;

    do {
        delete[] *json;
        sz *= 2;
        *json = new char[sz];
        ret = DPS_CBOR2JSON(cbor, len, *json, sz, pretty);
    } while (ret == DPS_ERR_OVERFLOW);
    if (ret != DPS_OK) {
        delete[] *json;
        *json = NULL;
    }
    return ret;
}
%}
DPS_Status JSON2CBOR(const char* json, uint8_t** cbor, size_t* n);
DPS_Status CBOR2JSON(const uint8_t* cbor, size_t len, int pretty, char** json);

/*
 * These are workarounds for uninitialized variable warnings in the generated code
 */
%typemap(default) int noWildCard {
    $1 = 0;
}
%typemap(default) int16_t {
    $1 = 0;
}

%typemap(default) (const uint8_t *bytes, size_t n) {
    $1 = NULL;
    $2 = 0;
}
%typemap(in) (const uint8_t* bytes, size_t n) (int res = 0) {
    res = AsVal_bytes($input, &$1, &$2);
    if (!SWIG_IsOK(res)) {
        SWIG_exception_fail(SWIG_ArgError(res), "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
    }
}
%typemap(freearg) (const uint8_t* bytes, size_t n) {
    if (SWIG_IsNewObj(res$argnum)) {
        delete[] $1;
    }
}

%typemap(in) const DPS_UUID* {
    void *argp = NULL;
    int res = SWIG_ConvertPtr($input, &argp ,SWIGTYPE_p__DPS_UUID, 0 | 0);
    if (!SWIG_IsOK(res)) {
        SWIG_exception_fail(SWIG_ArgError(res), "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'"); 
    }
    $1 = (DPS_UUID *)(argp);
}
%typemap(argout) const DPS_UUID* {
    /* Define empty typemap to prevent non-const typemap from being used */
}

%typemap(in,numinputs=0) DPS_UUID* {
    $1 = new _DPS_UUID();
}
%typemap(argout) DPS_UUID* {
    $result = SWIG_NewPointerObj(SWIG_as_voidptr($1), SWIGTYPE_p__DPS_UUID, SWIG_POINTER_OWN);
}

%include <dps/dbg.h>
%include <dps/dps.h>
%include <dps/err.h>
%include <dps/event.h>
%include <dps/json.h>
%include <dps/synchronous.h>
%include <dps/uuid.h>

%include "dps_impl.i"

%init %{
    InitializeModule();
%}
