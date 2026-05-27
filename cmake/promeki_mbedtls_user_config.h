/*
 * @file      promeki_mbedtls_user_config.h
 * @copyright Jason Howard. All rights reserved.
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

/* --- Build-time surface reduction --------------------------------------
 *
 * libpromeki only ever talks TLS-1.2-or-newer over TCP — never DTLS,
 * never anything with PSK auth, never EC J-PAKE.  The runtime
 * SecurityProfile::Strict already keeps these key-exchange families
 * off the wire, but disabling them at compile time has two real
 * benefits:
 *
 *  - Smaller attack surface in the code that *is* linked: a CVE in
 *    code that's compiled-but-never-called still ships.  No code, no
 *    bug class.
 *  - Smaller binary: libpromeki.so drops noticeable size from
 *    eliminating these unused .o's during LTO.
 *
 * If a future caller needs one of these back (e.g. a PSK use case
 * for an embedded device), the right move is to remove the matching
 * #undef here, not to override per-call — because the code wouldn't
 * even be in the build.
 *
 * What we keep:
 *  - TLS 1.2 + TLS 1.3 over TCP
 *  - ECDHE-RSA, ECDHE-ECDSA key exchanges (the workhorse pairs)
 *  - AEAD ciphers (AES-GCM, AES-CCM, ChaCha20-Poly1305)
 *  - Server (SSL_SRV_C) + client (SSL_CLI_C)
 *  - PSA crypto + threading
 *
 * What we drop:
 *  - DTLS — we don't run TLS over datagrams; UDP is RTP-only, and
 *    SRT links its own isolated mbedTLS-3.6 bundle (see
 *    cmake/promeki_srt_bundle.cmake) that's symbol-localized away
 *    from this build, so disabling DTLS here cannot break SRT.
 *  - PSK key-exchange families — no PSK consumer in libpromeki.
 *  - EC J-PAKE — no use case.
 */

#undef MBEDTLS_SSL_PROTO_DTLS
#undef MBEDTLS_SSL_DTLS_ANTI_REPLAY
#undef MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE
#undef MBEDTLS_SSL_DTLS_CONNECTION_ID
#undef MBEDTLS_SSL_DTLS_HELLO_VERIFY
#undef MBEDTLS_SSL_DTLS_SRTP

#undef MBEDTLS_KEY_EXCHANGE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_DHE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_RSA_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECDHE_PSK_ENABLED
#undef MBEDTLS_KEY_EXCHANGE_ECJPAKE_ENABLED

#undef PSA_WANT_ALG_JPAKE
#undef PSA_WANT_ALG_FFDH
#undef PSA_WANT_KEY_TYPE_DH_KEY_PAIR_BASIC
#undef PSA_WANT_KEY_TYPE_DH_KEY_PAIR_DERIVE
#undef PSA_WANT_KEY_TYPE_DH_KEY_PAIR_GENERATE
#undef PSA_WANT_KEY_TYPE_DH_KEY_PAIR_IMPORT
#undef PSA_WANT_KEY_TYPE_DH_PUBLIC_KEY

#endif /* PROMEKI_MBEDTLS_USER_CONFIG_H */
