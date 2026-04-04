# Network Protocols

**Phase:** 3B
**Dependencies:** Phase 3A (TcpSocket, UdpSocket, AbstractSocket)
**Library:** `promeki`
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

## HttpRequest

Data object: method, URL, headers, body. Shareable.

**Files:**
- [ ] `include/promeki/httprequest.h`
- [ ] `src/network/httprequest.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `enum Method { Get, Post, Put, Delete, Head, Patch, Options }`
- [ ] `Method method() const`, `setMethod(Method)`
- [ ] `String url() const`, `setUrl(const String &)`
- [ ] `HashMap<String, String> headers() const`, `setHeaders(...)`
- [ ] `String header(const String &name) const`
- [ ] `void setHeader(const String &name, const String &value)`
- [ ] `Buffer body() const`, `setBody(const Buffer &)`
- [ ] `void setBody(const String &)` — convenience for text bodies
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`

---

## HttpResponse

Data object: status code, headers, body. Shareable.

**Files:**
- [ ] `include/promeki/httpresponse.h`
- [ ] `src/network/httpresponse.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `int statusCode() const`
- [ ] `String reasonPhrase() const`
- [ ] `HashMap<String, String> headers() const`
- [ ] `String header(const String &name) const`
- [ ] `Buffer body() const`
- [ ] `String bodyAsString() const` — convenience
- [ ] `bool isSuccess() const` — 2xx status
- [ ] `bool isRedirect() const` — 3xx status
- [ ] `bool isError() const` — 4xx/5xx status
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`

---

## HttpClient

Async HTTP client. Returns `Future<HttpResponse>`.

**Files:**
- [ ] `include/promeki/httpclient.h`
- [ ] `src/network/httpclient.cpp`
- [ ] `tests/httpclient.cpp`

**Implementation checklist:**
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `Future<HttpResponse> send(const HttpRequest &request)`
- [ ] `Future<HttpResponse> get(const String &url)`
- [ ] `Future<HttpResponse> post(const String &url, const Buffer &body)`
- [ ] `Future<HttpResponse> put(const String &url, const Buffer &body)`
- [ ] `Future<HttpResponse> del(const String &url)` — "delete" is reserved keyword
- [ ] `void setDefaultHeader(const String &name, const String &value)` — applied to all requests
- [ ] `void setBaseUrl(const String &url)` — prepended to relative URLs
- [ ] `void setTimeout(unsigned int ms)` — per-request timeout
- [ ] `PROMEKI_SIGNAL(requestFinished, HttpResponse)`
- [ ] Internal: HTTP/1.1 implementation over TcpSocket
  - [ ] Request serialization (method line, headers, body)
  - [ ] Response parsing (status line, headers, body)
  - [ ] Chunked transfer encoding support
  - [ ] Content-Length handling
  - [ ] Connection: keep-alive support
  - [ ] Redirect following (configurable, default on, max 5)
- [ ] EventLoop integration for async operation
- [ ] Doctest: basic GET/POST via loopback (may need simple test server), request/response serialization

---

## SslContext

TLS/SSL configuration via vendored mbedTLS.

**Files:**
- [ ] `include/promeki/sslcontext.h`
- [ ] `src/network/sslcontext.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (mbedTLS headers), namespace
- [ ] `enum SslProtocol { TlsV1_2, TlsV1_3, SecureProtocols }`
- [ ] `void setProtocol(SslProtocol)`
- [ ] `Error setCertificate(const FilePath &certFile)` — PEM or DER
- [ ] `Error setCertificate(const Buffer &certData)`
- [ ] `Error setPrivateKey(const FilePath &keyFile, const String &passphrase = {})`
- [ ] `Error setPrivateKey(const Buffer &keyData, const String &passphrase = {})`
- [ ] `Error setCaCertificates(const FilePath &caFile)` — trusted CA bundle
- [ ] `Error setCaCertificates(const Buffer &caData)`
- [ ] `void setVerifyPeer(bool enable)` — default true
- [ ] `void setVerifyDepth(int depth)`
- [ ] Internal: wraps `mbedtls_ssl_config`, `mbedtls_x509_crt`, `mbedtls_pk_context`

---

## SslSocket

TcpSocket with TLS encryption via mbedTLS.

**Files:**
- [ ] `include/promeki/sslsocket.h`
- [ ] `src/network/sslsocket.cpp`
- [ ] `tests/sslsocket.cpp`

**Implementation checklist:**
- [ ] Derive from `TcpSocket`
- [ ] `void setSslContext(const SslContext &ctx)`
- [ ] `Error startEncryption()` — initiates TLS handshake (client mode)
- [ ] `Error startServerEncryption()` — server-side handshake
- [ ] `bool isEncrypted() const`
- [ ] `String peerCertificateSubject() const` — info from peer cert
- [ ] Override `read()` — wraps `mbedtls_ssl_read()`
- [ ] Override `write()` — wraps `mbedtls_ssl_write()`
- [ ] Override `close()` — TLS shutdown then TCP close
- [ ] `PROMEKI_SIGNAL(encrypted)` — emitted after successful handshake
- [ ] `PROMEKI_SIGNAL(sslErrors, List<String>)` — emitted on certificate errors
- [ ] Internal: wraps `mbedtls_ssl_context`, `mbedtls_net_context`
- [ ] Non-blocking handshake support (integrate with EventLoop)
- [ ] Doctest: encrypted loopback connection (self-signed cert for testing)

---

## WebSocket

Message-oriented WebSocket protocol. Not IODevice-derived (message-oriented, not byte-stream).

**Files:**
- [ ] `include/promeki/websocket.h`
- [ ] `src/network/websocket.cpp`
- [ ] `tests/websocket.cpp`

**Implementation checklist:**
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `enum State { Disconnected, Connecting, Connected, Closing }`
- [ ] `Error connectToUrl(const String &url)` — ws:// or wss://
- [ ] `void disconnect()`
- [ ] `State state() const`
- [ ] `Error sendTextMessage(const String &message)`
- [ ] `Error sendBinaryMessage(const Buffer &message)`
- [ ] `void ping(const Buffer &payload = {})`
- [ ] `String peerAddress() const`
- [ ] `PROMEKI_SIGNAL(connected)`
- [ ] `PROMEKI_SIGNAL(disconnected)`
- [ ] `PROMEKI_SIGNAL(textMessageReceived, String)`
- [ ] `PROMEKI_SIGNAL(binaryMessageReceived, Buffer)`
- [ ] `PROMEKI_SIGNAL(pongReceived, Buffer)`
- [ ] `PROMEKI_SIGNAL(errorOccurred, Error)`
- [ ] Internal: WebSocket frame parser/serializer
  - [ ] HTTP upgrade handshake
  - [ ] Frame types: text, binary, ping, pong, close
  - [ ] Masking (client -> server)
  - [ ] Fragmented message reassembly
- [ ] Uses TcpSocket (or SslSocket for wss://) internally
- [ ] EventLoop integration
- [ ] Doctest: loopback echo test, text and binary messages

---

## Vendor mbedTLS

- [ ] Add mbedTLS to `thirdparty/mbedtls/`
- [ ] CMake: build as static library with `-fPIC`
- [ ] Link into `promeki`
- [ ] Verify build on Linux (primary), consider macOS/Windows stubs
