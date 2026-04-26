/**
 * @file      sslcontext.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sslcontext.h>
#include <promeki/file.h>
#include <promeki/fileinfo.h>
#include <promeki/logger.h>
#include <psa/crypto.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/psa_util.h>
#include <mbedtls/error.h>
#include <atomic>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SslContext);

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
        static std::atomic<bool> done{false};
        if(done.load(std::memory_order_acquire)) return Error::Ok;
        const psa_status_t rc = psa_crypto_init();
        if(rc != PSA_SUCCESS && rc != PSA_ERROR_ALREADY_EXISTS) {
                promekiWarn("SslContext: psa_crypto_init failed (%d)", (int)rc);
                return Error::LibraryFailure;
        }
        done.store(true, std::memory_order_release);
        return Error::Ok;
}

} // anonymous namespace

// ============================================================
// Pimpl: hold mbedTLS state out of the public header.
// ============================================================
struct SslContext::Impl {
        mbedtls_ssl_config              conf{};
        mbedtls_x509_crt                ownCert{};
        bool                            hasOwnCert = false;
        mbedtls_pk_context              ownKey{};
        bool                            hasOwnKey = false;
        mbedtls_x509_crt                caChain{};
        bool                            hasCaChain = false;

        // mbedtls_ssl_config_defaults requires us to declare an
        // endpoint role at config time.  We initialize for SERVER
        // role (the more common case for libpromeki); SslSocket
        // flips the bit on its per-socket ssl_context when used in
        // client mode (mbedtls_ssl_conf_endpoint).
        bool                            configReady = false;

        SslProtocol                     protocol = SecureProtocols;
        bool                            verifyPeer = true;
        int                             verifyDepth = 9;

        Impl() {
                mbedtls_ssl_config_init(&conf);
                mbedtls_x509_crt_init(&ownCert);
                mbedtls_pk_init(&ownKey);
                mbedtls_x509_crt_init(&caChain);
        }
        ~Impl() {
                mbedtls_ssl_config_free(&conf);
                if(hasOwnCert) mbedtls_x509_crt_free(&ownCert);
                if(hasOwnKey)  mbedtls_pk_free(&ownKey);
                if(hasCaChain) mbedtls_x509_crt_free(&caChain);
        }

        Error ensureConfig() {
                if(configReady) return Error::Ok;
                Error e = ensurePsaCrypto();
                if(e.isError()) return e;

                int rc = mbedtls_ssl_config_defaults(&conf,
                        MBEDTLS_SSL_IS_SERVER,
                        MBEDTLS_SSL_TRANSPORT_STREAM,
                        MBEDTLS_SSL_PRESET_DEFAULT);
                if(rc != 0) {
                        promekiWarn("SslContext: config_defaults failed (-0x%04x)",
                                static_cast<unsigned>(-rc));
                        return Error::LibraryFailure;
                }
                // No mbedtls_ssl_conf_rng required: with PSA Crypto
                // initialized, the SSL layer pulls randomness from
                // PSA automatically.
                applyProtocol();
                applyAuthMode();
                configReady = true;
                return Error::Ok;
        }

        void applyProtocol() {
                switch(protocol) {
                        case TlsV1_2:
                                mbedtls_ssl_conf_min_tls_version(&conf,
                                        MBEDTLS_SSL_VERSION_TLS1_2);
                                mbedtls_ssl_conf_max_tls_version(&conf,
                                        MBEDTLS_SSL_VERSION_TLS1_2);
                                break;
                        case TlsV1_3:
                                mbedtls_ssl_conf_min_tls_version(&conf,
                                        MBEDTLS_SSL_VERSION_TLS1_3);
                                mbedtls_ssl_conf_max_tls_version(&conf,
                                        MBEDTLS_SSL_VERSION_TLS1_3);
                                break;
                        case SecureProtocols:
                                mbedtls_ssl_conf_min_tls_version(&conf,
                                        MBEDTLS_SSL_VERSION_TLS1_2);
                                mbedtls_ssl_conf_max_tls_version(&conf,
                                        MBEDTLS_SSL_VERSION_TLS1_3);
                                break;
                }
        }

        void applyAuthMode() {
                // mbedTLS authmode is *symmetric* — for SERVER
                // endpoints it controls whether to require a client
                // certificate, and for CLIENT endpoints it controls
                // whether to verify the server's certificate.  The
                // common production shapes are:
                //
                //   - HTTPS server: NEVER require client cert (the
                //     web norm).  Only switch to REQUIRED when the
                //     deployment is actually doing mutual TLS and
                //     a CA chain is registered as the trust anchor.
                //   - HTTPS client: ALWAYS verify the server cert.
                //
                // We approximate that by mapping verifyPeer=true to
                // REQUIRED only when a CA chain has been loaded.
                // Server contexts that just have a server cert /
                // key (no CA chain) become VERIFY_NONE — no
                // client-cert requirement.  Client contexts that
                // load a CA bundle become VERIFY_REQUIRED (the safe
                // default).  Callers can explicitly opt into
                // mutual TLS by configuring a CA chain on the
                // server-side context.
                int mode;
                if(!verifyPeer) mode = MBEDTLS_SSL_VERIFY_NONE;
                else if(hasCaChain) mode = MBEDTLS_SSL_VERIFY_REQUIRED;
                else                mode = MBEDTLS_SSL_VERIFY_NONE;
                mbedtls_ssl_conf_authmode(&conf, mode);
        }
};

// ============================================================
// Public surface
// ============================================================

SslContext::SslContext() : _d(ImplPtr::create()) {}
// Out-of-line destructor: required because Impl is incomplete in the
// header, so the compiler-generated UniquePtr<Impl> destructor cannot
// be emitted there.
SslContext::~SslContext() = default;

void SslContext::setProtocol(SslProtocol protocol) {
        _d->protocol = protocol;
        if(_d->configReady) _d->applyProtocol();
}

SslContext::SslProtocol SslContext::protocol() const { return _d->protocol; }

void SslContext::setVerifyPeer(bool enable) {
        _d->verifyPeer = enable;
        if(_d->configReady) _d->applyAuthMode();
}

bool SslContext::verifyPeer() const { return _d->verifyPeer; }

void SslContext::setVerifyDepth(int depth) { _d->verifyDepth = depth; }
int  SslContext::verifyDepth() const       { return _d->verifyDepth; }

bool SslContext::hasCertificate() const     { return _d->hasOwnCert; }
bool SslContext::hasCaCertificates() const  { return _d->hasCaChain; }

void *SslContext::nativeConfig() const {
        // ensureConfig is the lazy-init point: callers that drive
        // mbedtls_ssl_setup against this config need it populated.
        // UniquePtr<Impl>::operator-> returns a mutable Impl * even
        // from a const context, so no const_cast is required.
        _d->ensureConfig();
        return &_d->conf;
}

// ----------------------------------------------------
// Helpers
// ----------------------------------------------------

namespace {

// Read an entire file into a heap Buffer plus a NUL terminator;
// the latter is required by mbedTLS's PEM auto-detection.
static Error readWholeFile(const FilePath &path, Buffer &out) {
        File f(path.toString());
        Error err = f.open(IODevice::ReadOnly);
        if(err.isError()) return err;
        auto sizeRes = f.size();
        if(sizeRes.second().isError()) return sizeRes.second();
        const int64_t sz = sizeRes.first();
        Buffer b(static_cast<size_t>(sz) + 1);
        int64_t got = f.read(b.data(), sz);
        if(got < 0) return Error::IOError;
        static_cast<char *>(b.data())[got] = '\0';
        b.setSize(static_cast<size_t>(got) + 1);
        out = std::move(b);
        return Error::Ok;
}

static Error mbedtlsErrorToError(int rc, const char *what) {
        if(rc == 0) return Error::Ok;
        char errbuf[160] = {0};
        mbedtls_strerror(rc, errbuf, sizeof(errbuf));
        promekiWarn("SslContext: %s failed (-0x%04x): %s",
                what, static_cast<unsigned>(-rc), errbuf);
        return Error::ParseFailed;
}

} // anonymous namespace

// ----------------------------------------------------
// Server credentials
// ----------------------------------------------------

Error SslContext::setCertificate(const FilePath &file) {
        Buffer b;
        Error e = readWholeFile(file, b);
        if(e.isError()) return e;
        return setCertificate(b);
}

Error SslContext::setCertificate(const Buffer &certData) {
        // mbedTLS 3.6's X.509 layer routes public-key validation
        // through PSA Crypto, so PSA must be initialized before
        // parsing.  Doing it here (rather than only inside
        // ensureConfig) means callers that load a cert before
        // doing anything else still get a working parse.
        Error psa = ensurePsaCrypto();
        if(psa.isError()) return psa;
        if(_d->hasOwnCert) {
                mbedtls_x509_crt_free(&_d->ownCert);
                mbedtls_x509_crt_init(&_d->ownCert);
                _d->hasOwnCert = false;
        }
        const int rc = mbedtls_x509_crt_parse(&_d->ownCert,
                static_cast<const unsigned char *>(certData.data()),
                certData.size());
        Error e = mbedtlsErrorToError(rc, "x509_crt_parse(cert)");
        if(e.isError()) return e;
        _d->hasOwnCert = true;
        if(_d->hasOwnKey) {
                if(_d->ensureConfig().isError()) return Error::LibraryFailure;
                int crc = mbedtls_ssl_conf_own_cert(&_d->conf, &_d->ownCert, &_d->ownKey);
                if(crc != 0) return mbedtlsErrorToError(crc, "ssl_conf_own_cert");
        }
        return Error::Ok;
}

Error SslContext::setPrivateKey(const FilePath &file, const String &passphrase) {
        Buffer b;
        Error e = readWholeFile(file, b);
        if(e.isError()) return e;
        return setPrivateKey(b, passphrase);
}

Error SslContext::setPrivateKey(const Buffer &keyData, const String &passphrase) {
        if(_d->hasOwnKey) {
                mbedtls_pk_free(&_d->ownKey);
                mbedtls_pk_init(&_d->ownKey);
                _d->hasOwnKey = false;
        }
        Error psa = ensurePsaCrypto();
        if(psa.isError()) return psa;
        // mbedTLS 3.6 dropped the trailing RNG callback pair from
        // mbedtls_pk_parse_key — entropy is sourced from PSA when
        // the key format demands it.
        const int rc = mbedtls_pk_parse_key(&_d->ownKey,
                static_cast<const unsigned char *>(keyData.data()),
                keyData.size(),
                passphrase.isEmpty() ? nullptr
                        : reinterpret_cast<const unsigned char *>(passphrase.cstr()),
                passphrase.byteCount());
        Error e = mbedtlsErrorToError(rc, "pk_parse_key");
        if(e.isError()) return e;
        _d->hasOwnKey = true;
        if(_d->hasOwnCert) {
                if(_d->ensureConfig().isError()) return Error::LibraryFailure;
                int crc = mbedtls_ssl_conf_own_cert(&_d->conf, &_d->ownCert, &_d->ownKey);
                if(crc != 0) return mbedtlsErrorToError(crc, "ssl_conf_own_cert");
        }
        return Error::Ok;
}

// ----------------------------------------------------
// Trust store
// ----------------------------------------------------

Error SslContext::setCaCertificates(const FilePath &caFile) {
        Buffer b;
        Error e = readWholeFile(caFile, b);
        if(e.isError()) return e;
        return setCaCertificates(b);
}

Error SslContext::setCaCertificates(const Buffer &caData) {
        Error psa = ensurePsaCrypto();
        if(psa.isError()) return psa;
        if(_d->hasCaChain) {
                mbedtls_x509_crt_free(&_d->caChain);
                mbedtls_x509_crt_init(&_d->caChain);
                _d->hasCaChain = false;
        }
        const int rc = mbedtls_x509_crt_parse(&_d->caChain,
                static_cast<const unsigned char *>(caData.data()),
                caData.size());
        Error e = mbedtlsErrorToError(rc, "x509_crt_parse(ca)");
        if(e.isError()) return e;
        _d->hasCaChain = true;
        if(_d->ensureConfig().isError()) return Error::LibraryFailure;
        mbedtls_ssl_conf_ca_chain(&_d->conf, &_d->caChain, nullptr);
        // Re-apply authmode now that hasCaChain has flipped:
        // verifyPeer=true with a fresh CA chain promotes the mode
        // to VERIFY_REQUIRED.
        _d->applyAuthMode();
        return Error::Ok;
}

Error SslContext::setSystemCaCertificates() {
        // Probe well-known Linux locations.  No equivalent on
        // macOS / Windows yet; on those platforms callers should
        // load a bundled CA file explicitly.  Returns NotExist
        // when no candidate worked so callers can fall back.
        static const char *kCandidates[] = {
                "/etc/ssl/certs/ca-certificates.crt",       // Debian / Ubuntu
                "/etc/pki/tls/certs/ca-bundle.crt",         // RHEL / Fedora
                "/etc/ssl/cert.pem",                        // OpenBSD / macOS
                "/etc/ssl/ca-bundle.pem",                   // openSUSE
        };
        for(const char *path : kCandidates) {
                FileInfo info{path};
                if(!info.exists() || !info.isFile()) continue;
                Error e = setCaCertificates(FilePath(path));
                if(e.isOk()) return Error::Ok;
        }
        return Error::NotExist;
}

PROMEKI_NAMESPACE_END
