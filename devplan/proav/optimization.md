# ProAV Optimization (network transmit path)

**Phase:** 4D
**Library:** `promeki`
**Standards:** All work follows `CODING_STANDARDS.md`; new code
requires unit tests.

Network transmit-path optimization for ST 2110-class workloads.
Foundation work is complete — what remains is the per-packet
`SCM_TXTIME` deadline computation in the RTP backend and a future
DPDK transport.

## Shipped

- `UdpSocket::writeDatagrams()` — `sendmmsg()` batch send (with a
  portable `sendto()` fallback). `Datagram::txTimeNs` populates
  `SCM_TXTIME` cmsgs when non-zero.
- `UdpSocket::setPacingRate()` / `clearPacingRate()` — kernel pacing
  via `SO_MAX_PACING_RATE` / `fq` qdisc.
- `UdpSocket::setTxTime(bool, clockId)` — `SO_TXTIME` enable path.
- `RtpSession::sendPacketsPaced()` — userspace pacing fallback that
  spreads packets across a configurable `Duration`.
- `PacketTransport` abstraction with `UdpSocketTransport` and
  `LoopbackTransport` (for tests). `RtpSession` migrated onto it.
- VBR per-frame pacing in `RtpMediaIO`: when no
  `VideoRtpTargetBitrate` is set, the rate cap is recomputed from
  each frame's actual byte count before dispatch.

## Remaining

- [ ] **Per-packet `SCM_TXTIME` deadlines.** Wire
  `RtpPacingMode::TxTime` through `RtpMediaIO` so the backend computes
  per-packet deadlines from the frame rate and stamps them onto the
  `Datagram::txTimeNs` field before handing to `sendPackets()`.
  Today `TxTime` mode falls through to `KernelFq`.
- [ ] **NIC / qdisc capability probe.** Detect at configure time
  whether ETF + hardware TX scheduling is available (Intel i210/i225,
  similar) and log a warning when only software ETF is available.
- [ ] **DPDK transport backend.** The `PacketTransport` interface is
  ready; the concrete implementation lands when a customer needs it
  (`rte_eth_tx_burst()` on an mbuf ring, hardware TX scheduling for
  pacing where available, userspace ring spacing otherwise).
- [ ] **Convenience overload** for many-packets-to-same-destination:
  `int writeDatagrams(const void * const *data, const size_t *sizes,
  int count, const SocketAddress &dest)`. Low priority — the generic
  `DatagramList` form already serves `RtpSession::sendPackets()`.
- [ ] **Performance benchmark** comparing syscall counts / wall time
  for N packets via the loop vs. batch. Tracked as a case under
  `utils/promeki-bench/cases/network.cpp` in
  [infra/benchmarking.md](../infra/benchmarking.md).

## DPDK-readiness constraints (preserved for the eventual DPDK transport)

To keep the DPDK door open:

- RtpSession must not touch socket fds directly; all packet I/O goes
  through `PacketTransport`.
- Packet data must be contiguous (DPDK mbufs require it; `RtpPacket`
  already complies).
- No kernel-specific assumptions in the media layer; pacing
  configuration goes through `PacketTransport::setPacingRate()`, not
  raw `setsockopt` calls in `RtpSession` or backends.
- Batch-first API. `sendPacket()` stays for convenience but
  `sendPackets()` is the primary path.
- Buffer lifetime: `sendPackets()` does not assume the transport
  copies synchronously; the contract states callers must not modify
  packet data until the call returns.
