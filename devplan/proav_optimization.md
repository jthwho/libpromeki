# ProAV Optimization and Cleanup

**Phase:** 4D
**Dependencies:** Phase 4A (MediaNode, MediaPipeline), Phase 4B (concrete nodes), Phase 3A (sockets)
**Library:** `promeki-proav`, `promeki-network`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

This document covers four areas of improvement: high-performance font rendering, a proper video codec abstraction, automatic node processing in the pipeline, and batch UDP transmission with kernel-side packet pacing.

---

## BitmapFont — Fast Native-Format Font Rendering

FontPainter currently renders text by loading each glyph through FreeType on every `drawText()` call, extracting per-pixel alpha values, and compositing them via `PaintEngine::compositePoints()`. This works but is slow — every frame re-rasterizes glyphs and does per-pixel alpha blending through the paint engine abstraction.

**Goal:** A new `BitmapFont` class that pre-renders glyphs as bitmaps in the target pixel format at construction time. Drawing a glyph becomes a simple `memcpy` (or small set of memcpys for the glyph rows) directly into the image buffer — no FreeType calls, no alpha blending, no paint engine indirection per frame.

**Files:**
- [ ] `include/promeki/proav/bitmapfont.h`
- [ ] `src/bitmapfont.cpp`
- [ ] `tests/bitmapfont.cpp`

**Design:**

The key insight is that for overlay text (timecodes, labels, burn-ins), the font, size, and colors are known at configure time and don't change frame-to-frame. We can pre-render every needed glyph into the native pixel format once, then blit them with memcpy at process time.

**Implementation checklist:**
- [ ] `BitmapFont(const FilePath &fontPath, int pixelSize, const PixelFormat &pf)` — construct with font file, size, and target pixel format
- [ ] `Error prepare(const String &charset)` — pre-render all glyphs in the given character set. Default charset covers ASCII printable (0x20-0x7E) plus common timecode characters (`:`, `;`, `.`). Uses FreeType to rasterize each glyph, then converts the alpha bitmap into the target pixel format with foreground/background colors baked in.
- [ ] `void setForegroundColor(const Color &fg)` — glyph foreground color (must call `prepare()` again after changing)
- [ ] `void setBackgroundColor(const Color &bg)` — glyph background color. If set, glyphs are rendered with an opaque background (no alpha blending needed at draw time). If not set, background pixels are left transparent (requires compositing at draw time — slower path).
- [ ] `Color foregroundColor() const`
- [ ] `Color backgroundColor() const`
- [ ] `int pixelSize() const`
- [ ] `PixelFormat pixelFormat() const`
- [ ] `bool isPrepared() const`

**Glyph storage:**
- [ ] Internal `Map<uint32_t, GlyphBitmap>` keyed by Unicode codepoint
- [ ] `struct GlyphBitmap` — pre-rendered glyph data:
  - [ ] `Buffer data` — pixel data in target format, row-major
  - [ ] `int width` — glyph bitmap width in pixels
  - [ ] `int height` — glyph bitmap height in pixels
  - [ ] `int bearingX` — horizontal offset from pen position
  - [ ] `int bearingY` — vertical offset from baseline
  - [ ] `int advance` — horizontal advance to next glyph position
  - [ ] `int bytesPerRow` — stride of the glyph bitmap

**Drawing API:**
- [ ] `void drawText(uint8_t *dst, int dstStride, int x, int y, const String &text) const` — blit pre-rendered glyphs directly into a raw image buffer. Each glyph row is a memcpy from the GlyphBitmap into the destination. No PaintEngine needed.
- [ ] `void drawText(Image &img, int x, int y, const String &text) const` — convenience that extracts the plane pointer and stride from the Image
- [ ] `Size2Di32 measureText(const String &text) const` — compute bounding box without drawing (sum of advances + max ascent/descent)
- [ ] `int lineHeight() const` — total line height (ascent + descent + leading)
- [ ] `int ascent() const`
- [ ] `int descent() const`

**Performance characteristics:**
- `prepare()` is slow (FreeType rasterization + format conversion) — called once at configure time
- `drawText()` is fast — one memcpy per glyph row, no branches, no alpha math when background color is set
- For opaque-background mode: each glyph is fully pre-composited, draw is pure memcpy
- For transparent-background mode: falls back to a simple per-pixel copy-if-nonzero loop (still much faster than FreeType re-rasterization)
- Memory cost: ~(charsetSize * pixelSize^2 * bytesPerPixel) — negligible for typical use

**Migration:**
- [ ] Update `TimecodeOverlayNode` to use `BitmapFont` instead of `FontPainter` — create `BitmapFont` in `configure()` with the image's pixel format, then use `drawText()` directly in `process()`
- [ ] `FontPainter` remains available for dynamic / one-shot text rendering where pre-rendering doesn't make sense

**Doctest:**
- [ ] Construct BitmapFont, prepare ASCII charset
- [ ] Measure text, verify dimensions are reasonable
- [ ] Draw text into a test image, verify pixels are non-zero in expected region
- [ ] Verify opaque background mode produces fully opaque glyph regions
- [ ] Round-trip: same text drawn twice at same position produces identical pixels

---

## Video Codec System

JpegEncoderNode currently embeds all JPEG compression logic (libjpeg-turbo setup, subsampling configuration, memory destination, error handling) directly in its `process()` method. This works for the single JPEG case, but as we add more codecs (H.264, HEVC, etc.) this pattern doesn't scale. The codec logic should be factored out into a proper abstraction.

**Goal:** A `VideoEncoder` / `VideoDecoder` abstraction that encapsulates codec configuration and encode/decode operations. Codec nodes become thin wrappers that pull frames, hand them to the codec, and deliver the result.

**Files:**
- [ ] `include/promeki/proav/videoencoder.h`
- [ ] `src/videoencoder.cpp`
- [ ] `include/promeki/proav/videodecoder.h`
- [ ] `src/videodecoder.cpp`
- [ ] `include/promeki/proav/jpegcodec.h`
- [ ] `src/jpegcodec.cpp`
- [ ] `tests/videoencoder.cpp`
- [ ] `tests/videodecoder.cpp`
- [ ] `tests/jpegcodec.cpp`

### VideoEncoder

Base class for all video encoders.

- [ ] `enum Codec { JPEG, H264, HEVC }` — or use FourCC for open-ended codec identification
- [ ] `virtual Error configure(const ImageDesc &inputDesc)` — set up encoder for the given input format
- [ ] `virtual Result<Buffer::Ptr> encode(const Image &img)` — encode a single image, return compressed data
- [ ] `virtual void flush()` — flush any buffered frames (relevant for temporal codecs, no-op for JPEG)
- [ ] `virtual bool isConfigured() const`
- [ ] `ImageDesc inputDesc() const`
- [ ] `EncodedDesc encodedDesc() const` — describes the output (codec FourCC, quality, etc.)
- [ ] Static factory: `static VideoEncoder *create(const FourCC &codec)` — instantiate encoder by codec ID
- [ ] Static registry following the same pattern as MediaNode: `registerEncoderType()` / `registeredEncoderTypes()`

### VideoDecoder

Base class for all video decoders.

- [ ] `virtual Error configure(const EncodedDesc &inputDesc)` — set up decoder for the given compressed format
- [ ] `virtual Result<Image> decode(const Buffer &data)` — decode compressed data, return image
- [ ] `virtual bool isConfigured() const`
- [ ] `EncodedDesc inputDesc() const`
- [ ] `ImageDesc outputDesc() const`
- [ ] Static factory and registry (same pattern as VideoEncoder)

### JpegCodec

Concrete JPEG encoder and decoder using libjpeg-turbo.

**JpegEncoder (derives from VideoEncoder):**
- [ ] `void setQuality(int quality)` — JPEG quality 1-100 (default: 80)
- [ ] `int quality() const`
- [ ] `void setSubsampling(enum { Sub444, Sub422, Sub420 })` — chroma subsampling (default: Sub422 for RTP compat)
- [ ] Move existing libjpeg-turbo setup, error handling, and compression logic from `JpegEncoderNode::process()` into `encode()`
- [ ] `encode()` returns a `Buffer::Ptr` containing the JPEG bitstream

**JpegDecoder (derives from VideoDecoder):**
- [ ] `decode()` decompresses JPEG data into an Image
- [ ] Uses `jpeg_mem_src()` for in-memory input

### VideoEncoderNode / VideoDecoderNode

Generic pipeline nodes that delegate to a VideoEncoder/VideoDecoder instance.

- [ ] `include/promeki/proav/videoencodernode.h` / `src/videoencodernode.cpp`
- [ ] `include/promeki/proav/videodecodernode.h` / `src/videodecodernode.cpp`
- [ ] `VideoEncoderNode` takes an image input, encodes via its `VideoEncoder`, outputs encoded data
- [ ] `VideoDecoderNode` takes encoded input, decodes via its `VideoDecoder`, outputs image
- [ ] Configurable: `setEncoder(VideoEncoder *)` or construct with codec FourCC
- [ ] Property interface exposes codec-specific settings

### Migration

- [ ] Refactor `JpegEncoderNode` to use `JpegEncoder` internally (or replace with `VideoEncoderNode` + `JpegEncoder`)
- [ ] Existing tests and vidgen usage should continue to work unchanged
- [ ] `JpegEncoderNode` can remain as a convenience alias/typedef if desired

### Doctest

- [ ] JpegEncoder: configure, encode test image, verify output is valid JPEG
- [ ] JpegDecoder: decode JPEG data, verify image dimensions and pixel format
- [ ] Round-trip: encode then decode, verify image is visually equivalent (within JPEG loss)
- [ ] VideoEncoderNode: end-to-end pipeline test with JPEG encoder
- [ ] Codec registry: register, create by FourCC, list registered codecs

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

#### Option 3: Userspace pacing fallback

For platforms without kernel pacing support, or for fine-grained control.

- [ ] `RtpPacer` utility class that spaces out `sendto()` calls using `clock_nanosleep()` or busy-wait between packets
- [ ] Calculates inter-packet gap from target bitrate: `gap_ns = (packet_size * 8 * 1e9) / bitrate_bps`
- [ ] Less desirable — keeps a thread busy and is subject to scheduling jitter — but works everywhere
- [ ] Only implement this if kernel options prove insufficient

#### Option 4: DPDK (Future)

DPDK bypasses the kernel network stack entirely — packets go directly from userspace to the NIC via polled-mode drivers. This eliminates all syscall overhead, all kernel qdisc pacing, and all context switches. Packet pacing is done in userspace with direct hardware timestamp injection on supported NICs.

This is the endgame for professional ST 2110 / AES67 deployments where nanosecond-level timing matters and dedicated NICs are available. It's also a fundamentally different programming model — no sockets, no file descriptors, no kernel involvement — so it requires careful abstraction to coexist with the socket-based path.

**We are NOT implementing DPDK now**, but the abstractions we build must not preclude it. See the "DPDK-Readiness Design Constraints" section below for what this means concretely.

### Recommended Implementation Order

1. **`SO_MAX_PACING_RATE`** — add `setPacingRate()` to UdpSocket. Minimal code, big impact. `RtpSession` or sink nodes set this based on stream bitrate at configure time.
2. **`sendmmsg()` batch sending** — add `writeDatagrams()` to UdpSocket, update `RtpSession::sendPackets()`. Reduces syscall overhead.
3. **`SO_TXTIME`** — add as an advanced option for deployments with hardware support. Per-packet timestamps for ST 2110-level precision.
4. **Userspace fallback** — only if needed.
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
