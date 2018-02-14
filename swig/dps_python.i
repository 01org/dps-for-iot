%module(docstring="Distributed Publish Subscribe for IoT") dps
%feature("autodoc", "1");

%{
extern "C" {
#include <dps/dps.h>
#include <dps/dbg.h>
#include <dps/synchronous.h>
#include <safe_lib.h>
};
#include "dps.hh"

/*
 * Debug control for this module
 */
DPS_DEBUG_CONTROL(DPS_DEBUG_ON);
%}

%include "cdata.i"
%include "constraints.i"
%include "typemaps.i"

/*
 * This warning is not relevant
 */
%warnfilter(451) _DPS_KeyCert;

/*
 * Ignore this warning generated by the nested union inside DPS_Key.
 * Suppressing it with warnfilter doesn't appear to work.
 */
#pragma SWIG nowarn=312

/*
 * Functions that must not be exposed in Python
 */
%ignore DPS_CreateNode;
%ignore DPS_DestroyKeyStore;
%ignore DPS_GetKeyStoreData;
%ignore DPS_GetLoop;
%ignore DPS_GetPublicationData;
%ignore DPS_GetSubscriptionData;
%ignore DPS_KeyStoreHandle;
%ignore DPS_MemoryKeyStoreHandle;
%ignore DPS_PublicationGetNumTopics;
%ignore DPS_PublicationGetTopic;
%ignore DPS_SetKeyStoreData;
%ignore DPS_SetPublicationData;
%ignore DPS_SetSubscriptionData;
%ignore DPS_SubscriptionGetNumTopics;
%ignore DPS_SubscriptionGetTopic;
%ignore _DPS_Key;
%ignore _DPS_KeyCert;
%ignore _DPS_KeyEC;
%ignore _DPS_KeyId;
%ignore _DPS_KeySymmetric;

/*
 * Declarations that are not relevant in Python
 */
%ignore DPS_TRUE;
%ignore DPS_FALSE;

/*
 * Only exposing the synchronous versions of these
 */
%ignore DPS_Link;
%ignore DPS_Unlink;
%ignore DPS_ResolveAddress;

/*
 * Module is called dps we don't need the DPS prefix on every function.
 * Note: can't combine strip and undercase, so regex instead.
 */
%rename("debug") DPS_Debug;
%rename("set_ca") DPS_SetCA;
%rename("set_trusted_ca") DPS_SetTrustedCA;
%rename("%(regex:/DPS_([A-Z][a-z0-9]+|UUID)/\\L\\1/)s", %$isfunction) "";
%rename("%(regex:/DPS_([A-Z][a-z0-9]+|UUID)([A-Z][a-z0-9]+|UUID)/\\L\\1_\\L\\2/)s", %$isfunction) "";
%rename("%(regex:/DPS_([A-Z][a-z0-9]+|UUID)([A-Z][a-z0-9]+|UUID)([A-Z][a-z0-9]+|UUID)/\\L\\1_\\L\\2_\\L\\3/)s", %$isfunction) "";
%rename("%(regex:/DPS_([A-Z][a-z0-9]+|UUID)([A-Z][a-z0-9]+|UUID)([A-Z][a-z0-9]+|UUID)([A-Z][a-z0-9]+|UUID)/\\L\\1_\\L\\2_\\L\\3_\\L\\4/)s", %$isfunction) "";
%rename("%(strip:[DPS_])s", %$not %$isfunction) "";

/*
 * Mapping for types from stdint.h
 */
%typemap(in) uint8_t* = char*;
%typemap(in) int16_t = int;
%typemap(out) int16_t = int;
%typemap(in) uint16_t = unsigned int;
%typemap(out) uint16_t = unsigned int;
%typemap(in) uint32_t = unsigned long;
%typemap(out) uint32_t = unsigned long;
%typemap(in) DPS_UUID* = PyObject*;

/*
 * Debug control
 */
%inline %{
extern int DPS_Debug;
%}

/*
 * This allows topic strings to be expressed as a list of strings
 */
%typemap(in) (const char** topics, size_t numTopics) {
    /* Expecting a list of strings */
    if (PyList_Check($input)) {
        Py_ssize_t i;
        Py_ssize_t sz = PyList_Size($input);
        $1 = (char**)malloc((sz + 1) * sizeof(char*));
        for (i = 0; i < sz; ++i) {
            PyObject *ob = PyList_GetItem($input, i);
            if (PyString_Check(ob))
                $1[i] = PyString_AsString(ob);
            else {
                PyErr_SetString(PyExc_TypeError,"must be a list of one or more strings");
                SWIG_fail;
            }
        }
        $1[i] = 0;
        $2 = sz;
    } else {
        PyErr_SetString(PyExc_TypeError,"not a list");
        SWIG_fail;
    }
}

/*
 * Post function call cleanup for topic strings
 */
%typemap(freearg) (const char** topics, size_t numTopics) {
    free($1);
}

%typemap(in,numinputs=0,noblock=1) size_t* n {
  size_t sz;
  $1 = &sz;
}

%typemap(out) const char** subscription_get_topics {
    $result = PyList_New(sz);
    for (size_t i = 0; i < sz; ++i) {
        PyList_SetItem($result, i, PyUnicode_FromString($1[i]));
    }
    free($1);
}

%inline %{
const char** subscription_get_topics(const DPS_Subscription* sub, size_t* n)
{
    *n = DPS_SubscriptionGetNumTopics(sub);
    const char** topics = (const char**)calloc(*n, sizeof(const char *));
    for (size_t i = 0; i < *n; ++i) {
        topics[i] = DPS_SubscriptionGetTopic(sub, i);
    }
    return topics;
}
%}

%typemap(out) const char** publication_get_topics {
    $result = PyList_New(sz);
    for (size_t i = 0; i < sz; ++i) {
        PyList_SetItem($result, i, PyUnicode_FromString($1[i]));
    }
    free($1);
}

%inline %{
const char** publication_get_topics(const DPS_Publication* pub, size_t* n)
{
    *n = DPS_PublicationGetNumTopics(pub);
    const char** topics = (const char**)calloc(*n, sizeof(const char *));
    for (size_t i = 0; i < *n; ++i) {
        topics[i] = DPS_PublicationGetTopic(pub, i);
    }
    return topics;
}
%}

%{
/*
 * For now just allow strings as payloads.
 * Eventually need to figure out how to handle binary data.
 */
static uint8_t* AllocPayload(PyObject* py, size_t* len)
{
    uint8_t* str = NULL;
    if (PyString_Check(py)) {
        Py_ssize_t sz = PyString_Size(py);
        str = (uint8_t*)malloc(sz + 1);
        memcpy_s(str, sz, PyString_AsString(py), sz);
        str[sz] = 0;
        *len = (size_t)sz;
    } else {
        PyErr_SetString(PyExc_TypeError,"not a string");
    }
    return str;
}
%}

%typemap(in) (const uint8_t* pubPayload, size_t len) {
    $1 = AllocPayload($input, &$2);
    if (!$1) {
        PyErr_SetString(PyExc_MemoryError,"Allocation of pub payload failed");
        SWIG_fail;
    }
}

%typemap(in) (const uint8_t* ackPayload, size_t len) {
    $1 = AllocPayload($input, &$2);
    if (!$1) {
        PyErr_SetString(PyExc_MemoryError,"Allocation of ack payload failed");
        SWIG_fail;
    }
}

%typemap(freearg) (const uint8_t* pubPayload, size_t len) {
    free($1);
}

%typemap(freearg) (const uint8_t* ackPayload, size_t len) {
    free($1);
}

/*
 * Type maps for default arguments
 */

%typemap(default) const char* separators {
    $1 = NULL;
}

%typemap(default) DPS_AcknowledgementHandler {
    $1 = NULL;
}

%typemap(default) (const uint8_t* payload, size_t len) {
    $1 = NULL;
    $2 = 0;
}

%typemap(default) (int16_t ttl) {
    $1 = 0;
}

%typemap(default) (DPS_OnNodeDestroyed cb, void* data) {
    $1 = NULL;
    $2 = NULL;
}

%typemap(default) (const char* key, const char* password) {
    $1 = NULL;
    $2 = NULL;
}

%inline %{
static void _ClearAckHandler(DPS_Publication* pub)
{
    PyObject* cb = (PyObject*)DPS_GetPublicationData(pub);
    Py_XDECREF(cb);
}
%}

/*
 * Dereference the python callback function when freeing a publication
 */
%pythonprepend DPS_DestroyPublication {
   _ClearAckHandler(pub)
}

/*
 * Publication acknowledgment function calls into Python
 */
%{
static void AckHandler(DPS_Publication* pub, uint8_t* payload, size_t len)
{
    PyObject* cb = (PyObject*)DPS_GetPublicationData(pub);
    PyObject* pubObj;
    PyObject* ret;
    PyGILState_STATE gilState;

    if (!cb) {
        return;
    }
    /*
     * This callback was called from an external thread so we
     * need to get the Global-Interpreter-Interlock before we
     * can call into the Python interpreter.
     */
    gilState = PyGILState_Ensure();

    pubObj = SWIG_NewPointerObj(SWIG_as_voidptr(pub), SWIGTYPE_p__DPS_Publication, 0);
    ret = PyObject_CallFunction(cb, (char*)"Os#", pubObj, payload, len);
    Py_XDECREF(pubObj);
    Py_XDECREF(ret);
    /*
     * All done we can release the lock
     */
    PyGILState_Release(gilState);
}
%}

/*
 * Acknowledgement callback wrapper
 */
%typemap(in) DPS_AcknowledgementHandler {
    if (!PyCallable_Check($input)) {
        PyErr_SetString(PyExc_TypeError,"not a function");
        SWIG_fail;
    }
    if (arg1) {
        DPS_Status ret = DPS_SetPublicationData(arg1, $input);
        if (ret != DPS_OK) {
            PyErr_SetString(PyExc_EnvironmentError,"unable to set callback");
            SWIG_fail;
        }
        Py_INCREF($input);
        $1 = AckHandler;
    }
}

/*
 * Publication received callback call into Python function
 */
%{
void PubHandler(DPS_Subscription* sub, const DPS_Publication* pub, uint8_t* payload, size_t len)
{
    PyObject* cb = (PyObject*)DPS_GetSubscriptionData(sub);
    PyObject* pubObj;
    PyObject* subObj;
    PyObject* ret;
    PyGILState_STATE gilState;

    if (!cb) {
        DPS_ERRPRINT("Callback is NULL\n");
        return;
    }
    DPS_DBGPRINT("PubHandler\n");
    /*
     * This callback was called from an external thread so we
     * need to get the Global-Interpreter-Interlock before we
     * can call into the Python interpreter.
     */
    gilState = PyGILState_Ensure();

    pubObj = SWIG_NewPointerObj(SWIG_as_voidptr(pub), SWIGTYPE_p__DPS_Publication, 0);
    subObj = SWIG_NewPointerObj(SWIG_as_voidptr(sub), SWIGTYPE_p__DPS_Subscription, 0);
    ret = PyObject_CallFunction(cb, (char*)"OOs#", subObj, pubObj, payload, len);
    Py_XDECREF(pubObj);
    Py_XDECREF(subObj);
    Py_XDECREF(ret);
    /*
     * All done we can release the lock
     */
    PyGILState_Release(gilState);
}
%}

%inline %{
static void _ClearPubHandler(DPS_Subscription* sub)
{
    PyObject* cb = (PyObject*)DPS_GetSubscriptionData(sub);
    Py_XDECREF(cb);
}
%}

/*
 * Dereference the python callback function when freeing a publication
 */
%pythonprepend DPS_DestroySubscription {
   _ClearPubHandler(sub)
}

/*
 * Publication callback wrapper
 */
%typemap(in) DPS_PublicationHandler {
    if (!PyCallable_Check($input)) {
        PyErr_SetString(PyExc_TypeError,"not a function");
        SWIG_fail;
    }
    if (arg1) {
        DPS_Status ret = DPS_SetSubscriptionData(arg1, $input);
        if (ret != DPS_OK) {
            PyErr_SetString(PyExc_EnvironmentError,"unable to set callback");
            SWIG_fail;
        }
        Py_INCREF($input);
        $1 = PubHandler;
    }
}

/*
 * Key function calls Python
 */
%{
SWIGINTERN int SWIG_AsVal_int(PyObject* obj, int* val);

static PyObject* WrapBytes(const uint8_t* bytes, size_t len)
{
    return PyByteArray_FromStringAndSize((const char*)bytes, len);
}

static int UnwrapBytes(PyObject* obj, uint8_t** bytes, size_t* len)
{
    if (obj == Py_None) {
        (*bytes) = NULL;
    } else if (PyByteArray_Check(obj)) {
        (*len) = PyByteArray_Size(obj);
        (*bytes) = (uint8_t*)malloc((*len) * sizeof(uint8_t));
        if (!(*bytes)) {
            return SWIG_MemoryError;
        }
        memcpy((*bytes), PyByteArray_AsString(obj), (*len));
    } else if (PyList_Check(obj)) {
        (*len) = PyList_Size(obj);
        (*bytes) = (uint8_t*)malloc((*len) * sizeof(uint8_t));
        if (!(*bytes)) {
            return SWIG_MemoryError;
        }
        for (size_t i = 0; i < (*len); ++i) {
            PyObject *pValue = PyList_GetItem(obj, i);
            if (PyInt_Check(pValue)) {
                int32_t v = PyInt_AsLong(pValue);
                if (v >= 0 && v <= 255) {
                    (*bytes)[i] = (uint8_t)v;
                } else {
                    free((*bytes));
                    return SWIG_TypeError;
                }
            }
        }
    } else if (PyString_Check(obj)) {
        int alloc = SWIG_NEWOBJ;
        int res = SWIG_AsCharPtrAndSize(obj, (char**)bytes, len, &alloc);
        if (!SWIG_IsOK(res)) {
            return res;
        }
        --(*len); /* Don't include NUL terminator in length */
    } else {
       return SWIG_TypeError;
    }
    return SWIG_OK;
}

static void FreeBytes(uint8_t* bytes) {
    if (bytes) {
        free(bytes);
    }
}

typedef struct _KeyStoreFunctions {
    PyObject* keyAndId;
    PyObject* key;
    PyObject* ephemeralKey;
    PyObject* ca;
} KeyStoreFunctions;

static DPS_Status KeyAndIdHandler(DPS_KeyStoreRequest* request)
{
    DPS_Status status = DPS_ERR_MISSING;
    DPS_KeyStore* keyStore = DPS_KeyStoreHandle(request);
    KeyStoreFunctions* functions = (KeyStoreFunctions*)DPS_GetKeyStoreData(keyStore);
    PyObject* requestObj;
    PyObject* ret;
    PyGILState_STATE gilState;

    /*
     * This callback was called from an external thread so we
     * need to get the Global-Interpreter-Interlock before we
     * can call into the Python interpreter.
     */
    gilState = PyGILState_Ensure();

    requestObj = SWIG_NewPointerObj(SWIG_as_voidptr(request), SWIGTYPE_p__DPS_KeyStoreRequest, 0);
    ret = PyObject_CallFunction(functions->keyAndId, (char*)"O", requestObj);
    if (ret) {
        SWIG_AsVal_int(ret, &status);
    }
    Py_XDECREF(requestObj);
    Py_XDECREF(ret);
    /*
     * All done we can release the lock
     */
    PyGILState_Release(gilState);
    return status;
}

static DPS_Status KeyHandler(DPS_KeyStoreRequest* request, const DPS_KeyId* keyId)
{
    DPS_Status status = DPS_ERR_MISSING;
    DPS_KeyStore* keyStore = DPS_KeyStoreHandle(request);
    KeyStoreFunctions* functions = (KeyStoreFunctions*)DPS_GetKeyStoreData(keyStore);
    PyObject* requestObj;
    PyObject* keyIdObj;
    PyObject* ret;
    PyGILState_STATE gilState;

    /*
     * This callback was called from an external thread so we
     * need to get the Global-Interpreter-Interlock before we
     * can call into the Python interpreter.
     */
    gilState = PyGILState_Ensure();

    requestObj = SWIG_NewPointerObj(SWIG_as_voidptr(request), SWIGTYPE_p__DPS_KeyStoreRequest, 0);
    if (keyId) {
        keyIdObj = WrapBytes(keyId->id, keyId->len);
    } else {
        keyIdObj = Py_None;
    }
    ret = PyObject_CallFunction(functions->key, (char*)"OO", requestObj, keyIdObj);
    if (ret) {
        SWIG_AsVal_int(ret, &status);
    }
    Py_XDECREF(requestObj);
    Py_XDECREF(keyIdObj);
    Py_XDECREF(ret);
    /*
     * All done we can release the lock
     */
    PyGILState_Release(gilState);
    return status;
}

static DPS_Status EphemeralKeyHandler(DPS_KeyStoreRequest* request, const DPS_Key* key)
{
    DPS_Status status = DPS_ERR_MISSING;
    DPS_KeyStore* keyStore = DPS_KeyStoreHandle(request);
    KeyStoreFunctions* functions = (KeyStoreFunctions*)DPS_GetKeyStoreData(keyStore);
    PyObject* requestObj;
    PyObject* keyObj = Py_None;
    PyObject* ret = NULL;
    PyGILState_STATE gilState;

    /*
     * This callback was called from an external thread so we
     * need to get the Global-Interpreter-Interlock before we
     * can call into the Python interpreter.
     */
    gilState = PyGILState_Ensure();

    requestObj = SWIG_NewPointerObj(SWIG_as_voidptr(request), SWIGTYPE_p__DPS_KeyStoreRequest, 0);
    switch (key->type) {
    case DPS_KEY_SYMMETRIC: {
        dps::SymmetricKey k(key->symmetric.key, key->symmetric.len);
        keyObj = SWIG_NewPointerObj(SWIG_as_voidptr(&k), SWIGTYPE_p_dps__SymmetricKey, 0);
        ret = PyObject_CallFunction(functions->ephemeralKey, (char*)"OO", requestObj, keyObj);
        break;
    }
    case DPS_KEY_EC: {
        dps::ECKey k(key->ec.curve, key->ec.x, key->ec.y, key->ec.d);
        keyObj = SWIG_NewPointerObj(SWIG_as_voidptr(&k), SWIGTYPE_p_dps__ECKey, 0);
        ret = PyObject_CallFunction(functions->ephemeralKey, (char*)"OO", requestObj, keyObj);
        break;
    }
    case DPS_KEY_EC_CERT: {
        dps::CertKey k(key->cert.cert, key->cert.privateKey, key->cert.password);
        keyObj = SWIG_NewPointerObj(SWIG_as_voidptr(&k), SWIGTYPE_p_dps__CertKey, 0);
        ret = PyObject_CallFunction(functions->ephemeralKey, (char*)"OO", requestObj, keyObj);
        break;
    }
    }
    if (ret) {
        SWIG_AsVal_int(ret, &status);
    }
    Py_XDECREF(requestObj);
    Py_XDECREF(keyObj);
    Py_XDECREF(ret);
    /*
     * All done we can release the lock
     */
    PyGILState_Release(gilState);
    return status;
}

static DPS_Status CAHandler(DPS_KeyStoreRequest* request)
{
    DPS_Status status = DPS_ERR_MISSING;
    DPS_KeyStore* keyStore = DPS_KeyStoreHandle(request);
    KeyStoreFunctions* functions = (KeyStoreFunctions*)DPS_GetKeyStoreData(keyStore);
    PyObject* requestObj;
    PyObject* ret;
    PyGILState_STATE gilState;

    /*
     * This callback was called from an external thread so we
     * need to get the Global-Interpreter-Interlock before we
     * can call into the Python interpreter.
     */
    gilState = PyGILState_Ensure();

    requestObj = SWIG_NewPointerObj(SWIG_as_voidptr(request), SWIGTYPE_p__DPS_KeyStoreRequest, 0);
    ret = PyObject_CallFunction(functions->ca, (char*)"O", requestObj);
    if (ret) {
        SWIG_AsVal_int(ret, &status);
    }
    Py_XDECREF(requestObj);
    Py_XDECREF(ret);
    /*
     * All done we can release the lock
     */
    PyGILState_Release(gilState);
    return status;
}
%}

/*
 * Key callback wrapper
 */
%typemap(in) DPS_KeyAndIdHandler (PyObject* obj) {
    if (PyCallable_Check($input)) {
        obj = $input;
        Py_INCREF(obj);
        $1 = KeyAndIdHandler;
    } else {
        obj = Py_None;
        $1 = NULL;
    }
}

%typemap(in) DPS_KeyHandler (PyObject* obj) {
    if (PyCallable_Check($input)) {
        obj = $input;
        Py_INCREF(obj);
        $1 = KeyHandler;
    } else {
        obj = Py_None;
        $1 = NULL;
    }
}

%typemap(in) DPS_EphemeralKeyHandler (PyObject* obj) {
    if (PyCallable_Check($input)) {
        obj = $input;
        Py_INCREF(obj);
        $1 = EphemeralKeyHandler;
    } else {
        obj = Py_None;
        $1 = NULL;
    }
}

%typemap(in) DPS_CAHandler (PyObject* obj) {
    if (PyCallable_Check($input)) {
        obj = $input;
        Py_INCREF(obj);
        $1 = CAHandler;
    } else {
        obj = Py_None;
        $1 = NULL;
    }
}

%typemap(out) DPS_KeyStore* {
    KeyStoreFunctions* functions = (KeyStoreFunctions*)malloc(sizeof(KeyStoreFunctions));
    if (!functions) {
        PyErr_SetString(PyExc_MemoryError,"Allocation of key store functions failed");
        SWIG_fail;
    }
    functions->keyAndId = obj1;
    functions->key = obj2;
    functions->ephemeralKey = obj3;
    functions->ca = obj4;
    DPS_SetKeyStoreData(result, functions);
    $result = SWIG_NewPointerObj(SWIG_as_voidptr(result), $1_descriptor, 0 | 0);
}

%{
void destroy_key_store(DPS_KeyStore* keyStore)
{
    KeyStoreFunctions* functions = (KeyStoreFunctions*)DPS_GetKeyStoreData(keyStore);
    Py_XDECREF(functions->keyAndId);
    Py_XDECREF(functions->key);
    Py_XDECREF(functions->ephemeralKey);
    Py_XDECREF(functions->ca);
    free(functions);
    DPS_DestroyKeyStore(keyStore);
}
%}

void destroy_key_store(DPS_KeyStore* keyStore);

%{
static PyObject* UUIDToPyString(const DPS_UUID* uuid)
{
    const char* uuidStr = DPS_UUIDToString(uuid);
    if (uuidStr) {
        return PyString_FromString(uuidStr);
    } else {
        Py_RETURN_NONE;
    }
}
%}

%typemap(in) DPS_UUID* {
    DPS_UUID* uuid = NULL;

    if ($input != Py_None) {
        int j, i;
        if (!PyList_Check($input)) {
            PyErr_SetString(PyExc_TypeError,"DPS_UUID: not a list\n");
            SWIG_fail;
        }
        uuid = (DPS_UUID*)malloc(sizeof(DPS_UUID));
        if (!uuid) {
            PyErr_SetString(PyExc_MemoryError,"DPS_UUID: no memory\n");
            SWIG_fail;
        }
        for (j = 0, i = 0; j < PyList_Size($input); ++j) {
            PyObject *pValue = PyList_GetItem($input, j);
            if (PyInt_Check(pValue) && i < sizeof(DPS_UUID)) {
                int32_t v = PyInt_AsLong(pValue);
                if (v >= 0 && v <= 255) {
                    uuid->val[i++] = (uint8_t)v;
                } else {
                    PyErr_SetString(PyExc_TypeError,"uuid values must be in range 0..255");
                    free(uuid);
                    SWIG_fail;
                }
            } else {
                PyErr_SetString(PyExc_TypeError,"value is not int type or len > uuid");
                free(uuid);
                SWIG_fail;
            }
        }
    }
    $1 = uuid;
}

%typemap(freearg) DPS_UUID* {
    if ($1) {
        free($1);
    }
}

%typemap(out) DPS_UUID* {
    $result = UUIDToPyString($1);
}

%typemap(out) const DPS_UUID* {
    $result = UUIDToPyString($1);
}

/*
 * Used in DPS_SetContentKey.
 */
%typemap(in) (const DPS_Key* key) {
    DPS_Key* k = NULL;
    void* argp = NULL;

    if ($input == Py_None) {
        k = NULL;
    } else if (PyByteArray_Check($input) || PyList_Check($input)) {
        k = (DPS_Key*)calloc(1, sizeof(DPS_Key));
        if (!k) {
            SWIG_fail;
        }
        k->type = DPS_KEY_SYMMETRIC;
        if (!SWIG_IsOK(UnwrapBytes($input, (uint8_t**)&k->symmetric.key, &k->symmetric.len))) {
            free(k);
            SWIG_fail;
        }
    } else if (SWIG_IsOK(SWIG_ConvertPtr($input, &argp, SWIGTYPE_p_dps__SymmetricKey, 0 | 0))) {
        DPS_Key* key = (dps::SymmetricKey*)argp;
        k = (DPS_Key*)calloc(1, sizeof(DPS_Key));
        if (!k) {
            SWIG_exception_fail(SWIG_ERROR, "no memory");
        }
        k->type = DPS_KEY_SYMMETRIC;
        k->symmetric.len = key->symmetric.len;
        k->symmetric.key = (uint8_t*)malloc(k->symmetric.len);
        memcpy((uint8_t*)k->symmetric.key, key->symmetric.key, k->symmetric.len);
    } else if (SWIG_IsOK(SWIG_ConvertPtr($input, &argp, SWIGTYPE_p_dps__ECKey, 0 | 0))) {
        DPS_Key* key = (dps::ECKey*)argp;
        k = (DPS_Key*)calloc(1, sizeof(DPS_Key));
        if (!k) {
            SWIG_exception_fail(SWIG_ERROR, "no memory");
        }
        memcpy(k, key, sizeof(*k));
    } else if (SWIG_IsOK(SWIG_ConvertPtr($input, &argp, SWIGTYPE_p_dps__CertKey, 0 | 0))) {
        DPS_Key* key = (dps::CertKey*)argp;
        k = (DPS_Key*)calloc(1, sizeof(DPS_Key));
        if (!k) {
            SWIG_exception_fail(SWIG_ERROR, "no memory");
        }
        memcpy(k, key, sizeof(*k));
    }

    $1 = k;
}

%typemap(freearg) (const DPS_Key* key) {
    if ($1) {
        if (($1->type == DPS_KEY_SYMMETRIC) && $1->symmetric.key) {
            free((uint8_t*)$1->symmetric.key);
        }
        free($1);
    }
}

%typemap(in) (const DPS_KeyId* keyId) {
    DPS_KeyId* kid = NULL;

    if ($input != Py_None) {
        kid = (DPS_KeyId*)malloc(sizeof(DPS_KeyId));
        if (!kid) {
            SWIG_fail;
        }
        if (!SWIG_IsOK(UnwrapBytes($input, (uint8_t**)&kid->id, &kid->len))) {
            free(kid);
            SWIG_fail;
        }
    }

    $1 = kid;
}

%typemap(freearg) (const DPS_KeyId* keyId) {
    if ($1) {
        if ($1->id) {
            free((uint8_t*)$1->id);
        }
        free($1);
    }
}

%extend dps::Key {
    const DPS_KeyType type;
}

%extend dps::SymmetricKey {
    const uint8_t* const key;
}

%ignore dps::ECKey::CoordinateSize;
%extend dps::ECKey {
    const DPS_ECCurve curve;
    const uint8_t* const x;
    const uint8_t* const y;
    const uint8_t* const d;
}

%extend dps::CertKey {
    const char* const cert;
    const char* const privateKey;
    const char* const password;
}

%{
const DPS_KeyType dps_Key_type_get(dps::Key* key) { return key->type; }

const uint8_t* const dps_SymmetricKey_key_get(dps::SymmetricKey* key) { return key->symmetric.key; }

DPS_ECCurve dps_ECKey_curve_get(dps::ECKey* key) { return key->ec.curve; }
const uint8_t* const dps_ECKey_x_get(dps::ECKey* key) { return key->ec.x; }
const uint8_t* const dps_ECKey_y_get(dps::ECKey* key) { return key->ec.y; }
const uint8_t* const dps_ECKey_d_get(dps::ECKey* key) { return key->ec.d; }

const char* const dps_CertKey_cert_get(dps::CertKey* key) { return key->cert.cert; }
const char* const dps_CertKey_privateKey_get(dps::CertKey* key) { return key->cert.privateKey; }
const char* const dps_CertKey_password_get(dps::CertKey* key) { return key->cert.password; }
%}

%typemap(in) (const uint8_t* key, size_t len) {
    if (!SWIG_IsOK(UnwrapBytes($input, &$1, &$2))) {
        SWIG_exception_fail(SWIG_TypeError, "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
    }
}
%typemap(in) (const uint8_t* x), (const uint8_t* y), (const uint8_t* d) {
    size_t unused;
    if (!SWIG_IsOK(UnwrapBytes($input, &$1, &unused))) {
        SWIG_exception_fail(SWIG_TypeError, "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
    }
}

%typemap(freearg) (const uint8_t* key, size_t len),
                  (const uint8_t* x), (const uint8_t* y), (const uint8_t* d) {
    FreeBytes($1);
}

/*
 * Overloading of DPS_CreateNode not supported, so wrap DPS_CreateNode
 * and limit scope of typemap that converts a DPS_MemoryKeyStore* to a
 * DPS_KeyStore*.
 */
%typemap(in) DPS_KeyStore* {
    DPS_KeyStore* ks = NULL;
    if ($input) {
        void *argp;
        int res = SWIG_ConvertPtr($input, &argp, SWIGTYPE_p__DPS_KeyStore, 0 | 0);
        if (SWIG_IsOK(res)) {
            ks = (DPS_KeyStore*)argp;
        } else {
            res = SWIG_ConvertPtr($input, &argp, SWIGTYPE_p__DPS_MemoryKeyStore, 0 | 0);
            if (SWIG_IsOK(res)) {
                ks = DPS_MemoryKeyStoreHandle((DPS_MemoryKeyStore*)argp);
            }
        }
        if (!SWIG_IsOK(res)) {
            SWIG_exception_fail(SWIG_ArgError(res), "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
        }
    }
    $1 = ks;
}

%{
DPS_Node* create_node(const char* separators, DPS_KeyStore* keyStore, const DPS_KeyId* keyId)
{
    return DPS_CreateNode(separators, keyStore, keyId);
}
%}

DPS_Node* create_node(const char* separators, DPS_KeyStore* keyStore, const DPS_KeyId* keyId);

%typemap(in) DPS_KeyStore*;

%typemap(in) (const uint8_t* key, size_t len) {
    $2 = 0;
    if (!SWIG_IsOK(UnwrapBytes($input, &$1, &$2))) {
        SWIG_exception_fail(SWIG_TypeError, "in method '" "$symname" "', argument " "$argnum"" of type '" "$1_type""'");
    }
}

/*
 * Disallow NULL for these pointer types
 */
%apply Pointer NONNULL { DPS_Node* };
%apply Pointer NONNULL { DPS_Subscription* };
%apply Pointer NONNULL { DPS_Publication* };
%apply Pointer NONNULL { DPS_NodeAddress* };

/*
 * The DPS public header files
 */
%include <dps/err.h>
%include <dps/dps.h>
%include <dps/synchronous.h>
%include "dps.hh"

/*
 * Module initialization
 */
%init %{
    /* Must be called during module initialization to enable DPS callbacks */
    PyEval_InitThreads();
    DPS_Debug = 0;
%}
