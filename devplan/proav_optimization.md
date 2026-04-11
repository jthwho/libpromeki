# ProAV Optimization

**Phase:** 4D
**Dependencies:** Phase 3A (sockets), MediaIO framework
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

This document tracks optimization work for the network transmit path (batch send, kernel pacing, PacketTransport abstraction, DPDK-readiness). Font rendering, codec/CSC framework, and userspace packet pacing are complete — see git history.

**Completed:** Font rendering (Font/FastFont/BasicFont), ImageCodec/AudioCodec/JpegImageCodec, VideoTestPattern/AudioTestPattern, userspace packet pacing via `RtpSession::sendPacketsPaced()`, PaintEngine interleaved template overhaul, CSC framework and pipeline bug fixes, **kernel batch send (`sendmmsg` via `UdpSocket::writeDatagrams()`)**, **kernel pacing (`SO_MAX_PACING_RATE` via `UdpSocket::setPacingRate()`)**, **`SO_TXTIME` enable path**, **`PacketTransport` abstraction with `UdpSocketTransport` and `LoopbackTransport`**, **`RtpSession` migrated to `PacketTransport`** (with `setRemote()` at start time, batched send via the transport, and `setPacingRate()` wrapper for kernel pacing).

**Remaining:** per-packet `SCM_TXTIME` cmsg scheduling on top of the existing `setTxTime()` enable path (sender-side deadline computation), and future DPDK transport backend.  Compressed video (JPEG / JPEG XS) VBR streams are now paced per-frame by recomputing `setPacingRate()` from each frame's actual byte count before dispatch.  The new `MediaPipeline` (see `proav_pipeline.md`) is signal-driven end to end, so there is no separate pipeline-scheduler problem to solve in this document.

---

## Batch UDP Transmission: `sendmmsg()` — SHIPPED

`UdpSocket::writeDatagrams()` batches outbound datagrams through a single `sendmmsg()` call on Linux, with a portable `sendto()` fallback on other platforms.  `RtpSession::sendPackets()` builds the parallel datagram list and hands it to the transport in one shot, which is the path `MediaIOTask_Rtp` uses on every frame.

Shipped:
- `UdpSocket::Datagram { const void *data; size_t size; SocketAddress dest; uint64_t txTimeNs; }` and `UdpSocket::DatagramList = List<Datagram>`.
- `int UdpSocket::writeDatagrams(const DatagramList &)` — one `sendmmsg()` on Linux, `sendto()` fallback elsewhere.  Per-datagram `txTimeNs` fields produce `SCM_TXTIME` cmsgs when non-zero.
- `RtpSession::sendPackets()` goes through `PacketTransport::sendPackets()`, which in the UDP case delegates to `writeDatagrams()`.
- Doctest coverage in `tests/network/udpsocket.cpp`: loopback batch send (5 packets), empty list, closed-socket error, invalid entries.

Still on the wishlist but not blocking:
- [ ] Performance benchmark comparing syscall counts / wall time for N packets via the old `writeDatagram` loop vs batch — tracked as a case under `utils/promeki-bench/cases/network.cpp` in [benchmarking.md](benchmarking.md).
- [ ] Convenience overload for many-packets-to-same-destination: `int writeDatagrams(const void * const *data, const size_t *sizes, int count, const SocketAddress &dest)`.  Low priority — the generic `DatagramList` form already serves `RtpSession::sendPackets()`.

---

## Kernel Packet Pacing

Batching cuts syscalls, but the real goal is to let the kernel (or NIC) space packets out over time instead of bursting them. Three mechanisms are worth supporting, in order of complexity:

### Option A: `SO_MAX_PACING_RATE` (fq qdisc) — SHIPPED

Per-socket rate limit enforced by the `fq` qdisc. Modern Linux default; no per-packet overhead; works out of the box on any interface using `fq`.

Shipped:
- `Error UdpSocket::setPacingRate(uint64_t bytesPerSec)` — `setsockopt(SOL_SOCKET, SO_MAX_PACING_RATE)`, clamped to `UINT32_MAX`.
- `Error UdpSocket::clearPacingRate()` sets the rate to `PacingRateUnlimited`.
- `PacketTransport::setPacingRate()` virtual with `UdpSocketTransport` delegation.
- `RtpSession::setPacingRate()` convenience wrapper that forwards to the transport — used by `MediaIOTask_Rtp` in `KernelFq` mode to derive bytes/sec from the configured `VideoRtpTargetBitrate` (or from the descriptor as a fallback) at open time.

### Option B: `SO_TXTIME` + ETF qdisc — PARTIALLY SHIPPED

Per-packet transmit timestamp. Requires ETF qdisc and (for best results) NIC hardware TX scheduling (e.g. Intel i210/i225). Each packet carries a `SCM_TXTIME` cmsg on its `sendmsg()` or per-entry on `sendmmsg()`.

Shipped:
- `Error UdpSocket::setTxTime(bool enable, int clockId = CLOCK_TAI)` — `setsockopt(SOL_SOCKET, SO_TXTIME, struct sock_txtime)` on Linux.  Disabling is a no-op because a plain send() still works after the socket was configured for SO_TXTIME.
- `UdpSocket::Datagram::txTimeNs` field on the batch-send datagram struct.  When non-zero, `writeDatagrams()` attaches an `SCM_TXTIME` cmsg per datagram via a per-message control buffer (`CMSG_SPACE(sizeof(uint64_t))` slots stored in a parallel scratch `List<uint8_t>`).
- `PacketTransport::setTxTime()` virtual with `UdpSocketTransport` delegation.

Remaining:
- [ ] Wire `RtpPacingMode::TxTime` through `MediaIOTask_Rtp` so the backend computes per-packet deadlines from the frame rate and stamps them onto the `Datagram::txTimeNs` fields before handing to `sendPackets()`.  Currently `TxTime` mode still falls through to the `KernelFq` path.
- [ ] Probe NIC / qdisc support at configure time and log a warning when only software ETF is available (not hardware TX scheduling).

### Option C: Userspace pacing — DONE

Implemented as `RtpSession::sendPacketsPaced()`. `TimeStamp::sleepUntil()` between sends, spread packets across a given `Duration`. Works everywhere without kernel configuration. Automatically used by the current sink paths for large packet counts.

### Option D: DPDK — future

See the DPDK-Readiness section below. Not being built now, but `PacketTransport` must not preclude it.

---

## PacketTransport — Abstract Packet I/O Interface — SHIPPED

`RtpSession` previously talked directly to `UdpSocket`.  That coupling has been broken by `PacketTransport`, a thin abstract interface that sits between RTP/media code and the kernel (or future DPDK / AF_XDP / loopback test paths).

Shipped:
- `include/promeki/packettransport.h` — abstract base with `open`/`close`/`isOpen`, `sendPacket`, `sendPackets(const DatagramList &)`, `receivePacket`, `setPacingRate`, `setTxTime`.  `Datagram` struct matches `UdpSocket::Datagram` shape (data pointer, size, dest, optional `txTimeNs`).
- `include/promeki/udpsockettransport.h` + `src/network/udpsockettransport.cpp` — owns a `UdpSocket`, applies DSCP / multicast TTL / multicast interface / multicast loopback / reuseAddress at `open()` time.  `sendPackets()` translates the `PacketTransport::DatagramList` into `UdpSocket::DatagramList` and calls `writeDatagrams()` (sendmmsg) in one shot.  `setPacingRate()` / `setTxTime()` delegate to the underlying socket.
- `include/promeki/loopbacktransport.h` + `src/network/loopbacktransport.cpp` — in-process paired transport for tests.  `LoopbackTransport::pair(&a, &b)` links two instances so a send on one enqueues into the other's receive queue (buffered in a `List<QueueEntry>`).  Used by `tests/network/rtpsession.cpp` to exercise RtpSession end-to-end without touching the kernel network stack.
- `RtpSession` migrated onto `PacketTransport`:
  - `Error start(const SocketAddress &localAddr)` still exists and internally builds a `UdpSocketTransport`.
  - New `Error start(PacketTransport *transport)` lets callers hand in their own (already-open) transport — used by the RtpSession loopback tests and usable by future cases where multiple streams share a transport.
  - Destination-per-call overloads were removed; callers `setRemote()` once before sending.
  - `sendPackets()` builds a `PacketTransport::DatagramList` over the shared `RtpPacket` buffer and hands it to `_transport->sendPackets()`.
  - `setPacingRate()` wraps `_transport->setPacingRate()` so the task layer can set kernel pacing without knowing the concrete transport type.
- Test coverage:
  - `tests/network/udpsockettransport.cpp` — construction defaults, open/close, double-open Busy, double-close safe, sendPacket loopback, sendPackets batch loopback, receivePacket path, DSCP/multicast TTL applied at open, setPacingRate, error paths on closed transport.
  - `tests/network/loopbacktransport.cpp` — pairing, single/batch send, bidirectional, empty receive returns -1, close clears queue, setPacingRate/setTxTime accepted, destructor unhooks peer.
  - `tests/network/rtpsession.cpp` — every existing test migrated plus new cases for `start(PacketTransport *)`, `start(unopened transport) == NotOpen`, `start(nullptr) == InvalidArgument`, and `sendPackets via LoopbackTransport` verifying SSRC / sequence number / marker bits across a 4-packet batch.

Remaining (not blocking any current work):
- [ ] DPDK transport backend — the `PacketTransport` interface is ready; the concrete implementation lands when a first customer actually needs it.  `rte_eth_tx_burst()` on an mbuf ring, hardware TX scheduling for pacing where available, userspace ring spacing otherwise.

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

1. **`UdpSocket::setPacingRate()`** — SHIPPED
2. **`UdpSocket::writeDatagrams()` via `sendmmsg()`** + `RtpSession::sendPackets()` migration — SHIPPED
3. **`PacketTransport` + `UdpSocketTransport` + `LoopbackTransport`** — SHIPPED
4. **Unified `MediaIOTask_Rtp`** — SHIPPED (writer only; reader mode deferred, see `proav_nodes.md`)
5. **`SO_TXTIME` per-packet deadlines** — enable path and `Datagram::txTimeNs` shipped; the sender-side per-packet deadline computation in `MediaIOTask_Rtp` is the remaining piece
6. **DPDK backend** — future phase, once the transport abstraction has shipped and the first customer needs it
