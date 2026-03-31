# ProAV Optimization and Cleanup

**Phase:** 4D
**Dependencies:** Phase 4A (MediaNode, MediaPipeline), Phase 4B (concrete nodes), Phase 3A (sockets)
**Library:** `promeki-proav`, `promeki-network`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

This document covers four areas of improvement: high-performance font rendering, a proper video codec abstraction, automatic node processing in the pipeline, and batch UDP transmission with kernel-side packet pacing.

**Partial progress on packet pacing:** `RtpSession::sendPacketsPaced()` implements userspace inter-packet pacing using `Duration`/`TimeStamp::sleepUntil()`. `RtpVideoSinkNode` uses it automatically for large packet counts (>=32 packets, i.e., uncompressed video), spreading packets across 90% of the frame interval (ST 2110-21 style). Both RTP sink nodes now use Duration/TimeStamp instead of raw `std::chrono`. The `#ifdef PROMEKI_HAVE_NETWORK` guards have been removed from RTP sink node headers and sources (the conditional compilation is now handled at the CMake level). This is a working userspace fallback; the kernel-level optimizations (`sendmmsg()`, `SO_MAX_PACING_RATE`, `SO_TXTIME`) below remain planned.

---

## Font Rendering Framework — FastFont / BasicFont / Font

**Completed.** The font rendering system has been fully refactored from the earlier `TextRenderer` / `FontPainter` pair into a proper class hierarchy with a shared abstract base.

**Architecture:**
- `Font` — abstract base class managing common state (filename, size, colors, PaintEngine, kerning) and defining the rendering interface (`drawText()`, `measureText()`, `lineHeight()`, `ascender()`, `descender()`). State changes are propagated to subclasses via the `onStateChanged()` hook. `setPaintEngine()` is smart: it only calls `onStateChanged()` when the pixel format pointer changes, so switching between engines with the same format is cheap.
- `FastFont` (was `TextRenderer`) — high-performance cached glyph renderer. Pre-renders each glyph into a native-format `Image` using `PaintEngine::createPixel()` and blits via `PaintEngine::blit()`. Opaque background fill; not suitable for transparent overlays.
- `BasicFont` (was `FontPainter`) — alpha-compositing renderer. Rasterizes each glyph with FreeType and composites per-pixel using the foreground color and glyph alpha. No cache; low memory; correct transparency.

**Bug fixed:** RGBA8 blit had a double-subtraction clipping bug in `pixelformat_rgba8.cpp` that caused the source height to be incorrectly reduced when `destY` was large (e.g., rendering timecode at the bottom of a 1080p image). Fixed and covered by regression tests.

**TimecodeOverlayNode fix:** Now uses actual font metrics (`ascender()`, `lineHeight()`) for layout instead of the raw `_fontSize` integer, ensuring correct baseline placement at all font sizes.

**Files:**
- [x] `include/promeki/proav/font.h` / `src/font.cpp` — abstract base
- [x] `include/promeki/proav/fastfont.h` / `src/fastfont.cpp` — cached opaque renderer
- [x] `include/promeki/proav/basicfont.h` / `src/basicfont.cpp` — alpha-compositing renderer
- [x] `src/pixelformat_rgba8.cpp` — RGBA8 blit clipping bug fix
- [x] `src/timecodeoverlaynode.cpp` — font metrics used for layout
- [x] `docs/fonts.dox` — Font Rendering documentation page
- [x] `docs/modules.dox` — `proav_paint` group updated
- [x] `tests/fastfont.cpp` — full test suite for FastFont
- [x] `tests/basicfont.cpp` — full test suite for BasicFont
- [x] `tests/paintengine.cpp` — RGBA8 blit regression tests added
- [x] Deleted: `textrenderer.h`, `textrenderer.cpp`, `tests/textrenderer.cpp`, `fontpainter.h`, `fontpainter.cpp`

**Completed features (Font base):**
- [x] `setFontFilename()` / `fontFilename()`
- [x] `setFontSize()` / `fontSize()`
- [x] `setForegroundColor()` / `foregroundColor()`
- [x] `setBackgroundColor()` / `backgroundColor()`
- [x] `setPaintEngine()` — smart invalidation: only triggers `onStateChanged()` on pixel format change
- [x] `setKerningEnabled()` / `kerningEnabled()` — optional FreeType kerning support
- [x] `isValid()` — true when filename, size, and paint engine pixel format are all set
- [x] `descender()` metric (new, was missing from original classes)

**Completed features (FastFont):**
- [x] Cached glyph rendering via `Map<uint32_t, CachedGlyph>` (pre-rendered native-format `Image` + advance width)
- [x] Tiered cache invalidation: font-level (FT_Face + glyphs) vs pixel-level (glyph images only)
- [x] Deferred PaintEngine pattern: configure all properties with a null engine, then assign engine later
- [x] `TimecodeOverlayNode` migrated from old classes to `FastFont`

**Completed features (BasicFont):**
- [x] Per-pixel FreeType alpha compositing — correct anti-aliased text over any background
- [x] No glyph cache (minimal memory footprint)
- [x] Ignores background color (composites foreground over existing pixel content)

**Doctest (FastFont):**
- [x] Default construction, all getters
- [x] `setFontFilename` / `setFontSize` no-op when same value
- [x] `isValid()` requires filename + positive size + pixel format
- [x] `drawText` / `measureText` without font — returns false/0
- [x] `drawText` / `measureText` with bad font path — returns false/0
- [x] Font metrics after loading (`lineHeight > 0`, `ascender > 0`, `lineHeight >= ascender`)
- [x] `measureText("")` == 0; single char > 0; longer string wider
- [x] Monospace character widths equal (Fira Code)
- [x] `drawText` changes pixels; different color combos produce different sums
- [x] `drawText` on RGBA8 image
- [x] Cache invalidation: size change produces different widths; bad path returns 0
- [x] `setPaintEngine` same pixel format preserves cache
- [x] Deferred PaintEngine on RGB8 and RGBA8
- [x] Deferred PaintEngine measures same as direct construction
- [x] Large RGBA8 image (1920x1080): text at bottom draws pixels (regression)
- [x] Large RGB8 image: text at bottom draws pixels (regression)
- [x] `lineHeight == ascender + descender`
- [x] Metrics scale with font size
- [x] Metrics match between deferred and direct construction

**Doctest (BasicFont):**
- [x] Default construction, all getters
- [x] `setFontFilename`, `setFontSize`, `setForegroundColor`, `setKerningEnabled`
- [x] `drawText` / `measureText` without font — returns false/0
- [x] `drawText` with bad font path — returns false/0
- [x] Font metrics after loading
- [x] `measureText("")` == 0; longer string wider
- [x] `drawText` renders pixels; different foreground colors produce different sums
- [x] `drawText` on RGBA8 image
- [x] Changing font size works; changing to bad path returns 0
- [x] Deferred PaintEngine on RGB8 and RGBA8
- [x] Large RGBA8 image: text at bottom draws pixels (regression)
- [x] Large RGB8 image: text at bottom draws pixels (regression)
- [x] `lineHeight == ascender + descender`
- [x] Metrics match FastFont for same font and size
- [x] `measureText` matches FastFont

**Doctest (PaintEngine — RGBA8 blit regression):**
- [x] RGBA8 blit full image — correct pixels in/outside region
- [x] RGBA8 blit at high Y coordinate — regression for double-subtraction bug
- [x] RGBA8 blit at lower half of large image (1920x1080, destY=998)
- [x] RGBA8 blit clipped at right/bottom edge

---

## Video Codec System

**Partial progress:** `ImageCodec` / `AudioCodec` abstract base classes and `JpegImageCodec` are implemented. `VideoTestPattern` and `AudioTestPattern` have been extracted from `TestPatternNode` into standalone reusable classes. `JpegEncoderNode` now delegates to `JpegImageCodec`. See git log for details.

**Completed:**
- [x] `ImageCodec` abstract base class (`include/promeki/proav/codec.h`, `src/codec.cpp`) — name-based string registry (`registerCodec()`, `createCodec()`, `registeredCodecs()`), `encode()`/`decode()` virtuals, `canEncode()`/`canDecode()`, `lastError()`/`lastErrorMessage()`, `setError()`/`clearError()`
- [x] `AudioCodec` stub base class (same files) — name/description virtuals, future expansion
- [x] `PROMEKI_REGISTER_IMAGE_CODEC` macro for static self-registration
- [x] `JpegImageCodec` (`include/promeki/proav/jpegimagecodec.h`, `src/jpegimagecodec.cpp`) — derives from `ImageCodec`, libjpeg-turbo encode, quality 1-100 with clamping, `Subsampling` enum (444/422/420), registered as "jpeg"
- [x] `VideoTestPattern` (`include/promeki/proav/videotestpattern.h`, `src/videotestpattern.cpp`) — all 11 patterns, `create()`/`render()` dual-mode, `fromString()`/`toString()`, motion offset support
- [x] `AudioTestPattern` (`include/promeki/proav/audiotestpattern.h`, `src/audiotestpattern.cpp`) — Tone/Silence/LTC modes, `configure()`/`create()`/`render()`, `fromString()`/`toString()`
- [x] `JpegEncoderNode` refactored to delegate to `JpegImageCodec` — adds `Subsampling` config key, thread-safe stats
- [x] `TestPatternNode` refactored to delegate to `VideoTestPattern` and `AudioTestPattern`
- [x] Tests: `tests/jpegimagecodec.cpp`, `tests/videotestpattern.cpp`, `tests/audiotestpattern.cpp`, `tests/codec.cpp` (updated)

**Remaining — VideoEncoder/VideoDecoder pipeline abstraction:**

The `ImageCodec` design takes a stateful encode/decode approach (codec owns config, returns Image). A future `VideoEncoder`/`VideoDecoder` layer may be needed for temporal codecs (H.264, HEVC) that operate on streams rather than individual images. That abstraction remains unbuilt.

**Files still planned:**
- [ ] `include/promeki/proav/videoencoder.h` / `src/videoencoder.cpp`
- [ ] `include/promeki/proav/videodecoder.h` / `src/videodecoder.cpp`
- [ ] `tests/videoencoder.cpp` / `tests/videodecoder.cpp`

### VideoEncoder (still planned)

Base class for temporal/stream codecs (H.264, HEVC, etc.).

- [ ] `virtual Error configure(const ImageDesc &inputDesc)` — set up encoder for the given input format
- [ ] `virtual Result<Buffer::Ptr> encode(const Image &img)` — encode a single image, return compressed data
- [ ] `virtual void flush()` — flush buffered frames (temporal codecs only, no-op for intra-only)
- [ ] `virtual bool isConfigured() const`
- [ ] `ImageDesc inputDesc() const`
- [ ] `EncodedDesc encodedDesc() const`
- [ ] Static factory: `static VideoEncoder *create(const FourCC &codec)`
- [ ] Static registry: `registerEncoderType()` / `registeredEncoderTypes()`

### VideoDecoder (still planned)

- [ ] `virtual Error configure(const EncodedDesc &inputDesc)`
- [ ] `virtual Result<Image> decode(const Buffer &data)`
- [ ] Static factory and registry

### VideoEncoderNode / VideoDecoderNode (still planned)

Generic pipeline nodes that delegate to a VideoEncoder/VideoDecoder instance.

- [ ] `include/promeki/proav/videoencodernode.h` / `src/videoencodernode.cpp`
- [ ] `include/promeki/proav/videodecodernode.h` / `src/videodecodernode.cpp`

### JpegDecoder (still planned)

- [ ] `JpegImageCodec::decode()` — currently returns `Error::NotImplemented`. Implement using `jpeg_mem_src()`.

### Doctest (remaining)

- [ ] JpegImageCodec: decode round-trip (encode then decode, verify dimensions and pixel format match)
- [ ] VideoEncoder/VideoDecoder: once implemented

---

## Automatic Node Processing

Currently, node processing requires explicit `process()` calls. In tests this is done with manual drain loops (`while(queuedFrameCount() > 0) process()`), and in the pipeline the scheduling mechanism drives it. The goal is to make processing automatic — when a node has input data available (or is a source node that should be producing), the pipeline should schedule its `process()` without external intervention.

**This is primarily a MediaPipeline scheduling enhancement, not a node API change.**

### Current Behavior

- Source nodes and processing nodes all have `process()` as a pure virtual
- External code (tests, pipeline scheduler) must call `process()` explicitly
- No notification mechanism when input data arrives at a node

### Proposed Design

**Data-driven scheduling:** When a frame is delivered to a node's input queue (via `MediaLink::deliver()`), the node automatically becomes eligible for processing. The pipeline's scheduler picks it up and runs `process()` on the thread pool.

**Implementation checklist:**

- [ ] **MediaNode: work-available signaling**
  - [ ] Add `PROMEKI_SIGNAL(workAvailable)` — emitted when the node has work to do (input enqueued, or source node is ready to produce)
  - [ ] `MediaLink::deliver()` emits `workAvailable` on the sink node after enqueuing the frame
  - [ ] Source nodes (no inputs) emit `workAvailable` after each `process()` completes (they always have more work unless stopped)
  - [ ] `bool hasWork() const` — returns true if input queue is non-empty (processing nodes) or node is a running source

- [ ] **MediaPipeline: reactive scheduler**
  - [ ] Connect to each node's `workAvailable` signal during `start()`
  - [ ] On `workAvailable`: if the node isn't already scheduled, submit `process()` to the thread pool
  - [ ] Track per-node "scheduled" flag (atomic bool) to prevent double-scheduling
  - [ ] After `process()` completes: clear scheduled flag, re-check `hasWork()`, re-schedule if needed (handles burst delivery)
  - [ ] Source nodes: after `start()`, schedule initial `process()` to kick off the pipeline
  - [ ] Back-pressure: if downstream queue is full, `deliverOutput()` blocks (existing behavior). When it unblocks, the upstream node naturally completes its `process()` and can be re-scheduled.

- [ ] **Source node pacing**
  - [ ] Source nodes that produce as fast as possible (test pattern generators) are naturally gated by downstream back-pressure
  - [ ] Source nodes that are real-time paced (capture devices) manage their own timing and emit `workAvailable` when new data arrives from hardware
  - [ ] Add `bool isSourceNode() const` convenience — true if `inputPortCount() == 0`

- [ ] **Graceful stopping**
  - [ ] `stop()` clears all scheduled flags and drains pending thread pool tasks
  - [ ] Nodes in the middle of `process()` finish their current frame before stopping

- [ ] **Manual process() still works**
  - [ ] Direct `process()` calls continue to work for unit testing
  - [ ] Automatic scheduling is a pipeline-level feature, not baked into the node itself
  - [ ] Tests can still use manual drain loops without a pipeline

### Doctest

- [ ] Build simple source→sink pipeline, start it, verify frames flow without manual process() calls
- [ ] Fan-out: source→[A, B], verify both sinks receive frames
- [ ] Back-pressure: slow sink, verify source doesn't run ahead (queue depth stays bounded)
- [ ] Stop mid-stream: verify clean shutdown, no deadlocks
- [ ] Manual process() still works without a pipeline (unit test compatibility)

---

## Batch UDP Transmission and Kernel Packet Pacing

RtpSession currently sends packets in a loop, one `sendto()` syscall per packet. For a 1080p JPEG frame fragmented into ~60 RTP packets, that's 60 syscalls and 60 context switches per frame. At 30fps that's 1800 syscalls/sec just for video. This also means all packets in a burst hit the NIC at once, which can cause micro-bursts that overwhelm receiver buffers or network switches.

**Goal:** Hand the kernel a batch of packets at once via `sendmmsg()`, and explore options to let the kernel pace them out evenly rather than bursting — keeping as much of this out of userspace as possible.

### Batch Sending: `sendmmsg()`

Linux `sendmmsg()` (since 2.6.33) sends multiple datagrams in a single syscall. One context switch for the entire batch.

**UdpSocket additions:**

**Files:** existing `include/promeki/network/udpsocket.h`, `src/net/udpsocket.cpp`

- [ ] `struct Datagram` — describes one outgoing datagram:
  - [ ] `const void *data`
  - [ ] `size_t size`
  - [ ] `SocketAddress dest`
- [ ] `using DatagramList = List<Datagram>`
- [ ] `int writeDatagrams(const DatagramList &datagrams)` — sends all datagrams in a single `sendmmsg()` call. Returns number of datagrams sent, or -1 on error.
- [ ] Internal: builds `struct mmsghdr[]` + `struct iovec[]` arrays, calls `sendmmsg()`. Falls back to `sendto()` loop on platforms without `sendmmsg()` (compile-time `#ifdef`).
- [ ] `int writeDatagrams(const void * const *data, const size_t *sizes, int count, const SocketAddress &dest)` — convenience for sending multiple buffers to the same destination (common RTP case: many packets, one dest)

**RtpSession changes:**

- [ ] `sendPackets()` builds a `DatagramList` from the packet list, then calls `_socket->writeDatagrams()` in one shot instead of looping `writeDatagram()`
- [ ] Single-packet `sendPacket()` continues to use `writeDatagram()` (no benefit from batching one packet)

### Kernel Packet Pacing

The real win is not just batching the syscall, but having the kernel spread packets out over time so they arrive at even intervals instead of in a burst. There are several Linux mechanisms to explore:

#### Option 1: `SO_TXTIME` + ETF qdisc (Earliest TxTime First)

This is the most precise option — per-packet transmit timestamps. The kernel holds each packet until its designated time.

- [ ] **Research feasibility:** `SO_TXTIME` requires:
  - ETF qdisc configured on the interface (`tc qdisc add dev eth0 root etf clockid CLOCK_TAI delta 500000 offload`)
  - NIC hardware offload support (Intel i210, i225, or similar) for best results; software fallback works but is less precise
  - `cmsg` with `SCM_TXTIME` on each `sendmsg()` call (not compatible with `sendmmsg()` easily — each message needs its own cmsg)
- [ ] If viable: add `void setTxTime(bool enable)` to UdpSocket, and extend `Datagram` with an optional `uint64_t txTimeNs` field
- [ ] `sendmmsg()` does support per-message cmsgs via each `mmsghdr`'s `msg_hdr.msg_control` — so batch + pacing is possible, just requires per-message cmsg setup

**Verdict:** Most precise, but requires NIC support and system configuration. Good for professional AV-over-IP deployments. Worth supporting but can't be the only option.

#### Option 2: `SO_MAX_PACING_RATE` (fq qdisc)

Per-socket rate limiting. The kernel's `fq` (Fair Queue) qdisc paces packets from the socket to not exceed the specified bytes/sec.

- [ ] **Research feasibility:** requires `fq` qdisc (default on many modern kernels) — `tc qdisc show` to check
- [ ] `setsockopt(fd, SOL_SOCKET, SO_MAX_PACING_RATE, &rate, sizeof(rate))` — rate in bytes/sec
- [ ] Calculate rate from stream parameters: e.g., 1080p JPEG at 30fps with average 150KB/frame → ~4.5 MB/s → set pacing rate to ~5 MB/s with headroom
- [ ] Add `Error setPacingRate(uint64_t bytesPerSec)` to UdpSocket
- [ ] Add `Error clearPacingRate()` — remove rate limit (set to `~0U`)

**Verdict:** Simplest option. No per-packet work, no NIC requirements. Kernel does all the pacing. Slightly less precise than `SO_TXTIME` but good enough for most cases. **This should be the default/recommended approach.**

#### ~~Option 3: Userspace pacing fallback~~ (DONE)

Implemented as `RtpSession::sendPacketsPaced()` — spreads packets across a given Duration using `TimeStamp::sleepUntil()` between sends. `RtpVideoSinkNode` uses it for uncompressed video (>=32 packets per frame). Works everywhere without kernel configuration.

- [x] `RtpSession::sendPacketsPaced()` spaces out `writeDatagram()` calls using `TimeStamp::sleepUntil()`
- [x] `RtpVideoSinkNode` automatically uses paced sending for large packet counts (>=32), burst for small counts (MJPEG)
- [x] Inter-packet interval calculated as `spreadInterval / (packetCount - 1)`

#### Option 4: DPDK (Future)

DPDK bypasses the kernel network stack entirely — packets go directly from userspace to the NIC via polled-mode drivers. This eliminates all syscall overhead, all kernel qdisc pacing, and all context switches. Packet pacing is done in userspace with direct hardware timestamp injection on supported NICs.

This is the endgame for professional ST 2110 / AES67 deployments where nanosecond-level timing matters and dedicated NICs are available. It's also a fundamentally different programming model — no sockets, no file descriptors, no kernel involvement — so it requires careful abstraction to coexist with the socket-based path.

**We are NOT implementing DPDK now**, but the abstractions we build must not preclude it. See the "DPDK-Readiness Design Constraints" section below for what this means concretely.

### Recommended Implementation Order

1. **`SO_MAX_PACING_RATE`** — add `setPacingRate()` to UdpSocket. Minimal code, big impact. `RtpSession` or sink nodes set this based on stream bitrate at configure time.
2. **`sendmmsg()` batch sending** — add `writeDatagrams()` to UdpSocket, update `RtpSession::sendPackets()`. Reduces syscall overhead.
3. **`SO_TXTIME`** — add as an advanced option for deployments with hardware support. Per-packet timestamps for ST 2110-level precision.
4. ~~**Userspace fallback**~~ — **DONE.** `RtpSession::sendPacketsPaced()` with `TimeStamp::sleepUntil()`.
5. **DPDK backend** — future phase, once the transport abstraction is in place.

### RtpSession Integration

- [ ] `void setTargetBitrate(uint64_t bitsPerSec)` — used to compute pacing rate. Set automatically from stream parameters when known, or manually for custom rates.
- [ ] In `start()`: if target bitrate is set, call `_socket->setPacingRate()` with appropriate value (bitrate / 8, with headroom)
- [ ] `sendPackets()` switches to `writeDatagrams()` for batch sending

### Doctest

- [ ] `writeDatagrams()`: send batch of datagrams to loopback, receive all, verify contents and count
- [ ] `setPacingRate()`: set rate on socket, verify `getsockopt` returns expected value
- [ ] `writeDatagrams()` with single datagram: verify it still works (degenerate case)
- [ ] Performance test (optional/benchmark): compare syscall count and wall time for 100 packets via `writeDatagram()` loop vs single `writeDatagrams()` call

---

### DPDK-Readiness Design Constraints

DPDK replaces the entire kernel network path. There are no sockets, no `sendto()`, no qdiscs. Instead, you get raw `rte_mbuf` ring buffers and polled-mode NIC drivers. To support this later without rewriting the RTP/media layer, we need a transport abstraction that RtpSession and sink nodes program against, where the kernel socket path and a future DPDK path are both implementations of the same interface.

**The key insight:** RtpSession currently talks directly to `UdpSocket`. For DPDK readiness, it should instead talk to a transport interface that UdpSocket implements today and a DPDK backend can implement later.

#### PacketTransport — Abstract Packet I/O Interface

A minimal interface that covers what RtpSession actually needs from the network layer.

**Files:**
- [ ] `include/promeki/network/packettransport.h`

**Interface:**
- [ ] `class PacketTransport` — abstract base
  - [ ] `virtual Error open() = 0`
  - [ ] `virtual void close() = 0`
  - [ ] `virtual bool isOpen() const = 0`
  - [ ] `virtual ssize_t sendPacket(const void *data, size_t size, const SocketAddress &dest) = 0`
  - [ ] `virtual int sendPackets(const Datagram *datagrams, int count) = 0` — batch send, returns count sent
  - [ ] `virtual ssize_t receivePacket(void *data, size_t maxSize, SocketAddress *sender = nullptr) = 0`
  - [ ] `virtual Error setPacingRate(uint64_t bytesPerSec)` — default returns `Error::NotSupported`
  - [ ] `virtual Error setTxTime(bool enable)` — default returns `Error::NotSupported`
  - [ ] `virtual ~PacketTransport() = default`

**What this enables:**

| Backend | `sendPackets()` impl | Pacing | When |
|---|---|---|---|
| `UdpSocketTransport` | `sendmmsg()` on socket fd | `SO_MAX_PACING_RATE` / `SO_TXTIME` | Now |
| `DpdkTransport` | `rte_eth_tx_burst()` on mbuf ring | Hardware TX scheduling / userspace ring pacing | Future |
| `LoopbackTransport` | Direct memory copy to receiver | None needed | Testing |

#### UdpSocketTransport

Concrete implementation wrapping UdpSocket.

- [ ] `include/promeki/network/udpsockettransport.h`
- [ ] `src/net/udpsockettransport.cpp`
- [ ] Owns a `UdpSocket`, delegates all methods
- [ ] `sendPackets()` uses `sendmmsg()` via `UdpSocket::writeDatagrams()`
- [ ] `setPacingRate()` delegates to `UdpSocket::setPacingRate()`
- [ ] `setTxTime()` delegates to `UdpSocket::setTxTime()` (when implemented)
- [ ] Constructor takes bind address and optional multicast config

#### RtpSession Migration

- [ ] `RtpSession` takes a `PacketTransport *` instead of creating its own `UdpSocket`
  - [ ] `RtpSession(PacketTransport *transport, ObjectBase *parent = nullptr)`
  - [ ] Convenience constructor that creates a `UdpSocketTransport` internally (preserves existing usage)
- [ ] `sendPackets()` calls `_transport->sendPackets()` instead of looping `_socket->writeDatagram()`
- [ ] `start()` calls `_transport->open()` instead of socket open/bind directly

#### What DPDK Implementation Would Look Like (not built now, just validating the abstraction)

A future `DpdkTransport` would:
- Initialize a DPDK EAL and port in `open()`
- Allocate `rte_mbuf` from a mempool, fill with packet data, and burst-send via `rte_eth_tx_burst()` in `sendPackets()`
- Handle ARP/IP/UDP header construction in userspace (DPDK operates below IP)
- Implement receive via `rte_eth_rx_burst()` poll loop
- Pacing via hardware TX timestamps on NICs that support it, or token-bucket in userspace

The `PacketTransport` interface is deliberately minimal so this doesn't require heroic adaptation — it's just "send these packets, receive those packets."

#### LoopbackTransport (for testing)

- [ ] `include/promeki/network/loopbacktransport.h`
- [ ] `src/net/loopbacktransport.cpp`
- [ ] Two paired `LoopbackTransport` instances — what one sends, the other receives (via shared `Queue<Buffer>`)
- [ ] Useful for pipeline integration tests without actual network I/O
- [ ] Zero-copy: sender's buffer is directly enqueued for receiver

#### Design Constraints for All New Network Code

To keep the DPDK door open, the following rules apply to all work in this section:

- [ ] **RtpSession must not touch socket fds directly.** All packet I/O goes through `PacketTransport`.
- [ ] **Packet data must be contiguous.** DPDK mbufs expect contiguous packet data (no scatter-gather at the application level). The current `RtpPacket` already stores header+payload contiguously, so this is fine — just don't break it.
- [ ] **No kernel-specific assumptions in the media layer.** Pacing configuration goes through `PacketTransport::setPacingRate()`, not through raw `setsockopt` calls in RtpSession or sink nodes.
- [ ] **Batch-first API.** Even though `sendPacket()` exists for convenience, the primary path should be `sendPackets()` — DPDK's `rte_eth_tx_burst()` is inherently batch-oriented.
- [ ] **Buffer lifetime clarity.** `sendPackets()` must not assume the transport copies data synchronously. DPDK TX is asynchronous — the mbuf isn't released until the NIC is done. For the socket path this is a non-issue (sendmmsg copies into kernel buffers), but the interface contract should state that callers must not modify packet data until the call returns.
