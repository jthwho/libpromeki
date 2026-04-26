/**
 * @file      httpclient.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/httpclient.h>
#include <promeki/tcpsocket.h>
#include <promeki/eventloop.h>
#include <promeki/application.h>
#include <promeki/promise.h>
#include <promeki/socketaddress.h>
#include <promeki/ipv4address.h>
#include <promeki/logger.h>
#include <promeki/objectbase.tpp>
#if PROMEKI_ENABLE_TLS
#include <promeki/sslsocket.h>
#include <promeki/sslcontext.h>
#endif
#include <llhttp.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(HttpClient);

// ============================================================
// Per-request state machine
//
// A Pending owns:
//   - the TcpSocket the request rides on
//   - the llhttp parser configured for HTTP_RESPONSE
//   - the Promise<HttpResponse> the caller is waiting on
//   - the serialized request bytes + write cursor
//   - a back-pointer to the owning HttpClient (cleared on
//     destruction so a late callback doesn't dereference a
//     deleted client)
//
// A Pending is kept alive across async hops by self-pinning a
// SharedPtr to itself; the pin is dropped inside finish() once
// the request has been fully unwound.
// ============================================================

struct HttpClient::Pending {
                // Pending owns a Promise<HttpResponse> (move-only), so the
                // copy-cloning PROMEKI_SHARED_FINAL macro will not compile
                // here.  The NOCOPY variant adds the refcount + an abort()
                // clone stub; the matching @ref PendingPtr below disables
                // CoW so the abort path is never reached.
                PROMEKI_SHARED_FINAL_NOCOPY(Pending)

                HttpClient *client = nullptr;
                EventLoop  *loop = nullptr;

                HttpRequest           request;
                HttpResponse          response;
                Promise<HttpResponse> promise;
                bool                  promiseFulfilled = false;

                TcpSocket *socket = nullptr;
                int        ioHandle = -1;
                int        timerId = -1;

                llhttp_t          parser{};
                llhttp_settings_t settings{};

                // Header collection scratch (mirror of HttpConnection).
                String hdrField;
                String hdrValue;
                bool   hdrFieldComplete = false;
                bool   hdrValueComplete = false;

                // The reason phrase accumulates here while llhttp fires
                // on_status; assigned to the response in headers_complete.
                String statusBuf;

                // Outbound serialized request bytes + write cursor.
                Buffer writeBuf;
                size_t writeOffset = 0;

                bool   messageComplete = false;
                bool   firstWriteCheckPending = true;
                bool   useTls = false;
                bool   handshakeDone = false;
                String tlsHostname;

                int64_t maxBodyBytes = HttpClient::DefaultMaxBodyBytes;
                int64_t bodyBytesSoFar = 0;

                SharedPtr<Pending, false> selfPin;
#if PROMEKI_ENABLE_TLS
                SslContext::Ptr sslContext;
#endif

                void start();
                void onIoReady(uint32_t events);
                void readSome();
                void pumpWrite();
                void reregister(uint32_t mask);
                void finish(Error err);

                // llhttp callbacks (static thunks).
                static int cbStatus(llhttp_t *p, const char *at, size_t len);
                static int cbHeaderField(llhttp_t *p, const char *at, size_t len);
                static int cbHeaderValue(llhttp_t *p, const char *at, size_t len);
                static int cbHeadersComplete(llhttp_t *p);
                static int cbBody(llhttp_t *p, const char *at, size_t len);
                static int cbMessageComplete(llhttp_t *p);
};

// ============================================================
// Helpers
// ============================================================

namespace {

        Buffer serializeRequest(const HttpRequest &req) {
                String head;
                head += req.method().wireName();
                head += " ";
                const Url &u = req.url();
                if (u.path().isEmpty())
                        head += "/";
                else
                        head += u.path();
                if (!u.query().isEmpty()) {
                        head += "?";
                        bool first = true;
                        for (auto it = u.query().cbegin(); it != u.query().cend(); ++it) {
                                if (!first) head += "&";
                                first = false;
                                head += Url::percentEncode(it->first);
                                head += "=";
                                head += Url::percentEncode(it->second);
                        }
                }
                head += " HTTP/1.1\r\n";

                // Mandatory Host header — RFC 9110 §7.2.  Use the URL's
                // host[:port] form unless the caller already set their own.
                String hostHdr = req.headers().value("Host");
                if (hostHdr.isEmpty()) {
                        hostHdr = u.host();
                        if (u.port() != Url::PortUnset && u.port() != 80) {
                                hostHdr += ":" + String::number(u.port());
                        }
                }
                head += "Host: " + hostHdr + "\r\n";

                const Buffer &body = req.body();
                const size_t  bodyLen = body.isValid() ? body.size() : 0;
                if (bodyLen > 0 || req.method().allowsBody()) {
                        head += "Content-Length: " + String::number(bodyLen) + "\r\n";
                }

                // Force Connection: close so the socket EOFs after the
                // response — keep-alive requires keeping the parser alive
                // for additional messages, which is a v2 feature.
                if (!req.headers().contains("Connection")) {
                        head += "Connection: close\r\n";
                }
                if (!req.headers().contains("User-Agent")) {
                        head += "User-Agent: libpromeki/1.0\r\n";
                }
                // Caller-set headers, in canonical case order.
                req.headers().forEach([&](const String &name, const String &value) {
                        if (HttpHeaders::foldName(name) == "host") return;
                        head += name + ": " + value + "\r\n";
                });
                head += "\r\n";

                const size_t headLen = head.byteCount();
                Buffer       out(headLen + bodyLen);
                std::memcpy(out.data(), head.cstr(), headLen);
                if (bodyLen > 0) {
                        std::memcpy(static_cast<char *>(out.data()) + headLen, body.data(), bodyLen);
                }
                out.setSize(headLen + bodyLen);
                return out;
        }

        // Synchronous IPv4 hostname resolution.  Async DNS is out of scope
        // for v1; for our usual localhost / LAN targets the lookup is
        // cache-resident and effectively instantaneous.
        Error resolveHost(const String &host, uint32_t &outIPv4) {
                struct addrinfo hints{};
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_STREAM;
                struct addrinfo *res = nullptr;
                const int        rc = ::getaddrinfo(host.cstr(), nullptr, &hints, &res);
                if (rc != 0 || res == nullptr) {
                        if (res != nullptr) ::freeaddrinfo(res);
                        return Error::HostNotFound;
                }
                const struct sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(res->ai_addr);
                outIPv4 = ntohl(sin->sin_addr.s_addr);
                ::freeaddrinfo(res);
                return Error::Ok;
        }

} // anonymous namespace

// ============================================================
// llhttp callback thunks
// ============================================================

#define PEND(p) (static_cast<HttpClient::Pending *>((static_cast<llhttp_t *>(p))->data))

int HttpClient::Pending::cbStatus(llhttp_t *p, const char *at, size_t len) {
        PEND(p)->statusBuf += String(at, len);
        return 0;
}

int HttpClient::Pending::cbHeaderField(llhttp_t *p, const char *at, size_t len) {
        auto *self = PEND(p);
        if (self->hdrValueComplete) {
                self->response.headers().add(self->hdrField, self->hdrValue);
                self->hdrField.clear();
                self->hdrValue.clear();
                self->hdrFieldComplete = false;
                self->hdrValueComplete = false;
        }
        self->hdrField += String(at, len);
        self->hdrFieldComplete = true;
        return 0;
}

int HttpClient::Pending::cbHeaderValue(llhttp_t *p, const char *at, size_t len) {
        auto *self = PEND(p);
        self->hdrValue += String(at, len);
        self->hdrValueComplete = true;
        return 0;
}

int HttpClient::Pending::cbHeadersComplete(llhttp_t *p) {
        auto *self = PEND(p);
        if (self->hdrFieldComplete && self->hdrValueComplete) {
                self->response.headers().add(self->hdrField, self->hdrValue);
                self->hdrField.clear();
                self->hdrValue.clear();
                self->hdrFieldComplete = false;
                self->hdrValueComplete = false;
        }
        self->response.setStatus(self->parser.status_code);
        if (!self->statusBuf.isEmpty()) {
                self->response.setReasonPhrase(self->statusBuf);
        }
        self->response.setHttpVersion(String::sprintf("HTTP/%u.%u", self->parser.http_major, self->parser.http_minor));
        return 0;
}

int HttpClient::Pending::cbBody(llhttp_t *p, const char *at, size_t len) {
        auto *self = PEND(p);
        self->bodyBytesSoFar += static_cast<int64_t>(len);
        if (self->maxBodyBytes >= 0 && self->bodyBytesSoFar > self->maxBodyBytes) {
                return -1;
        }
        // Append to the response body buffer.  Reuse the
        // grow-and-copy idiom from HttpConnection — most responses
        // are small and bursty, so the simple path stays the fast
        // path even when we end up running through it many times.
        const Buffer &existing = self->response.body();
        const size_t  prev = existing.isValid() ? existing.size() : 0;
        Buffer        grown(prev + len);
        if (prev > 0) std::memcpy(grown.data(), existing.data(), prev);
        std::memcpy(static_cast<char *>(grown.data()) + prev, at, len);
        grown.setSize(prev + len);
        self->response.setBody(grown);
        return 0;
}

int HttpClient::Pending::cbMessageComplete(llhttp_t *p) {
        PEND(p)->messageComplete = true;
        return 0;
}

#undef PEND

// ============================================================
// Pending state-machine
// ============================================================

void HttpClient::Pending::start() {
        // Resolve host.  Synchronous; see helper rationale.
        uint32_t ipv4 = 0;
        Error    err = resolveHost(request.url().host(), ipv4);
        if (err.isError()) {
                finish(err);
                return;
        }

        int port = request.url().port();
        if (port == Url::PortUnset) port = useTls ? 443 : 80;

#if PROMEKI_ENABLE_TLS
        if (useTls) {
                SslSocket *ssl = new SslSocket();
                // Lazy-create a default SslContext when the caller
                // did not configure one — without it
                // SslSocket::startEncryption returns Invalid before
                // we get a chance to surface the real handshake or
                // network error.  The default context has
                // verifyPeer=true with no CA chain, which folds to
                // VERIFY_NONE per applyAuthMode's CA-aware policy
                // — sufficient for "talk to a real server but skip
                // verification" smoke calls; production users
                // configure their own context.
                if (!sslContext.isValid()) {
                        sslContext = SslContext::Ptr::takeOwnership(new SslContext());
                }
                ssl->setSslContext(sslContext);
                socket = ssl;
        } else {
                socket = new TcpSocket();
        }
#else
        socket = new TcpSocket();
#endif
        err = socket->open(IODevice::ReadWrite);
        if (err.isError()) {
                finish(err);
                return;
        }
        socket->setNonBlocking(true);

        // connectToHost on a non-blocking TcpSocket: under the
        // hood the implementation calls connect(), notices
        // EINPROGRESS, then poll()-waits for POLLOUT — so it
        // effectively still completes synchronously here.  A
        // hard error short-circuits the request.
        err = socket->connectToHost(SocketAddress(Ipv4Address(ipv4), static_cast<uint16_t>(port)));
        if (err.isError()) {
                finish(err);
                return;
        }

#if PROMEKI_ENABLE_TLS
        // Kick off the client TLS handshake.  startEncryption sets
        // up the mbedTLS state and runs the first handshake step;
        // continueHandshake (called from onIoReady) drives the
        // remaining steps as the kernel hands us bytes.
        if (useTls) {
                SslSocket *ssl = static_cast<SslSocket *>(socket);
                Error      sslErr = ssl->startEncryption(tlsHostname);
                if (sslErr.isError() && sslErr != Error::TryAgain) {
                        finish(sslErr);
                        return;
                }
                if (ssl->isEncrypted()) handshakeDone = true;
        }
#endif

        // Initialize parser.
        llhttp_settings_init(&settings);
        settings.on_status = &cbStatus;
        settings.on_header_field = &cbHeaderField;
        settings.on_header_value = &cbHeaderValue;
        settings.on_headers_complete = &cbHeadersComplete;
        settings.on_body = &cbBody;
        settings.on_message_complete = &cbMessageComplete;
        llhttp_init(&parser, HTTP_RESPONSE, &settings);
        parser.data = this;

        // Build outbound bytes.
        writeBuf = serializeRequest(request);
        writeOffset = 0;

        // Subscribe to write+read readiness.  The first IoWrite
        // tells us the connect resolved; subsequent ones drain the
        // request bytes.  IoRead fires once the response starts.
        reregister(EventLoop::IoRead | EventLoop::IoWrite);

        if (client != nullptr && client->_timeoutMs > 0 && loop != nullptr) {
                SharedPtr<Pending, false> pin = selfPin;
                timerId = loop->startTimer(
                        client->_timeoutMs,
                        [pin]() {
                                Pending *self = const_cast<Pending *>(pin.ptr());
                                if (self != nullptr && !self->promiseFulfilled) {
                                        self->finish(Error::Timeout);
                                }
                        },
                        /*singleShot=*/true);
        }
}

void HttpClient::Pending::reregister(uint32_t mask) {
        if (loop == nullptr || socket == nullptr) return;
        if (ioHandle >= 0) {
                loop->removeIoSource(ioHandle);
                ioHandle = -1;
        }
        const int fd = socket->socketDescriptor();
        if (fd < 0) return;
        SharedPtr<Pending, false> pin = selfPin;
        ioHandle = loop->addIoSource(fd, mask, [pin](int /*f*/, uint32_t events) {
                Pending *self = const_cast<Pending *>(pin.ptr());
                if (self != nullptr) self->onIoReady(events);
        });
}

void HttpClient::Pending::onIoReady(uint32_t events) {
        if (promiseFulfilled) return;

        // Connect-completion check: the first IoWrite signals that
        // the TCP three-way handshake settled.  Read SO_ERROR so a
        // refused connection translates to a specific libpromeki
        // error code rather than a generic ConnectionReset.
        if (firstWriteCheckPending && socket != nullptr && (events & (EventLoop::IoError | EventLoop::IoWrite))) {
                firstWriteCheckPending = false;
                int       soerr = 0;
                socklen_t soerrLen = sizeof(soerr);
                if (::getsockopt(socket->socketDescriptor(), SOL_SOCKET, SO_ERROR, &soerr, &soerrLen) == 0 &&
                    soerr != 0) {
                        finish(Error::syserr(soerr));
                        return;
                }
                if (events & EventLoop::IoError) {
                        finish(Error::ConnectionReset);
                        return;
                }
        } else if (events & EventLoop::IoError) {
                finish(Error::ConnectionReset);
                return;
        }

#if PROMEKI_ENABLE_TLS
        // TLS-client handshake: drive continueHandshake on each
        // ready event until it stops returning TryAgain.  The
        // initial startEncryption() call (which sets up mbedTLS
        // and runs the first step) was made earlier in
        // initiateClientHandshake().
        if (useTls && !handshakeDone) {
                SslSocket *ssl = static_cast<SslSocket *>(socket);
                Error      err = ssl->continueHandshake();
                if (err == Error::TryAgain) return;
                if (err.isError()) {
                        finish(err);
                        return;
                }
                handshakeDone = true;
                // Fall through to the normal write path so the
                // request bytes start flowing immediately.
        }
#endif

        if (events & EventLoop::IoWrite) pumpWrite();
        if (events & EventLoop::IoRead) readSome();
}

void HttpClient::Pending::pumpWrite() {
        if (socket == nullptr) return;
        // Connect-completion verification happened in onIoReady on
        // the first event (it has to look at SO_ERROR before deciding
        // whether to even invoke pumpWrite); by the time we get
        // here the socket is known to be writable.

        if (writeOffset >= writeBuf.size()) {
                reregister(EventLoop::IoRead);
                return;
        }
        const char   *src = static_cast<const char *>(writeBuf.data()) + writeOffset;
        const int64_t want = static_cast<int64_t>(writeBuf.size()) - static_cast<int64_t>(writeOffset);
        const int64_t n = socket->write(src, want);
        if (n < 0) return; // EAGAIN-ish; stay subscribed
        writeOffset += static_cast<size_t>(n);
        if (writeOffset >= writeBuf.size()) {
                reregister(EventLoop::IoRead);
        }
}

void HttpClient::Pending::readSome() {
        if (socket == nullptr) return;
        char          buf[8192];
        const int64_t n = socket->read(buf, sizeof(buf));
        if (n == 0) {
                // Peer closed.  Tell llhttp the stream is over so
                // it can finalize a Connection-close message.
                llhttp_finish(&parser);
                finish(messageComplete ? Error::Ok : Error::ConnectionReset);
                return;
        }
        if (n < 0) return; // EAGAIN: wait for more
        const llhttp_errno_t rc = llhttp_execute(&parser, buf, static_cast<size_t>(n));
        if (rc != HPE_OK) {
                finish(Error::Invalid);
                return;
        }
        if (messageComplete) finish(Error::Ok);
}

void HttpClient::Pending::finish(Error err) {
        if (promiseFulfilled) return;
        promiseFulfilled = true;

        if (loop != nullptr) {
                if (ioHandle >= 0) {
                        loop->removeIoSource(ioHandle);
                        ioHandle = -1;
                }
                if (timerId >= 0) {
                        loop->stopTimer(timerId);
                        timerId = -1;
                }
        }
        if (socket != nullptr) {
                socket->close();
                delete socket;
                socket = nullptr;
        }

        if (err.isOk()) {
                if (client != nullptr) {
                        client->requestFinishedSignal.emit(request, response);
                }
                promise.setValue(response);
        } else {
                if (client != nullptr) {
                        client->errorOccurredSignal.emit(err);
                }
                promise.setError(err);
        }

        // Retire from the client's active list and drop the
        // self-pin.  After the pin is gone the Pending will be
        // freed when the last shared reference (the lambda
        // capture currently holding `pin`) goes away.
        SharedPtr<Pending, false> pin = selfPin;
        selfPin = SharedPtr<Pending, false>{};
        if (client != nullptr) client->retire(pin);
}

// ============================================================
// HttpClient
// ============================================================

HttpClient::HttpClient(ObjectBase *parent) : ObjectBase(parent) {
        _loop = EventLoop::current();
        if (_loop == nullptr) _loop = Application::mainEventLoop();
}

HttpClient::~HttpClient() {
        // Cancel every in-flight request.  Destructor runs on the
        // owning thread; safe to mutate _active and Pending state
        // directly.  Each Pending releases its own self-pin in
        // finish(); after that the underlying object is freed.
        while (!_active.isEmpty()) {
                PendingPtr p = _active[0];
                Pending   *raw = const_cast<Pending *>(p.ptr());
                if (raw != nullptr) {
                        raw->client = nullptr; // disable callbacks
                        if (!raw->promiseFulfilled) raw->finish(Error::Cancelled);
                }
                if (!_active.isEmpty() && _active[0].ptr() == p.ptr()) {
                        // finish() should have called retire(), but
                        // belt-and-braces in case the back-pointer
                        // was already cleared.
                        _active.removeIf([&](const PendingPtr &x) { return x.ptr() == p.ptr(); });
                }
        }
}

void HttpClient::setDefaultHeader(const String &name, const String &value) {
        _defaultHeaders.set(name, value);
}

void HttpClient::removeDefaultHeader(const String &name) {
        _defaultHeaders.remove(name);
}

Future<HttpResponse> HttpClient::send(const HttpRequest &request) {
        HttpRequest req = request;
        resolveTargetUrl(req);
        applyDefaultHeaders(req);
        return dispatch(std::move(req));
}

Future<HttpResponse> HttpClient::get(const String &url) {
        HttpRequest r;
        r.setMethod(HttpMethod::Get);
        r.setUrl(Url::fromString(url).first());
        return send(r);
}

Future<HttpResponse> HttpClient::post(const String &url, const Buffer &body, const String &contentType) {
        HttpRequest r;
        r.setMethod(HttpMethod::Post);
        r.setUrl(Url::fromString(url).first());
        r.setBody(body);
        if (!contentType.isEmpty()) r.headers().set("Content-Type", contentType);
        return send(r);
}

Future<HttpResponse> HttpClient::put(const String &url, const Buffer &body, const String &contentType) {
        HttpRequest r;
        r.setMethod(HttpMethod::Put);
        r.setUrl(Url::fromString(url).first());
        r.setBody(body);
        if (!contentType.isEmpty()) r.headers().set("Content-Type", contentType);
        return send(r);
}

Future<HttpResponse> HttpClient::del(const String &url) {
        HttpRequest r;
        r.setMethod(HttpMethod::Delete);
        r.setUrl(Url::fromString(url).first());
        return send(r);
}

void HttpClient::resolveTargetUrl(HttpRequest &request) const {
        if (request.url().scheme().isEmpty() && _baseUrl.isValid()) {
                Url u = _baseUrl;
                u.setPath(request.url().path());
                u.setQuery(request.url().query());
                u.setFragment(request.url().fragment());
                request.setUrl(u);
        }
}

void HttpClient::applyDefaultHeaders(HttpRequest &request) const {
        // Defaults only apply when the per-request headers don't
        // already define the same name.  Matches Go's
        // http.Client.Do semantics — explicit beats default.
        _defaultHeaders.forEach([&](const String &name, const String &value) {
                if (!request.headers().contains(name)) {
                        request.headers().add(name, value);
                }
        });
}

Future<HttpResponse> HttpClient::dispatch(HttpRequest request) {
        PendingPtr pending = PendingPtr::create();
        Pending   *p = const_cast<Pending *>(pending.ptr());
        p->client = this;
        p->loop = _loop;
        p->request = std::move(request);
        p->maxBodyBytes = _maxBodyBytes;
        p->selfPin = pending;

        Future<HttpResponse> fut = p->promise.future();

        // Synchronous up-front rejections so the caller's Future
        // fulfills immediately rather than requiring an EventLoop
        // tick to learn that the URL is malformed.
        const Url &u = p->request.url();
        auto       reject = [&](Error err) {
                p->promise.setError(err);
                p->promiseFulfilled = true;
                p->selfPin = PendingPtr{};
        };
        if (!u.isValid() || u.host().isEmpty()) {
                reject(Error::Invalid);
                return fut;
        }
#if PROMEKI_ENABLE_TLS
        if (u.scheme() == "https") {
                p->useTls = true;
                p->tlsHostname = u.host();
                p->sslContext = _sslContext;
        } else if (u.scheme() != "http") {
                reject(Error::Invalid);
                return fut;
        }
#else
        if (u.scheme() == "https") {
                reject(Error::NotImplemented);
                return fut;
        }
        if (u.scheme() != "http") {
                reject(Error::Invalid);
                return fut;
        }
#endif
        if (_loop == nullptr) {
                reject(Error::Invalid);
                return fut;
        }

        _active.pushToBack(pending);

        // Hop the actual work onto the owning loop so the I/O
        // state machine runs on a single, predictable thread even
        // when send() is called cross-thread.
        PendingPtr pin = pending;
        _loop->postCallable([pin]() {
                Pending *self = const_cast<Pending *>(pin.ptr());
                if (self != nullptr && self->client != nullptr) self->start();
        });
        return fut;
}

void HttpClient::retire(const PendingPtr &p) {
        for (size_t i = 0; i < _active.size(); ++i) {
                if (_active[i].ptr() == p.ptr()) {
                        for (size_t j = i + 1; j < _active.size(); ++j) {
                                _active[j - 1] = _active[j];
                        }
                        _active.popFromBack();
                        return;
                }
        }
}

PROMEKI_NAMESPACE_END
