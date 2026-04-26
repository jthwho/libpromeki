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
#include <cstring>
#include <errno.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SslSocket);

// ============================================================
// Pimpl: hold mbedTLS state out of the public header.
// ============================================================
struct SslSocket::Impl {
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

} // anonymous namespace

// ============================================================
// SslSocket
// ============================================================

SslSocket::SslSocket(ObjectBase *parent) : TcpSocket(parent), _d(new Impl()) {}

SslSocket::~SslSocket() {
        if (_state == Encrypted && !_d->sentClose) {
                // Best-effort close_notify.  Ignore errors — the
                // socket is going away regardless.
                mbedtls_ssl_close_notify(&_d->ssl);
                _d->sentClose = true;
        }
        delete _d;
        _d = nullptr;
}

void SslSocket::setSslContext(SslContext::Ptr ctx) {
        if (_state != NotEncrypted) {
                promekiWarn("SslSocket::setSslContext: already handshaking, ignoring");
                return;
        }
        _ctx = std::move(ctx);
}

Error SslSocket::startEncryption(const String &hostname) {
        if (_state != NotEncrypted) return Error::AlreadyOpen;
        if (!_ctx.isValid()) return Error::Invalid;

        mbedtls_ssl_config *conf = static_cast<mbedtls_ssl_config *>(_ctx->nativeConfig());
        if (conf == nullptr) return Error::LibraryFailure;

        // Client endpoint: we may be reusing a config that was
        // originally created in SERVER mode (the SslContext default)
        // — flip the per-context endpoint via mbedtls_ssl_conf_endpoint.
        mbedtls_ssl_conf_endpoint(conf, MBEDTLS_SSL_IS_CLIENT);

        int rc = mbedtls_ssl_setup(&_d->ssl, conf);
        if (rc != 0) {
                promekiWarn("SslSocket: ssl_setup failed: %s", mbedtlsErrText(rc).cstr());
                return Error::LibraryFailure;
        }
        _d->sslReady = true;

        if (!hostname.isEmpty()) {
                rc = mbedtls_ssl_set_hostname(&_d->ssl, hostname.cstr());
                if (rc != 0) {
                        promekiWarn("SslSocket: set_hostname failed: %s", mbedtlsErrText(rc).cstr());
                        return Error::LibraryFailure;
                }
        }

        mbedtls_ssl_set_bio(&_d->ssl, this, bioSend, bioRecv, nullptr);
        _state = Handshaking;
        return performHandshakeStep();
}

Error SslSocket::startServerEncryption() {
        if (_state != NotEncrypted) return Error::AlreadyOpen;
        if (!_ctx.isValid()) return Error::Invalid;
        if (!_ctx->hasCertificate()) {
                promekiWarn("SslSocket::startServerEncryption: SslContext has no certificate");
                return Error::Invalid;
        }

        mbedtls_ssl_config *conf = static_cast<mbedtls_ssl_config *>(_ctx->nativeConfig());
        if (conf == nullptr) return Error::LibraryFailure;

        // SslContext defaults to SERVER endpoint, so no flip needed
        // for the typical path.  Set explicitly anyway in case the
        // same context was previously used for a client handshake.
        mbedtls_ssl_conf_endpoint(conf, MBEDTLS_SSL_IS_SERVER);

        int rc = mbedtls_ssl_setup(&_d->ssl, conf);
        if (rc != 0) {
                promekiWarn("SslSocket: ssl_setup failed: %s", mbedtlsErrText(rc).cstr());
                return Error::LibraryFailure;
        }
        _d->sslReady = true;

        mbedtls_ssl_set_bio(&_d->ssl, this, bioSend, bioRecv, nullptr);
        _state = Handshaking;
        return performHandshakeStep();
}

Error SslSocket::performHandshakeStep() {
        if (_state != Handshaking) return Error::Invalid;

        // mbedtls_ssl_handshake drives the full handshake but
        // suspends with MBEDTLS_ERR_SSL_WANT_READ / WRITE on a
        // non-blocking BIO.  Callers re-invoke when the loop says
        // the fd is ready in the corresponding direction.
        const int rc = mbedtls_ssl_handshake(&_d->ssl);
        if (rc == 0) {
                _state = Encrypted;
                encryptedSignal.emit();
                // Surface any non-fatal verification issues so the
                // application can log / decide.  When verifyPeer was
                // disabled the handshake completes despite errors —
                // we still report them so debug builds see them.
                const uint32_t flags = mbedtls_ssl_get_verify_result(&_d->ssl);
                if (flags != 0) {
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
        // Real failure.
        promekiWarn("SslSocket: handshake failed: %s", mbedtlsErrText(rc).cstr());
        _state = Failed;
        return Error::ConnectionReset;
}

Error SslSocket::continueHandshake() {
        return performHandshakeStep();
}

String SslSocket::peerCertificateSubject() const {
        if (_state != Encrypted) return String();
        const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&_d->ssl);
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
        const int rc = mbedtls_ssl_read(&_d->ssl, static_cast<unsigned char *>(data), static_cast<size_t>(maxSize));
        if (rc > 0) return rc;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
                errno = EAGAIN;
                return -1;
        }
        if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                return 0; // clean shutdown
        }
        return -1;
}

int64_t SslSocket::write(const void *data, int64_t maxSize) {
        if (_state != Encrypted) return TcpSocket::write(data, maxSize);
        const int rc =
                mbedtls_ssl_write(&_d->ssl, static_cast<const unsigned char *>(data), static_cast<size_t>(maxSize));
        if (rc > 0) return rc;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
                errno = EAGAIN;
                return -1;
        }
        return -1;
}

int64_t SslSocket::bytesAvailable() const {
        if (_state != Encrypted) return TcpSocket::bytesAvailable();
        // mbedtls_ssl_get_bytes_avail reports plaintext bytes that
        // have already been decoded and are sitting in the SSL
        // input buffer — exactly the right answer for callers that
        // poll-then-read.
        return static_cast<int64_t>(mbedtls_ssl_get_bytes_avail(&_d->ssl));
}

Error SslSocket::close() {
        if (_d != nullptr && _state == Encrypted && !_d->sentClose) {
                mbedtls_ssl_close_notify(&_d->ssl);
                _d->sentClose = true;
        }
        _state = NotEncrypted;
        return TcpSocket::close();
}

PROMEKI_NAMESPACE_END
