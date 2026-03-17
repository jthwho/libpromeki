# vidgen — Video Test Pattern Generator Utility

**Goal:** Build a command-line utility (`vidgen`) that generates video and audio test patterns, burns in timecode, and streams via RTP as either uncompressed (ST 2110-20) or Motion JPEG. The utility serves as a testbed for the proav pipeline and network libraries.

**Dependencies:** Phase 4A (pipeline), Phase 4B (nodes), Phase 3A (sockets), Phase 3C (RTP/SDP)

---

## Progress

**Completed:** Steps 1–3, 4A, 4E, 5 (pipeline framework, socket layer, prerequisites, TestPatternNode, FrameDemuxNode, AV-over-IP). See git history for details.

**Remaining work order:**
```
Step 4: Remaining Pipeline Nodes (TC Overlay, JPEG Encoder, FRC)
        |
        v
Step 6: Streaming Nodes (RtpVideoSinkNode, RtpAudioSinkNode)
        |
        v
Step 7: vidgen Utility
```

### Deferred Items from Completed Work

- **MediaNode**: Fatal messages propagating to MediaPipeline to stop the pipeline (requires pipeline processing loop integration)
- **MediaNode**: `_outgoingLinks` is unprotected — safe only if graph is never mutated while running. Read-write lock needed for hot-reconfiguration.
- **TestPatternNode**: `setSamplesPerFrame()` override (current auto-computation works; LTC mode uses LtcEncoder's own sample count)
- **TestPatternNode**: Pre-render optimization for static patterns (current impl renders fresh each frame)
- **TestPatternNode**: End-to-end LTC round-trip test via LtcDecoder (encoder→decoder tested separately in ltcdecoder.cpp)
- **LtcEncoder**: Multi-format `encode(tc, AudioDesc)` and int8_t→target format conversion
- **LtcDecoder**: `forward` field in DecodedTimecode (pending libvtc callback direction support), Audio→int8_t conversion, chunked decoding test

---

## Step 4: Remaining Pipeline Nodes

These nodes supplement the existing Phase 4B nodes in [proav_nodes.md](proav_nodes.md).

### 4B. TimecodeOverlayNode

Burns timecode text into video frames using FontPainter. Processing node (one Image input, one Image output). Reads timecode from the Image's own Metadata (not from a parent Frame — the metadata is propagated when the Frame is split).

**Files:**
- [ ] `include/promeki/proav/timecodeoverlaynode.h`
- [ ] `src/timecodeoverlaynode.cpp`
- [ ] `tests/timecodeoverlaynode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one Image input, one Image output
- [ ] `void setFontPath(const FilePath &path)` — path to .ttf file (required — no built-in default font yet)
- [ ] `void setFontSize(int points)` — default 36
- [ ] `void setPosition(int x, int y)` — text position on frame
- [ ] `enum Position { TopLeft, TopCenter, TopRight, BottomLeft, BottomCenter, BottomRight, Custom }`
- [ ] `void setPosition(Position pos)` — convenience, auto-calculates x/y based on frame size
- [ ] `void setTextColor(uint16_t r, uint16_t g, uint16_t b)` — default white
- [ ] `void setDrawBackground(bool enable)` — draw dark background behind text for legibility
- [ ] `void setCustomText(const String &text)` — additional text to render (e.g., "TEST SIGNAL")
- [ ] Override `configure()`:
  - [ ] Validate that font path is set and file exists; emit error if not
  - [ ] Initialize FontPainter with font file
  - [ ] Pass through input ImageDesc to output port
- [ ] Override `process()`:
  - [ ] Pull frame from input
  - [ ] Call `img.modify()` then `img->ensureExclusive()` to ensure exclusive buffer ownership (COW detach if shared, no-op in linear pipeline — see mutation safety convention in proav_pipeline.md)
  - [ ] Read timecode from Image's Metadata
  - [ ] Format TC as string via `Timecode::toString()`
  - [ ] Render text onto image buffer using FontPainter (safe — buffer is exclusively owned)
  - [ ] Optionally render custom text
  - [ ] Push frame to output
- [ ] Uses existing `FontPainter` and `PaintEngine` classes
- [ ] Doctest: overlay TC on test image, verify image was modified (basic pixel check)

### 4C. JpegEncoderNode

Compresses video frames to JPEG. Processing node (one Image input, one Encoded output). Uses existing libjpeg-turbo integration via ImageFile codec infrastructure.

**Files:**
- [ ] `include/promeki/proav/jpegencodernode.h`
- [ ] `src/jpegencodernode.cpp`
- [ ] `tests/jpegencodernode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one Image input, one Encoded output
- [ ] `void setQuality(int quality)` — JPEG quality 1-100, default 85
- [ ] `int quality() const`
- [ ] Override `configure()`:
  - [ ] Validate input pixel format is compatible with JPEG (RGB8, or convert)
  - [ ] Set output port EncodedDesc (codec=JPEG, sourceImageDesc from input, quality)
- [ ] Override `process()`:
  - [ ] Pull frame from input
  - [ ] Extract Image
  - [ ] Compress via libjpeg-turbo (already linked into promeki-proav)
  - [ ] Store compressed JPEG in `Buffer`
  - [ ] Create `Frame::Ptr` with compressed Buffer + original metadata (timecode, etc.)
  - [ ] Push to output
- [ ] Leverage existing ImageFile JPEG codec infrastructure
- [ ] Doctest: encode test image, verify JPEG header magic bytes, verify decompression round-trip

**Extended stats (via `extendedStats()`):**
- [ ] `"framesEncoded"` — total frames compressed
- [ ] `"avgCompressedSize"` — average JPEG output size in bytes
- [ ] `"compressionRatio"` — average ratio (uncompressed / compressed)

### 4D. FrameRateControlNode

Controls frame pacing for pipelines that don't have a timing-aware sink node. Passthrough node. Not used in the vidgen pipeline (RTP sink nodes handle timing), but useful for file-output pipelines, benchmarking, or any scenario where you need to throttle frame rate without a network sink.

**Files:**
- [ ] `include/promeki/proav/frameratecontrolnode.h`
- [ ] `src/frameratecontrolnode.cpp`
- [ ] `tests/frameratecontrolnode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one Image input, one Image output
- [ ] `void setFrameRate(const Rational &fps)` — target frame rate
- [ ] `Rational frameRate() const`
- [ ] `void setFreeRun(bool enable)` — if true, passes frames without pacing (for benchmarking)
- [ ] Override `configure()`: pass through ImageDesc, compute frame interval from rate
- [ ] Override `process()`:
  - [ ] Pull frame from input
  - [ ] Sleep/wait until next frame time (steady_clock based)
  - [ ] Push frame to output
  - [ ] Track actual vs target rate for drift detection
- [ ] Doctest: verify frame pacing within tolerance

**Extended stats (via `extendedStats()`):**
- [ ] `"framesPassed"` — total frames passed through
- [ ] `"actualFrameRate"` — measured frame rate over recent window
- [ ] `"driftMs"` — current timing drift in milliseconds

---

## Step 6: Streaming Sink Nodes (Phase 4B — network bridge)

These nodes live in promeki-proav, which depends on promeki-network for socket and RTP access.

### 6A. RtpVideoSinkNode

Sends video frames over RTP. Terminal node (one Image or Encoded input, no outputs). This is the node responsible for real-time pacing — it controls when packets are sent to maintain the correct frame rate on the wire.

**Files:**
- [ ] `include/promeki/proav/rtpvideosinknode.h`
- [ ] `src/rtpvideosinknode.cpp`
- [ ] `tests/rtpvideosinknode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one input port (Image or Encoded — configured based on upstream connection)
- [ ] `void setDestination(const SocketAddress &addr)` — unicast or multicast destination
- [ ] `void setMulticast(const SocketAddress &group)` — convenience for multicast
- [ ] `void setPayloadType(uint8_t pt)` — RTP payload type number
- [ ] `void setClockRate(uint32_t hz)` — RTP timestamp clock rate (90000 for video)
- [ ] `void setRtpPayload(RtpPayload *handler)` — payload packetizer
- [ ] `void setDscp(uint8_t dscp)` — QoS marking (default: AF41 for video)
- [ ] Override `configure()`:
  - [ ] Create RtpSession with configured parameters
  - [ ] Create PrioritySocket with DSCP setting
  - [ ] Select appropriate RtpPayload handler based on input format or explicit setting
- [ ] Override `start()`: start RtpSession
- [ ] Override `process()`:
  - [ ] Pull frame from input queue
  - [ ] Pack media data via RtpPayload handler (returns RtpPacketList — see RtpPacket below)
  - [ ] Pace transmission: send packets at the correct rate for the frame rate. This node is the real-time timing authority.
  - [ ] Send RTP packet(s) via RtpSession
  - [ ] Maintain RTP timestamp continuity
  - [ ] Check for EOS in frame metadata
  - [ ] On errors: call `emitWarning()` for transient issues (e.g., EAGAIN), `emitError()` for persistent failures (e.g., socket closed)
- [ ] Override `starvation()`: call `emitWarning("video underrun")`, optionally repeat last frame or send black
- [ ] Override `stop()`: stop RtpSession, close socket
- [ ] Doctest: send frames via loopback, verify RTP packet structure

**Extended stats (via `extendedStats()`):**
- [ ] `"packetsSent"` — total RTP packets sent
- [ ] `"bytesSent"` — total bytes sent
- [ ] `"underrunCount"` — starvation events

### 6B. RtpAudioSinkNode

Sends audio frames over RTP. Terminal node (one Audio input, no outputs). Accumulates incoming audio samples and builds RTP packets when enough samples are available for the configured packet time, independent of video frame boundaries.

**Files:**
- [ ] `include/promeki/proav/rtpaudiosinknode.h`
- [ ] `src/rtpaudiosinknode.cpp`
- [ ] `tests/rtpaudiosinknode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one Audio input port
- [ ] `void setDestination(const SocketAddress &addr)`
- [ ] `void setPayloadType(uint8_t pt)`
- [ ] `void setClockRate(uint32_t hz)` — typically sample rate (48000)
- [ ] `void setRtpPayload(RtpPayload *handler)` — default: RtpPayloadL24 or L16
- [ ] `void setPacketTime(double ptime)` — packet time in milliseconds (AES67 default: 1ms). Determines samples per packet (e.g., 1ms at 48kHz = 48 samples).
- [ ] `void setDscp(uint8_t dscp)` — default: EF (46) for audio
- [ ] Override `configure()`: create RtpSession, PrioritySocket, compute samples-per-packet from ptime and sample rate
- [ ] Override `process()`:
  - [ ] Pull audio frame from input queue
  - [ ] Append samples to internal accumulation buffer
  - [ ] While accumulation buffer has enough samples for a packet:
    - [ ] Extract samples-per-packet samples
    - [ ] Pack via RtpPayload handler
    - [ ] Send RTP packet via RtpSession
    - [ ] Pace transmission to maintain correct packet rate
  - [ ] Retain remaining samples for next process() call
  - [ ] Maintain sample-accurate RTP timestamps
  - [ ] Check for EOS in frame metadata; on EOS, flush remaining samples as a short final packet
  - [ ] On errors: call `emitWarning()` for transient issues, `emitError()` for persistent failures
- [ ] Override `starvation()`: call `emitWarning("audio underrun")`
- [ ] Override `stop()`: flush, stop session, close socket
- [ ] Doctest: send audio frames with various sizes, verify correct packet count and sample accumulation

**Extended stats (via `extendedStats()`):**
- [ ] `"packetsSent"` — total RTP packets sent
- [ ] `"samplesSent"` — total audio samples sent
- [ ] `"underrunCount"` — starvation events

---

## Step 7: vidgen Utility

Command-line utility that builds a pipeline from command-line options and runs it.

**Files:**
- [ ] `utils/vidgen/main.cpp`
- [ ] `utils/vidgen/CMakeLists.txt`
- [ ] Update top-level `CMakeLists.txt` to add `utils/vidgen/` subdirectory

**CMake:**
- [ ] `PROMEKI_BUILD_UTILS` option (default ON when both PROAV and NETWORK are enabled)
- [ ] `vidgen` executable target
- [ ] Links: `promeki-core`, `promeki-proav`, `promeki-network`

### Command-Line Interface

```
vidgen [OPTIONS]

Video Options:
  --width <W>              Frame width (default: 1920)
  --height <H>             Frame height (default: 1080)
  --framerate <R>          Frame rate as fraction or number (default: 29.97)
                           Accepts: 23.976, 24, 25, 29.97, 30, 50, 59.94, 60
                           Or fraction: 30000/1001
  --pattern <P>            Test pattern (default: colorbars)
                           Options: colorbars, colorbars75, ramp, grid, crosshatch,
                           checkerboard, black, white, noise, zoneplate
  --pixel-format <F>       Pixel format (default: rgb8)

Motion Options:
  --motion <S>             Pattern motion speed (default: 0.0 = static)
                           Positive = forward, negative = reverse
                           1.0 = one pattern-width per second

Audio Options:
  --audio-rate <R>         Sample rate in Hz (default: 48000)
  --audio-channels <N>     Number of channels (default: 2)
  --audio-tone <Hz>        Tone frequency in Hz (default: 1000)
  --audio-amplitude <A>    Amplitude 0.0-1.0 (default: 0.5)
  --audio-silence          Generate silence instead of tone
  --audio-ltc              Generate LTC (Linear Timecode) audio from the
                           timecode generator instead of tone
  --ltc-level <L>          LTC output level 0.0-1.0 (default: 0.5)
  --ltc-channel <N>        Channel for LTC (default: 0, -1 = all channels)
  --no-audio               Disable audio output entirely

Timecode Options:
  --tc-start <TC>          Starting timecode (default: 01:00:00:00)
  --tc-df                  Use drop-frame timecode (only at 30000/1001 fps,
                           ignored at other rates; default: NDF)
  --tc-burn                Burn timecode into video
  --tc-font <PATH>         Font file for TC burn (required with --tc-burn)
  --tc-size <PTS>          Font size in points (default: 36)
  --tc-position <POS>      Position: topleft, topcenter, topright,
                           bottomleft, bottomcenter, bottomright (default: bottomcenter)

Streaming Options:
  --dest <IP:PORT>         Destination address (required)
  --multicast <GROUP:PORT> Multicast group (alternative to --dest)
  --transport <T>          Transport mode (default: st2110)
                           Options: st2110, mjpeg
  --jpeg-quality <Q>       JPEG quality 1-100 for mjpeg mode (default: 85)
  --audio-dest <IP:PORT>   Audio destination (default: same as video, port+2).
                           If video port+2 exceeds 65535, wraps to port 1024
                           with a warning.
  --sdp <FILE>             Write SDP file describing the stream

General:
  --duration <SEC>         Run for N seconds (default: unlimited)
  --verbose                Print pipeline statistics
  --list-patterns          List available test patterns
  --list-formats           List available pixel formats
```

**Drop-frame behavior:** The default is always non-drop-frame. The `--tc-df` flag enables drop-frame only when the frame rate is 30000/1001 (29.97 fps). At all other frame rates, `--tc-df` is silently ignored. The `--tc-ndf` flag from earlier drafts has been removed — NDF is the default.

### Pipeline Construction

vidgen dynamically builds a `MediaPipeline` based on command-line options:

**ST 2110 (uncompressed) mode:**
```
                                         ┌──(Image)──→ [TimecodeOverlayNode] ──→ RtpVideoSinkNode (RFC 4175)
TestPatternNode ─(Frame)──→ FrameDemuxNode─┤
                                         └──(Audio)──→ RtpAudioSinkNode (L24)
```

**Motion JPEG mode:**
```
                                         ┌──(Image)──→ [TimecodeOverlayNode] ──→ JpegEncoderNode ──(Encoded)──→ RtpVideoSinkNode (RFC 2435)
TestPatternNode ─(Frame)──→ FrameDemuxNode─┤
                                         └──(Audio)──→ RtpAudioSinkNode (L24)
```

FrameDemuxNode splits the Frame into separate Image and Audio streams. Each carries its own copy of the Frame's metadata (timecode, frame number, timestamp). This explicit demux approach keeps MediaLink simple and makes the data flow visible in the graph.

`[TimecodeOverlayNode]` is inserted only when `--tc-burn` is specified. Timecode metadata is always present on Image objects regardless — the overlay just renders it visually.

### Implementation checklist:
- [ ] Parse command-line arguments (use simple argv parsing — no external deps)
- [ ] Construct appropriate nodes based on options
- [ ] Build MediaGraph, connect nodes (including FrameDemuxNode for Frame→Image/Audio split)
- [ ] Create MediaPipeline, start
- [ ] Connect to `messageEmitted` signals on all nodes for centralized logging
- [ ] Handle SIGINT/SIGTERM for clean shutdown
- [ ] Optionally write SDP file describing the output stream
- [ ] Print periodic statistics when `--verbose` is set (uses `NodeStats` from each node: frame count, actual fps, queue depths, plus `extendedStats()` for bytes sent, etc.)
- [ ] Clean shutdown: stop pipeline, close sockets, report final statistics

---

## Existing Building Blocks (no new work needed)

These already exist in the library and will be used by the remaining work:

| Class | Used By | Purpose |
|---|---|---|
| `PaintEngine` | TimecodeOverlayNode | 2D drawing on images |
| `FontPainter` | TimecodeOverlayNode | FreeType text rendering |
| `ImageFile` (JPEG codec) | JpegEncoderNode | libjpeg-turbo compression |
| `RtpSession` | RtpVideoSinkNode, RtpAudioSinkNode | RTP packet send |
| `RtpPayload*` | RtpVideoSinkNode, RtpAudioSinkNode | Payload packetization |
| `PrioritySocket` | RtpVideoSinkNode, RtpAudioSinkNode | DSCP/QoS socket |
| `SdpSession` | vidgen | SDP file generation |
| `MulticastManager` | vidgen | Multicast group management |
| `TestPatternNode` | vidgen | Video/audio/TC test source |
| `FrameDemuxNode` | vidgen | Frame→Image+Audio split |
| `ThreadPool` | MediaPipeline | Worker thread management |

---

## Testing Strategy

### Unit Tests (per-class, run during build)
Each new class gets its own doctest test file as specified in the checklists above.

### Integration Test: Pipeline Loopback
- [ ] `tests/vidgen_pipeline.cpp` — builds a minimal vidgen pipeline (test pattern → demux → TC overlay → loopback sink) and verifies frames flow correctly, TC increments, and frame count matches expected.

### Manual Verification
- Receive vidgen streams with FFmpeg: `ffplay -protocol_whitelist rtp,udp -i stream.sdp`
- Receive with VLC: `vlc stream.sdp`
- Verify ST 2110 compliance with packet analysis (Wireshark RTP dissector)

---

## Future Extensions (not in scope for initial vidgen)

These are natural next steps after vidgen is working:

- **General Mux/Demux system** — generalized Mux and Demux nodes that handle arbitrary media type fan-in/fan-out, breaking out and recombining sub-streams (e.g., extracting Image from Frame, processing it, and recombining with original Audio into a new Frame). FrameDemuxNode is the minimal initial solution; the general system replaces it.
- **JSON pipeline definition** — `MediaGraph::toJson()` / `MediaGraph::fromJson()` to define pipelines as JSON documents. The node type registry and property interface built into MediaNode (see `proav_pipeline.md` design constraint) are the foundation for this. Includes Variant conversions for AudioDesc, VideoDesc, ImageDesc, SocketAddress, etc.
- **PtpClock integration** — lock RTP timestamps to PTP grandmaster for true ST 2110 compliance
- **RtpSourceNode** — receive RTP streams into the pipeline (enables monitoring/recording)
- **SrtSocket / SrtSinkNode** — SRT streaming output
- **NDI output** — via vendor SDK wrapper
- **Video file output** — complement file-based source nodes from Phase 4B
- **Text overlay node** — general text overlay (not just timecode)
- **Multi-stream** — multiple video/audio essences from single vidgen instance
- **HTTP/WebSocket SDP server** — serve SDP via HTTP for NMOS-style discovery
- **Built-in default font** — embedded bitmap or TTF font for TimecodeOverlayNode so `--tc-font` becomes optional
