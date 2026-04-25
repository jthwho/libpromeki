/**
 * @file      sslcontext.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/buffer.h>
#include <promeki/filepath.h>
#include <promeki/error.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief TLS/SSL configuration shared across @ref SslSocket instances.
 * @ingroup network
 *
 * @ref SslContext owns the certificate / private-key / trust-store
 * material plus the protocol-version policy that an @ref SslSocket
 * uses when handshaking.  One context is typically built once at
 * startup (server side) or program init (client side) and shared by
 * every socket.
 *
 * Underneath, the context wraps mbedTLS's @c mbedtls_ssl_config,
 * @c mbedtls_x509_crt, and @c mbedtls_pk_context types — the
 * pImpl is heap-allocated lazily so simply default-constructing an
 * @ref SslContext does not pull in mbedTLS state.
 *
 * @par Threading
 * Configuration calls (@ref setCertificate, @ref setPrivateKey,
 * @ref setCaCertificates, etc.) must be made before the context is
 * handed to any @ref SslSocket.  After that it is read-only and may
 * be shared across threads — mbedTLS's @c mbedtls_ssl_config is
 * documented as safe to share across handshakes once initialized.
 *
 * @par Example
 * @code
 * SslContext ctx;
 * ctx.setProtocol(SslContext::SecureProtocols);
 * ctx.setCertificate(FilePath("/etc/server.crt"));
 * ctx.setPrivateKey (FilePath("/etc/server.key"));
 * httpServer.setSslContext(ctx);
 * @endcode
 */
class SslContext {
        PROMEKI_SHARED_FINAL_NOCOPY(SslContext)
        public:
                /** @brief Shared pointer type for SslContext. */
                using Ptr = SharedPtr<SslContext, false>;

                /** @brief List of contexts. */
                using List = promeki::List<SslContext>;

                /** @brief Permitted TLS protocol version policy. */
                enum SslProtocol {
                        TlsV1_2,                ///< TLS 1.2 only.
                        TlsV1_3,                ///< TLS 1.3 only.
                        SecureProtocols         ///< TLS 1.2 + TLS 1.3 (the secure modern set).
                };

                /** @brief Constructs an empty context with @ref SecureProtocols. */
                SslContext();

                /** @brief Destructor. */
                ~SslContext();

                /// @brief Non-copyable: the underlying mbedTLS state has identity.
                SslContext(const SslContext &) = delete;
                /// @brief Non-copy-assignable for the same reason.
                SslContext &operator=(const SslContext &) = delete;

                /** @brief Sets the allowed protocol version range. */
                void setProtocol(SslProtocol protocol);

                /** @brief Returns the current protocol policy. */
                SslProtocol protocol() const;

                // ----------------------------------------------------
                // Server-side credentials
                // ----------------------------------------------------

                /**
                 * @brief Loads a server certificate from a PEM/DER file.
                 *
                 * @param file Path to the certificate file.  PEM and
                 *             DER are auto-detected.
                 * @return @ref Error::Ok on success, or @ref Error::ParseFailed
                 *         on invalid input.
                 */
                Error setCertificate(const FilePath &file);

                /** @brief Loads a server certificate from memory. */
                Error setCertificate(const Buffer &certData);

                /**
                 * @brief Loads the server's private key.
                 *
                 * @param file       Path to the key file (PEM/DER).
                 * @param passphrase Optional passphrase for an encrypted PEM key.
                 */
                Error setPrivateKey(const FilePath &file,
                                    const String &passphrase = String());

                /** @brief Loads the server's private key from memory. */
                Error setPrivateKey(const Buffer &keyData,
                                    const String &passphrase = String());

                // ----------------------------------------------------
                // Trust store (used for peer verification on both sides)
                // ----------------------------------------------------

                /**
                 * @brief Loads CA certificates that anchor peer verification.
                 *
                 * The bundle may contain multiple PEM certificates.
                 */
                Error setCaCertificates(const FilePath &caFile);

                /** @brief Loads CA certificates from memory. */
                Error setCaCertificates(const Buffer &caData);

                /**
                 * @brief Loads the host platform's default CA bundle.
                 *
                 * On Linux this probes the well-known locations
                 * (@c /etc/ssl/certs/ca-certificates.crt etc.) until one
                 * succeeds.  Returns @ref Error::NotExist if no bundle
                 * can be located.
                 */
                Error setSystemCaCertificates();

                /**
                 * @brief Enables or disables peer-certificate verification.
                 *
                 * Defaults to @c true.  Disable only for development
                 * (e.g. self-signed loopback testing).  When disabled
                 * a verification failure is tolerated but still
                 * reported via @ref SslSocket::sslErrorsSignal.
                 */
                void setVerifyPeer(bool enable);

                /** @brief Returns whether peer verification is enabled. */
                bool verifyPeer() const;

                /** @brief Sets the maximum certificate-chain depth for verification. */
                void setVerifyDepth(int depth);

                /** @brief Returns the maximum certificate-chain depth. */
                int verifyDepth() const;

                /** @brief Whether a server certificate has been installed. */
                bool hasCertificate() const;

                /** @brief Whether a CA bundle has been installed. */
                bool hasCaCertificates() const;

                /**
                 * @brief Returns the underlying mbedTLS configuration handle.
                 *
                 * Exposed for @ref SslSocket; opaque @c void* avoids
                 * leaking mbedTLS headers into consumer compilations.
                 * Cast to @c mbedtls_ssl_config* in implementation
                 * files that include @c mbedtls/ssl.h directly.
                 */
                void *nativeConfig() const;

        private:
                struct Impl;
                Impl *_d = nullptr;
};

PROMEKI_NAMESPACE_END
