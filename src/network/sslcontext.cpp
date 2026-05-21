/**
 * @file      sslcontext.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h> // PROMEKI_ENABLE_TLS
#include <promeki/sslcontext.h>
#include <promeki/file.h>
#include <promeki/fileinfo.h>
#include <promeki/logger.h>
#include <promeki/memspace.h>
#if PROMEKI_ENABLE_TLS
#include <psa/crypto.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/psa_util.h>
#include <mbedtls/error.h>
#endif
#include <promeki/atomic.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SslContext);

#if PROMEKI_ENABLE_TLS

// ============================================================
// PSA Crypto initialization
//
// mbedTLS 3.6 moved entropy / DRBG behind PSA Crypto; the public
// SSL/TLS surface now expects PSA to be initialized once per
// process before any handshake runs.  PSA's init is idempotent
// (returns ALREADY_EXISTS when called twice) but we still want a
// single, lock-free guard to avoid re-entering for every new
// SslContext instance.
// ============================================================
namespace {

        static Error ensurePsaCrypto() {
                static Atomic<bool> done{false};
                if (done.load(MemoryOrder::Acquire)) return Error::Ok;
                const psa_status_t rc = psa_crypto_init();
                if (rc != PSA_SUCCESS && rc != PSA_ERROR_ALREADY_EXISTS) {
                        promekiWarn("SslContext: psa_crypto_init failed (%d)", (int)rc);
                        return Error::LibraryFailure;
                }
                done.store(true, MemoryOrder::Release);
                return Error::Ok;
        }

} // anonymous namespace

// ============================================================
// Pimpl: hold mbedTLS state out of the public header.
//
// PROMEKI_SHARED_FINAL gives the impl a native @c RefCount so
// @c SharedPtr<Impl, false> in the @ref SslContext handle can
// refcount it directly without the proxy.  The mbedTLS state has
// identity (free / init / parse calls all mutate fields in place),
// so we deliberately do not enable CoW — the @c <false> template
// argument keeps @c SharedPtr::modify off the clone path, which
// would otherwise abort at runtime because @c mbedtls_ssl_config /
// @c mbedtls_x509_crt are not copy-constructible.
// ============================================================
struct SslContext::Impl {
                PROMEKI_SHARED_FINAL(Impl)

                mbedtls_ssl_config conf{};
                mbedtls_x509_crt   ownCert{};
                bool               hasOwnCert = false;
                mbedtls_pk_context ownKey{};
                bool               hasOwnKey = false;
                mbedtls_x509_crt   caChain{};
                bool               hasCaChain = false;

                // mbedtls_ssl_config_defaults requires us to declare an
                // endpoint role at config time.  We initialize for SERVER
                // role (the more common case for libpromeki); SslSocket
                // flips the bit on its per-socket ssl_context when used in
                // client mode (mbedtls_ssl_conf_endpoint).
                bool configReady = false;

                SslProtocol protocol = SecureProtocols;
                // Client-side: verify the server's certificate.
                bool verifyPeer = true;
                // Server-side: require + verify a client cert (mutual
                // TLS).  Defaults to false — the standard HTTPS
                // server pattern of not asking clients for a cert.
                bool requireClientCert = false;
                int  verifyDepth = 9;

                Impl() {
                        mbedtls_ssl_config_init(&conf);
                        mbedtls_x509_crt_init(&ownCert);
                        mbedtls_pk_init(&ownKey);
                        mbedtls_x509_crt_init(&caChain);
                }
                ~Impl() {
                        // Each of these mbedTLS _free helpers invokes
                        // mbedtls_platform_zeroize on the underlying
                        // storage before releasing it:
                        //  - mbedtls_ssl_config_free  zeros mbedtls_ssl_config
                        //  - mbedtls_pk_free          zeros mbedtls_pk_context
                        //                             and the nested
                        //                             RSA / EC keypair (mbedtls_mpi_free
                        //                             zeroizes each bignum limb)
                        //  - mbedtls_x509_crt_free    zeros mbedtls_x509_crt
                        // So sensitive material is wiped before the
                        // pages are returned to the heap — we don't
                        // need to re-zeroize from this side.
                        mbedtls_ssl_config_free(&conf);
                        if (hasOwnCert) mbedtls_x509_crt_free(&ownCert);
                        if (hasOwnKey) mbedtls_pk_free(&ownKey);
                        if (hasCaChain) mbedtls_x509_crt_free(&caChain);
                }

                Error ensureConfig() {
                        if (configReady) return Error::Ok;
                        Error e = ensurePsaCrypto();
                        if (e.isError()) return e;

                        int rc = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                                             MBEDTLS_SSL_PRESET_DEFAULT);
                        if (rc != 0) {
                                promekiWarn("SslContext: config_defaults failed (-0x%04x)", static_cast<unsigned>(-rc));
                                return Error::LibraryFailure;
                        }
                        // No mbedtls_ssl_conf_rng required: with PSA Crypto
                        // initialized, the SSL layer pulls randomness from
                        // PSA automatically.  Authmode stays at the
                        // server-default VERIFY_NONE; SslSocket sets
                        // the role-appropriate value at handshake time.
                        applyProtocol();
                        configReady = true;
                        return Error::Ok;
                }

                void applyProtocol() {
                        switch (protocol) {
                                case TlsV1_2:
                                        mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
                                        mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
                                        break;
                                case TlsV1_3:
                                        mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_3);
                                        mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_3);
                                        break;
                                case SecureProtocols:
                                        mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
                                        mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_3);
                                        break;
                        }
                }

                // No applyAuthMode helper: mbedTLS authmode is
                // role-specific (on a server it controls "require
                // client cert", on a client it controls "verify
                // server cert"), and a single shared SslContext does
                // not know which role any given SslSocket will use it
                // for.  @ref SslSocket sets the authmode explicitly
                // in @c startEncryption / @c startServerEncryption
                // based on the @c verifyPeer / @c hasCaChain
                // intent recorded here.  mbedtls_ssl_config_defaults
                // initializes the conf with @c MBEDTLS_SSL_VERIFY_NONE
                // (the server-side default), which is a safe state to
                // park in until SslSocket runs.
};

// ============================================================
// Public surface
// ============================================================

bool SslContext::hasTlsSupport() {
        return true;
}

SslContext::SslContext() : _d(SharedPtr<Impl, false>::create()) {
        // Best-effort load of the system CA bundle.  Failure is
        // non-fatal here — the fail-closed verify policy in
        // SslSocket surfaces "no CAs configured" at handshake time
        // with a clear error.  We deliberately do not warn from the
        // constructor because that would fire on every default
        // construction in a build without a system bundle, drowning
        // the real signal at handshake.
        (void)setSystemCaCertificates();
}
// Out-of-line destructor / copy / move: required because Impl is
// incomplete in the header, so the compiler-generated SharedPtr<Impl,
// false> internals cannot be emitted there without falling through
// the IsSharedObject<Impl> trait (which itself can't see the
// _promeki_refct member of an incomplete type).
SslContext::~SslContext() = default;
SslContext::SslContext(const SslContext &other) = default;
SslContext &SslContext::operator=(const SslContext &other) = default;
SslContext::SslContext(SslContext &&other) noexcept = default;
SslContext &SslContext::operator=(SslContext &&other) noexcept = default;

bool SslContext::isValid() const {
        return _d.isValid();
}

void SslContext::setProtocol(SslProtocol protocol) {
        Impl *p = _d.modify();
        p->protocol = protocol;
        if (p->configReady) p->applyProtocol();
}

SslContext::SslProtocol SslContext::protocol() const {
        return _d->protocol;
}

void SslContext::setVerifyPeer(bool enable) {
        // No live re-apply: SslSocket reads verifyPeer() at handshake
        // start and sets the role-appropriate authmode on the conf.
        _d.modify()->verifyPeer = enable;
}

bool SslContext::verifyPeer() const {
        return _d->verifyPeer;
}

void SslContext::setRequireClientCert(bool require) {
        _d.modify()->requireClientCert = require;
}

bool SslContext::requireClientCert() const {
        return _d->requireClientCert;
}

void SslContext::setVerifyDepth(int depth) {
        _d.modify()->verifyDepth = depth;
}
int SslContext::verifyDepth() const {
        return _d->verifyDepth;
}

bool SslContext::hasCertificate() const {
        return _d->hasOwnCert;
}
bool SslContext::hasCaCertificates() const {
        return _d->hasCaChain;
}

String SslContext::toString() const {
        if (!isValid()) return "SslContext(moved-from)";
        const char *proto = "SecureProtocols";
        switch (_d->protocol) {
                case TlsV1_2: proto = "TlsV1_2"; break;
                case TlsV1_3: proto = "TlsV1_3"; break;
                case SecureProtocols: proto = "SecureProtocols"; break;
        }
        String s = String::sprintf("SslContext(protocol=%s, verifyPeer=%s",
                                   proto, _d->verifyPeer ? "true" : "false");
        if (_d->hasOwnCert) s += ", hasCert";
        if (_d->hasOwnKey) s += ", hasKey";
        if (_d->hasCaChain) s += ", hasCa";
        s += ")";
        return s;
}

Error SslContext::writeToStream(DataStream &) const {
        // Refuse to put certs / private keys / CA chains onto the
        // DataStream wire — anywhere a Variant gets persisted or
        // transmitted, this body would be the leak path.  Declared
        // only to satisfy the PROMEKI_DATATYPE member-API surface;
        // see the header for the full rationale.
        return Error::NotSupported;
}

void *SslContext::nativeConfig() const {
        // ensureConfig is the lazy-init point: callers that drive
        // mbedtls_ssl_setup against this config need it populated.
        // Does not flip the @c attached flag — SslSocket pokes this
        // accessor as part of its handshake plumbing and should not
        // promote an unattached context to attached.
        _d.modify()->ensureConfig();
        return &_d.modify()->conf;
}

// ----------------------------------------------------
// Helpers
// ----------------------------------------------------

namespace {

        // Read an entire file into a heap Buffer plus a NUL terminator;
        // the latter is required by mbedTLS's PEM auto-detection.
        //
        // When @p secure is true the temp Buffer is allocated from
        // @ref MemSpace::SystemSecure — the backing memory is
        // page-locked (no swap), marked @c MADV_DONTDUMP (excluded
        // from core dumps), and @c explicit_bzero'd on destruction.
        // Used for private-key reads so the PEM bytes do not linger
        // in freed heap memory after parsing.  Cert / CA reads can
        // skip this — those payloads are public material.
        static Error readWholeFile(const FilePath &path, Buffer &out, bool secure) {
                File  f(path.toString());
                Error err = f.open(IODevice::ReadOnly);
                if (err.isError()) {
                        promekiWarn("SslContext: open '%s' failed (%s)", path.toString().cstr(), err.name().cstr());
                        return err;
                }
                auto sizeRes = f.size();
                if (sizeRes.second().isError()) {
                        promekiWarn("SslContext: size('%s') failed (%s)", path.toString().cstr(),
                                    sizeRes.second().name().cstr());
                        return sizeRes.second();
                }
                const int64_t sz = sizeRes.first();
                const MemSpace ms = secure ? MemSpace(MemSpace::SystemSecure) : MemSpace::Default;
                Buffer        b(static_cast<size_t>(sz) + 1, Buffer::DefaultAlign, ms);
                int64_t       got = f.read(b.data(), sz);
                if (got < 0) {
                        promekiWarn("SslContext: read('%s') failed (expected %lld bytes)", path.toString().cstr(),
                                    static_cast<long long>(sz));
                        return Error::IOError;
                }
                static_cast<char *>(b.data())[got] = '\0';
                b.setSize(static_cast<size_t>(got) + 1);
                out = std::move(b);
                return Error::Ok;
        }

        static Error mbedtlsErrorToError(int rc, const char *what) {
                if (rc == 0) return Error::Ok;
                char errbuf[160] = {0};
                mbedtls_strerror(rc, errbuf, sizeof(errbuf));
                promekiWarn("SslContext: %s failed (-0x%04x): %s", what, static_cast<unsigned>(-rc), errbuf);
                return Error::ParseFailed;
        }

} // anonymous namespace

// ----------------------------------------------------
// Server credentials
// ----------------------------------------------------

Error SslContext::setCertificate(const FilePath &file) {
        Buffer b;
        Error  e = readWholeFile(file, b, /*secure=*/false);
        if (e.isError()) return e;
        return setCertificate(b);
}

Error SslContext::setCertificate(const Buffer &certData) {
        // mbedTLS 3.6's X.509 layer routes public-key validation
        // through PSA Crypto, so PSA must be initialized before
        // parsing.  Doing it here (rather than only inside
        // ensureConfig) means callers that load a cert before
        // doing anything else still get a working parse.
        Error psa = ensurePsaCrypto();
        if (psa.isError()) return psa;
        Impl *p = _d.modify();
        if (p->hasOwnCert) {
                mbedtls_x509_crt_free(&p->ownCert);
                mbedtls_x509_crt_init(&p->ownCert);
                p->hasOwnCert = false;
        }
        const int rc = mbedtls_x509_crt_parse(&p->ownCert, static_cast<const unsigned char *>(certData.data()),
                                              certData.size());
        Error     e = mbedtlsErrorToError(rc, "x509_crt_parse(cert)");
        if (e.isError()) return e;
        p->hasOwnCert = true;
        if (p->hasOwnKey) {
                if (p->ensureConfig().isError()) return Error::LibraryFailure;
                int crc = mbedtls_ssl_conf_own_cert(&p->conf, &p->ownCert, &p->ownKey);
                if (crc != 0) return mbedtlsErrorToError(crc, "ssl_conf_own_cert");
        }
        return Error::Ok;
}

Error SslContext::setPrivateKey(const FilePath &file, const String &passphrase) {
        // Route the on-disk PEM bytes through a page-locked,
        // wipe-on-destroy Buffer — the temp goes out of scope as
        // soon as @ref setPrivateKey returns and we don't want the
        // raw key material to linger in freed heap pages.
        Buffer b;
        Error  e = readWholeFile(file, b, /*secure=*/true);
        if (e.isError()) return e;
        return setPrivateKey(b, passphrase);
}

Error SslContext::setPrivateKey(const Buffer &keyData, const String &passphrase) {
        Impl *p = _d.modify();
        if (p->hasOwnKey) {
                mbedtls_pk_free(&p->ownKey);
                mbedtls_pk_init(&p->ownKey);
                p->hasOwnKey = false;
        }
        Error psa = ensurePsaCrypto();
        if (psa.isError()) return psa;
        // mbedTLS 3.6 dropped the trailing RNG callback pair from
        // mbedtls_pk_parse_key — entropy is sourced from PSA when
        // the key format demands it.
        const int rc = mbedtls_pk_parse_key(
                &p->ownKey, static_cast<const unsigned char *>(keyData.data()), keyData.size(),
                passphrase.isEmpty() ? nullptr : reinterpret_cast<const unsigned char *>(passphrase.cstr()),
                passphrase.byteCount());
        Error e = mbedtlsErrorToError(rc, "pk_parse_key");
        if (e.isError()) return e;
        p->hasOwnKey = true;
        if (p->hasOwnCert) {
                if (p->ensureConfig().isError()) return Error::LibraryFailure;
                int crc = mbedtls_ssl_conf_own_cert(&p->conf, &p->ownCert, &p->ownKey);
                if (crc != 0) return mbedtlsErrorToError(crc, "ssl_conf_own_cert");
        }
        return Error::Ok;
}

// ----------------------------------------------------
// Trust store
// ----------------------------------------------------

Error SslContext::setCaCertificates(const FilePath &caFile) {
        Buffer b;
        Error  e = readWholeFile(caFile, b, /*secure=*/false);
        if (e.isError()) return e;
        return setCaCertificates(b);
}

Error SslContext::setCaCertificates(const Buffer &caData) {
        Error psa = ensurePsaCrypto();
        if (psa.isError()) return psa;
        Impl *p = _d.modify();
        if (p->hasCaChain) {
                mbedtls_x509_crt_free(&p->caChain);
                mbedtls_x509_crt_init(&p->caChain);
                p->hasCaChain = false;
        }
        const int rc =
                mbedtls_x509_crt_parse(&p->caChain, static_cast<const unsigned char *>(caData.data()), caData.size());
        Error e = mbedtlsErrorToError(rc, "x509_crt_parse(ca)");
        if (e.isError()) return e;
        p->hasCaChain = true;
        if (p->ensureConfig().isError()) return Error::LibraryFailure;
        mbedtls_ssl_conf_ca_chain(&p->conf, &p->caChain, nullptr);
        // No applyAuthMode here: SslSocket reads hasCaCertificates()
        // at handshake start and sets the role-appropriate authmode
        // (client REQUIRED if verifyPeer + CAs loaded; server NONE
        // unless explicitly doing mutual TLS).
        return Error::Ok;
}

Error SslContext::setSystemCaCertificates() {
        // Probe well-known Linux locations.  No equivalent on
        // macOS / Windows yet; on those platforms callers should
        // load a bundled CA file explicitly.  Returns NotExist
        // when no candidate worked so callers can fall back.
        static const char *kCandidates[] = {
                "/etc/ssl/certs/ca-certificates.crt", // Debian / Ubuntu
                "/etc/pki/tls/certs/ca-bundle.crt",   // RHEL / Fedora
                "/etc/ssl/cert.pem",                  // OpenBSD / macOS
                "/etc/ssl/ca-bundle.pem",             // openSUSE
        };
        for (const char *path : kCandidates) {
                FileInfo info{path};
                if (!info.exists() || !info.isFile()) continue;
                Error e = setCaCertificates(FilePath(path));
                if (e.isOk()) return Error::Ok;
        }
        promekiWarnOnce("SslContext::setSystemCaCertificates: no system CA bundle found in any of the well-known paths");
        return Error::NotExist;
}

#else // PROMEKI_ENABLE_TLS

// ============================================================
// TLS-disabled stubs
//
// We still ship the @ref SslContext class so consumers' public
// API stays shape-stable across feature configurations, but
// every operation that would exercise mbedTLS returns
// @c Error::NotSupported.  Storage-only state (protocol /
// verifyPeer / verifyDepth) round-trips through the Impl so
// callers that build a context but never actually do a
// handshake see consistent values.
// ============================================================

struct SslContext::Impl {
                PROMEKI_SHARED_FINAL(Impl)

                SslProtocol protocol = SecureProtocols;
                bool        verifyPeer = true;
                bool        requireClientCert = false;
                int         verifyDepth = 9;
};

bool SslContext::hasTlsSupport() {
        return false;
}

SslContext::SslContext() : _d(SharedPtr<Impl, false>::create()) {
        // No system-CA auto-load: setSystemCaCertificates returns
        // NotSupported in this build, and trying to call it would
        // be wasted work.
}
// Out-of-line destructor / copy / move: required because Impl is
// incomplete in the header, so the compiler-generated SharedPtr<Impl,
// false> internals cannot be emitted there without falling through
// the IsSharedObject<Impl> trait (which itself can't see the
// _promeki_refct member of an incomplete type).
SslContext::~SslContext() = default;
SslContext::SslContext(const SslContext &other) = default;
SslContext &SslContext::operator=(const SslContext &other) = default;
SslContext::SslContext(SslContext &&other) noexcept = default;
SslContext &SslContext::operator=(SslContext &&other) noexcept = default;

bool SslContext::isValid() const {
        return _d.isValid();
}

void SslContext::setProtocol(SslProtocol protocol) {
        _d.modify()->protocol = protocol;
}
SslContext::SslProtocol SslContext::protocol() const {
        return _d->protocol;
}

void SslContext::setVerifyPeer(bool enable) {
        _d.modify()->verifyPeer = enable;
}
bool SslContext::verifyPeer() const {
        return _d->verifyPeer;
}

void SslContext::setRequireClientCert(bool require) {
        _d.modify()->requireClientCert = require;
}
bool SslContext::requireClientCert() const {
        return _d->requireClientCert;
}

void SslContext::setVerifyDepth(int depth) {
        _d.modify()->verifyDepth = depth;
}
int SslContext::verifyDepth() const {
        return _d->verifyDepth;
}

bool SslContext::hasCertificate() const {
        return false;
}
bool SslContext::hasCaCertificates() const {
        return false;
}

String SslContext::toString() const {
        if (!isValid()) return "SslContext(moved-from, tls-disabled)";
        const char *proto = "SecureProtocols";
        switch (_d->protocol) {
                case TlsV1_2: proto = "TlsV1_2"; break;
                case TlsV1_3: proto = "TlsV1_3"; break;
                case SecureProtocols: proto = "SecureProtocols"; break;
        }
        return String::sprintf("SslContext(protocol=%s, verifyPeer=%s, tls-disabled)",
                               proto, _d->verifyPeer ? "true" : "false");
}

Error SslContext::writeToStream(DataStream &) const {
        // Same rationale as the TLS-enabled build — even though this
        // stub configuration carries no cert / key material, refusing
        // here keeps the surface identical so callers can't rely on
        // wire-serialization being available in one build and not the
        // other.
        return Error::NotSupported;
}

void *SslContext::nativeConfig() const {
        return nullptr;
}

Error SslContext::setCertificate(const FilePath &) {
        return Error::NotSupported;
}
Error SslContext::setCertificate(const Buffer &) {
        return Error::NotSupported;
}
Error SslContext::setPrivateKey(const FilePath &, const String &) {
        return Error::NotSupported;
}
Error SslContext::setPrivateKey(const Buffer &, const String &) {
        return Error::NotSupported;
}
Error SslContext::setCaCertificates(const FilePath &) {
        return Error::NotSupported;
}
Error SslContext::setCaCertificates(const Buffer &) {
        return Error::NotSupported;
}
Error SslContext::setSystemCaCertificates() {
        return Error::NotSupported;
}

#endif // PROMEKI_ENABLE_TLS

PROMEKI_NAMESPACE_END
