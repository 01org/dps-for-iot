#ifndef _DPS_ERR_H
#define _DPS_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int DPS_Status;

/**
 * Return codes
 */
#define DPS_OK                     0 /**< Function succeeded */
#define DPS_ERR_OK                 0 /**< Alias for DPS_OK */
#define DPS_ERR_FAILURE            1 /**< Non-specific failure */
#define DPS_ERR_NULL               2 /**< Invalid null pointer */
#define DPS_ERR_ARGS               3 /**< Invalid argument(s) */
#define DPS_ERR_RESOURCES          4 /**< Resource failure, typically memory allocation */
#define DPS_ERR_READ               5 /**< Read operation failed */
#define DPS_ERR_WRITE              6 /**< Write operation failed */
#define DPS_ERR_TIMEOUT            7 /**< Operation timed out */
#define DPS_ERR_EOD                8 /**< Unexpected end of data */
#define DPS_ERR_OVERFLOW           9 /**< Buffer overflow */
#define DPS_ERR_NETWORK           10 /**< Network or socket error */
#define DPS_ERR_INVALID           11 /**< A value was invalid */
#define DPS_ERR_BUSY              12 /**< Operation cannot be performed right now */
#define DPS_ERR_EXISTS            13 /**< Something not expected was present */
#define DPS_ERR_MISSING           14 /**< Something expected was missting */
#define DPS_ERR_STALE             15 /**< A publication was stale */
#define DPS_ERR_NO_ROUTE          16 /**< There is no route to the requested destination */
#define DPS_ERR_NOT_STARTED       17 /**< Node has not yet been started */
#define DPS_ERR_NOT_INITIALIZED   18 /**< Object has not yet been initialized */
#define DPS_ERR_EXPIRED           19 /**< A remote node has expired */
#define DPS_ERR_UNRESOLVED        20 /**< Name resolution failed */
#define DPS_ERR_NODE_DESTROYED    21 /**< Node has already been destroyed */
#define DPS_ERR_EOF               22 /**< End of file or socket closed */

const char* DPS_ErrTxt(DPS_Status s);

#ifdef __cplusplus
}
#endif

#endif
