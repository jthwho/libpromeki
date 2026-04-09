# ProAV Optimization

**Phase:** 4D
**Dependencies:** Phase 3A (sockets), MediaIO framework
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

This document tracks optimization work for the network transmit path (batch send, kernel pacing, PacketTransport abstraction, DPDK-readiness). Font rendering, codec/CSC framework, and userspace packet pacing are complete — see git history.

**Completed:** Font rendering (Font/FastFont/BasicFont), ImageCodec/AudioCodec/JpegImageCodec, VideoTestPattern/AudioTestPattern, userspace packet pacing via `RtpSession::sendPacketsPaced()`, PaintEngine interleaved template overhaul, CSC framework and pipeline bug fixes.

**Remaining:** kernel-level batch send (`sendmmsg`), kernel pacing (`SO_MAX_PACING_RATE` / `SO_TXTIME`), `PacketTransport` abstraction for DPDK-readiness. The automatic node-processing section from the previous revision is dropped — it was tied to the deprecated `MediaNode` pipeline. The new `MediaPipeline` (see `proav_pipeline.md`) is signal-driven end to end and has no equivalent scheduling problem.

---

## Batch UDP Transmission: `sendmmsg()`

`RtpSession` currently sends packets one `sendto()` per packet. For a 1080p JPEG fragmented into ~60 RTP packets at 30 fps that is 1800 syscalls/sec just for video, plus the micro-burst the NIC sees each frame interval. `sendmmsg()` hands the kernel the whole batch in one syscall.

**UdpSocket additions:**

Files: existing `include/promeki/udpsocket.h`, `src/network/udpsocket.cpp`

- [ ] `struct Datagram { const void *data; size_t size; SocketAddress dest; }`
- [ ] `using DatagramList = List<Datagram>`
- [ ] `int writeDatagrams(const DatagramList &datagrams)` — one `sendmmsg()` call; returns datagrams sent or -1
- [ ] Fallback to `sendto()` loop on platforms without `sendmmsg()` (compile-time `#ifdef`)
- [ ] Convenience overload for many-packets-to-same-destination: `int writeDatagrams(const void * const *data, const size_t *sizes, int count, const SocketAddress &dest)`

**RtpSession changes:**

- [ ] `sendPackets()` builds a `DatagramList` and calls `_socket->writeDatagrams()` in one shot
- [ ] Single-packet `sendPacket()` keeps using `writeDatagram()`

**Doctest:**
- [ ] Loopback batch send, verify count and contents
- [ ] Single-datagram degenerate case
- [ ] Performance benchmark (optional): compare wall time and syscall count for 100 packets via the old loop vs batch

---

## Kernel Packet Pacing

Batching cuts syscalls, but the real goal is to let the kernel (or NIC) space packets out over time instead of bursting them. Three mechanisms are worth supporting, in order of complexity:

### Option A: `SO_MAX_PACING_RATE` (fq qdisc) — default/recommended

Per-socket rate limit enforced by the `fq` qdisc. Modern Linux default; no per-packet overhead; works out of the box on any interface using `fq`.

- [ ] `Error UdpSocket::setPacingRate(uint64_t bytesPerSec)` — `setsockopt(SOL_SOCKET, SO_MAX_PACING_RATE)`
- [ ] `Error UdpSocket::clearPacingRate()` — sets to `~0U`
- [ ] `RtpSession::setTargetBitrate(uint64_t bitsPerSec)` — called from sink stages / MediaIO backends at configure time; computes pacing rate (bitrate/8 + headroom) and pushes it to the socket in `start()`

**Verdict:** Simplest option, ~15 lines of code in UdpSocket. Should be the default for any sink that knows its target bitrate.

### Option B: `SO_TXTIME` + ETF qdisc — precise pacing for ST 2110-grade deployments

Per-packet transmit timestamp. Requires ETF qdisc and (for best results) NIC hardware TX scheduling (e.g. Intel i210/i225). Each packet carries a `SCM_TXTIME` cmsg on its `sendmsg()` or per-entry on `sendmmsg()`.

- [ ] `Error UdpSocket::setTxTime(bool enable)`
- [ ] Extend `Datagram` with an optional `uint64_t txTimeNs`
- [ ] `writeDatagrams()` sets a per-message `cmsg` with `SCM_TXTIME` when the datagram has a non-zero `txTimeNs`
- [ ] Probe NIC support at configure time; log degradation if only software ETF is available

**Verdict:** Significant setup cost (kernel config, NIC support), but the only way to get truly precise pacing for ST 2110-21. Add after Option A and `sendmmsg()` are working.

### Option C: Userspace pacing — DONE

Implemented as `RtpSession::sendPacketsPaced()`. `TimeStamp::sleepUntil()` between sends, spread packets across a given `Duration`. Works everywhere without kernel configuration. Automatically used by the current sink paths for large packet counts.

### Option D: DPDK — future

See the DPDK-Readiness section below. Not being built now, but `PacketTransport` must not preclude it.

---

## PacketTransport — Abstract Packet I/O Interface

`RtpSession` currently talks directly to `UdpSocket`. That couples every RTP code path to kernel sockets. A thin transport abstraction breaks that coupling and lets future backends (DPDK, AF_XDP, loopback testing) drop in without touching the RTP/media layer.

**Files:**
- [ ] `include/promeki/packettransport.h`
- [ ] `include/promeki/udpsockettransport.h` / `src/network/udpsockettransport.cpp`
- [ ] `include/promeki/loopbacktransport.h` / `src/network/loopbacktransport.cpp`

**Interface:**

- [ ] `class PacketTransport` (abstract)
  - [ ] `virtual Error open() = 0`
  - [ ] `virtual void close() = 0`
  - [ ] `virtual bool isOpen() const = 0`
  - [ ] `virtual ssize_t sendPacket(const void *data, size_t size, const SocketAddress &dest) = 0`
  - [ ] `virtual int sendPackets(const Datagram *datagrams, int count) = 0`
  - [ ] `virtual ssize_t receivePacket(void *data, size_t maxSize, SocketAddress *sender = nullptr) = 0`
  - [ ] `virtual Error setPacingRate(uint64_t bytesPerSec)` — default `NotSupported`
  - [ ] `virtual Error setTxTime(bool enable)` — default `NotSupported`
  - [ ] `virtual ~PacketTransport() = default`

**Backends:**

| Backend | `sendPackets()` impl | Pacing | When |
|---|---|---|---|
| `UdpSocketTransport` | `sendmmsg()` via `UdpSocket::writeDatagrams()` | `SO_MAX_PACING_RATE` / `SO_TXTIME` | Now |
| `LoopbackTransport` | Direct memory copy to paired instance | None | For tests |
| `DpdkTransport` | `rte_eth_tx_burst()` on mbuf ring | Hardware TX scheduling / userspace ring pacing | Future |

**UdpSocketTransport:**
- [ ] Owns a `UdpSocket`, delegates every method
- [ ] `sendPackets()` calls `UdpSocket::writeDatagrams()`
- [ ] `setPacingRate()` / `setTxTime()` delegate to the socket
- [ ] Constructor takes bind address and optional multicast config

**LoopbackTransport:**
- [ ] Two paired instances share a `Queue<Buffer>`; one sends, the other receives
- [ ] Zero-copy: sender's buffer is enqueued directly for the receiver
- [ ] Lets `MediaIOTask_RtpVideo` / `RtpAudio` integration tests run entirely in-process

**RtpSession migration:**
- [ ] `RtpSession(PacketTransport *transport, ObjectBase *parent = nullptr)` — new primary constructor
- [ ] Convenience constructor that internally builds a `UdpSocketTransport` — preserves existing usage sites
- [ ] `sendPackets()` calls `_transport->sendPackets()` instead of looping `_socket->writeDatagram()`
- [ ] `start()` / `stop()` dispatch to the transport instead of raw socket calls

---

## Design Constraints for All New Network Code

To keep the DPDK door open:

- [ ] **RtpSession must not touch socket fds directly.** All packet I/O goes through `PacketTransport`
- [ ] **Packet data must be contiguous.** DPDK mbufs require contiguous packet data; current `RtpPacket` already complies
- [ ] **No kernel-specific assumptions in the media layer.** Pacing configuration goes through `PacketTransport::setPacingRate()`, not raw `setsockopt` calls in RtpSession or MediaIO backends
- [ ] **Batch-first API.** `sendPacket()` stays for convenience, but the primary path is `sendPackets()`
- [ ] **Buffer lifetime.** `sendPackets()` must not assume the transport copies synchronously. The socket path is fine (sendmmsg copies into kernel buffers); the interface contract should state that callers must not modify packet data until the call returns

---

## Implementation Order

1. **`UdpSocket::setPacingRate()`** — one small commit, huge impact
2. **`UdpSocket::writeDatagrams()` via `sendmmsg()`** + `RtpSession::sendPackets()` migration
3. **`PacketTransport` + `UdpSocketTransport` + `LoopbackTransport`** — lay the abstraction and migrate `RtpSession`
4. **`MediaIOTask_RtpVideo` / `MediaIOTask_RtpAudio`** — build against `PacketTransport`, not `UdpSocket`
5. **`SO_TXTIME`** — precise pacing for ST 2110-grade deployments
6. **DPDK backend** — future phase, once the transport abstraction has shipped and the first customer needs it
