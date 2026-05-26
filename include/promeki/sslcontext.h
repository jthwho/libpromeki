/**
 * @file      sslcontext.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/buffer.h>
#include <promeki/filepath.h>
#include <promeki/error.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>
#include <promeki/datatype.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief TLS/SSL configuration shared across @ref SslSocket instances.
 * @ingroup network
 *
 * @ref SslContext is a value-type handle that wraps an internal
 * @c SharedPtr to its mbedTLS state.  Copying an @ref SslContext is
 * a refcount bump on that shared state, so a single configured
 * context can ride into many @ref SslSocket / @ref HttpClient /
 * @ref HttpServer / @ref WebSocket / @ref RtmpClient instances by
 * value without deep-cloning the certificate / private-key / trust-
 * store material.
 *
 * Underneath, the impl wraps mbedTLS's @c mbedtls_ssl_config,
 * @c mbedtls_x509_crt, and @c mbedtls_pk_context types.  In a build
 * without @c PROMEKI_ENABLE_TLS the public surface stays the same so
 * consumer code is shape-stable across feature configurations, but
 * every mutator returns @ref Error::NotSupported and
 * @ref nativeConfig returns @c nullptr.
 *
 * @par Default state
 * A default-constructed @ref SslContext is ready for typical
 * client-side use: the constructor best-effort-loads the system CA
 * bundle via @ref setSystemCaCertificates (no error is reported
 * when the bundle is unavailable — the fail-closed verify policy
 * in @ref SslSocket surfaces the misconfiguration at handshake
 * time with a clear error).  Consumers can therefore pass a
 * default-constructed @ref SslContext directly to a client and get
 * publicly-trusted @c https / @c wss / @c rtmps handshakes out of
 * the box.  Server-side TLS termination requires
 * @ref setCertificate and @ref setPrivateKey on top of the default
 * — consumers detect a TLS-configured server by checking
 * @ref hasCertificate.
 *
 * @par Sharing semantics
 * Copies of an @ref SslContext refer to the same underlying state
 * — mutators applied through any handle are visible through every
 * other handle that names the same impl.  This matches the intended
 * use case (build the context once, hand it to many sockets) and is
 * why @ref SslContext does @b not implement copy-on-write the way
 * @ref Buffer / @ref Frame / @ref String do.
 *
 * @par Thread Safety
 * Mixed.  Configuration calls (@ref setCertificate,
 * @ref setPrivateKey, @ref setCaCertificates, etc.) must be made
 * before the context is handed to any @ref SslSocket — the
 * configuration phase is single-threaded.  After that the context
 * is read-only and may be shared across threads; mbedTLS's
 * @c mbedtls_ssl_config is documented as safe to share across
 * handshakes once initialized.
 *
 * @par Security model
 * The default-constructed context applies the
 * @ref SecurityProfile::Strict preset: TLS 1.2+ minimum, AEAD-only
 * ciphers over ECDHE, modern curves (X25519 / P-256 / P-384),
 * modern signature algorithms (Ed25519 / ECDSA / RSA-PSS), and
 * renegotiation explicitly disabled.  Callers can opt down to
 * @ref SecurityProfile::Compatible to interop with legacy HTTPS
 * endpoints that only speak TLS 1.2 + RSA-kex + CBC-MAC; see
 * @ref SecurityProfile for the full per-profile breakdown.
 *
 * Known gaps:
 *  - **Certificate revocation is not checked.** Neither CRL fetching
 *    nor OCSP-stapling is wired into the handshake path, so a
 *    revoked-but-otherwise-valid server certificate verifies
 *    successfully.  The vendored mbedTLS 3.6 LTS does not ship a
 *    client-side OCSP API (there is no @c mbedtls_ocsp_* surface,
 *    only an OID constant for the OCSP-signing EKU); the
 *    @c status_request TLS extension is parsed but not actionable
 *    from user code.  Closing this gap would require either
 *    upstream mbedTLS 4.x support or a from-scratch OCSP
 *    request/response stack on top of @ref HttpClient.  The
 *    recommended mitigation today is short-lived server
 *    certificates (Let's Encrypt's 90-day default, or shorter for
 *    private PKI) — the modern industry consensus is that
 *    short-cert hygiene is a more reliable revocation answer
 *    than online OCSP, which is widely soft-failed in practice.
 *    See @c devplan/network/tls.md for the deferral rationale and
 *    re-evaluation triggers.
 *  - **TLS session resumption is not used.** Each connection runs a
 *    fresh handshake.  For the current one-shot download / RPC
 *    workloads this is the right trade — session tickets would only
 *    save CPU on reconnect storms we don't have.
 *
 * Sensitive material handling:
 *  - **mbedTLS internal state** (the @c mbedtls_pk_context,
 *    @c mbedtls_x509_crt, and @c mbedtls_ssl_config) is wiped via
 *    @c mbedtls_platform_zeroize inside the matching @c _free
 *    functions, which run from the impl's destructor.  Key bignum
 *    limbs are zeroized by @c mbedtls_mpi_free for every key type.
 *  - **Reading a private key from a file path**
 *    (@ref setPrivateKey(const FilePath &, const String &)) stages
 *    the on-disk PEM through a @ref MemSpace::SystemSecure buffer:
 *    page-locked (excluded from swap), @c MADV_DONTDUMP (excluded
 *    from core dumps), and @c explicit_bzero'd on destruction.
 *    Cert and CA reads use a regular host buffer — those payloads
 *    are public material.
 *  - **Diagnostic output** (@ref toString) never embeds cert / key
 *    bytes.
 *  - **DataStream serialization** is intentionally refused (see
 *    @ref writeToStream) so a @ref Variant carrying an
 *    @ref SslContext cannot leak credentials through a persistence
 *    or IPC path.
 *
 * @par Verify policy
 * The class records two @em independent verify intents — one for
 * each handshake role — and @ref SslSocket applies the role-
 * appropriate mbedTLS authmode at handshake time:
 *  - **Client side** consults @ref verifyPeer (default @c true,
 *    "verify the server's cert"):
 *    - @c true with a CA chain loaded → VERIFY_REQUIRED.
 *    - @c true with no CA chain → @ref Error::Invalid up-front
 *      (fail-closed; silently going VERIFY_NONE here is the classic
 *      TLS footgun).
 *    - @c false → VERIFY_NONE.  Loopback / self-signed / dev work
 *      opts out explicitly this way.
 *  - **Server side** consults @ref requireClientCert (default
 *    @c false, "ask the client for a cert and validate it"):
 *    - @c false → VERIFY_NONE.  Standard HTTPS server, doesn't
 *      request client certs.
 *    - @c true with a CA chain loaded → VERIFY_REQUIRED (mutual
 *      TLS).
 *    - @c true with no CA chain → @ref Error::Invalid up-front
 *      (no anchors to verify a client cert against).
 *
 * The two flags are deliberately separate.  An earlier design
 * conflated them through @ref verifyPeer + @ref hasCaCertificates,
 * but combining auto-loaded system CAs with the server's mutual-TLS
 * trigger silently turned any HTTPS server into a mutual-auth
 * server, which is the opposite of what callers want.
 *
 * Because the default constructor pre-loads the system CA bundle,
 * the typical client path lands in the @c VERIFY_REQUIRED branch
 * automatically: @c https / @c wss / @c rtmps against publicly-
 * trusted servers verifies out of the box, and a missing or
 * unreadable bundle fails-closed at handshake time rather than
 * going silently insecure.
 *
 * What this class does @b not do:
 *  - It does not override mbedTLS's allocator, so heap allocations
 *    mbedTLS makes during key parsing / handshake stay in normal
 *    pageable memory.  Cleartext key material spends only a brief
 *    window in those buffers — mbedTLS zeroizes before freeing —
 *    but a swap-out during that window is theoretically observable.
 *    Applications that need stronger guarantees should mlock the
 *    whole process or set @c RLIMIT_MEMLOCK + use a kernel that
 *    enforces locked-pages, and disable core dumps with
 *    @c prctl(PR_SET_DUMPABLE, 0).
 *  - When the caller supplies a key Buffer through
 *    @ref setPrivateKey(const Buffer &, const String &), the
 *    Buffer's backing memory is the caller's responsibility — use
 *    a @ref MemSpace::SystemSecure buffer if the key bytes should
 *    be wiped on destruction.
 *  - The passphrase passed to @ref setPrivateKey is consumed
 *    transiently but not zeroized by this class — the @ref String
 *    is the caller's lifetime.
 *
 * @par Example
 * @code
 * SslContext ctx;
 * ctx.setProtocol(SslContext::SecureProtocols);   // lazy-allocates impl
 * ctx.setCertificate(FilePath("/etc/server.crt"));
 * ctx.setPrivateKey (FilePath("/etc/server.key"));
 * httpServer.setSslContext(ctx);                  // copy = refcount bump
 * @endcode
 */
class SslContext {
        public:
                PROMEKI_DATATYPE(SslContext, DataTypeSslContext, 1)

                /** @brief List of contexts. */
                using List = ::promeki::List<SslContext>;

                /** @brief Permitted TLS protocol version policy. */
                enum SslProtocol {
                        TlsV1_2,        ///< TLS 1.2 only.
                        TlsV1_3,        ///< TLS 1.3 only.
                        SecureProtocols ///< TLS 1.2 + TLS 1.3 (the secure modern set).
                };

                /**
                 * @brief Coherent presets for the cipher-suite / group / signature-algorithm triple.
                 *
                 * A security profile bundles three otherwise-independent
                 * TLS policy knobs into a single named choice so callers
                 * don't have to chase RFCs to pick a coherent set.  The
                 * defaults bias toward the @ref Strict end of the
                 * spectrum.
                 *
                 *  - @ref Strict (the default) — TLS 1.3 plus the AEAD-
                 *    only / PFS-only subset of TLS 1.2.  Ciphers:
                 *    AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305 over
                 *    ECDHE only.  Groups: X25519, NIST P-256, NIST
                 *    P-384.  Signature algorithms: Ed25519, ECDSA-P256,
                 *    ECDSA-P384, RSA-PSS (with SHA-256 / SHA-384).
                 *    Rejects CBC-MAC suites (Lucky13 / POODLE class),
                 *    static RSA key exchange (no forward secrecy),
                 *    brainpool curves (low adoption, side-channel
                 *    concerns), and PKCS#1 v1.5 signatures.  This is
                 *    what every libpromeki consumer gets out of the
                 *    box.
                 *
                 *  - @ref Compatible — keeps @ref Strict's curves and
                 *    signature algorithms but widens the cipher suite
                 *    list back to mbedTLS's defaults, which include
                 *    TLS 1.2 CBC-MAC suites and static RSA key
                 *    exchange.  Use only when connecting to legacy
                 *    HTTPS endpoints (older appliances, embedded
                 *    devices) that have never upgraded past TLS 1.2 +
                 *    RSA-kex.  Modern servers continue to negotiate the
                 *    AEAD suites first; the legacy suites are only
                 *    offered as a fallback.
                 *
                 * Renegotiation is disabled in both profiles
                 * (@c MBEDTLS_SSL_RENEGOTIATION_DISABLED).  libpromeki
                 * has no caller-driven renegotiation today and the
                 * explicit disable forecloses a Logjam-class footgun
                 * even if a future build flips the compile-time
                 * default.
                 *
                 * @note The profile is applied to the underlying
                 *       @c mbedtls_ssl_config at the same time as the
                 *       protocol version policy (lazy on first
                 *       @ref nativeConfig call, or eagerly if a profile
                 *       change happens after the config is already
                 *       built).  An in-flight @ref SslSocket already
                 *       past @c mbedtls_ssl_setup keeps its values
                 *       frozen from setup time; the change takes effect
                 *       on the next handshake.
                 *
                 * @note **Certificate revocation is not currently
                 *       checked** under either profile.  Neither CRL
                 *       fetching nor OCSP-stapling is wired into the
                 *       handshake path; a revoked-but-otherwise-valid
                 *       server certificate verifies successfully today.
                 *       This is a documented gap, not a profile
                 *       difference; closing it would require either an
                 *       online CRL/OCSP responder integration or
                 *       OCSP-stapling support in @ref SslSocket.
                 */
                enum SecurityProfile {
                        Strict,     ///< AEAD-only + PFS-only + modern curves/sigs (default).
                        Compatible, ///< Adds CBC-MAC + static-RSA suites for legacy servers.
                };

                /**
                 * @brief Reports whether this build can actually speak TLS.
                 *
                 * Single source of truth for the @c PROMEKI_ENABLE_TLS
                 * feature flag: when @c false every @ref SslContext
                 * mutator (other than the trivial protocol /
                 * verifyPeer / verifyDepth setters) returns
                 * @ref Error::NotSupported and @ref nativeConfig
                 * returns @c nullptr, so consumers can check this
                 * once instead of inspecting build macros.
                 *
                 * @ref HttpClient::hasTlsSupport,
                 * @ref HttpServer::hasTlsSupport, and
                 * @ref WebSocket::hasTlsSupport delegate here.
                 */
                static bool hasTlsSupport();

                /**
                 * @brief Constructs a context with default secure settings.
                 *
                 * Best-effort-loads the system CA bundle via
                 * @ref setSystemCaCertificates so the context is
                 * ready for client use against publicly-trusted
                 * servers.  If no system bundle is available
                 * (@ref Error::NotExist) the context is still
                 * constructed but has no CA anchors — the
                 * fail-closed verify policy in @ref SslSocket
                 * surfaces this at handshake time.
                 */
                SslContext();

                /** @brief Destructor. */
                ~SslContext();

                // The copy / move special members are defined
                // out-of-line because @ref Impl is incomplete here —
                // an inline @c =default would instantiate the
                // @c SharedPtr internals against the forward-declared
                // type, miss the @c IsSharedObject trait, and route
                // through @c SharedPtrProxy<Impl>::~SharedPtrProxy()
                // which would invoke @c delete on an incomplete type.

                /// @brief Copy = refcount bump on the underlying impl.
                SslContext(const SslContext &other);

                /// @brief Copy-assign = refcount bump on the underlying impl.
                SslContext &operator=(const SslContext &other);

                /// @brief Move = handoff of the underlying impl.
                SslContext(SslContext &&other) noexcept;

                /// @brief Move-assign = handoff of the underlying impl.
                SslContext &operator=(SslContext &&other) noexcept;

                /**
                 * @brief True when the handle refers to a live impl.
                 *
                 * Returns @c false only after the handle has been
                 * moved-from.  Use the specific predicates
                 * (@ref hasCertificate, @ref hasCaCertificates,
                 * @ref verifyPeer) when checking configuration
                 * state — "valid" here means "the SharedPtr is
                 * non-null," not "configured for TLS termination."
                 */
                bool isValid() const;

                /** @brief Identity equality (same underlying impl). */
                bool operator==(const SslContext &other) const { return _d == other._d; }

                /** @brief Identity inequality. */
                bool operator!=(const SslContext &other) const { return !(*this == other); }

                /**
                 * @brief Returns a debug-readable summary of the context state.
                 *
                 * Examples:
                 * @code
                 * SslContext()                                 // "SslContext(unattached)"
                 * after setProtocol(TlsV1_3)                   // "SslContext(protocol=TlsV1_3, verifyPeer=true)"
                 * after setCertificate + setPrivateKey         // "SslContext(protocol=SecureProtocols, verifyPeer=true, hasCert, hasKey)"
                 * @endcode
                 *
                 * Diagnostic-only; cert / key bytes are deliberately
                 * not included.  There is no @c fromString counterpart
                 * — round-tripping a TLS configuration through a
                 * string has no useful meaning.
                 */
                String toString() const;

                /** @brief Sets the allowed protocol version range. */
                void setProtocol(SslProtocol protocol);

                /** @brief Returns the current protocol policy (default @ref SecureProtocols when null). */
                SslProtocol protocol() const;

                /**
                 * @brief Selects the cipher / group / signature-algorithm preset.
                 *
                 * The new profile takes effect the next time mbedTLS
                 * touches the underlying @c mbedtls_ssl_config —
                 * immediately if the config has already been built
                 * (@ref nativeConfig was called), or on first
                 * @ref nativeConfig if not.  Mid-handshake
                 * @ref SslSocket instances are unaffected; the config
                 * snapshot they took at @c mbedtls_ssl_setup time is
                 * frozen until they go through the handshake again.
                 *
                 * See @ref SecurityProfile for what each preset
                 * includes and excludes.
                 */
                void setSecurityProfile(SecurityProfile profile);

                /** @brief Returns the current security profile (default @ref Strict). */
                SecurityProfile securityProfile() const;

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
                Error setPrivateKey(const FilePath &file, const String &passphrase = String());

                /**
                 * @brief Loads the server's private key from memory.
                 *
                 * The Buffer's backing memory is the caller's
                 * responsibility — for keys that should be wiped on
                 * destruction, allocate the Buffer from
                 * @ref MemSpace::SystemSecure so the bytes are
                 * page-locked and @c explicit_bzero'd when the
                 * Buffer falls out of scope.  The file-path overload
                 * above does this automatically for the temp
                 * read-buffer.
                 */
                Error setPrivateKey(const Buffer &keyData, const String &passphrase = String());

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
                 * @brief Enables or disables client-side server-cert verification.
                 *
                 * Client-side knob only — controls whether
                 * @ref SslSocket validates the server's certificate
                 * during the handshake.  Defaults to @c true.
                 * Handshakes fail-closed when verification is
                 * enabled but no CA chain has been loaded (the
                 * default constructor pre-loads the system bundle,
                 * so the typical client path passes).  Set @c false
                 * for development against self-signed servers;
                 * mbedTLS verification problems are still reported
                 * via @ref SslSocket::sslErrorsSignal for visibility.
                 *
                 * Server-side handshakes ignore this flag; use
                 * @ref setRequireClientCert to opt into mutual TLS.
                 */
                void setVerifyPeer(bool enable);

                /** @brief Returns the client-side server-cert verification flag (default @c true). */
                bool verifyPeer() const;

                /**
                 * @brief Enables or disables mutual TLS on the server side.
                 *
                 * Server-side knob only — when @c true, the server
                 * requires connecting clients to present a
                 * certificate verifiable against the loaded CA
                 * chain (see @ref setCaCertificates).  Defaults to
                 * @c false: the standard HTTPS server pattern of
                 * not requesting a client cert.
                 *
                 * When enabled, @ref SslSocket fail-closes the
                 * server handshake with @ref Error::Invalid if no CA
                 * chain has been loaded — there'd be no anchors to
                 * validate the client cert against.
                 *
                 * Client-side handshakes ignore this flag; use
                 * @ref setVerifyPeer to control server-cert
                 * verification.
                 */
                void setRequireClientCert(bool require);

                /** @brief Returns the server-side mutual-TLS flag (default @c false). */
                bool requireClientCert() const;

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
                 * Lazy-initializes the @c mbedtls_ssl_config_defaults
                 * the first time it's requested.  Exposed for
                 * @ref SslSocket; opaque @c void* avoids leaking
                 * mbedTLS headers into consumer compilations.  Cast to
                 * @c mbedtls_ssl_config* in implementation files that
                 * include @c mbedtls/ssl.h directly.  Returns
                 * @c nullptr in a build with @c PROMEKI_ENABLE_TLS off.
                 */
                void *nativeConfig() const;

                /**
                 * @brief DataStream wire serializer — intentionally @b not supported.
                 *
                 * Always returns @ref Error::NotSupported.  An
                 * @ref SslContext carries certificate and private-key
                 * material, and the @ref DataStream surface is used
                 * for persistence (config files, IPC, network frames,
                 * Variant-typed databases).  Putting cert / key bytes
                 * on that surface would leak them anywhere a
                 * @ref Variant gets serialized.  The method exists
                 * only so the type can host @ref PROMEKI_DATATYPE.
                 */
                Error writeToStream(DataStream &s) const;

                /**
                 * @brief DataStream wire reader — intentionally @b not supported.
                 *
                 * Always returns @ref Error::NotSupported for the
                 * same reason as @ref writeToStream.  Required only
                 * to satisfy the @ref PROMEKI_DATATYPE dispatch
                 * trait.
                 */
                template <uint32_t V> static Result<SslContext> readFromStream(DataStream &s);

        private:
                struct Impl;

                // mutable: @c nativeConfig is a @c const accessor but
                // lazy-initializes the underlying @c mbedtls_ssl_config
                // on first call, so the @c SharedPtr handle has to be
                // reachable through a non-const @c modify() path.
                mutable SharedPtr<Impl, false> _d;
};

// Primary template for @ref SslContext::readFromStream: always
// reports @ref Error::NotSupported, regardless of wire version, for
// the reasons documented on the declaration.  Inline so the
// @ref PROMEKI_DATATYPE @c dispatchRead body can resolve it without
// dragging the cpp into header consumers.
template <uint32_t V> inline Result<SslContext> SslContext::readFromStream(DataStream &) {
        return Result<SslContext>(SslContext(), Error::NotSupported);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
