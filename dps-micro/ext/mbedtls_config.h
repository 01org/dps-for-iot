/* Use config file, to replace mbed defaults. It is set during
 * compilation with MBEDTLS_USER_CONFIG_FILE environment variable. */

#define MBEDTLS_DEPRECATED_REMOVED

#undef MBEDTLS_ARC4_C
#undef MBEDTLS_BLOWFISH_C
#undef MBEDTLS_CAMELLIA_C
#undef MBEDTLS_CCM_C
#undef MBEDTLS_DES_C
#undef MBEDTLS_DHM_C
#undef MBEDTLS_ECP_DP_BP256R1_ENABLED
#undef MBEDTLS_ECP_DP_BP384R1_ENABLED
#undef MBEDTLS_ECP_DP_BP512R1_ENABLED
#undef MBEDTLS_ECP_DP_CURVE25519_ENABLED
#undef MBEDTLS_ECP_DP_SECP192K1_ENABLED
#undef MBEDTLS_ECP_DP_SECP192R1_ENABLED
#undef MBEDTLS_ECP_DP_SECP224K1_ENABLED
#undef MBEDTLS_ECP_DP_SECP224R1_ENABLED
#undef MBEDTLS_ECP_DP_SECP256K1_ENABLED
#undef MBEDTLS_ECP_DP_SECP256R1_ENABLED
#undef MBEDTLS_FS_IO
#undef MBEDTLS_GENPRIME
#undef MBEDTLS_KEY_EXCHANGE_DHE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_DHE_RSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDH_RSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_RSA_PSK_ENABLED
#undef MBEDTLS_NET_C
#undef MBEDTLS_PEM_WRITE_C
#undef MBEDTLS_PKCS12_C
#undef MBEDTLS_PKCS5_C
#undef MBEDTLS_PK_RSA_ALT_SUPPORT
#undef MBEDTLS_RIPEMD160_C
#undef MBEDTLS_RSA_C
#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_SHA1_C
#undef MBEDTLS_SSL_CBC_RECORD_SPLITTING
#undef MBEDTLS_SSL_PROTO_TLS1
#undef MBEDTLS_SSL_PROTO_TLS1_1
#undef MBEDTLS_X509_RSASSA_PSS_SUPPORT

/* For Zephyr */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#undef MBEDTLS_ENTROPY_PLATFORM
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_FS_IO
#undef MBEDTLS_HAVE_TIME_DATE
#undef MBEDTLS_HAVE_TIME

