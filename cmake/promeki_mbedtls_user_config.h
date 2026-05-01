/*
 * @file      promeki_mbedtls_user_config.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * User config appended to the vendored mbedTLS / TF-PSA-Crypto default
 * configurations.  Selected by passing this file path via the
 * MBEDTLS_USER_CONFIG_FILE and TF_PSA_CRYPTO_USER_CONFIG_FILE cache
 * variables in the top-level CMakeLists.
 *
 * The mbedTLS PSA crypto subsystem maintains process-global state — the
 * key-slot table, the global CTR_DRBG, and per-context AES tables — that
 * is shared whenever multiple threads use TLS.  Without
 * MBEDTLS_THREADING_C, those globals race; ThreadSanitizer flags
 * dozens of warnings inside psa_crypto.c, ctr_drbg.c, and aes.c on a
 * concurrent TLS workload.  Turning on the threading layer makes those
 * accesses correct.
 *
 * MBEDTLS_THREADING_PTHREAD picks the built-in pthread mutex
 * implementation so we don't have to install a custom mbedtls_threading
 * back end in our own code.
 */

#ifndef PROMEKI_MBEDTLS_USER_CONFIG_H
#define PROMEKI_MBEDTLS_USER_CONFIG_H

#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD

#endif /* PROMEKI_MBEDTLS_USER_CONFIG_H */
