# Network Socket Layer

**Phase:** 3A
**Dependencies:** Phase 1 (HashMap, Mutex, WaitCondition, Future), Phase 2 (IODevice)
**Library:** `promeki-network` (new shared library, CMake option `PROMEKI_BUILD_NETWORK`)

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Headers under `include/promeki/network/`. Uses raw POSIX sockets (no libuv/asio). Socket interfaces are virtual/abstract so WASM can later provide Emscripten-based backends without API changes. Must not preclude a future `SrtSocket` subclass.

---

## CMake Setup — **DONE**

- [x] Add `PROMEKI_BUILD_NETWORK` option (default ON)
- [x] Create `include/promeki/network/` directory
- [x] Create `promeki-network` shared library target (SHARED, with VERSION/SOVERSION matching other libraries)
- [x] Link against `promeki-core`
- [x] Create `unittest-network` test executable, register with CTest
- [x] Install targets (follow existing pattern from promeki-core/promeki-proav):
  - [x] Headers installed to `include/promeki/network/`
  - [x] Shared library installed to standard lib location
  - [x] CMake package config for `find_package(promeki-network)`
  - [x] Export target so downstream projects can `target_link_libraries(... promeki-network)`
- [ ] mbedTLS static library built and absorbed into `promeki-network.so` (same pattern as zlib/libjpeg in promeki-proav) *(deferred — not needed until TLS/DTLS support is added)*

---

## SocketAddress — **DONE**

Simple data object: IP address + port. No PROMEKI_SHARED_FINAL (simple value type).

**Files:**
- [x] `include/promeki/network/socketaddress.h`
- [x] `src/net/socketaddress.cpp`
- [x] `tests/net/socketaddress.cpp`

**Implementation checklist:**
- [x] Header guard, includes, namespace
- [x] Internal storage: NetworkAddress + uint16_t port
- [x] Constructors: default, from NetworkAddress + port, from Ipv4Address + port, from Ipv6Address + port
- [x] `static Result<SocketAddress> fromString(const String &hostPort)` — parse "host:port"
- [x] `toString()` — returns "host:port" string (IPv6 bracketed)
- [x] `address()` — returns `const NetworkAddress &`
- [x] `port()` — returns `uint16_t`
- [x] `setAddress(const NetworkAddress &)`, `setPort(uint16_t)`
- [x] `isIPv4()`, `isIPv6()` — returns bool
- [x] `isNull()` — returns true if not set
- [x] `isLoopback()` — returns true for 127.0.0.1 / ::1
- [x] `isMulticast()` — returns true for multicast addresses
- [x] `operator==`, `operator!=`
- [x] `toSockAddr()` — fills `struct sockaddr_storage`, returns size (with port)
- [x] `fromSockAddr()` — construct from sockaddr (extracts port)
- [x] Static `any(uint16_t port)` — INADDR_ANY
- [x] Static `localhost(uint16_t port)` — 127.0.0.1
- [x] TextStream `operator<<`
- [x] Doctest: construction, fromString parsing, IPv4/IPv6 detection, toString round-trip, sockaddr round-trip

---

## AbstractSocket — **DONE**

Derives from IODevice. Base for TCP, UDP, raw sockets.

**Files:**
- [x] `include/promeki/network/abstractsocket.h`
- [x] `src/net/abstractsocket.cpp`
- [x] `tests/net/abstractsocket.cpp`

**Implementation checklist:**
- [x] Header guard, includes, namespace
- [x] Derive from `IODevice`, use `PROMEKI_OBJECT`
- [x] `enum SocketType { TcpSocketType, UdpSocketType, RawSocketType }`
- [x] `enum SocketState { Unconnected, Connecting, Connected, Bound, Closing, Listening }`
- [x] `SocketType socketType() const`
- [x] `SocketState state() const`
- [x] `Error bind(const SocketAddress &address)`
- [x] `Error connectToHost(const SocketAddress &address)` — supports non-blocking (EINPROGRESS)
- [x] `void disconnectFromHost()` — TCP close or UDP AF_UNSPEC disconnect
- [x] `SocketAddress localAddress() const`
- [x] `SocketAddress peerAddress() const`
- [x] `Error waitForConnected(unsigned int timeoutMs = 0)` — poll-based, returns `Error::Ok` or `Error::Timeout`
- [ ] `Error waitForDisconnected(unsigned int timeoutMs = 0)` *(deferred — not needed for current use cases)*
- [ ] Hint system (setHint/hint/supportedHints) *(deferred — IODevice has no hint infrastructure; socket options are exposed via dedicated methods on subclasses and via setSocketOption/socketOption escape hatch)*
- [x] `Error setSocketOption(int level, int option, int value)` — raw setsockopt
- [x] `Result<int> socketOption(int level, int option) const` — raw getsockopt
- [x] `int socketDescriptor() const` — raw fd access
- [x] `void setSocketDescriptor(int fd)` — adopt existing fd
- [x] `PROMEKI_SIGNAL(connected)`
- [x] `PROMEKI_SIGNAL(disconnected)`
- [x] `PROMEKI_SIGNAL(stateChanged, SocketState)`
- [x] Protected: `int _fd`, `SocketState _state`, `SocketType _type`, `SocketAddress _localAddress`, `SocketAddress _peerAddress`
- [x] Protected: `createSocket()`, `closeSocket()`, `setNonBlocking()`, `updateLocalAddress()`, `setState()`
- [x] `isSequential()` override returns `true`
- [ ] EventLoop integration: register fd for read/write notifications *(deferred — poll-based waiting works for current use; EventLoop fd registration is future work)*
- [x] Doctest: state transitions, bind, socket options, setSocketDescriptor

---

## TcpSocket — **DONE**

Stream-oriented TCP socket. Uses IODevice read/write.

**Files:**
- [x] `include/promeki/network/tcpsocket.h`
- [x] `src/net/tcpsocket.cpp`
- [x] `tests/net/tcpsocket.cpp`

**Implementation checklist:**
- [x] Derive from `AbstractSocket`
- [x] Constructor sets socket type to `TcpSocketType`
- [x] Override `open()` — creates socket fd (AF_INET, SOCK_STREAM)
- [x] `openIpv6()` — creates AF_INET6 socket
- [x] Override `read()` — wraps `recv()`, detects peer disconnect (returns 0)
- [x] Override `write()` — wraps `send()` with MSG_NOSIGNAL
- [x] Override `close()` — delegates to `closeSocket()`
- [x] Override `bytesAvailable()` — `ioctl(FIONREAD)`
- [x] `setNoDelay(bool)` — TCP_NODELAY
- [x] `setKeepAlive(bool)` — SO_KEEPALIVE
- [ ] EventLoop integration: register fd for read/write notifications *(deferred)*
- [x] Doctest: open/close, IPv6, loopback connect/send/receive via TcpServer echo test

---

## TcpServer — **DONE**

Listens for incoming TCP connections.

**Files:**
- [x] `include/promeki/network/tcpserver.h`
- [x] `src/net/tcpserver.cpp`
- [x] `tests/net/tcpserver.cpp`

**Implementation checklist:**
- [x] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [x] `Error listen(const SocketAddress &address, int backlog = 50)` — with SO_REUSEADDR
- [x] `void close()`
- [x] `bool isListening() const`
- [x] `SocketAddress serverAddress() const`
- [x] `TcpSocket *nextPendingConnection()` — returns accepted socket (caller owns)
- [x] `bool hasPendingConnections() const` — poll-based check
- [x] `Error waitForNewConnection(unsigned int timeoutMs = 0)` — poll-based, returns `Error::Ok` or `Error::Timeout`
- [x] `void setMaxPendingConnections(int count)`
- [x] `PROMEKI_SIGNAL(newConnection)`
- [ ] EventLoop integration: register listen fd, accept on readyRead *(deferred)*
- [x] Doctest: listen, accept connection, loopback echo test, timeout

---

## UdpSocket — **DONE**

Datagram-oriented UDP socket. Must support multicast for AV-over-IP use. Must not preclude future SrtSocket subclass.

**Files:**
- [x] `include/promeki/network/udpsocket.h`
- [x] `src/net/udpsocket.cpp`
- [x] `tests/net/udpsocket.cpp`

**Implementation checklist:**
- [x] Derive from `AbstractSocket`
- [x] Constructor sets socket type to `UdpSocketType`
- [x] Override `open()` — creates socket fd (AF_INET, SOCK_DGRAM)
- [x] `openIpv6()` — creates AF_INET6 socket with IPV6_V6ONLY disabled
- [x] `ssize_t writeDatagram(const void *data, size_t size, const SocketAddress &dest)`
- [x] `ssize_t writeDatagram(const Buffer &data, const SocketAddress &dest)`
- [x] `ssize_t readDatagram(void *data, size_t maxSize, SocketAddress *sender = nullptr)`
- [x] `bool hasPendingDatagrams() const`
- [x] `ssize_t pendingDatagramSize() const`
- [x] `Error joinMulticastGroup(const SocketAddress &group)` — `IP_ADD_MEMBERSHIP` / `IPV6_JOIN_GROUP`
- [x] `Error leaveMulticastGroup(const SocketAddress &group)` — `IP_DROP_MEMBERSHIP` / `IPV6_LEAVE_GROUP`
- [x] `Error joinMulticastGroup(const SocketAddress &group, const String &interface)` — interface-specific join via `ip_mreqn`
- [x] `Error setMulticastTTL(int ttl)` — `IP_MULTICAST_TTL` / `IPV6_MULTICAST_HOPS`
- [x] `Error setMulticastLoopback(bool enable)` — `IP_MULTICAST_LOOP` / `IPV6_MULTICAST_LOOP`
- [x] `Error setMulticastInterface(const String &interface)` — `IP_MULTICAST_IF` / `IPV6_MULTICAST_IF`
- [x] `Error setReuseAddress(bool enable)` — SO_REUSEADDR
- [x] `Error setDscp(uint8_t dscp)` — IP_TOS / IPV6_TCLASS
- [x] IODevice `read()`/`write()` overrides: use connected-mode UDP (requires prior `connectToHost`)
- [ ] `PROMEKI_SIGNAL(readyRead)` — emitted when datagram available *(deferred — inherited from IODevice but not auto-emitted; requires EventLoop fd registration)*
- [ ] EventLoop integration: register fd for read notifications *(deferred)*
- [x] Doctest: loopback send/receive datagram, connected mode, multicast join/leave/loopback, DSCP, multiple datagrams

---

## RawSocket — **DONE**

Raw Ethernet frame send/receive. For AV-over-IP raw packet work.

**Files:**
- [x] `include/promeki/network/rawsocket.h`
- [x] `src/net/rawsocket.cpp`
- [x] `tests/net/rawsocket.cpp`

**Implementation checklist:**
- [x] Derive from `AbstractSocket`
- [x] Constructor sets socket type to `RawSocketType`
- [x] `void setInterface(const String &interfaceName)` — configure before open
- [x] `void setProtocol(uint16_t ethertype)` — filter by ethertype
- [x] Override `open()` — creates `AF_PACKET`/`SOCK_RAW` socket (Linux)
- [x] Override `read()` — wraps `recv()`
- [x] Override `write()` — wraps `send()`
- [x] `Error setPromiscuous(bool enable)` — `PACKET_MR_PROMISC` via `PACKET_ADD_MEMBERSHIP`/`PACKET_DROP_MEMBERSHIP`
- [x] Platform guards: Linux `AF_PACKET`; macOS BPF deferred; returns `Error::NotSupported` on unsupported platforms
- [x] Requires root/CAP_NET_RAW — `open()` returns `Error::PermissionDenied` if insufficient permissions
- [x] Doctest: construction, configuration, open error handling (permission-dependent)
