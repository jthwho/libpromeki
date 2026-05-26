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
#include <cstdint>
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
                // Pending owns a Promise<HttpResponse> (move-only), so it
                // is not copy-constructible.  PROMEKI_SHARED_FINAL routes
                // to the abort-on-clone path automatically; the matching
                // @ref PendingPtr below disables CoW so the abort path is
                // never reached at runtime.
                PROMEKI_SHARED_FINAL(Pending)

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

                // Buffered body assembly.  Held privately and assigned
                // to @c response.body() once at finish() so we don't
                // repeatedly mutate the response's CoW Buffer (which
                // historically allocated + memcpy'd the whole body on
                // every chunk — O(N²) for multi-GB downloads).  When
                // @c request.bodySink() is set the bytes are routed to
                // the sink instead and @c bodyBuf stays empty.
                Buffer bodyBuf;
                size_t bodyBufCapacity = 0;

                // Content-Length advertised by the server, or -1 when
                // not announced (chunked transfer-encoding etc.).
                // Captured once in cbHeadersComplete so the body sink
                // and progress callback see the same value the on-disk
                // file should grow to.
                int64_t announcedContentLength = -1;

                // Set by cbHeadersComplete when the response is a 3xx
                // we plan to follow.  Suppresses the body sink and
                // progress callback for this hop so a tiny redirect
                // body (HF returns "<html>...moved...</html>") never
                // reaches the destination file; cbMessageComplete
                // then triggers re-dispatch via performRedirect().
                bool   isRedirectHop = false;

                // Hops remaining before we give up and surface the
                // 3xx response to the caller.  Initialised from
                // HttpClient::maxRedirects() at dispatch time; the
                // default is non-zero (transparent redirect-following
                // matches every other mainstream HTTP client).
                int    redirectsRemaining = 0;

                // Set when a body sink or progress callback aborts the
                // download from inside an llhttp parser callback.  We
                // can't tear down the socket from there (we'd be
                // pulling the rug out from under llhttp_execute), so
                // the callback returns -1 to stop parsing and stashes
                // the concrete error code here; readSome notices the
                // HPE_USER stop and calls finish() with this code
                // instead of the generic Error::Invalid.
                Error cancelError = Error::Ok;

                SharedPtr<Pending, false> selfPin;
#if PROMEKI_ENABLE_TLS
                SslContext sslContext;
#endif

                void start();
                void onIoReady(uint32_t events);
                void readSome();
                void pumpWrite();
                void reregister(uint32_t mask);
                void finish(Error err);

                // Re-dispatches the request to @p newUrl after a 3xx
                // hop completes.  Tears down the per-hop state
                // (socket, parser, per-response scratch) and resets
                // every counter the next start() will need.  The
                // selfPin and Promise are preserved, so the caller's
                // Future fulfils once and only once when the
                // redirect chain bottoms out (or when a hop fails).
                void performRedirect(const String &newUrl);

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
                // Prefer the parsed verbatim query when it's still
                // available — re-encoding the map would corrupt AWS-
                // style signed URLs whose HMAC was computed over a
                // specific percent-encoded form.  Url::fromString sets
                // _rawQuery and the query mutators clear it, so any
                // URL we get from a redirect's Location header (or any
                // other parsed source) round-trips intact.
                const String &rawQuery = u.rawQuery();
                if (!rawQuery.isEmpty()) {
                        head += "?";
                        head += rawQuery;
                } else if (!u.query().isEmpty()) {
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
                        promekiWarn("HttpClient: getaddrinfo('%s') failed (rc=%d %s)", host.cstr(), rc,
                                    gai_strerror(rc));
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

        // Decide up-front whether this hop is a redirect we'll chase
        // internally.  When true, we silently absorb the response —
        // no body buffering, no sink invocation, no initial progress
        // tick — so the caller's sink only ever sees the final 2xx
        // bytes.  A 3xx with no Location header (or a relative one,
        // or a chain too long) falls through to the normal path and
        // surfaces as a regular HttpResponse the caller can inspect.
        if (self->response.status().isRedirect() && self->redirectsRemaining > 0) {
                const String loc = self->response.headers().value("Location");
                if (!loc.isEmpty() &&
                    (loc.startsWith("http://") || loc.startsWith("https://"))) {
                        self->isRedirectHop = true;
                        promekiDebug("HttpClient: %d %s on %s — will follow (remaining=%d)",
                                     (int)self->parser.status_code,
                                     self->statusBuf.isEmpty() ? "" : self->statusBuf.cstr(),
                                     self->request.url().briefForLog().cstr(),
                                     self->redirectsRemaining);
                        return 0;
                }
        }

        // Capture the announced length once.  Use the 64-bit parser
        // because Content-Length is a 64-bit quantity on multi-GB
        // downloads — the 32-bit toInt would clamp / OutOfRange.
        const String cl = self->response.headers().value("Content-Length");
        if (!cl.isEmpty()) {
                Error          parseErr;
                const int64_t  v = cl.toInt64(&parseErr);
                if (parseErr.isOk() && v >= 0) {
                        self->announcedContentLength = v;
                }
        }

        // Pre-size the buffered body when the server told us how big
        // it'll be.  Skips the geometric-growth path entirely for the
        // common case (one allocation, no realloc-and-copy).  When a
        // body sink is installed we don't allocate at all; bytes are
        // streamed straight through.
        if (!self->request.bodySink() && self->announcedContentLength > 0 &&
            (self->maxBodyBytes < 0 || self->announcedContentLength <= self->maxBodyBytes)) {
                self->bodyBuf = Buffer(static_cast<size_t>(self->announcedContentLength));
                self->bodyBufCapacity = static_cast<size_t>(self->announcedContentLength);
        }

        // Final-response breadcrumb: status, declared length, and
        // content-type cover ~all the questions a field debugger
        // asks about a failed request.  Body bytes are deliberately
        // not logged (we'd dwarf everything else and risk leaking
        // sensitive data); the bytes are reflected later via
        // finish()'s success/failure summary.
        promekiDebug("HttpClient: %d %s on %s contentLength=%lld type=%s sink=%s",
                     (int)self->parser.status_code,
                     self->statusBuf.isEmpty() ? "" : self->statusBuf.cstr(),
                     self->request.url().briefForLog().cstr(),
                     (long long)self->announcedContentLength,
                     self->response.headers().value("Content-Type").cstr(),
                     self->request.bodySink() ? "stream" : "buffered");

        // Initial progress tick so the caller learns the total size
        // before any bytes arrive — useful for rendering a
        // bytes/percent display even on a quick-failing 304 response.
        const HttpRequest::ProgressCallback &pcb = self->request.progressCallback();
        if (pcb && self->cancelError.isOk()) {
                if (!pcb(0, self->announcedContentLength)) {
                        self->cancelError = Error::Cancelled;
                        return -1;
                }
        }
        return 0;
}

int HttpClient::Pending::cbBody(llhttp_t *p, const char *at, size_t len) {
        auto *self = PEND(p);
        if (!self->cancelError.isOk()) return -1;
        // Redirect hop: silently swallow the body.  The next hop's
        // sink/progress invocations are what the caller cares about.
        if (self->isRedirectHop) return 0;
        self->bodyBytesSoFar += static_cast<int64_t>(len);
        if (self->maxBodyBytes >= 0 && self->bodyBytesSoFar > self->maxBodyBytes) {
                promekiWarn("HttpClient: response body for %s exceeded maxBodyBytes=%lld "
                            "(received=%lld, announced=%lld) — aborting",
                            self->request.url().briefForLog().cstr(),
                            static_cast<long long>(self->maxBodyBytes),
                            static_cast<long long>(self->bodyBytesSoFar),
                            static_cast<long long>(self->announcedContentLength));
                self->cancelError = Error::NoMem;
                return -1;
        }

        // Route 1: caller supplied a body sink — hand the chunk over
        // and never touch the in-memory buffer.  This is the path
        // multi-GB downloads take; the in-memory buffer would dwarf
        // physical RAM on large model weights.
        const HttpRequest::BodySink &sink = self->request.bodySink();
        if (sink) {
                Error sinkErr = sink(at, len);
                if (sinkErr.isError()) {
                        promekiWarn("HttpClient: bodySink rejected chunk for %s "
                                    "(%lld bytes, total %lld of %lld): %s",
                                    self->request.url().briefForLog().cstr(),
                                    static_cast<long long>(len),
                                    static_cast<long long>(self->bodyBytesSoFar),
                                    static_cast<long long>(self->announcedContentLength),
                                    sinkErr.name().cstr());
                        self->cancelError = sinkErr;
                        return -1;
                }
        } else {
                // Route 2: assemble in-memory.  Pre-sized in
                // cbHeadersComplete when Content-Length was known
                // (the fast path); otherwise grow geometrically.
                const size_t currentSize =
                        self->bodyBuf.isValid() ? self->bodyBuf.size() : 0;
                if (currentSize + len > self->bodyBufCapacity) {
                        // Geometric growth: pick the smallest
                        // power-of-two at least large enough to hold
                        // the new total.  Start at 32 KiB so a tiny
                        // first chunk doesn't lock us into a 1 KiB
                        // capacity that gets doubled dozens of times.
                        size_t target = self->bodyBufCapacity ? self->bodyBufCapacity : 32 * 1024;
                        while (target < currentSize + len) {
                                if (target > (SIZE_MAX / 2)) {
                                        target = currentSize + len;
                                        break;
                                }
                                target *= 2;
                        }
                        Buffer grown(target);
                        if (!grown.isValid()) {
                                self->cancelError = Error::NoMem;
                                return -1;
                        }
                        if (currentSize > 0) {
                                std::memcpy(grown.data(), self->bodyBuf.data(), currentSize);
                                grown.setSize(currentSize);
                        }
                        self->bodyBuf = grown;
                        self->bodyBufCapacity = target;
                }
                std::memcpy(static_cast<char *>(self->bodyBuf.data()) + currentSize, at, len);
                self->bodyBuf.setSize(currentSize + len);
        }

        const HttpRequest::ProgressCallback &pcb = self->request.progressCallback();
        if (pcb) {
                if (!pcb(self->bodyBytesSoFar, self->announcedContentLength)) {
                        self->cancelError = Error::Cancelled;
                        return -1;
                }
        }
        return 0;
}

int HttpClient::Pending::cbMessageComplete(llhttp_t *p) {
        PEND(p)->messageComplete = true;
        return 0;
}

void HttpClient::Pending::performRedirect(const String &newUrl) {
        // One concise breadcrumb covers the redirect-chase decision:
        // where we came from, where we're going, and how many more
        // hops are still on the budget.  Logged via urlForLog so any
        // signed-CDN query string stays out of the log line.
        promekiDebug("HttpClient: following redirect from %s -> %s (remaining=%d)",
                     request.url().briefForLog().cstr(),
                     Url::fromString(newUrl).first().briefForLog().cstr(),
                     redirectsRemaining);

        // Tear down everything touched by the previous hop.  We're
        // about to call start() again; it expects a virgin Pending
        // (apart from selfPin / client / loop / promise / request).
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

        --redirectsRemaining;

        // Point the request at the new URL.  Re-evaluate TLS so a
        // http→https upgrade (or vice versa) takes effect.
        request.setUrl(Url::fromString(newUrl).first());
        const Url &u = request.url();
        if (!u.isValid() || u.host().isEmpty()) {
                finish(Error::Invalid);
                return;
        }
#if PROMEKI_ENABLE_TLS
        if (u.scheme() == "https") {
                useTls = true;
                tlsHostname = u.host();
        } else if (u.scheme() == "http") {
                useTls = false;
                tlsHostname = String();
        } else {
                finish(Error::Invalid);
                return;
        }
#else
        if (u.scheme() == "https") {
                finish(Error::NotImplemented);
                return;
        }
        if (u.scheme() != "http") {
                finish(Error::Invalid);
                return;
        }
#endif

        // Reset all per-hop scratch.  Anything that survives across
        // hops (cancelError, isRedirectHop) is reset here too so the
        // next hop starts clean.
        response = HttpResponse();
        statusBuf.clear();
        hdrField.clear();
        hdrValue.clear();
        hdrFieldComplete = false;
        hdrValueComplete = false;
        writeBuf = Buffer();
        writeOffset = 0;
        messageComplete = false;
        firstWriteCheckPending = true;
        handshakeDone = false;
        bodyBytesSoFar = 0;
        bodyBuf = Buffer();
        bodyBufCapacity = 0;
        announcedContentLength = -1;
        isRedirectHop = false;
        cancelError = Error::Ok;

        start();
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

        // Per-hop connect breadcrumb.  Resolved IPv4 + chosen port +
        // TLS yes/no gives a field debugger enough context to spot
        // "wrong DNS answer", "blocked by firewall on port X", or
        // "client picked plaintext where TLS was expected" without
        // running a packet capture.
        promekiDebug("HttpClient: %s %s -> %u.%u.%u.%u:%d tls=%s",
                     request.method().wireName().cstr(),
                     request.url().briefForLog().cstr(),
                     (ipv4 >> 24) & 0xff, (ipv4 >> 16) & 0xff,
                     (ipv4 >> 8) & 0xff, ipv4 & 0xff,
                     port,
#if PROMEKI_ENABLE_TLS
                     useTls ? "yes" : "no"
#else
                     "no(disabled)"
#endif
        );

#if PROMEKI_ENABLE_TLS
        if (useTls) {
                SslSocket *ssl = new SslSocket();
                // SslContext's default constructor already auto-loads
                // the system CA bundle, so handing the (possibly
                // default) sslContext through is sufficient — no
                // lazy-load needed here.  If the system bundle was
                // unavailable the handshake fails-closed in
                // SslSocket::startEncryption with a clear error.
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

        // Disable Nagle and bump SO_RCVBUF before connect() so the
        // options take effect on the SYN.  Nagle was hurting both
        // small-API latency and large-download head-of-line latency
        // (the request was always one packet; Nagle adds nothing).
        // The receive-buffer bump raises the advertised TCP window;
        // 4 MiB is the sweet spot for typical Linux defaults (kernel
        // doubles to 8 MiB internally, well below the 16 MiB
        // net.core.rmem_max default that comes shipped with most
        // distros).
        socket->setNoDelay(true);
        socket->setReceiveBufferSize(4 * 1024 * 1024);

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
        // 64 KiB read buffer.  llhttp doesn't care how large each
        // execute() chunk is, and a larger buffer means fewer syscalls
        // + fewer mbedtls_ssl_read invocations per TLS record (TLS
        // 1.2 records cap at 16 KiB, TLS 1.3 at 16 KiB plus a tiny
        // suffix, so 64 KiB covers ~4 records per read).  The old
        // 8 KiB value was a meaningful throughput bottleneck on
        // hundred-MB / multi-GB downloads.
        char          buf[64 * 1024];
        const int64_t n = socket->read(buf, sizeof(buf));
        if (n == 0) {
                // Peer closed.  Tell llhttp the stream is over so
                // it can finalize a Connection-close message.
                llhttp_finish(&parser);
                if (messageComplete && isRedirectHop) {
                        performRedirect(response.headers().value("Location"));
                        return;
                }
                finish(messageComplete ? Error::Ok : Error::ConnectionReset);
                return;
        }
        if (n < 0) return; // EAGAIN: wait for more
        const llhttp_errno_t rc = llhttp_execute(&parser, buf, static_cast<size_t>(n));
        if (rc != HPE_OK) {
                // A body sink, the progress callback, or our own
                // maxBodyBytes guard stops parsing by returning -1
                // from an llhttp callback; the concrete error is
                // stashed on cancelError so we don't lose it to the
                // generic Error::Invalid.
                finish(cancelError.isOk() ? Error::Invalid : cancelError);
                return;
        }
        if (messageComplete) {
                if (isRedirectHop) {
                        performRedirect(response.headers().value("Location"));
                        return;
                }
                finish(Error::Ok);
        }
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

        // Commit the assembled buffered body to the response (if any).
        // Done here instead of on every chunk so we mutate the
        // response's CoW Buffer exactly once per request — the body
        // was previously rebuilt on every parser callback, which
        // dominated wall-clock time on large downloads.
        if (bodyBuf.isValid()) {
                response.setBody(bodyBuf);
                bodyBuf = Buffer();
        }

        if (err.isOk()) {
                // One-line lifecycle summary on success.  Status +
                // bytes lets a debug-level operator confirm the
                // request landed how they expected without scrolling
                // through every readSome wake-up.
                promekiDebug("HttpClient: done %s status=%d body=%lld bytes",
                             request.url().briefForLog().cstr(),
                             response.status().value(),
                             static_cast<long long>(bodyBytesSoFar));
                if (client != nullptr) {
                        client->requestFinishedSignal.emit(request, response);
                }
                promise.setValue(response);
        } else {
                // The corresponding failure path is a warn so the
                // operator sees it even with debug off — naming the
                // URL + the partial byte counts makes a field log
                // line self-diagnosing without forcing the user to
                // turn on debug and reproduce.  Error::Cancelled is
                // routine (caller-driven shutdown / Ctrl-C); skip
                // the warn in that case so we don't spam the log on
                // graceful aborts.
                if (err != Error::Cancelled) {
                        promekiWarn("HttpClient: request failed %s — %s "
                                    "(received=%lld of %lld)",
                                    request.url().briefForLog().cstr(),
                                    err.name().cstr(),
                                    static_cast<long long>(bodyBytesSoFar),
                                    static_cast<long long>(announcedContentLength));
                } else {
                        promekiDebug("HttpClient: cancelled %s (received=%lld of %lld)",
                                     request.url().briefForLog().cstr(),
                                     static_cast<long long>(bodyBytesSoFar),
                                     static_cast<long long>(announcedContentLength));
                }
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
                // setQuery clears _rawQuery; restore it from the
                // request URL so signed-URL bytes survive the merge.
                u.setRawQuery(request.url().rawQuery());
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
        p->redirectsRemaining = _maxRedirects;
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
        PendingPtr        pin = pending;
        static const auto kStartLabel = EventLoop::Label{"HttpClient.startPending"};
        _loop->postCallable(kStartLabel, [pin]() {
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
