# Network Socket Layer

**Phase:** 3A
**Dependencies:** Phase 1 (HashMap, Mutex, WaitCondition, Future), Phase 2 (IODevice)
**Library:** `promeki-network` (new shared library, CMake option `PROMEKI_BUILD_NETWORK`)

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Headers under `include/promeki/net/`. Uses raw POSIX sockets (no libuv/asio). Socket interfaces are virtual/abstract so WASM can later provide Emscripten-based backends without API changes. Must not preclude a future `SrtSocket` subclass.

---

## CMake Setup

- [ ] Add `PROMEKI_BUILD_NETWORK` option (default ON)
- [ ] Create `include/promeki/net/` directory
- [ ] Create `promeki-network` shared library target (SHARED, with VERSION/SOVERSION matching other libraries)
- [ ] Link against `promeki-core`
- [ ] Create `unittest-network` test executable, register with CTest
- [ ] Install targets (follow existing pattern from promeki-core/promeki-proav):
  - [ ] Headers installed to `include/promeki/net/`
  - [ ] Shared library installed to standard lib location
  - [ ] CMake package config for `find_package(promeki-network)`
  - [ ] Export target so downstream projects can `target_link_libraries(... promeki-network)`
- [ ] mbedTLS static library built and absorbed into `promeki-network.so` (same pattern as zlib/libjpeg in promeki-proav)

---

## SocketAddress

Simple data object: IP address + port. No PROMEKI_SHARED_FINAL (simple value type).

**Files:**
- [ ] `include/promeki/net/socketaddress.h`
- [ ] `src/net/socketaddress.cpp`
- [ ] `tests/socketaddress.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace (promeki or promeki::net)
- [ ] Internal storage: `struct sockaddr_storage` or separate IP + port
- [ ] Constructors: default, from `String` host + `uint16_t` port, from `struct sockaddr *`
- [ ] `static std::pair<SocketAddress, Error> fromString(const String &hostPort)` — parse "host:port"
- [ ] `toString()` — returns "host:port" string
- [ ] `host()` — returns `String` (IP address or hostname)
- [ ] `port()` — returns `uint16_t`
- [ ] `setHost(const String &)`, `setPort(uint16_t)`
- [ ] `isIPv4()`, `isIPv6()` — returns bool
- [ ] `isNull()` — returns true if not set
- [ ] `isLoopback()` — returns true for 127.0.0.1 / ::1
- [ ] `operator==`, `operator!=`
- [ ] `toSockAddr()` — fills `struct sockaddr_storage`, returns size
- [ ] Static `any(uint16_t port)` — INADDR_ANY
- [ ] Static `localhost(uint16_t port)` — 127.0.0.1
- [ ] Doctest: construction, fromString parsing, IPv4/IPv6 detection, toString round-trip

---

## AbstractSocket

Derives from IODevice. Base for TCP, UDP, raw sockets.

**Files:**
- [ ] `include/promeki/net/abstractsocket.h`
- [ ] `src/net/abstractsocket.cpp`
- [ ] `tests/abstractsocket.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Derive from `IODevice`, use `PROMEKI_OBJECT`
- [ ] `enum SocketType { TcpSocket, UdpSocket, RawSocket }`
- [ ] `enum SocketState { Unconnected, Connecting, Connected, Bound, Closing, Listening }`
- [ ] `SocketType socketType() const`
- [ ] `SocketState state() const`
- [ ] `Error bind(const SocketAddress &address)`
- [ ] `Error connectToHost(const SocketAddress &address)`
- [ ] `void disconnectFromHost()`
- [ ] `SocketAddress localAddress() const`
- [ ] `SocketAddress peerAddress() const`
- [ ] `Error waitForConnected(unsigned int timeoutMs = 0)` — returns `Error::Ok` or `Error::Timeout`
- [ ] `Error waitForDisconnected(unsigned int timeoutMs = 0)` — returns `Error::Ok` or `Error::Timeout`
- [ ] Override `setHint()` — map IODevice hints (NoDelay, KeepAlive, ReuseAddress, SendBufferSize, RecvBufferSize, NonBlocking, DSCP) to `setsockopt()` calls. Subclasses extend for type-specific hints.
- [ ] Override `hint()` — query current socket option values via `getsockopt()`
- [ ] Override `supportedHints()` — return socket-relevant hints
- [ ] `void setSocketOption(int level, int option, int value)` — escape hatch for raw setsockopt not covered by hints
- [ ] `int socketOption(int level, int option)` — escape hatch for raw getsockopt
- [ ] `int socketDescriptor() const` — raw fd access for advanced use
- [ ] `void setSocketDescriptor(int fd)` — adopt existing fd
- [ ] `PROMEKI_SIGNAL(connected)`
- [ ] `PROMEKI_SIGNAL(disconnected)`
- [ ] `PROMEKI_SIGNAL(stateChanged, SocketState)`
- [ ] Protected: `int _fd`, `SocketState _state`, `SocketType _type`
- [ ] Protected: helper methods for socket creation, non-blocking mode, polling
- [ ] `isSequential()` override returns `true`
- [ ] Doctest: state transitions (via concrete subclass)

---

## TcpSocket

Stream-oriented TCP socket. Uses IODevice read/write.

**Files:**
- [ ] `include/promeki/net/tcpsocket.h`
- [ ] `src/net/tcpsocket.cpp`
- [ ] `tests/tcpsocket.cpp`

**Implementation checklist:**
- [ ] Derive from `AbstractSocket`
- [ ] Constructor sets socket type to `TcpSocket`
- [ ] Override `open()` — creates socket fd (`AF_INET`/`AF_INET6`, `SOCK_STREAM`)
- [ ] Override `read()` — wraps `recv()`
- [ ] Override `write()` — wraps `send()`
- [ ] Override `close()` — `::close(fd)`, emit `disconnected`, update state
- [ ] Override `bytesAvailable()` — `ioctl(FIONREAD)`
- [ ] NoDelay and KeepAlive via hint system: `setHint(NoDelay, true)`, `setHint(KeepAlive, true)`
- [ ] Non-blocking connect support: connect returns immediately, `connected` signal emitted when done
- [ ] EventLoop integration: register fd for read/write notifications
- [ ] Doctest: loopback connect/send/receive, state transitions

---

## TcpServer

Listens for incoming TCP connections.

**Files:**
- [ ] `include/promeki/net/tcpserver.h`
- [ ] `src/net/tcpserver.cpp`
- [ ] `tests/tcpserver.cpp`

**Implementation checklist:**
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `Error listen(const SocketAddress &address, int backlog = 50)`
- [ ] `void close()`
- [ ] `bool isListening() const`
- [ ] `SocketAddress serverAddress() const`
- [ ] `TcpSocket *nextPendingConnection()` — returns accepted socket (caller owns)
- [ ] `bool hasPendingConnections() const`
- [ ] `Error waitForNewConnection(unsigned int timeoutMs = 0)` — returns `Error::Ok` or `Error::Timeout`
- [ ] `void setMaxPendingConnections(int count)`
- [ ] `PROMEKI_SIGNAL(newConnection)`
- [ ] EventLoop integration: register listen fd, accept on readyRead
- [ ] Doctest: listen, accept connection, loopback echo test

---

## UdpSocket

Datagram-oriented UDP socket. Must support multicast for AV-over-IP use. Must not preclude future SrtSocket subclass.

**Files:**
- [ ] `include/promeki/net/udpsocket.h`
- [ ] `src/net/udpsocket.cpp`
- [ ] `tests/udpsocket.cpp`

**Implementation checklist:**
- [ ] Derive from `AbstractSocket`
- [ ] Constructor sets socket type to `UdpSocket`
- [ ] Override `open()` — creates socket fd (`AF_INET`/`AF_INET6`, `SOCK_DGRAM`)
- [ ] `ssize_t writeDatagram(const void *data, size_t size, const SocketAddress &dest)`
- [ ] `ssize_t writeDatagram(const Buffer &data, const SocketAddress &dest)`
- [ ] `ssize_t readDatagram(void *data, size_t maxSize, SocketAddress *sender = nullptr)`
- [ ] `Buffer readDatagram(size_t maxSize, SocketAddress *sender = nullptr)`
- [ ] `bool hasPendingDatagrams() const`
- [ ] `ssize_t pendingDatagramSize() const`
- [ ] `Error joinMulticastGroup(const SocketAddress &group)` — `IP_ADD_MEMBERSHIP`
- [ ] `Error leaveMulticastGroup(const SocketAddress &group)` — `IP_DROP_MEMBERSHIP`
- [ ] `Error joinMulticastGroup(const SocketAddress &group, const String &interface)` — interface-specific join
- [ ] Multicast TTL via hint system: `setHint(MulticastTTL, ttl)` — maps to `IP_MULTICAST_TTL`
- [ ] `void setMulticastLoopback(bool enable)` — `IP_MULTICAST_LOOP` (could also be a hint)
- [ ] `Error setMulticastInterface(const String &interface)` — `IP_MULTICAST_IF`
- [ ] IODevice `read()`/`write()` overrides: use connected-mode UDP (requires prior `connectToHost`)
- [ ] `PROMEKI_SIGNAL(readyRead)` — emitted when datagram available
- [ ] EventLoop integration: register fd for read notifications
- [ ] Doctest: loopback send/receive datagram, multicast join/leave (loopback)

---

## RawSocket

Raw Ethernet frame send/receive. For AV-over-IP raw packet work.

**Files:**
- [ ] `include/promeki/net/rawsocket.h`
- [ ] `src/net/rawsocket.cpp`
- [ ] `tests/rawsocket.cpp`

**Implementation checklist:**
- [ ] Derive from `AbstractSocket`
- [ ] Constructor sets socket type to `RawSocket`
- [ ] `Error setInterface(const String &interfaceName)` — bind to specific NIC
- [ ] `Error setProtocol(uint16_t ethertype)` — filter by ethertype (e.g., 0x0800 for IP)
- [ ] Override `open()` — creates `AF_PACKET`/`SOCK_RAW` socket (Linux), `BPF` (macOS)
- [ ] Override `read()` — reads full Ethernet frames
- [ ] Override `write()` — sends raw Ethernet frames
- [ ] `void setPromiscuous(bool enable)` — promiscuous mode on interface
- [ ] Platform guards: Linux `AF_PACKET`, macOS `BPF`; error on unsupported platforms
- [ ] Requires root/CAP_NET_RAW — `open()` returns appropriate error if insufficient permissions
- [ ] Doctest: limited testing (may require elevated permissions; test construction and error handling at minimum)
