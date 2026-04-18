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
- [ ] **EventLoop integration** — register socket fds for read/write notifications on AbstractSocket, TcpSocket, TcpServer, UdpSocket *(poll-based waiting works for current use; the foundation is now in place: `EventLoop::addIoSource` / `removeIoSource` landed in Phase 4l, so wiring existing sockets is a straightforward follow-on)*
- [ ] **UdpSocket::readyRead signal** *(requires EventLoop fd registration — unblocked by Phase 4l IoSource API)*

## IPC / Local Socket Layer — COMPLETE (Phase 4l)

`LocalSocket` (POSIX `AF_UNIX SOCK_STREAM`) and `LocalServer` have been added to the network layer. Both integrate with `EventLoop::addIoSource` / `removeIoSource` for async-ready plumbing and are tested via `tests/unit/network/localsocket.cpp` covering listen, connect, read/write roundtrip, timeout, and close semantics. `KlvFrame` / `KlvReader` / `KlvWriter` provide a 4-byte-FourCC + 4-byte-length-BE KLV framing layer over any `IODevice`; tested via `tests/unit/network/klvframe.cpp`.
