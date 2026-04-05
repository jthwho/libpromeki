# Network Socket Layer — COMPLETE

**Phase:** 3A
**Library:** `promeki` (feature flag `PROMEKI_ENABLE_NETWORK`)

All socket classes implemented, tested, and merged: SocketAddress, AbstractSocket, TcpSocket, TcpServer, UdpSocket, RawSocket. Raw POSIX sockets, no libuv/asio. Virtual/abstract interfaces for future WASM backends and SrtSocket subclass.

---

## Deferred Items

These items were deferred from the completed classes:

- [ ] **mbedTLS** — static library build, absorbed into `libpromeki.so` *(not needed until TLS/DTLS in Phase 3B)*
- [ ] **AbstractSocket::waitForDisconnected()** *(not needed for current use cases)*
- [ ] **Hint system** (setHint/hint/supportedHints) *(IODevice has no hint infrastructure; socket options exposed via setSocketOption/socketOption)*
- [ ] **EventLoop integration** — register socket fds for read/write notifications on AbstractSocket, TcpSocket, TcpServer, UdpSocket *(poll-based waiting works for current use)*
- [ ] **UdpSocket::readyRead signal** *(requires EventLoop fd registration)*
