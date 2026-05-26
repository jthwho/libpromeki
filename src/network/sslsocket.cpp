/**
 * @file      sslsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sslsocket.h>
#include <promeki/logger.h>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include <mbedtls/x509_crt.h>
#include <cstring>
#include <errno.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SslSocket);

// ============================================================
// Pimpl: hold mbedTLS state out of the public header.
// ============================================================
struct SslSocket::Impl {
                PROMEKI_SHARED_FINAL(Impl)

                mbedtls_ssl_context ssl{};
                bool                sslReady = false; ///< after mbedtls_ssl_setup
                bool                sentClose = false;
                Impl() { mbedtls_ssl_init(&ssl); }
                ~Impl() { mbedtls_ssl_free(&ssl); }
};

namespace {

        // mbedTLS BIO send: bridges into TcpSocket::write.  Returns the
        // MBEDTLS_ERR_SSL_WANT_WRITE sentinel on EAGAIN-style would-block
        // errors so the SSL layer knows to suspend until the loop signals
        // IoWrite again.
        static int bioSend(void *ctx, const unsigned char *buf, size_t len) {
                auto         *self = static_cast<SslSocket *>(ctx);
                const int64_t n = self->TcpSocket::write(buf, static_cast<int64_t>(len));
                if (n >= 0) return static_cast<int>(n);
                if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
                if (errno == EPIPE || errno == ECONNRESET) return MBEDTLS_ERR_NET_CONN_RESET;
                return MBEDTLS_ERR_NET_SEND_FAILED;
        }

        // mbedTLS BIO recv: bridges into TcpSocket::read.  Same EAGAIN
        // translation.  Returns 0 on peer EOF — mbedTLS treats that as
        // "connection closed cleanly" or "connection closed unexpectedly"
        // depending on whether a close_notify was received.
        static int bioRecv(void *ctx, unsigned char *buf, size_t len) {
                auto         *self = static_cast<SslSocket *>(ctx);
                const int64_t n = self->TcpSocket::read(buf, static_cast<int64_t>(len));
                if (n > 0) return static_cast<int>(n);
                if (n == 0) return 0;
                if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
                if (errno == ECONNRESET) return MBEDTLS_ERR_NET_CONN_RESET;
                return MBEDTLS_ERR_NET_RECV_FAILED;
        }

        static String mbedtlsErrText(int rc) {
                char buf[128] = {0};
                mbedtls_strerror(rc, buf, sizeof(buf));
                return String(buf);
        }

        // Pretty-print an X.509 DN into a short String.  Returns the
        // empty String on the rare error path; the caller decides
        // whether to elide the field or substitute a placeholder.
        static String x509DnString(const mbedtls_x509_name *dn) {
                if (dn == nullptr) return String();
                char buf[256] = {0};
                int  rc = mbedtls_x509_dn_gets(buf, sizeof(buf), dn);
                if (rc < 0) return String();
                return String(buf);
        }

        // Render the verify-result bitfield as a short, fixed-form
        // human string so a single warn line is self-diagnosing in
        // a field log.  Unrecognized flag bits collapse to a
        // hex-formatted fallback so we never silently drop signal.
        static String verifyFlagsToString(uint32_t flags) {
                if (flags == 0) return String("ok");
                String s;
                auto   append = [&s](const char *what) {
                        if (!s.isEmpty()) s += ",";
                        s += what;
                };
                if (flags & MBEDTLS_X509_BADCERT_EXPIRED) append("expired");
                if (flags & MBEDTLS_X509_BADCERT_REVOKED) append("revoked");
                if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) append("cn-mismatch");
                if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) append("not-trusted");
                if (flags & MBEDTLS_X509_BADCERT_FUTURE) append("not-yet-valid");
                if (flags & MBEDTLS_X509_BADCERT_BAD_MD) append("bad-hash");
                if (flags & MBEDTLS_X509_BADCERT_BAD_PK) append("bad-pk");
                if (flags & MBEDTLS_X509_BADCERT_BAD_KEY) append("bad-key");
                if (flags & MBEDTLS_X509_BADCERT_OTHER) append("other");
                if (s.isEmpty()) s = String::sprintf("0x%08x", flags);
                return s;
        }

} // anonymous namespace

// ============================================================
// SslSocket
// ============================================================

SslSocket::SslSocket(ObjectBase *parent)
    : TcpSocket(parent), _d(SharedPtr<Impl>::create()) {}

SslSocket::~SslSocket() {
        if (_state == Encrypted && !_d->sentClose) {
                promekiDebug("SslSocket::~SslSocket: implicit close_notify "
                             "(role=%s peer=%s)",
                             _isClient ? "client" : "server",
                             _isClient ? _hostname.cstr() : "(connected client)");
                // Best-effort close_notify.  Ignore errors — the
                // socket is going away regardless.
                Impl *d = _d.modify();
                mbedtls_ssl_close_notify(&d->ssl);
                d->sentClose = true;
        }
        // _d's SharedPtr destructor releases the Impl when the last
        // reference drops — no explicit delete needed.
}

void SslSocket::setSslContext(SslContext ctx) {
        if (_state != NotEncrypted) {
                promekiWarn("SslSocket::setSslContext: already handshaking, ignoring");
                return;
        }
        _ctx = std::move(ctx);
}

Error SslSocket::startEncryption(const String &hostname) {
        if (_state != NotEncrypted) return Error::AlreadyOpen;

        _hostname = hostname;
        _isClient = true;
        promekiDebug("SslSocket::startEncryption: client handshake to '%s' "
                     "(profile=%s, verifyPeer=%s, hasCa=%s)",
                     hostname.cstr(),
                     _ctx.securityProfile() == SslContext::Strict ? "Strict" : "Compatible",
                     _ctx.verifyPeer() ? "true" : "false",
                     _ctx.hasCaCertificates() ? "true" : "false");

        // Fail-closed verify policy: refuse to start a client
        // handshake when the caller asked for verification but did
        // not load any CA anchors.  Without CAs the handshake would
        // either be silently insecure (the historical mbedTLS
        // VERIFY_NONE fallback) or pointlessly reject every cert.
        // Either way the right answer is to surface the
        // misconfiguration up-front so production code doesn't
        // accidentally trust unverified servers.  Callers who really
        // want unauthenticated TLS (loopback smoke tests, self-
        // signed dev servers) call setVerifyPeer(false) explicitly.
        if (_ctx.verifyPeer() && !_ctx.hasCaCertificates()) {
                promekiWarn("SslSocket::startEncryption: refusing client handshake to '%s' "
                            "without CA chain — call setSystemCaCertificates() / "
                            "setCaCertificates() or setVerifyPeer(false) on the "
                            "SslContext first",
                            hostname.cstr());
                return Error::Invalid;
        }

        // nativeConfig lazy-initializes the underlying mbedTLS
        // configuration on first call.  Returns nullptr only when
        // the build does not ship TLS.
        mbedtls_ssl_config *conf = static_cast<mbedtls_ssl_config *>(_ctx.nativeConfig());
        if (conf == nullptr) return Error::NotSupported;

        // Client endpoint: we may be reusing a config that was
        // originally created in SERVER mode (the SslContext default)
        // — flip the per-context endpoint via mbedtls_ssl_conf_endpoint.
        mbedtls_ssl_conf_endpoint(conf, MBEDTLS_SSL_IS_CLIENT);

        // Apply the client-side authmode policy.  Both branches are
        // explicit so we never inherit a stale value left over from
        // a prior server-side use of the same shared config.
        mbedtls_ssl_conf_authmode(
                conf, _ctx.verifyPeer() ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_NONE);

        Impl *d = _d.modify();
        int   rc = mbedtls_ssl_setup(&d->ssl, conf);
        if (rc != 0) {
                promekiWarn("SslSocket: client ssl_setup for '%s' failed: %s",
                            hostname.cstr(), mbedtlsErrText(rc).cstr());
                return Error::LibraryFailure;
        }
        d->sslReady = true;

        if (!hostname.isEmpty()) {
                rc = mbedtls_ssl_set_hostname(&d->ssl, hostname.cstr());
                if (rc != 0) {
                        promekiWarn("SslSocket: set_hostname('%s') failed: %s",
                                    hostname.cstr(), mbedtlsErrText(rc).cstr());
                        return Error::LibraryFailure;
                }
        }

        mbedtls_ssl_set_bio(&d->ssl, this, bioSend, bioRecv, nullptr);
        _state = Handshaking;
        return performHandshakeStep();
}

Error SslSocket::startServerEncryption() {
        if (_state != NotEncrypted) return Error::AlreadyOpen;
        if (!_ctx.hasCertificate()) {
                promekiWarn("SslSocket::startServerEncryption: SslContext has no certificate");
                return Error::Invalid;
        }

        _hostname.clear();
        _isClient = false;
        promekiDebug("SslSocket::startServerEncryption: server handshake "
                     "(profile=%s, requireClientCert=%s, hasCa=%s)",
                     _ctx.securityProfile() == SslContext::Strict ? "Strict" : "Compatible",
                     _ctx.requireClientCert() ? "true" : "false",
                     _ctx.hasCaCertificates() ? "true" : "false");

        // nativeConfig lazy-allocates on first call; returns nullptr
        // only in TLS-disabled builds.
        mbedtls_ssl_config *conf = static_cast<mbedtls_ssl_config *>(_ctx.nativeConfig());
        if (conf == nullptr) return Error::NotSupported;

        // SslContext defaults to SERVER endpoint, so no flip needed
        // for the typical path.  Set explicitly anyway in case the
        // same context was previously used for a client handshake.
        mbedtls_ssl_conf_endpoint(conf, MBEDTLS_SSL_IS_SERVER);

        // Apply the server-side authmode policy.  Mutual TLS is
        // opt-in via setRequireClientCert — the default is the
        // standard HTTPS server pattern (VERIFY_NONE, don't ask
        // clients for a cert).  We deliberately do NOT consult
        // verifyPeer here: that's the client-side knob for
        // verifying the @em server's cert, and conflating the two
        // led to a footgun where auto-loaded system CAs would
        // silently flip a default HTTPS server into mutual-TLS
        // mode.  When mutual TLS is requested we fail-closed if no
        // CA chain has been loaded — there'd be no anchors to
        // validate the client cert against.
        if (_ctx.requireClientCert()) {
                if (!_ctx.hasCaCertificates()) {
                        promekiWarn("SslSocket::startServerEncryption: requireClientCert is set "
                                    "but no CA chain is configured — call setCaCertificates() "
                                    "with the trust anchors before requesting mutual TLS");
                        return Error::Invalid;
                }
                mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        } else {
                mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
        }

        Impl *d = _d.modify();
        int   rc = mbedtls_ssl_setup(&d->ssl, conf);
        if (rc != 0) {
                promekiWarn("SslSocket: server ssl_setup failed: %s", mbedtlsErrText(rc).cstr());
                return Error::LibraryFailure;
        }
        d->sslReady = true;

        mbedtls_ssl_set_bio(&d->ssl, this, bioSend, bioRecv, nullptr);
        _state = Handshaking;
        return performHandshakeStep();
}

Error SslSocket::performHandshakeStep() {
        if (_state != Handshaking) return Error::Invalid;

        Impl *d = _d.modify();
        // mbedtls_ssl_handshake drives the full handshake but
        // suspends with MBEDTLS_ERR_SSL_WANT_READ / WRITE on a
        // non-blocking BIO.  Callers re-invoke when the loop says
        // the fd is ready in the corresponding direction.
        const int rc = mbedtls_ssl_handshake(&d->ssl);
        if (rc == 0) {
                _state = Encrypted;
                // Pull the negotiated state out of mbedTLS so the
                // debug line names exactly what was agreed.  Useful
                // when an in-field error report says "TLS broke" —
                // the pre-flight session details narrow the search
                // immediately (wrong cipher, downgraded TLS version,
                // unexpected peer cert, etc.).
                const char *negVersion = mbedtls_ssl_get_version(&d->ssl);
                const char *negCipher = mbedtls_ssl_get_ciphersuite(&d->ssl);
                const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&d->ssl);
                const uint32_t          flags = mbedtls_ssl_get_verify_result(&d->ssl);
                String                  subj = peer ? x509DnString(&peer->subject) : String();
                String                  issuer = peer ? x509DnString(&peer->issuer) : String();
                promekiDebug("SslSocket: handshake ok role=%s peer=%s "
                             "tls=%s cipher=%s subject=\"%s\" issuer=\"%s\" verify=%s",
                             _isClient ? "client" : "server",
                             _isClient ? _hostname.cstr() : "(connected client)",
                             negVersion ? negVersion : "?",
                             negCipher ? negCipher : "?",
                             subj.isEmpty() ? "(none)" : subj.cstr(),
                             issuer.isEmpty() ? "(none)" : issuer.cstr(),
                             verifyFlagsToString(flags).cstr());

                encryptedSignal.emit();
                // Surface any non-fatal verification issues so the
                // application can log / decide.  When verifyPeer was
                // disabled the handshake completes despite errors —
                // we still report them so debug builds see them, and
                // we promote them to a warn so the field log shows
                // them even when debug is off (verification is the
                // exact kind of thing operators need to see by
                // default).
                if (flags != 0) {
                        const String flagText = verifyFlagsToString(flags);
                        promekiWarn("SslSocket: peer-cert verify flags %s for %s "
                                    "subject=\"%s\" (verifyPeer was %s)",
                                    flagText.cstr(),
                                    _isClient ? _hostname.cstr() : "client",
                                    subj.isEmpty() ? "(none)" : subj.cstr(),
                                    _ctx.verifyPeer() ? "true" : "false");
                        StringList errs;
                        if (flags & MBEDTLS_X509_BADCERT_EXPIRED) errs.pushToBack("certificate expired");
                        if (flags & MBEDTLS_X509_BADCERT_REVOKED) errs.pushToBack("certificate revoked");
                        if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) errs.pushToBack("hostname mismatch");
                        if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) errs.pushToBack("not trusted");
                        if (errs.isEmpty()) errs.pushToBack(String::sprintf("verify result 0x%08x", flags));
                        sslErrorsSignal.emit(errs);
                }
                return Error::Ok;
        }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
                // Caller re-invokes when the loop signals readiness.
                return Error::TryAgain;
        }
        // Real failure.  Include role + hostname + negotiated state
        // (if any partial state is available) so the warn line is
        // self-diagnosing in a field log.
        const char *negVersion = mbedtls_ssl_get_version(&d->ssl);
        const char *negCipher = mbedtls_ssl_get_ciphersuite(&d->ssl);
        promekiWarn("SslSocket: %s handshake to '%s' failed: %s "
                    "(tls=%s cipher=%s)",
                    _isClient ? "client" : "server",
                    _isClient ? _hostname.cstr() : "(connected client)",
                    mbedtlsErrText(rc).cstr(),
                    negVersion ? negVersion : "n/a",
                    negCipher ? negCipher : "n/a");
        _state = Failed;
        return Error::ConnectionReset;
}

Error SslSocket::continueHandshake() {
        return performHandshakeStep();
}

String SslSocket::peerCertificateSubject() const {
        if (_state != Encrypted) return String();
        // mbedtls_ssl_get_peer_cert takes a non-const ssl pointer but
        // the call only reads cached peer state — safe to const_cast
        // off the read-only @c _d view here so the accessor stays
        // const.  (Same convention applies to @ref bytesAvailable.)
        Impl *d = const_cast<Impl *>(_d.operator->());
        const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&d->ssl);
        if (peer == nullptr) return String();
        char buf[512] = {0};
        int  rc = mbedtls_x509_dn_gets(buf, sizeof(buf), &peer->subject);
        if (rc < 0) return String();
        return String(buf);
}

// ----------------------------------------------------
// IODevice overrides
// ----------------------------------------------------

int64_t SslSocket::read(void *data, int64_t maxSize) {
        if (_state != Encrypted) return TcpSocket::read(data, maxSize);
        Impl    *d = _d.modify();
        auto    *buf = static_cast<unsigned char *>(data);
        int64_t  total = 0;

        // Drain every decoded TLS record currently sitting in
        // mbedTLS's input buffer plus any whole records the kernel
        // has handed up since the last call.  Single-call versions
        // capped each invocation at one TLS record (~16 KiB, see
        // MBEDTLS_SSL_IN_CONTENT_LEN) which forced the upstream
        // event loop into one epoll wake-up per record — measured
        // in Phase A as the cause of the ~30 % throughput gap vs
        // curl on Hugging Face downloads.  Looping here lets a
        // single readiness event consume up to the caller's full
        // buffer in one go, matching curl's drain pattern.
        //
        // Loop invariants:
        //   - We return the bytes already buffered as soon as
        //     mbedTLS signals WANT_READ / WANT_WRITE (no more data
        //     ready right now).
        //   - We propagate peer close cleanly as a 0 return only
        //     when nothing else is buffered, so a coalesced
        //     "last-record + close_notify" wake-up still delivers
        //     the bytes first.
        //   - The first hard error short-circuits everything; any
        //     bytes already collected ride into the caller's hands
        //     so they are not silently dropped.
        while (total < maxSize) {
                const size_t want = static_cast<size_t>(maxSize - total);
                const int    rc = mbedtls_ssl_read(&d->ssl, buf + total, want);
                if (rc > 0) {
                        total += rc;
                        continue;
                }
                if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE ||
                    rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
                        // No more decoded bytes available right
                        // now.  NewSessionTicket is a TLS 1.3
                        // post-handshake message; we don't use
                        // session resumption, so treat it as "call
                        // again".  Return whatever we already
                        // gathered; the caller will be re-armed on
                        // the next ready event.
                        if (total > 0) return total;
                        errno = EAGAIN;
                        return -1;
                }
                if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                        // Clean shutdown.  If anything was already
                        // decoded, surface that first so the caller
                        // sees the trailing bytes before the EOF.
                        if (total > 0) return total;
                        return 0;
                }
                promekiWarnThrottled(1000, "SslSocket::read mbedtls_ssl_read failed: %s",
                                     mbedtlsErrText(rc).cstr());
                if (total > 0) return total;
                return -1;
        }
        return total;
}

int64_t SslSocket::write(const void *data, int64_t maxSize) {
        if (_state != Encrypted) return TcpSocket::write(data, maxSize);
        Impl     *d = _d.modify();
        const int rc =
                mbedtls_ssl_write(&d->ssl, static_cast<const unsigned char *>(data), static_cast<size_t>(maxSize));
        if (rc > 0) return rc;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
                errno = EAGAIN;
                return -1;
        }
        promekiWarnThrottled(1000, "SslSocket::write mbedtls_ssl_write failed: %s",
                             mbedtlsErrText(rc).cstr());
        return -1;
}

int64_t SslSocket::bytesAvailable() const {
        if (_state != Encrypted) return TcpSocket::bytesAvailable();
        // mbedtls_ssl_get_bytes_avail reports plaintext bytes that
        // have already been decoded and are sitting in the SSL
        // input buffer — exactly the right answer for callers that
        // poll-then-read.  Takes a non-const pointer but only reads
        // cached state; const_cast through the read-only @c _d view.
        Impl *d = const_cast<Impl *>(_d.operator->());
        return static_cast<int64_t>(mbedtls_ssl_get_bytes_avail(&d->ssl));
}

Error SslSocket::close() {
        if (_state == Encrypted && !_d->sentClose) {
                promekiDebug("SslSocket::close: sending close_notify (role=%s peer=%s)",
                             _isClient ? "client" : "server",
                             _isClient ? _hostname.cstr() : "(connected client)");
                Impl *d = _d.modify();
                mbedtls_ssl_close_notify(&d->ssl);
                d->sentClose = true;
        } else if (_state != NotEncrypted) {
                promekiDebug("SslSocket::close: closing in state=%d (no close_notify sent)",
                             static_cast<int>(_state));
        }
        _state = NotEncrypted;
        return TcpSocket::close();
}

PROMEKI_NAMESPACE_END
