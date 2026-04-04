# vidgen — Video Test Pattern Generator Utility

**Goal:** Build a command-line utility (`vidgen`) that generates video and audio test patterns, burns in timecode, and streams via RTP as either uncompressed (ST 2110-20) or Motion JPEG. The utility serves as a testbed for the proav pipeline and network libraries.

**Dependencies:** Phase 4A (pipeline), Phase 4B (nodes), Phase 3A (sockets), Phase 3C (RTP/SDP)

---

## Progress

**Completed:** Steps 1–3, 4A, 4B, 4C, 4E, 5, 6, 7 (pipeline framework, socket layer, prerequisites, TestPatternNode, TimecodeOverlayNode, JpegEncoderNode, FrameDemuxNode, AV-over-IP, RTP streaming sink nodes, vidgen utility). Pipeline refactoring to MediaSink/MediaSource/MediaNodeConfig/MediaPipelineConfig model also complete. See git history for details.

**Remaining work order:**
```
Step 4D: FrameRateControlNode (not required for vidgen — optional)
```

### Deferred Items from Completed Work

- **MediaNode**: Fatal messages propagating to MediaPipeline to stop the pipeline (requires pipeline processing loop integration)
- **MediaNode**: MediaSink/MediaSource connection lists are unprotected — safe only if the pipeline graph is never mutated while running. Read-write lock needed for hot-reconfiguration.
- **TestPatternNode**: `setSamplesPerFrame()` override (current auto-computation works; LTC mode uses LtcEncoder's own sample count)
- **TestPatternNode**: Pre-render optimization for static patterns (current impl renders fresh each frame)
- **TestPatternNode**: End-to-end LTC round-trip test via LtcDecoder (encoder→decoder tested separately in ltcdecoder.cpp)
- **AudioGen**: `_sampleCount` bug fixed (was not incrementing between `generate()` calls, breaking multi-chunk continuity)
- **LtcEncoder**: Multi-format `encode(tc, AudioDesc)` and int8_t→target format conversion
- **LtcDecoder**: `forward` field in DecodedTimecode (pending libvtc callback direction support), Audio→int8_t conversion, chunked decoding test
- **JpegEncoderNode**: YUV input format support (currently only RGB8/RGBA8); decompression round-trip test not included (would need JpegDecoderNode or ImageFile). Now forces 4:2:2 subsampling for RFC 2435 type 1 compatibility.

---

## Step 4: Remaining Pipeline Nodes

These nodes supplement the existing Phase 4B nodes in [proav_nodes.md](proav_nodes.md).

### ~~4B. TimecodeOverlayNode~~ (DONE)

Burns timecode text into video frames using FontPainter. Processing node (one Image input, one Image output). Reads timecode from the Image's own Metadata (not from a parent Frame — the metadata is propagated when the Frame is split).

**Implementation notes:**
- Position enum with 6 presets plus Custom, auto-calculates x/y based on frame size with margin = fontSize/2
- Configurable font path, size, text color (16-bit RGB), background toggle, custom text
- COW-safe: calls `img.modify()` then `img->ensureExclusive()` before rendering
- Reads timecode from Image's Metadata, falls back to "--:--:--:--" placeholder
- Uses PaintEngine + FontPainter for text rendering with optional dark background
- Tests: 16 test cases covering all positions, pixel modification, metadata preservation, RGBA8, custom text, background toggle, multi-frame, registry

### ~~4C. JpegEncoderNode~~ (DONE)

Compresses video frames to JPEG. Processing node (one Image input, one Image output with JPEG pixel format). Uses libjpeg-turbo directly via `jpeg_mem_dest` for in-memory compression.

**Implementation notes:**
- Output uses Image with compressed pixel format (e.g. JPEG_RGB8) rather than separate Encoded type — compressed JPEG data lives in the Image's plane buffer, exposed via `Image::isCompressed()` / `Image::compressedSize()` / `Image::fromCompressedData()`
- Added `Image::isCompressed()`, `Image::compressedSize()`, `Image::fromCompressedData()` to support compressed image representation
- Renamed `PixelFormat::compressed()` → `PixelFormat::isCompressed()` for consistency
- Fixed JPEG `__planeSize()` to return 0 when `Metadata::CompressedSize` is absent (was undefined behavior)
- Custom libjpeg error handler using `longjmp` instead of `exit()`
- Maps RGB8→JPEG_RGB8, RGBA8→JPEG_RGBA8 automatically
- Tests verify JPEG SOI/EOI markers, metadata propagation, quality-affects-size, multi-frame encoding, empty frame passthrough, extended stats, and node registry

### 4D. FrameRateControlNode

Controls frame pacing for pipelines that don't have a timing-aware sink node. Passthrough node. Not used in the vidgen pipeline (RTP sink nodes handle timing), but useful for file-output pipelines, benchmarking, or any scenario where you need to throttle frame rate without a network sink.

**Files:**
- [ ] `include/promeki/frameratecontrolnode.h`
- [ ] `src/proav/frameratecontrolnode.cpp`
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
- [ ] `"FramesPassed"` — total frames passed through
- [ ] `"ActualFrameRate"` — measured frame rate over recent window
- [ ] `"DriftMs"` — current timing drift in milliseconds

---

## ~~Step 6: Streaming Sink Nodes (Phase 4B — network bridge)~~ (DONE)

These nodes live in the `promeki` library (proav and network sources). RTP sink nodes are conditionally compiled when `PROMEKI_ENABLE_NETWORK` and `PROMEKI_ENABLE_PROAV` are both enabled.

### ~~6A. RtpVideoSinkNode~~ (DONE)

Terminal sink node (one Image input, no outputs). Real-time pacing authority — sleeps between frames using `steady_clock` + `sleep_until` to maintain target frame rate without drift accumulation. Uses `RtpSession` directly (not PrioritySocket) with DSCP set via `UdpSocket::setDscp()`.

**Implementation notes:**
- Pacing: first frame sets `_nextFrameTime = now()`, subsequent frames advance by `_frameInterval` then `sleep_until`
- RTP timestamp increment = `clockRate * fps.denominator() / fps.numerator()`
- Supports both compressed (`compressedSize()`) and uncompressed (`lineStride() * height()`) images
- Multicast join on start if `setMulticast()` was called
- Default DSCP 34 (AF41, broadcast video), default PT 96, default clock rate 90000
- Tests: 13 tests — construction, registry, port structure, configure failures (no payload/dest/framerate), configure success, start/stop lifecycle, loopback send with RTP header verification, timestamp continuity across frames, starvation counter, extended stats
- Also added `Metadata::EndOfStream` (bool) to the metadata system for EOS signaling

### ~~6B. RtpAudioSinkNode~~ (DONE)

Terminal sink node (one Audio input, no outputs). Accumulates samples in a dynamically-sized buffer and emits RTP packets when enough data fills a packet. No self-pacing — relies on video sink for pipeline timing.

**Implementation notes:**
- Accumulation buffer starts at 4x packet size, grows dynamically if needed
- `_samplesPerPacket = packetTime * 0.001 * clockRate` (e.g., 1ms at 48kHz = 48 samples)
- `_bytesPerSampleFrame` from `AudioDesc::bytesPerSampleStride()`, with lazy init from first audio frame if port descriptor not set
- Cross-boundary handling: `memmove` shifts remaining data after each packet send
- EOS flush: on `Metadata::EndOfStream`, flushes partial accumulation as a short final packet
- Default DSCP 46 (EF, real-time audio), default PT 97, default clock rate 48000, default ptime 1.0ms
- Tests: 12 tests — construction, registry, port structure, configure failures (no payload/dest), configure success, start/stop lifecycle, sub-packet accumulation (no send), full packet send via loopback, cross-boundary (1.5x), timestamp tracking, starvation counter, extended stats

**CMake changes:**
- All network, proav, and RTP sources are part of the single `promeki` library (consolidated from separate promeki-core, promeki-proav, promeki-network libraries)
- RTP sink node sources are conditionally compiled when both `PROMEKI_ENABLE_NETWORK` and `PROMEKI_ENABLE_PROAV` are ON
- `PROMEKI_HAVE_NETWORK` define removed; code that was guarded by it is now compiled unconditionally

---

## ~~Step 7: vidgen Utility~~ (DONE)

Command-line utility that builds a pipeline from command-line options and runs it.

**Files:**
- [x] `utils/vidgen/main.cpp`
- [x] `utils/vidgen/CMakeLists.txt`
- [x] Update `utils/CMakeLists.txt` to add `vidgen/` subdirectory (conditional on `PROMEKI_ENABLE_NETWORK`)

**CMake:**
- [x] `PROMEKI_BUILD_UTILS` option (already existed, default ON)
- [x] `vidgen` executable target
- [x] Links: `promeki::promeki`

**Implementation notes:**
- Single-file implementation (~740 lines) with simple argv parsing
- Processing loop drives graph manually in topological order on the main thread; RtpVideoSinkNode provides real-time pacing via sleep_until
- Pipeline construction is dynamic: TimecodeOverlayNode inserted only with `--tc-burn`, JpegEncoderNode only with `--transport mjpeg`, audio nodes only without `--no-audio`
- RTP payload handlers are stack-allocated in main() and outlive the pipeline
- SDP generation uses SdpSession with correct media descriptions for both ST 2110 and MJPEG modes
- Default bundled font (FiraCode) used for TC burn when `--tc-font` not specified
- Verbose mode prints stats every 5 seconds: frame count, video/audio packets sent, bytes sent

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
  --audio-level <dBFS>     Tone level in dBFS (default: -30)
  --audio-silence          Generate silence instead of tone
  --audio-ltc              Generate LTC (Linear Timecode) audio from the
                           timecode generator instead of tone
  --ltc-level <dBFS>       LTC output level in dBFS (default: -20)
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
- [x] Parse command-line arguments (simple argv parsing — no external deps)
- [x] Construct appropriate nodes based on options
- [x] Build MediaGraph, connect nodes (including FrameDemuxNode for Frame→Image/Audio split)
- [x] Create MediaPipeline, start
- [x] Connect to `messageEmitted` signals on all nodes for centralized logging
- [x] Handle SIGINT/SIGTERM for clean shutdown
- [x] Optionally write SDP file describing the output stream
- [x] Print periodic statistics when `--verbose` is set (every 5s: frame count, video/audio packets sent, bytes sent)
- [x] Clean shutdown: stop pipeline, report final statistics

---

## Existing Building Blocks (no new work needed)

These already exist in the library and will be used by the remaining work:

| Class | Used By | Purpose |
|---|---|---|
| `PaintEngine` | TimecodeOverlayNode | 2D drawing on images |
| `FontPainter` | TimecodeOverlayNode | FreeType text rendering |
| `Image` (compressed support) | JpegEncoderNode | `isCompressed()`, `compressedSize()`, `fromCompressedData()` |
| `RtpSession` | RtpVideoSinkNode, RtpAudioSinkNode | RTP packet send |
| `RtpPayload*` | RtpVideoSinkNode, RtpAudioSinkNode | Payload packetization |
| `UdpSocket` | RtpVideoSinkNode, RtpAudioSinkNode | UDP transport with DSCP via `setDscp()` |
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
- [ ] `tests/vidgen_pipeline.cpp` — builds a minimal vidgen pipeline (test pattern → demux → TC overlay → loopback sink) and verifies frames flow correctly, TC increments, and frame count matches expected. (Deferred — testrender utility already validates this pattern; vidgen smoke-tested with loopback streaming.)

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
- ~~**Built-in default font**~~ — done: FiraCode bundled, `--tc-font` is optional
