# Network Protocols

**Phase:** 3B — **COMPLETE**
**Dependencies:** Phase 3A (TcpSocket, UdpSocket, AbstractSocket)
**Library:** `promeki`
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

## Overview & design choices

The HTTP stack is **parser + libpromeki primitives**, not a full third-party
server framework.  Vendored `llhttp` (MIT, ~3 KLOC, maintained by Node.js)
handles HTTP/1.x request and response parsing; everything else — connection
lifecycle, routing, response serialization, file streaming, TLS, WebSocket
upgrade — is built on top of `TcpSocket` / `TcpServer`, the EventLoop's
`addIoSource` API, and the existing `Variant` / JSON / `IODevice`
infrastructure.

The design models Go's `net/http`: a server owns a router (mux), routes map
patterns to handlers, handlers receive a request and write a response.
HTTP/2 is intentionally out of scope — `llhttp` is HTTP/1.x only and the
upgrade path to nghttp2 is a much larger lift.

**Threading model.**  An `HttpServer` defaults to running on
`Application::mainEventLoop()`.  Constructed inside a `Thread` worker, it
attaches to that thread's `EventLoop` instead.  All accept, parse, route,
and write activity happens on the owning loop's thread; cross-thread
handler completions go through `EventLoop::postCallable()` or the existing
signal/slot machinery (auto-marshalled when the slot is on a different loop).

Build is gated by `PROMEKI_ENABLE_HTTP` (default ON; requires
`PROMEKI_ENABLE_NETWORK`).  Vendored llhttp may be replaced by a system
install via `PROMEKI_USE_SYSTEM_LLHTTP`.

---

## HttpMethod

TypedEnum class for the HTTP request method.  Lives in
`include/promeki/httpmethod.h`.  Values: `Get`, `Head`, `Post`, `Put`,
`Delete`, `Patch`, `Options`, `Connect`, `Trace`.  Default value: `Get`.

**Files:**
- [x] `include/promeki/httpmethod.h`
- [x] `src/network/httpmethod.cpp`
- [x] `tests/unit/network/httpmethod.cpp`

---

## HttpStatus

TypedEnum class for the HTTP response status code.  Lives in
`include/promeki/httpstatus.h`.  Includes the well-known 1xx/2xx/3xx/4xx/5xx
codes plus a `reasonPhrase()` accessor that maps to the canonical text
(`200 -> "OK"`, `404 -> "Not Found"`, etc.).

**Files:**
- [x] `include/promeki/httpstatus.h`
- [x] `src/network/httpstatus.cpp`
- [x] `tests/unit/network/httpstatus.cpp`

---

## HttpHeaders

Case-insensitive key map for HTTP headers.  Wraps a flat `Entry::List`
internally with a parallel `KeyBucket::List` for lower-case lookup; arrival
order and canonical casing are preserved for serialization.

**Files:**
- [x] `include/promeki/httpheaders.h`
- [x] `src/network/httpheaders.cpp`
- [x] `tests/unit/network/httpheaders.cpp`

**Implementation checklist:**
- [x] `set(const String &name, const String &value)` — canonical-case overwrite
- [x] `add(const String &name, const String &value)` — append (e.g. Set-Cookie)
- [x] `value(const String &name, const String &defaultValue = {}) const`
- [x] `values(const String &name) const` — returns `StringList`
- [x] `contains(const String &name) const`
- [x] `remove(const String &name)`
- [x] `forEach(std::function<void(const String &, const String &)>) const` — canonical key
- [x] PROMEKI_SHARED_FINAL, `::Ptr`

---

## HttpRequest

Shareable data object: method, URL, headers, body, parsed path-params.

**Files:**
- [x] `include/promeki/httprequest.h`
- [x] `src/network/httprequest.cpp`
- [x] `tests/unit/network/httprequest.cpp`

**Implementation checklist:**
- [x] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [x] `HttpMethod method() const`, `setMethod(const HttpMethod &)`
- [x] `Url url() const`, `setUrl(const Url &)` — request URI as full Url
- [x] `String path() const` — convenience for `url().path()`
- [x] `String queryValue(const String &) const` — convenience for `url().queryValue()`
- [x] `HttpHeaders headers() const`, `setHeaders(...)`
- [x] `String header(const String &) const` — convenience
- [x] `Buffer body() const`, `setBody(const Buffer &)`, `setBody(const String &)`
- [x] `String bodyAsString() const`
- [x] `JsonObject bodyAsJson(Error * = nullptr) const`
- [x] `void setBody(const JsonObject &)` / `setBody(const JsonArray &)` — sets `Content-Type: application/json`
- [x] Path params: `HashMap<String, String> pathParams() const`,
      `String pathParam(const String &, const String & = {}) const`
- [x] `String httpVersion() const` — e.g. `"HTTP/1.1"`

---

## HttpResponse

Shareable data object: status, headers, body.  Optional streamed body for
large file responses.

**Files:**
- [x] `include/promeki/httpresponse.h`
- [x] `src/network/httpresponse.cpp`
- [x] `tests/unit/network/httpresponse.cpp`

**Implementation checklist:**
- [x] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [x] `HttpStatus status() const`, `setStatus(const HttpStatus &)`,
      overload accepting `int` for non-well-known codes
- [x] `String reasonPhrase() const` — defaulted from status, overridable
- [x] `HttpHeaders headers() const`, `setHeaders(...)`, `setHeader(name, value)`,
      `addHeader(name, value)`
- [x] `Buffer body() const`, `setBody(const Buffer &)`, `setBody(const String &)`
- [x] `void setJson(const JsonObject &)`, `setJson(const JsonArray &)` —
      auto sets Content-Type
- [x] `void setText(const String &)`, `setHtml(const String &)`,
      `setBinary(const Buffer &, const String &mimeType)`
- [x] Streamed body: `void setBodyStream(IODevice::Shared, int64_t length = -1,
      const String &mimeType = {})` — connection consumes the device,
      writes via chunked or Content-Length depending on `length`
- [x] `bool isSuccess() const` (2xx), `isRedirect()` (3xx), `isError()` (4xx/5xx)
- [x] Convenience factories: `HttpResponse::ok(JsonObject)`,
      `HttpResponse::notFound(String msg = {})`, `HttpResponse::badRequest(String)`,
      `HttpResponse::methodNotAllowed(String allow)`,
      `HttpResponse::internalError(String)`, `HttpResponse::noContent()`
- [x] `HttpResponse::UpgradeHook` callback for 101 / WebSocket upgrade

---

## HttpHandler

Base class for HTTP handlers.  Two flavours coexist: subclass-style
(`HttpHandler` derived) and lambda-style (`HttpHandlerFunc`).

**Files:**
- [x] `include/promeki/httphandler.h`
- [x] `src/network/httphandler.cpp`

**Implementation checklist:**
- [x] `using HttpHandlerFunc = std::function<void(const HttpRequest &, HttpResponse &)>;`
- [x] `using HttpMiddleware = std::function<void(const HttpRequest &, HttpResponse &, std::function<void()> next)>;`
- [x] `class HttpHandler` — abstract, `virtual void serve(const HttpRequest &, HttpResponse &) = 0;`
- [x] `class HttpFunctionHandler : public HttpHandler` — wraps an `HttpHandlerFunc`
- [x] `class HttpHandler::Ptr` — `SharedPtr<HttpHandler, false>` (CoW disabled — handlers carry identity)

---

## HttpRouter

Go-style mux: pattern + method → handler.  Exact match wins, then longest
prefix match, then parameterized match.  Path params syntax: `{name}` for
single segment, `{name:*}` for greedy tail.

**Files:**
- [x] `include/promeki/httprouter.h`
- [x] `src/network/httprouter.cpp`
- [x] `tests/unit/network/httprouter.cpp`

**Implementation checklist:**
- [x] `void route(const String &pattern, HttpMethod, HttpHandlerFunc)` — register
- [x] `void route(const String &pattern, HttpMethod, HttpHandler::Ptr)` — register typed handler
- [x] `void any(const String &pattern, HttpHandlerFunc)` — any method
- [x] `void use(HttpMiddleware)` — middleware (composed in order, calls `next()`)
- [x] `void dispatch(HttpRequest &, HttpResponse &)` — matches and invokes; populates `HttpRequest` path params
- [x] Pattern compilation (static helpers: `compilePattern`, `matchPattern`, `patternScore`)
- [x] Default 404 / 405 handlers, overridable via
      `setNotFoundHandler` / `setMethodNotAllowedHandler`
- [x] Doctest: exact match, prefix match, single & greedy params, method
      filtering, middleware ordering, default 404 / 405

---

## HttpConnection

One TCP connection.  Owns a parser state, an output queue, and the
TcpSocket.  Internal — not part of the public surface beyond what
`HttpServer` exposes.

**Files:**
- [x] `include/promeki/httpconnection.h`
- [x] `src/network/httpconnection.cpp`

**Implementation checklist:**
- [x] Construct with an accepted `TcpSocket *` (takes ownership)
- [x] Registers fd with the EventLoop via `addIoSource`
- [x] llhttp callback wiring: builds `HttpRequest` incrementally
      (method, URL, headers, body chunks, message complete)
- [x] Response serialization: status line + headers + Content-Length
      or `Transfer-Encoding: chunked` framing
- [x] Streamed body: pulls from `IODevice` only when fd is writable
- [x] Keep-alive: HTTP/1.1 default-on, HTTP/1.0 default-off, honors
      `Connection: close`
- [x] Half-close on write completion when keep-alive is disabled
- [x] Per-connection idle timeout (configurable on the server)
- [x] Signals: `requestReceived`, `responseSent`, `closed`, `errorOccurred`
- [x] TLS: `setNeedsServerHandshake()` drives `SslSocket::continueHandshake()`
      before the first HTTP byte is accepted
- [x] Protocol upgrade: `completeProtocolUpgrade()` fires
      `HttpResponse::UpgradeHook` after the 101 response drains

---

## HttpServer

Public top-level server class.  Owns a `TcpServer`, a `HttpRouter`, and
the live `HttpConnection` instances.

**Files:**
- [x] `include/promeki/httpserver.h`
- [x] `src/network/httpserver.cpp`
- [x] `tests/unit/network/httpserver.cpp`

**Implementation checklist:**
- [x] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [x] Constructor records the current EventLoop (or
      `Application::mainEventLoop()` when no loop is current)
- [x] `Error listen(const SocketAddress &)`,
      `Error listen(uint16_t port)` (binds to `::any(port)`)
- [x] `void close()`, `bool isListening() const`,
      `SocketAddress serverAddress() const`
- [x] `HttpRouter &router()`, `const HttpRouter &router() const`
- [x] `void route(const String &, HttpMethod, HttpHandlerFunc)` — convenience forwarding
- [x] `void use(HttpMiddleware)` — middleware forwarding
- [x] `void setIdleTimeoutMs(unsigned int)`, `void setMaxBodyBytes(int64_t)`
- [x] `void setSslContext(SslContext::Ptr)` — TLS termination
- [x] Reflection adapters (see VariantDatabase / VariantLookup section below)
- [x] `routeWebSocket(pattern, WebSocketHandler)` — drives upgrade + hands socket to WebSocket
- [x] Signals: `requestReceived(HttpRequest)`,
      `responseSent(HttpRequest, HttpResponse)`, `errorOccurred(Error)`
- [x] Doctest: loopback GET / POST / PUT / DELETE, JSON body round-trip,
      404 / 405, file serving, exposeDatabase end-to-end

---

## HttpFileHandler

Static file / resource serving rooted at a `Dir` (real or `:/...`).

**Files:**
- [x] `include/promeki/httpfilehandler.h`
- [x] `src/network/httpfilehandler.cpp`
- [x] `tests/unit/network/httpfilehandler.cpp`

**Implementation checklist:**
- [x] Constructor: `HttpFileHandler(Dir root)`,
      `HttpFileHandler(const String &rootPath)`
- [x] Path resolution: strip the route prefix, reject `..` traversal
- [x] Mime type lookup table (small, well-known set; extensible via
      `addMimeType(ext, type)`)
- [x] ETag from `size + mtime`
- [x] `Last-Modified` header from file mtime
- [x] `If-None-Match` -> 304
- [x] `Range` header: single-range byte serving via 206 + `Content-Range`
- [x] Streams via `IODevice` on the response (no full-file buffering)
- [x] Index file fallback (default `"index.html"` for `/`-terminated paths)
- [x] `setListDirectories(bool)` (default false)

---

## VariantDatabase / VariantLookup HTTP adapters

The libpromeki-specific value-add: existing reflection registries become
self-describing HTTP APIs with one call.

**Files:**
- [x] Template helpers live in `include/promeki/httpserver.h`;
      non-template helpers in `src/network/httpserver.cpp`

**Surface:**
- [x] `template <CompiledString N> void HttpServer::exposeDatabase(
        const String &mountPath, VariantDatabase<N> &db,
        bool readOnly = false)` — mounts:
    - `GET  <mountPath>`           → JSON object of all keys → values
    - `GET  <mountPath>/{key}`     → single value (with spec when declared)
    - `PUT  <mountPath>/{key}`     → set value, validated via the registered `VariantSpec`
    - `DELETE <mountPath>/{key}`   → clears the entry
    - `GET  <mountPath>/_schema`   → schema document for all declared IDs
- [x] `template <typename T> void HttpServer::exposeLookup(
        const String &mountPath, T &target)` — mounts:
    - `GET <mountPath>/{path:*}` → `VariantLookup<T>::resolve` then JSON-encode
- [x] Doctest: round-trip get/set against a small VariantDatabase, schema
      shape, validation rejection on out-of-range values, 404 on unknown
      keys, DELETE, basic VariantLookup nested-path resolution

---

## HttpClient

Async HTTP client.  Returns `Future<HttpResponse>`.

**Files:**
- [x] `include/promeki/httpclient.h`
- [x] `src/network/httpclient.cpp`
- [x] `tests/unit/network/httpclient.cpp`

**Implementation checklist:**
- [x] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [x] `Future<HttpResponse> send(const HttpRequest &request)`
- [x] `Future<HttpResponse> get(const String &url)`
- [x] `Future<HttpResponse> post(const String &url, const Buffer &body, const String &contentType)`
- [x] `Future<HttpResponse> put(const String &url, const Buffer &body, const String &contentType)`
- [x] `Future<HttpResponse> del(const String &url)` — "delete" is reserved keyword
- [x] `void setDefaultHeader(const String &name, const String &value)` — applied to all requests
- [x] `void setBaseUrl(const Url &url)` — prepended to relative URLs
- [x] `void setTimeoutMs(unsigned int ms)` — per-request timeout
- [x] `void setSslContext(SslContext::Ptr)` — for https:// URLs
- [x] `PROMEKI_SIGNAL(requestFinished, HttpRequest, HttpResponse)`
- [x] `PROMEKI_SIGNAL(errorOccurred, Error)`
- [x] Internal Pending state machine: request serialization, llhttp response
      parsing, Content-Length + chunked transfer encoding, per-request
      timeout via EventLoop timer
- [x] Doctest: basic GET/POST via loopback against the server, 404 surfaces
      as response (not error), default headers, DELETE, timeout, bad URL

---

## Base64

RFC 4648 encode / decode.  Needed for `Sec-WebSocket-Key` / `Sec-WebSocket-Accept`;
also useful library-wide.

**Files:**
- [x] `include/promeki/base64.h`
- [x] `src/core/base64.cpp`
- [x] `tests/unit/base64.cpp`

---

## SslContext

TLS/SSL configuration via vendored mbedTLS.

**Files:**
- [x] `include/promeki/sslcontext.h`
- [x] `src/network/sslcontext.cpp`
- [x] `tests/unit/network/sslcontext.cpp`

**Implementation checklist:**
- [x] `enum SslProtocol { TlsV1_2, TlsV1_3, SecureProtocols }`
- [x] `void setProtocol(SslProtocol)`
- [x] `Error setCertificate(const FilePath &certFile)` — PEM or DER
- [x] `Error setCertificate(const Buffer &certData)`
- [x] `Error setPrivateKey(const FilePath &keyFile, const String &passphrase = {})`
- [x] `Error setPrivateKey(const Buffer &keyData, const String &passphrase = {})`
- [x] `Error setCaCertificates(const FilePath &caFile)` — trusted CA bundle
- [x] `Error setCaCertificates(const Buffer &caData)`
- [x] `Error setSystemCaCertificates()` — probes well-known Linux CA bundle paths
- [x] `void setVerifyPeer(bool enable)` — default true
- [x] `void setVerifyDepth(int depth)`
- [x] `bool hasCertificate() const`, `bool hasCaCertificates() const`
- [x] `void *nativeConfig() const` — opaque handle for SslSocket
- [x] pImpl (`struct Impl`) hides mbedTLS headers; `PROMEKI_SHARED_FINAL_NOCOPY`
- [x] Doctest: default state, protocol set/get, verifyPeer toggle, load valid
      cert + key, reject malformed cert, load CA bundle, nativeConfig

---

## SslSocket

TcpSocket with TLS encryption via mbedTLS.

**Files:**
- [x] `include/promeki/sslsocket.h`
- [x] `src/network/sslsocket.cpp`
- [x] `tests/unit/network/sslsocket.cpp`

**Implementation checklist:**
- [x] Derive from `TcpSocket`
- [x] `void setSslContext(SslContext::Ptr ctx)`
- [x] `Error startEncryption(const String &hostname = {})` — client-side handshake
- [x] `Error startServerEncryption()` — server-side handshake
- [x] `Error continueHandshake()` — drives an in-flight handshake one step
- [x] `bool isEncrypted() const`
- [x] `String peerCertificateSubject() const`
- [x] Override `read()` — wraps `mbedtls_ssl_read()`
- [x] Override `write()` — wraps `mbedtls_ssl_write()`
- [x] Override `bytesAvailable()` — decoded + buffered plaintext bytes
- [x] Override `close()` — TLS shutdown then TCP close
- [x] `PROMEKI_SIGNAL(encrypted)` — emitted after successful handshake
- [x] `PROMEKI_SIGNAL(sslErrors, StringList)` — certificate verification errors
- [x] Non-blocking handshake support (integrate with EventLoop)
- [x] Doctest: default state, startEncryption without context fails,
      setSslContext attaches context

---

## WebSocket

Message-oriented WebSocket protocol.  Not IODevice-derived (message-oriented,
not byte-stream).  Server-side: `HttpConnection` exposes an upgrade hook —
when a handler responds with `101 Switching Protocols`, the connection
detaches from the HTTP parser and hands the raw socket to a `WebSocket`
instance.

**Files:**
- [x] `include/promeki/websocket.h`
- [x] `src/network/websocket.cpp`
- [x] `tests/unit/network/websocket.cpp`

**Implementation checklist:**
- [x] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [x] `enum State { Disconnected, Connecting, Connected, Closing }`
- [x] `enum CloseCode` (RFC 6455 §7.4)
- [x] `Error connectToUrl(const String &url)` — ws:// or wss://
- [x] `void disconnect(uint16_t code, const String &reason)`
- [x] `void abort()` — force close
- [x] `State state() const`
- [x] `Error sendTextMessage(const String &message)`
- [x] `Error sendBinaryMessage(const Buffer &message)`
- [x] `Error ping(const Buffer &payload = {})`
- [x] Subprotocol: `setRequestedSubprotocols`, `negotiatedSubprotocol()`
- [x] `void setSslContext(SslContext::Ptr)` for wss://
- [x] `PROMEKI_SIGNAL(connected)`
- [x] `PROMEKI_SIGNAL(disconnected)`
- [x] `PROMEKI_SIGNAL(textMessageReceived, String)`
- [x] `PROMEKI_SIGNAL(binaryMessageReceived, Buffer)`
- [x] `PROMEKI_SIGNAL(pongReceived, Buffer)`
- [x] `PROMEKI_SIGNAL(errorOccurred, Error)`
- [x] Internal: WebSocket frame parser/serializer
  - [x] HTTP upgrade handshake — `HttpServer::routeWebSocket(pattern, fn)`
        registers a route that validates upgrade headers, sends the 101
        with `Sec-WebSocket-Accept`, then transfers the socket to a new
        WebSocket via `HttpResponse::UpgradeHook` fired from
        `HttpConnection::completeProtocolUpgrade`
  - [x] Frame types: text, binary, ping, pong, close
  - [x] Masking (client → server)
  - [x] Fragmented message reassembly
- [x] Uses TcpSocket (or SslSocket for wss://) internally
- [x] EventLoop integration
- [x] Doctest: RFC 6455 accept-value vector, text echo round trip, binary
      echo round trip, ping/pong, rejection of non-upgrade GET

---

## End-to-end TLS test

**Files:**
- [x] `tests/unit/network/httptls.cpp`

HttpServer + HttpClient loopback through SslSocket (self-signed RSA-2048
cert, client verification disabled).  GET and POST round trips verified.

---

## Vendor llhttp

- [x] llhttp release source in `thirdparty/llhttp/` (single header
      `llhttp.h` plus C sources `api.c`, `http.c`, `llhttp.c`)
- [x] CMake: built as static library (`promeki_llhttp`) with `-fPIC`
- [x] THIRD-PARTY-LICENSES entry

---

## Vendor mbedTLS

- [x] mbedTLS in `thirdparty/mbedtls/` (git submodule)
- [x] CMake: builds as static libraries linked into `promeki`
- [x] Verified on Linux
