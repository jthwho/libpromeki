/**
 * @file      sslsocket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/tcpsocket.h>
#include <promeki/sslcontext.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief @ref TcpSocket extension that wraps the byte stream in TLS.
 * @ingroup network
 *
 * @ref SslSocket inherits @ref TcpSocket so existing event-loop
 * machinery (poll, addIoSource, IODevice read/write) works
 * unchanged — the only differences for callers are:
 *
 *  - Before any I/O, attach an @ref SslContext via
 *    @ref setSslContext.
 *  - After the underlying TCP connection is up, call
 *    @ref startEncryption (client side) or
 *    @ref startServerEncryption (server side) to drive the TLS
 *    handshake.  Both calls are non-blocking; the handshake
 *    completes as the loop's I/O readiness events arrive.
 *  - @ref encryptedSignal fires once the handshake succeeds.
 *  - @ref read / @ref write proxy through @c mbedtls_ssl_read /
 *    @c mbedtls_ssl_write transparently.
 *
 * @par Thread Safety
 * Inherits @ref TcpSocket / @ref IODevice: thread-affine.  An
 * SslSocket binds to one EventLoop's thread for the duration of
 * its life.  The attached @ref SslContext may be shared across
 * many SslSocket instances on many threads — its mutable
 * lifecycle ends before the first handshake.
 *
 * @par TLS framing vs TCP byte stream
 * @ref bytesAvailable reports the number of plaintext bytes that
 * mbedTLS has already decoded and buffered, not the raw TCP
 * receive-buffer size.  This means a poll-then-read loop that
 * checks @c bytesAvailable() will always see zero until enough
 * ciphertext has arrived to produce at least one record.
 */
class SslSocket : public TcpSocket {
                PROMEKI_OBJECT(SslSocket, TcpSocket)
        public:
                /** @brief Convenience list type. */
                using List = promeki::List<SslSocket *>;

                /** @brief Constructs a socket with no SslContext attached. */
                explicit SslSocket(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Tears down the TLS session. */
                ~SslSocket() override;

                /**
                 * @brief Attaches the @ref SslContext used at handshake time.
                 *
                 * The context's lifetime must outlive this socket;
                 * pass a @ref SslContext::Ptr if you need shared
                 * ownership semantics.  Setting the context is a
                 * no-op once a handshake has started.
                 */
                void setSslContext(SslContext::Ptr ctx);

                /** @brief Returns the attached SslContext. */
                SslContext::Ptr sslContext() const { return _ctx; }

                /**
                 * @brief Initiates a client-side TLS handshake.
                 *
                 * The underlying TCP connection must already be
                 * established (call @ref connectToHost first).  The
                 * handshake runs asynchronously; @ref encryptedSignal
                 * fires on success, @ref errorOccurredSignal fires on
                 * failure.
                 *
                 * @param hostname Optional SNI / server-name hint.
                 *                 When non-empty the value is sent as
                 *                 the @c server_name extension.
                 */
                Error startEncryption(const String &hostname = String());

                /**
                 * @brief Initiates a server-side TLS handshake.
                 *
                 * Use this on a socket whose descriptor was just
                 * accepted from a TCP server.  An @ref SslContext
                 * configured with a server certificate + private
                 * key is required.
                 */
                Error startServerEncryption();

                /**
                 * @brief Continues an in-flight handshake one step.
                 *
                 * Returns @ref Error::Ok once the handshake has
                 * fully completed (and @ref isEncrypted is true);
                 * @ref Error::TryAgain when more I/O is needed
                 * (caller should re-invoke after the next ready
                 * event); a real error code on hard failure.
                 *
                 * Used by event-driven consumers (such as
                 * @ref HttpConnection) to drive the handshake to
                 * completion across multiple I/O wakes after the
                 * initial @ref startEncryption / @ref startServerEncryption
                 * call returned @ref Error::TryAgain.
                 */
                Error continueHandshake();

                /** @brief True after a successful handshake. */
                bool isEncrypted() const { return _state == Encrypted; }

                /**
                 * @brief Returns the peer certificate's subject DN, or empty.
                 *
                 * Available after the handshake completes.  Useful
                 * for client identification and audit logging.
                 */
                String peerCertificateSubject() const;

                // ----------------------------------------------------
                // IODevice overrides — proxy through mbedtls_ssl_*
                // ----------------------------------------------------

                int64_t read(void *data, int64_t maxSize) override;
                int64_t write(const void *data, int64_t maxSize) override;
                int64_t bytesAvailable() const override;
                Error   close() override;

                /** @brief Emitted once the TLS handshake succeeds. @signal */
                PROMEKI_SIGNAL(encrypted);

                /**
                 * @brief Emitted with a list of human-readable verification errors.
                 *
                 * Fires once after the handshake when peer
                 * verification was attempted but produced one or more
                 * errors (e.g. self-signed cert, expired cert).  When
                 * @ref SslContext::verifyPeer is true the handshake
                 * still aborts; with verification disabled this is
                 * the channel for inspecting what would have failed.
                 *
                 * @signal
                 */
                PROMEKI_SIGNAL(sslErrors, StringList);

        private:
                struct Impl;
                Impl           *_d = nullptr;
                SslContext::Ptr _ctx;

                enum SslState {
                        NotEncrypted,
                        Handshaking,
                        Encrypted,
                        Failed
                };
                SslState _state = NotEncrypted;

                Error performHandshakeStep();
};

PROMEKI_NAMESPACE_END
