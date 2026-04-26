/**
 * @file      httpconnection.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httpconnection.h>
#include <promeki/tcpsocket.h>
#include <promeki/eventloop.h>
#include <promeki/objectbase.tpp>
#include <promeki/socketaddress.h>
#include <promeki/url.h>
#include <promeki/logger.h>
#if PROMEKI_ENABLE_TLS
#include <promeki/sslsocket.h>
#endif
#include <llhttp.h>
#include <cstring>
#include <algorithm>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(HttpConnection);

// ============================================================
// Pimpl: hold the C parser type out of the header so consumers
// don't pay the llhttp.h include cost.
// ============================================================
struct HttpConnection::Impl {
                llhttp_t          parser;
                llhttp_settings_t settings;
};

// ============================================================
// Construction / lifecycle
// ============================================================

HttpConnection::HttpConnection(TcpSocket *socket, ObjectBase *parent)
    : ObjectBase(parent), _impl(UniquePtr<Impl>::create()), _socket(socket) {
        // Re-parent the socket so its lifetime matches the connection
        // even if the caller drops their reference.
        if (_socket != nullptr) _socket->setParent(this);

        _loop = EventLoop::current();
        _readBuf = Buffer(8192);
        _writeQueue = Buffer(0);

        // llhttp setup.  The parser lives entirely on the Impl; the
        // C callbacks read/write through parser->data which we point
        // back at this connection.
        llhttp_settings_init(&_impl->settings);
        _impl->settings.on_message_begin = reinterpret_cast<llhttp_cb>(&HttpConnection::cbMessageBegin);
        _impl->settings.on_url = reinterpret_cast<llhttp_data_cb>(&HttpConnection::cbUrl);
        _impl->settings.on_header_field = reinterpret_cast<llhttp_data_cb>(&HttpConnection::cbHeaderField);
        _impl->settings.on_header_value = reinterpret_cast<llhttp_data_cb>(&HttpConnection::cbHeaderValue);
        _impl->settings.on_headers_complete = reinterpret_cast<llhttp_cb>(&HttpConnection::cbHeadersComplete);
        _impl->settings.on_body = reinterpret_cast<llhttp_data_cb>(&HttpConnection::cbBody);
        _impl->settings.on_message_complete = reinterpret_cast<llhttp_cb>(&HttpConnection::cbMessageComplete);

        llhttp_init(&_impl->parser, HTTP_REQUEST, &_impl->settings);
        _impl->parser.data = this;
}

HttpConnection::~HttpConnection() {
        close();
}

String HttpConnection::peerAddress() const {
        if (_socket == nullptr) return String();
        return _socket->peerAddress().toString();
}

// ============================================================
// Public knobs
// ============================================================

void HttpConnection::setRequestHandler(RequestHandler handler) {
        _handler = std::move(handler);
}

void HttpConnection::setNeedsServerHandshake() {
#if PROMEKI_ENABLE_TLS
        _needsServerHandshake = true;
#endif
}

Error HttpConnection::start() {
        if (_socket == nullptr || !_socket->isOpen()) return Error::NotOpen;
        if (_loop == nullptr) {
                _loop = EventLoop::current();
                if (_loop == nullptr) return Error::Invalid;
        }
        if (_ioHandle >= 0) return Error::Ok; // idempotent

        // Non-blocking mode: the EventLoop drives reads/writes,
        // blocking calls would defeat that and stall the loop.
        _socket->setNonBlocking(true);

        const int fd = _socket->socketDescriptor();
        if (fd < 0) return Error::NotOpen;

        // TLS-server handshake: kick it off here so the very first
        // handshake step happens synchronously (cheap and avoids an
        // extra wakeup) and the loop drives any continuations on
        // subsequent IoRead / IoWrite events.
#if PROMEKI_ENABLE_TLS
        if (_needsServerHandshake) {
                SslSocket *ssl = static_cast<SslSocket *>(_socket);
                Error      err = ssl->startServerEncryption();
                if (err.isError() && err != Error::TryAgain) {
                        return err;
                }
                _state = ssl->isEncrypted() ? State::Reading : State::Handshaking;
        } else {
                _state = State::Reading;
        }
#else
        _state = State::Reading;
#endif

        // The handshake (if any) needs both read and write
        // readiness — mbedtls returns WANT_READ / WANT_WRITE based
        // on whatever direction it just blocked on, and the loop
        // wakes us when either side is ready.
        const uint32_t mask =
                (_state == State::Handshaking) ? (EventLoop::IoRead | EventLoop::IoWrite) : EventLoop::IoRead;
        _ioHandle = _loop->addIoSource(fd, mask, [this](int fd, uint32_t events) { onIoReady(fd, events); });
        if (_ioHandle < 0) return Error::Invalid;

        // Idle timeout: a single shot that resets each time we make
        // forward progress.  Keep-alive idle clients eventually trip
        // it and we close them out so the server doesn't accumulate
        // ghosts.
        if (_idleTimeoutMs > 0) {
                _timerId = _loop->startTimer(_idleTimeoutMs, [this]() { onIdleTimeout(); }, /*singleShot=*/true);
        }

        return Error::Ok;
}

void HttpConnection::close() {
        if (_state == State::Closed) return;
        _state = State::Closed;

        // Detach any async-stream readyRead hook before we let go of
        // the body device — the signal target is `this`, and we're
        // about to stop running the pump.
        detachStreamReadyRead();
        _streamSource = IODevice::Shared{};

        if (_loop != nullptr) {
                if (_ioHandle >= 0) {
                        _loop->removeIoSource(_ioHandle);
                        _ioHandle = -1;
                }
                if (_timerId >= 0) {
                        _loop->stopTimer(_timerId);
                        _timerId = -1;
                }
        }
        if (_socket != nullptr && _socket->isOpen()) {
                _socket->close();
        }
        closedSignal.emit();
}

// ============================================================
// I/O event handling
// ============================================================

void HttpConnection::onIoReady(int fd, uint32_t events) {
        (void)fd;
        if (_state == State::Closed) return;

        // Reset the idle timer on every readiness event — both reads
        // and writes count as forward progress.
        if (_timerId >= 0 && _loop != nullptr) {
                _loop->stopTimer(_timerId);
                _timerId = -1;
        }
        if (_idleTimeoutMs > 0 && _loop != nullptr) {
                _timerId = _loop->startTimer(_idleTimeoutMs, [this]() { onIdleTimeout(); }, /*singleShot=*/true);
        }

        if (events & EventLoop::IoError) {
                close();
                return;
        }

#if PROMEKI_ENABLE_TLS
        if (_state == State::Handshaking) {
                SslSocket *ssl = static_cast<SslSocket *>(_socket);
                Error      err = ssl->continueHandshake();
                if (err == Error::TryAgain) return;
                if (err.isError()) {
                        close();
                        return;
                }
                // Handshake done — drop write subscription (we'll
                // re-arm it when we have a response to send) and
                // proceed to the request parser.
                if (_loop != nullptr && _ioHandle >= 0) {
                        _loop->removeIoSource(_ioHandle);
                        _ioHandle = _loop->addIoSource(_socket->socketDescriptor(), EventLoop::IoRead,
                                                       [this](int f, uint32_t e) { onIoReady(f, e); });
                }
                _state = State::Reading;
                // Don't immediately try to read; the next IoRead
                // tick will fire when the client sends bytes.
                return;
        }
#endif

        if (events & EventLoop::IoWrite) pumpWrite();
        if (events & EventLoop::IoRead) readSome();
}

void HttpConnection::onIdleTimeout() {
        promekiDebug("HttpConnection: idle timeout, closing");
        close();
}

void HttpConnection::readSome() {
        if (_state == State::Closed || _socket == nullptr) return;

        char         *dst = static_cast<char *>(_readBuf.data());
        const int64_t cap = static_cast<int64_t>(_readBuf.allocSize());
        int64_t       n = _socket->read(dst, cap);
        if (n == 0) {
                // Peer half-closed.  Tell llhttp so it can finalize
                // any in-flight message, then close out.
                llhttp_finish(&_impl->parser);
                close();
                return;
        }
        if (n < 0) {
                // EAGAIN is expected on a non-blocking socket; the
                // EventLoop will wake us when more data arrives.
                // The library's read() returns -1 in that case but
                // does not set the error reliably, so we just bail.
                return;
        }

        const llhttp_errno_t rc = llhttp_execute(&_impl->parser, dst, static_cast<size_t>(n));
        if (rc != HPE_OK && rc != HPE_PAUSED && rc != HPE_PAUSED_UPGRADE) {
                const char *reason = llhttp_get_error_reason(&_impl->parser);
                promekiWarn("HttpConnection: parse error: %s", reason ? reason : "(unknown)");
                // Best-effort 400 reply, then close.  Reuse the
                // pending request as the responseSent context so
                // observers see the failure surface.
                HttpResponse res = HttpResponse::badRequest(reason ? reason : "");
                enqueueResponse(std::move(res));
                _keepAlive = false;
                errorOccurredSignal.emit(Error::Invalid);
        }
}

// ============================================================
// llhttp callbacks
// ============================================================

#define CONN(p) (static_cast<HttpConnection *>((static_cast<llhttp_t *>(p))->data))

int HttpConnection::cbMessageBegin(void *parser) {
        auto *self = CONN(parser);
        self->resetForNextRequest();
        return 0;
}

int HttpConnection::cbUrl(void *parser, const char *at, size_t len) {
        auto *self = CONN(parser);
        self->_urlBuf += String(at, static_cast<size_t>(len));
        return 0;
}

int HttpConnection::cbHeaderField(void *parser, const char *at, size_t len) {
        auto *self = CONN(parser);
        if (self->_hdrValueComplete) {
                // Previous (field, value) pair finished — flush it
                // before we start collecting a new field.
                self->flushPendingHeaderPair();
        }
        self->_hdrField += String(at, static_cast<size_t>(len));
        self->_hdrFieldComplete = true;
        self->_hdrValueComplete = false;
        return 0;
}

int HttpConnection::cbHeaderValue(void *parser, const char *at, size_t len) {
        auto *self = CONN(parser);
        self->_hdrValue += String(at, static_cast<size_t>(len));
        self->_hdrValueComplete = true;
        return 0;
}

int HttpConnection::cbHeadersComplete(void *parser) {
        auto *self = CONN(parser);
        self->flushPendingHeaderPair();

        // Method.
        const llhttp_method_t mt = static_cast<llhttp_method_t>(self->_impl->parser.method);
        const char           *mname = llhttp_method_name(mt);
        if (mname != nullptr) {
                self->_pendingRequest.setMethod(HttpMethod{String(mname)});
        }

        // HTTP version.
        self->_pendingRequest.setHttpVersion(
                String::sprintf("HTTP/%u.%u", self->_impl->parser.http_major, self->_impl->parser.http_minor));

        // URL.  llhttp gives us the raw request-target which may be
        // origin-form ("/path?q") or absolute-form ("http://host/...").
        // Try the absolute form first; fall back to building a Url
        // from path + Host header.
        Result<Url> urlResult = Url::fromString(self->_urlBuf);
        Url         u = urlResult.first();
        if (urlResult.second().isError() || u.scheme().isEmpty()) {
                u = Url();
                u.setScheme("http");
                const String host = self->_pendingRequest.header("Host");
                if (!host.isEmpty()) u.setHost(host);
                // Split the request target into path and query.
                const size_t q = self->_urlBuf.find('?');
                if (q == String::npos) {
                        u.setPath(self->_urlBuf);
                } else {
                        u.setPath(self->_urlBuf.left(q));
                        // Parse the query string into the Url's map.
                        const String qs = self->_urlBuf.mid(q + 1);
                        Url          qsUrl = Url::fromString(String("x:?") + qs).first();
                        u.setQuery(qsUrl.query());
                }
        }
        self->_pendingRequest.setUrl(u);
        self->_pendingRequest.setPeerAddress(self->peerAddress());

        // Default keep-alive policy: HTTP/1.1 keeps alive unless told
        // otherwise; HTTP/1.0 closes unless told otherwise.  llhttp
        // computes the post-headers verdict for us.
        self->_keepAlive = (llhttp_should_keep_alive(&self->_impl->parser) != 0);

        // Honor 100-continue: if the client sent Expect: 100-continue
        // we send the interim status now so they start uploading.
        // We do not validate body size here; the limit check fires
        // on body callbacks below.
        const String expect = self->_pendingRequest.header("Expect");
        if (!expect.isEmpty() && expect.toLower() == "100-continue") {
                static const char *kContinue = "HTTP/1.1 100 Continue\r\n\r\n";
                if (self->_socket != nullptr) {
                        self->_socket->write(kContinue, std::strlen(kContinue));
                }
        }
        return 0;
}

int HttpConnection::cbBody(void *parser, const char *at, size_t len) {
        auto *self = CONN(parser);
        self->_bodyBytesSoFar += static_cast<int64_t>(len);
        if (self->_maxBodyBytes >= 0 && self->_bodyBytesSoFar > self->_maxBodyBytes) {
                // 413: stop reading, queue the error reply, and ask
                // llhttp to bail so the next execute() returns the
                // user error.
                HttpResponse res;
                res.setStatus(HttpStatus::PayloadTooLarge);
                res.setText("Payload Too Large");
                self->enqueueResponse(std::move(res));
                self->_keepAlive = false;
                return -1;
        }

        // Append the body chunk to the request's body buffer.  The
        // common case is a single contiguous chunk; for chunked
        // transfer-encoding we may see many.
        Buffer       existing = self->_pendingRequest.body();
        const size_t prev = existing.isValid() ? existing.size() : 0;
        Buffer       grown(prev + len);
        if (prev > 0) std::memcpy(grown.data(), existing.data(), prev);
        std::memcpy(static_cast<char *>(grown.data()) + prev, at, len);
        grown.setSize(prev + len);
        self->_pendingRequest.setBody(grown);
        return 0;
}

int HttpConnection::cbMessageComplete(void *parser) {
        auto *self = CONN(parser);
        self->deliverRequest();
        return 0;
}

#undef CONN

void HttpConnection::flushPendingHeaderPair() {
        if (_hdrFieldComplete && _hdrValueComplete) {
                _pendingRequest.headers().add(_hdrField, _hdrValue);
        }
        _hdrField.clear();
        _hdrValue.clear();
        _hdrFieldComplete = false;
        _hdrValueComplete = false;
}

void HttpConnection::resetForNextRequest() {
        _pendingRequest = HttpRequest{};
        _urlBuf.clear();
        _hdrField.clear();
        _hdrValue.clear();
        _hdrFieldComplete = false;
        _hdrValueComplete = false;
        _bodyBytesSoFar = 0;
}

// ============================================================
// Request -> handler -> response
// ============================================================

void HttpConnection::deliverRequest() {
        _state = State::AwaitingResponse;
        // Snapshot the request for downstream signals.  The handler
        // may move pieces out of the live request, so we copy first.
        _lastRequest = _pendingRequest;

        requestReceivedSignal.emit(_pendingRequest);

        HttpResponse response;
        if (_handler) {
                _handler(_pendingRequest, response);
        } else {
                response.setStatus(HttpStatus::NotImplemented);
                response.setText("No request handler installed");
        }

        enqueueResponse(std::move(response));
}

Error HttpConnection::postResponse(HttpResponse response) {
        if (_state != State::AwaitingResponse) {
                return Error::Invalid;
        }
        enqueueResponse(std::move(response));
        return Error::Ok;
}

// ============================================================
// Response serialization & write pump
// ============================================================

namespace {

        // Append-friendly write helper that grows a Buffer's logical size.
        // The Buffer API is sized-but-fixed-capacity; we resize the
        // underlying allocation when the requested size exceeds availSize().
        static void appendBytes(Buffer &buf, const char *src, size_t n) {
                if (n == 0) return;
                const size_t haveLogical = buf.isValid() ? buf.size() : 0;
                const size_t haveCap = buf.isValid() ? buf.availSize() : 0;
                if (haveLogical + n <= haveCap) {
                        std::memcpy(static_cast<char *>(buf.data()) + haveLogical, src, n);
                        buf.setSize(haveLogical + n);
                        return;
                }
                size_t newCap = std::max<size_t>(haveCap * 2, haveLogical + n);
                if (newCap < 256) newCap = 256;
                Buffer grown(newCap);
                if (haveLogical > 0) std::memcpy(grown.data(), buf.data(), haveLogical);
                std::memcpy(static_cast<char *>(grown.data()) + haveLogical, src, n);
                grown.setSize(haveLogical + n);
                buf = std::move(grown);
        }

        static void appendStr(Buffer &buf, const String &s) {
                appendBytes(buf, s.cstr(), s.byteCount());
        }

        static void appendCStr(Buffer &buf, const char *s) {
                appendBytes(buf, s, std::strlen(s));
        }

} // anonymous namespace

void HttpConnection::enqueueResponse(HttpResponse response) {
        // Build status line + headers into _writeQueue.  Body is
        // either inline (from response.body()) or streamed.
        appendStr(_writeQueue, response.httpVersion());
        appendCStr(_writeQueue, " ");
        appendStr(_writeQueue, String::number(response.status().value()));
        appendCStr(_writeQueue, " ");
        appendStr(_writeQueue, response.reasonPhrase());
        appendCStr(_writeQueue, "\r\n");

        const bool    stream = response.hasBodyStream();
        const int64_t streamLen = stream ? response.bodyStreamLength() : -1;
        const bool    useChunked = stream && streamLen < 0;

        // Compute Content-Length / Transfer-Encoding handling.
        if (stream) {
                if (useChunked) {
                        response.headers().set("Transfer-Encoding", "chunked");
                        response.headers().remove("Content-Length");
                } else {
                        response.headers().set("Content-Length", String::number(streamLen));
                        response.headers().remove("Transfer-Encoding");
                }
        } else {
                const size_t bodyLen = response.body().isValid() ? response.body().size() : 0;
                response.headers().set("Content-Length", String::number(bodyLen));
                response.headers().remove("Transfer-Encoding");
        }

        // Connection management.  Default behavior was already
        // computed during cbHeadersComplete; honor any explicit
        // override the handler set on the response.
        const String connHdr = response.headers().value("Connection");
        if (connHdr.toLower() == "close") _keepAlive = false;
        if (!_keepAlive)
                response.headers().set("Connection", "close");
        else if (connHdr.isEmpty())
                response.headers().set("Connection", "keep-alive");

        // Emit headers in their canonical case order.
        response.headers().forEach([&](const String &name, const String &value) {
                appendStr(_writeQueue, name);
                appendCStr(_writeQueue, ": ");
                appendStr(_writeQueue, value);
                appendCStr(_writeQueue, "\r\n");
        });
        appendCStr(_writeQueue, "\r\n");

        if (!stream && response.body().isValid() && response.body().size() > 0) {
                appendBytes(_writeQueue, static_cast<const char *>(response.body().data()), response.body().size());
        }

        if (stream) {
                _streamSource = response.takeBodyStream();
                _streamRemaining = streamLen;
                _streamChunked = useChunked;
        }

        // Capture an optional protocol-upgrade hook.  Fires once
        // the wire write of the 101 response drains in pumpWrite.
        if (response.status().value() == 101 && response.upgradeHook()) {
                _pendingUpgradeHook = response.upgradeHook();
                _keepAlive = false; // Connection no longer speaks HTTP
        }

        _state = State::Writing;

        // Tell the loop we want write-readiness now.  A simple
        // re-register: drop the old handle and add a new one with
        // the read+write mask.
        if (_loop != nullptr && _ioHandle >= 0) {
                _loop->removeIoSource(_ioHandle);
                const int fd = _socket->socketDescriptor();
                _ioHandle = _loop->addIoSource(fd, EventLoop::IoRead | EventLoop::IoWrite,
                                               [this](int f, uint32_t e) { onIoReady(f, e); });
        }

        responseSentSignal.emit(_lastRequest, response);

        // Drive an immediate first write so a small response leaves
        // the connection without an extra poll round trip.
        pumpWrite();
}

void HttpConnection::pumpWrite() {
        if (_state != State::Writing || _socket == nullptr) return;

        // Drain the prepared header+inline body buffer.
        if (_writeQueue.isValid() && _writeOffset < _writeQueue.size()) {
                const char   *src = static_cast<const char *>(_writeQueue.data()) + _writeOffset;
                const int64_t want = static_cast<int64_t>(_writeQueue.size() - _writeOffset);
                const int64_t n = _socket->write(src, want);
                if (n < 0) {
                        // Treat as transient (EAGAIN-like); the loop
                        // wakes us on the next IoWrite.
                        return;
                }
                _writeOffset += static_cast<size_t>(n);
                if (_writeOffset < _writeQueue.size()) return; // more to send
        }

        // Header/inline body fully sent; if we have a stream, pull
        // the next chunk from it and re-fill the queue.
        if (_streamSource.isValid()) {
                IODevice *dev = const_cast<IODevice *>(_streamSource.ptr());
                char      chunk[8192];
                int64_t   cap = sizeof(chunk);
                if (_streamRemaining >= 0 && _streamRemaining < cap) {
                        cap = _streamRemaining;
                }
                if (cap == 0 && !_streamChunked) {
                        // Done with a fixed-length stream.
                        detachStreamReadyRead();
                        _streamSource = IODevice::Shared{};
                        _writeQueue = Buffer(0);
                        _writeOffset = 0;
                } else if (cap == 0 && _streamChunked) {
                        // Final 0-length chunk + trailer terminator.
                        detachStreamReadyRead();
                        _writeQueue = Buffer(0);
                        appendCStr(_writeQueue, "0\r\n\r\n");
                        _writeOffset = 0;
                        _streamSource = IODevice::Shared{};
                        // Recurse to push the terminator out.
                        pumpWrite();
                        return;
                } else {
                        const int64_t got = dev->read(chunk, cap);
                        if (got < 0) {
                                // Stream error: bail.
                                detachStreamReadyRead();
                                _streamSource = IODevice::Shared{};
                                close();
                                return;
                        }
                        if (got == 0) {
                                // Two distinct cases share read()==0:
                                //
                                //  (a) atEnd()==true: the device has
                                //      no more bytes and never will —
                                //      this is the end-of-stream
                                //      condition the original
                                //      pull-based loop assumed.
                                //
                                //  (b) atEnd()==false: the device is
                                //      idle but the producer side
                                //      hasn't yet handed it any
                                //      bytes.  Park: unsubscribe from
                                //      IoWrite, hook the device's
                                //      readyRead signal, and re-enter
                                //      pumpWrite when the producer
                                //      enqueues more.  Default-impl
                                //      atEnd() falls through to (a)
                                //      so file-backed bodies preserve
                                //      their old behaviour.
                                if (!dev->atEnd()) {
                                        attachStreamReadyRead();
                                        if (_loop != nullptr && _ioHandle >= 0) {
                                                _loop->removeIoSource(_ioHandle);
                                                const int fd = _socket->socketDescriptor();
                                                _ioHandle = _loop->addIoSource(
                                                        fd, EventLoop::IoRead,
                                                        [this](int f, uint32_t e) { onIoReady(f, e); });
                                        }
                                        _writeQueue = Buffer(0);
                                        _writeOffset = 0;
                                        _streamParked = true;
                                        return;
                                }
                                // Stream exhausted with declared EOF.
                                // For chunked we still need the
                                // 0-length terminator.
                                detachStreamReadyRead();
                                _writeQueue = Buffer(0);
                                _writeOffset = 0;
                                if (_streamChunked) appendCStr(_writeQueue, "0\r\n\r\n");
                                _streamSource = IODevice::Shared{};
                                pumpWrite();
                                return;
                        }
                        _writeQueue = Buffer(0);
                        _writeOffset = 0;
                        if (_streamChunked) {
                                String hdr = String::sprintf("%llx\r\n", static_cast<unsigned long long>(got));
                                appendStr(_writeQueue, hdr);
                                appendBytes(_writeQueue, chunk, static_cast<size_t>(got));
                                appendCStr(_writeQueue, "\r\n");
                        } else {
                                appendBytes(_writeQueue, chunk, static_cast<size_t>(got));
                                _streamRemaining -= got;
                        }
                        // Send the new chunk in this same wake.
                        pumpWrite();
                        return;
                }
        }

        // No more bytes pending.  If a protocol upgrade was queued,
        // hand the socket off now — bypasses the normal keep-alive /
        // close decision because the socket no longer belongs to us.
        if (_pendingUpgradeHook) {
                completeProtocolUpgrade();
                return;
        }

        // No more bytes pending.  Drop the write subscription so we
        // don't busy-spin on IoWrite, and decide what to do next.
        if (_loop != nullptr && _ioHandle >= 0) {
                _loop->removeIoSource(_ioHandle);
                const int fd = _socket->socketDescriptor();
                _ioHandle = _loop->addIoSource(fd, EventLoop::IoRead, [this](int f, uint32_t e) { onIoReady(f, e); });
        }
        _writeQueue = Buffer(0);
        _writeOffset = 0;

        if (_keepAlive) {
                _state = State::Reading;
                resetForNextRequest();
        } else {
                close();
        }
}

// ============================================================
// Async-read parking
// ============================================================

void HttpConnection::attachStreamReadyRead() {
        if (_streamReadyReadConnected) return;
        if (!_streamSource.isValid()) return;
        IODevice *dev = const_cast<IODevice *>(_streamSource.ptr());
        // Use the ObjectBase-aware overload: when the producer emits
        // from a different thread, the slot is marshalled onto this
        // connection's EventLoop instead of running inline on the
        // producer thread.
        _streamReadyReadSlotId = dev->readyReadSignal.connect([this]() { onStreamReadyRead(); }, this);
        _streamReadyReadConnected = true;
}

void HttpConnection::detachStreamReadyRead() {
        if (!_streamReadyReadConnected) return;
        if (_streamSource.isValid()) {
                IODevice *dev = const_cast<IODevice *>(_streamSource.ptr());
                dev->readyReadSignal.disconnect(_streamReadyReadSlotId);
        }
        _streamReadyReadConnected = false;
        _streamReadyReadSlotId = 0;
}

void HttpConnection::onStreamReadyRead() {
        // We were parked because a previous read() returned 0 with
        // atEnd()==false.  Wake the pump: re-arm IoWrite so the next
        // tick can drain whatever new bytes the producer pushed.
        if (_state != State::Writing || !_streamParked) return;
        _streamParked = false;
        if (_loop != nullptr && _ioHandle >= 0 && _socket != nullptr) {
                _loop->removeIoSource(_ioHandle);
                const int fd = _socket->socketDescriptor();
                _ioHandle = _loop->addIoSource(fd, EventLoop::IoRead | EventLoop::IoWrite,
                                               [this](int f, uint32_t e) { onIoReady(f, e); });
        }
        // Don't recurse into pumpWrite here — the device's read()
        // may not yet have anything (the wake was advisory) and the
        // IoWrite re-arm above will fire pumpWrite via onIoReady on
        // the next tick anyway.  But for an immediate-availability
        // case we get one less RTT by trying right now.
        pumpWrite();
}

void HttpConnection::completeProtocolUpgrade() {
        // The 101 response has fully drained on the wire.  Detach
        // the socket from this connection (so close() below doesn't
        // tear it down), unregister IO, invoke the hook, then have
        // the connection self-destruct via the normal closed-signal
        // reaping path.
        TcpSocket *detached = _socket;
        _socket = nullptr;
        if (_loop != nullptr && _ioHandle >= 0) {
                _loop->removeIoSource(_ioHandle);
                _ioHandle = -1;
        }
        if (_loop != nullptr && _timerId >= 0) {
                _loop->stopTimer(_timerId);
                _timerId = -1;
        }
        // Re-parent the detached socket to no parent — the upgrade
        // hook becomes its new owner (typically a WebSocket which
        // re-parents in adoptUpgradedSocket).
        if (detached != nullptr) detached->setParent(nullptr);

        HttpResponse::UpgradeHook hook;
        hook.swap(_pendingUpgradeHook);
        if (hook) hook(detached);

        // We can't deliver the socket to a hook that's also keeping
        // the HttpConnection alive.  Mark closed and emit the signal
        // so HttpServer reaps us.
        _state = State::Closed;
        closedSignal.emit();
}

PROMEKI_NAMESPACE_END
