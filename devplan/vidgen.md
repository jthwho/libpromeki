# vidgen — Video Test Pattern Generator Utility

**Goal:** Build a command-line utility (`vidgen`) that generates video and audio test patterns, burns in timecode, and streams via RTP as either uncompressed (ST 2110-20) or Motion JPEG. The utility serves as a testbed for the proav pipeline and network libraries.

**Dependencies:** Phase 4A (pipeline), Phase 4B (nodes), Phase 3A (sockets), Phase 3C (RTP/SDP)

---

## Work Order

The work is organized into steps that build on each other. Steps 1-2 can be done in parallel since they are independent. The library work is the primary focus — vidgen is the integration point that validates everything.

```
Step 1: Pipeline Framework (Phase 4A)          Step 2: Socket Layer (Phase 3A)
        |                                               |
        v                                               v
Step 3: Prerequisites (PaintEngine, TC Gen,    Step 5: AV-over-IP (Phase 3C)
        LTC Encoder/Decoder)                            |
        |                                               |
        v                                               |
Step 4: Pipeline Nodes (TestPattern, TC                 |
        Overlay, JPEG Encoder, FRC)                     |
        |                                               |
        +------------------+----------------------------+
                           |
                           v
                  Step 6: Streaming Nodes (Phase 4B-net)
                           |
                           v
                  Step 7: vidgen Utility
```

---

## Step 1: Pipeline Framework (Phase 4A)

Already fully specified in [proav_pipeline.md](proav_pipeline.md). Implement in this order:

1. ~~**MediaPort** — connection point data object~~ **DONE**
2. ~~**MediaNode** — base processing node (error reporting, statistics, thread-safe stats)~~ **DONE**
3. ~~**MediaLink** — buffered port-to-port connection~~ **DONE**
4. ~~**MediaGraph** — DAG of connected nodes, topology validation~~ **DONE**
5. ~~**MediaPipeline** — owns graph + thread pool, orchestrates processing (atomic state)~~ **DONE**
6. ~~**EncodedDesc** — encoded media descriptor~~ **DONE**

### MediaNode Error Reporting — **DONE**

All nodes need a uniform way to report errors during `process()`. This is part of the MediaNode base class, not a per-node concern.

**Additions to MediaNode (in proav_pipeline.md):**
- [x] `enum Severity { Info, Warning, Error, Fatal }`
- [x] `struct NodeMessage` — structured message from a node:
  - [x] `Severity severity`
  - [x] `String message`
  - [x] `uint64_t frameNumber` — which frame the error relates to (0 if not frame-specific)
  - [x] `TimeStamp timestamp`
- [x] `PROMEKI_SIGNAL(messageEmitted, NodeMessage)` — nodes emit messages via this signal. Pipeline connects to it for centralized logging/handling.
- [x] Protected: `void emitMessage(Severity severity, const String &message)` — convenience for subclasses to emit messages with auto-populated frame number and timestamp
- [x] Protected: `void emitWarning(const String &message)` — shorthand for Warning severity
- [x] Protected: `void emitError(const String &message)` — shorthand for Error severity. Also transitions node to Error state and emits `errorOccurred`.
- [ ] Fatal messages propagate to MediaPipeline, which stops the pipeline. *(Deferred — requires pipeline processing loop integration.)*

This replaces ad-hoc error handling in individual nodes. Nodes call `emitWarning("underrun")` or `emitError("socket write failed")` and the pipeline infrastructure handles routing, logging, and state transitions.

### Thread Safety — **DONE**

Thread safety hardening for concurrent pipeline operation:
- [x] `MediaPipeline::_state` is `std::atomic<State>` for safe cross-thread reads
- [x] `MediaNode` stats fields (`_processCount`, `_starvationCount`, `_lastProcessDuration`, `_avgProcessDuration`, `_peakProcessDuration`, `_peakQueueDepth`) guarded by `_statsMutex`
- [x] `Queue<T>` (used for `_inputQueue`) is already thread-safe (mutex + condition variable)
- [ ] `_outgoingLinks` in MediaNode is unprotected — safe only if graph is never mutated while running. If hot-reconfiguration is ever needed, a read-write lock is required.

### MediaNode Statistics — **DONE**

Base-level statistics common to all nodes. Avoids ad-hoc per-node counters.

**Additions to MediaNode:**
- [x] `struct NodeStats` — snapshot of node performance:
  - [x] `uint64_t processCount` — total process() invocations
  - [x] `uint64_t starvationCount` — total starvation() invocations
  - [x] `double lastProcessDuration` — wall-clock time of last process() call (seconds)
  - [x] `double avgProcessDuration` — exponential moving average of process() duration
  - [x] `double peakProcessDuration` — peak process() duration
  - [x] `int currentQueueDepth` — current input queue depth
  - [x] `int peakQueueDepth` — peak input queue depth observed
- [x] `NodeStats stats() const` — return current statistics
- [x] `void resetStats()` — reset all counters
- [x] Pipeline automatically wraps `process()` calls with timing instrumentation (via `recordProcessTiming()`)
- [x] Individual nodes can extend with node-specific stats via `virtual Map<String, Variant> extendedStats() const` — returns additional stats as key-value pairs (e.g., RtpVideoSinkNode adds "packetsSent", "bytesSent")

---

## Step 2: Socket Layer (Phase 3A) — **DONE**

Already fully specified in [network_sockets.md](network_sockets.md). For vidgen we need at minimum:

1. ~~**CMake setup** — new `promeki-network` library~~ **DONE**
2. ~~**SocketAddress** — IP + port data object~~ **DONE**
3. ~~**AbstractSocket** — IODevice-derived base~~ **DONE**
4. ~~**UdpSocket** — datagram socket with multicast support~~ **DONE**

TcpSocket, TcpServer, and RawSocket are not required for vidgen but should be implemented for library completeness since they share the AbstractSocket base. All implemented:
5. ~~**TcpSocket** — stream-oriented TCP socket~~ **DONE**
6. ~~**TcpServer** — TCP connection listener~~ **DONE**
7. ~~**RawSocket** — raw Ethernet frame socket (Linux AF_PACKET)~~ **DONE**

---

## Step 3: Prerequisites

These utility classes must be implemented before the pipeline nodes that depend on them.

### 3A. PaintEngine Completion — **DONE**

The existing PaintEngine has several drawing methods commented out that are needed by test pattern generators. These must be implemented before TestPatternNode.

**Files:** existing `include/promeki/proav/paintengine.h`, `src/paintengine.cpp`, `tests/paintengine.cpp`

**Implementation checklist (implemented as default methods in Impl base, all pixel formats inherit):**
- [x] `drawRect(const Pixel &pixel, const Rect<int32_t> &rect)` — outline rectangle
- [x] `fillRect(const Pixel &pixel, const Rect<int32_t> &rect)` — filled rectangle (critical for color bars)
- [x] `drawCircle(const Pixel &pixel, const Point2Di32 &center, int radius)` — outline circle (midpoint algorithm)
- [x] `fillCircle(const Pixel &pixel, const Point2Di32 &center, int radius)` — filled circle (scanline fill)
- [x] `drawEllipse(const Pixel &pixel, const Point2Di32 &center, const Size2Du32 &size)` — outline ellipse (midpoint algorithm)
- [x] `fillEllipse(const Pixel &pixel, const Point2Di32 &center, const Size2Du32 &size)` — filled ellipse (scanline fill)
- [x] Implemented as virtual methods in base Impl (delegates to drawPoints) — all pixel formats work automatically
- [x] Update existing doctest with tests for new methods

---

### 3B. TimecodeGenerator — **DONE**

General-purpose timecode generator. A simple, non-ObjectBase utility class that produces a sequence of timecode values with controllable direction and jam capability. Lives in `promeki-core` alongside the existing `Timecode` class.

**Files:**
- [x] `include/promeki/core/timecodegenerator.h`
- [x] `src/timecodegenerator.cpp`
- [x] `tests/timecodegenerator.cpp`

**Implementation checklist:**
- [x] Header guard, includes, namespace
- [x] Simple data object (no `PROMEKI_SHARED_FINAL` — cheap to copy, no shared state)
- [x] `enum RunMode { Still, Forward, Reverse }`
- [x] Constructor: `TimecodeGenerator()` — default (mode derived later from frame rate)
- [x] Constructor: `TimecodeGenerator(const FrameRate &frameRate, bool dropFrame = false)` — sets mode from rate

**Frame rate / mode configuration:**
- [x] `void setFrameRate(const FrameRate &frameRate)` — sets TC mode automatically from rate + dropFrame flag
- [x] `FrameRate frameRate() const`
- [x] `void setDropFrame(bool df)` — only takes effect when frame rate is 30000/1001. At all other rates, dropFrame is forced to false. Recalculates mode.
- [x] `bool dropFrame() const`
- [x] `Timecode::Mode timecodeMode() const` — the resolved mode (read-only, derived from frameRate + dropFrame)
- [x] Mode derivation rules (default is always non-drop-frame):
  - [x] 23.976/24 fps → NDF24
  - [x] 25 fps → NDF25
  - [x] 29.97 fps (30000/1001) → NDF30 (default), DF30 only if `setDropFrame(true)` was called
  - [x] 30 fps → NDF30
  - [x] Other rates → custom mode via libvtc `vtc_format_find_or_create()`

**Run control:**
- [x] `void setRunMode(RunMode mode)` — Still, Forward, or Reverse. Default: Forward.
- [x] `RunMode runMode() const`

**Timecode state:**
- [x] `void setTimecode(const Timecode &tc)` — set current timecode (initial value or jam)
- [x] `Timecode timecode() const` — current timecode value
- [x] `void jam(const Timecode &tc)` — jam to a new value (equivalent to setTimecode, but semantically distinct: use for mid-run resync). Does not change the reset value.
- [x] `Timecode advance()` — advance one frame according to runMode and return the **previous** value (the value for the current frame). Forward increments, Reverse decrements, Still returns the same value repeatedly.
- [x] `uint64_t frameCount() const` — total frames advanced (counts advance() calls)
- [x] `void reset()` — reset frameCount to 0 and timecode to the value set by setTimecode (not the jammed value)

**Doctest:**
- [x] Default construction, verify invalid/default state
- [x] Set frame rate → verify correct mode derivation for each standard rate
- [x] Forward mode: advance N frames, verify TC increments correctly (including DF skip at 29.97)
- [x] Reverse mode: advance N frames, verify TC decrements
- [x] Still mode: advance N frames, verify TC stays the same
- [x] Jam: run forward, jam to new value, verify TC continues from jammed value
- [x] Reset: verify frameCount resets and TC returns to start
- [x] Drop frame at 29.97: verify frames 0 and 1 are skipped at minute boundaries (except 10-minute marks)
- [x] Non-29.97 with dropFrame=true: verify dropFrame is forced to false (mode stays NDF)
- [x] Default at 29.97 without setDropFrame: verify NDF30 (not DF30)

---

### 3C. LtcEncoder — **DONE**

Encodes timecode values into LTC (Linear Timecode) audio samples. Wraps libvtc's `VtcLTCEncoder`. Lives in `promeki-proav` since it produces `Audio` objects (Timecode itself stays in core, but LTC encoding is an audio-domain operation).

LTC is an audio-domain encoding of SMPTE timecode using biphase-mark modulation. It's recorded on an audio track or transmitted as a line-level signal. The encoder produces audio samples that can be mixed into an audio stream or output on a dedicated channel.

**Files:**
- [x] `include/promeki/proav/ltcencoder.h`
- [x] `src/ltcencoder.cpp`
- [x] `tests/ltcencoder.cpp`

**Implementation checklist:**
- [x] Header guard, includes, namespace
- [x] Simple utility class (not ObjectBase, not shareable — owns encoder state, not copyable, movable)

**Configuration:**
- [x] Constructor: `LtcEncoder(int sampleRate, float level = 0.5f)`
- [x] `int sampleRate() const`
- [x] `void setLevel(float level)` — output amplitude 0.0–1.0
- [x] `float level() const`

**Encoding:**
- [x] `Audio encode(const Timecode &tc)` — encode one timecode frame, return Audio containing the LTC samples for that frame. Sample count varies slightly per frame (e.g., ~2002 samples at 48kHz/24fps). Output is mono.
- [ ] `Audio encode(const Timecode &tc, const AudioDesc &desc)` — encode and convert to target AudioDesc (sample format, optionally duplicate to multiple channels) *(deferred — not needed for current pipeline; TestPatternNode handles multi-channel embedding directly)*
- [x] `size_t frameSizeApprox(const VtcFormat *format) const` — approximate samples per LTC frame (for buffer pre-allocation)

**Internal:**
- [x] Wraps `VtcLTCEncoder` struct
- [x] `vtc_ltc_encoder_init()` called in constructor
- [x] `vtc_ltc_audio_encode()` called in encode()
- [ ] Converts libvtc's `int8_t` output to the target sample format (float32, int16, etc.) *(deferred — paired with multi-format encode() above)*

**Doctest:**
- [x] Encode a known timecode, verify output sample count is reasonable for the sample rate
- [x] Encode at different sample rates (48000, 96000), verify proportional sample counts
- [x] Verify output level: peak sample values should match configured level
- [x] Encode sequential timecodes, feed output to LtcDecoder, verify round-trip

---

### 3D. LtcDecoder — **DONE**

Decodes LTC audio samples back into timecode values. Wraps libvtc's `VtcLTCDecoder`. Lives in `promeki-proav` alongside LtcEncoder.

**Files:**
- [x] `include/promeki/proav/ltcdecoder.h`
- [x] `src/ltcdecoder.cpp`
- [x] `tests/ltcdecoder.cpp`

**Implementation checklist:**
- [x] Header guard, includes, namespace
- [x] Simple utility class (not ObjectBase — but uses a callback mechanism for decoded TC delivery). Not copyable, movable.

**Configuration:**
- [x] Constructor: `LtcDecoder(int sampleRate)`
- [x] `int sampleRate() const`
- [x] `void setThresholds(int8_t lower, int8_t upper)` — hysteresis thresholds for edge detection (default: ±3)
- [x] `void setFuzz(int fuzz)` — timing tolerance in samples (default: 3)

**Decoding:**
- [x] `struct DecodedTimecode` — result struct:
  - [x] `Timecode timecode` — the decoded value
  - [x] `int64_t sampleStart` — sample position where this frame began
  - [x] `int64_t sampleLength` — number of samples in this frame
  - [ ] `bool forward` — true if LTC was running forward, false if reverse *(pending: libvtc callback does not expose direction yet)*
- [x] `using DecodedList = List<DecodedTimecode>`
- [x] `DecodedList decode(const Audio &audio)` — feed audio samples, return any timecodes decoded from this chunk. May return 0, 1, or multiple results depending on how much audio is provided.
- [x] `DecodedList decode(const int8_t *samples, size_t count)` — low-level: feed raw int8_t samples directly
- [x] `void reset()` — clear decoder state (e.g., after seeking or switching sources)

**Internal:**
- [x] Wraps `VtcLTCDecoder` struct
- [x] Uses `vtc_ltc_decoder_init()` with internal static callback
- [x] Callback accumulates results into member `DecodedList` during `decode()` calls
- [ ] Converts input Audio samples to `int8_t` range before feeding to libvtc *(currently assumes int8_t input — conversion deferred until needed)*

**Doctest:**
- [x] Encode→Decode round-trip: encode a sequence of timecodes, decode the audio, verify values match
- [ ] Verify forward/reverse detection *(pending: libvtc callback does not expose direction yet)*
- [x] Verify sampleStart/sampleLength are consistent
- [ ] Feed audio in small chunks, verify decoder still finds timecodes across chunk boundaries *(incremental decoding supported by libvtc, not yet explicitly tested)*
- [x] Reset: verify decoder state is cleared

---

## Step 4: Pipeline Nodes

These nodes depend on the prerequisites from Step 3 and the pipeline framework from Step 1. They supplement the existing Phase 4B nodes in [proav_nodes.md](proav_nodes.md).

### 4A. TestPatternNode — **DONE**

Combined video + audio + metadata test pattern generator. Source node (no inputs, one Frame output). Produces complete `Frame` objects with synchronized video, audio, and timecode metadata on each process cycle.

**Downstream fan-out:** TestPatternNode outputs complete Frames. Splitting a Frame into separate Image and Audio streams for independent processing is handled by Demux nodes (future work — see Future Extensions). For the initial vidgen pipeline, the Frame flows intact to nodes that can accept Frame input, or we use a simple FrameDemuxNode (see 4E below) to split as needed.

**Metadata propagation:** Image and Audio objects each carry their own Metadata. When a Frame is split, the relevant metadata (timecode, frame number, timestamp) is copied to both the extracted Image and Audio. Nodes that operate on Image or Audio always have access to the metadata they need without depending on the parent Frame.

**Files:**
- [x] `include/promeki/proav/testpatternnode.h`
- [x] `src/testpatternnode.cpp`
- [x] `tests/testpatternnode.cpp`

**Implementation checklist:**
- [x] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [x] Constructor: creates one Frame output port (carries image + audio + metadata)

**Video pattern configuration:**
- [x] `enum Pattern { ColorBars, ColorBars75, Ramp, Grid, Crosshatch, Checkerboard, SolidColor, White, Black, Noise, ZonePlate }`
- [x] `void setPattern(Pattern p)`
- [x] `Pattern pattern() const`
- [x] `void setVideoDesc(const VideoDesc &desc)` — frame rate, resolution, pixel format
- [x] `void setSolidColor(uint16_t r, uint16_t g, uint16_t b)` — for SolidColor pattern

**Motion configuration:**
- [x] `void setMotion(double speed)` — motion speed and direction. 0.0 = static (default), positive = forward, negative = reverse. Magnitude controls speed (1.0 = one pattern-width per second).
- [x] `double motion() const`
- [x] Motion behavior per pattern (direction is pattern-specific):
  - [x] **ColorBars / ColorBars75** — horizontal scroll (positive = left, negative = right)
  - [x] **Ramp** — horizontal scroll
  - [x] **Grid / Crosshatch** — diagonal scroll
  - [x] **Checkerboard** — diagonal scroll
  - [x] **ZonePlate** — phase rotation (positive = clockwise, negative = counter-clockwise)
  - [x] **Noise** — always animated (new random values each frame regardless of motion value)
  - [x] **SolidColor / White / Black** — motion has no effect (static by nature)
- [x] Internal: `_motionOffset` accumulator (double, subpixel precision), advanced by `motion * pixelsPerFrame` each process() cycle. Wraps at pattern period for seamless looping.

**Timecode configuration (delegates to internal TimecodeGenerator):**
- [x] `TimecodeGenerator &timecodeGenerator()` — direct access to the internal generator for full control
- [x] `const TimecodeGenerator &timecodeGenerator() const`
- [x] `void setStartTimecode(const Timecode &tc)` — convenience, calls `timecodeGenerator().setTimecode(tc)`. Default: 01:00:00:00.
- [x] `void setDropFrame(bool df)` — convenience, calls `timecodeGenerator().setDropFrame(df)`. Default: false (NDF). Only takes effect at 30000/1001 frame rate.
- [x] `Timecode currentTimecode() const` — convenience, calls `timecodeGenerator().timecode()`
- [x] `uint64_t frameCount() const` — total frames generated
- [x] TimecodeGenerator's frame rate is set automatically from VideoDesc during `configure()`

**Audio configuration:**
- [x] `void setAudioDesc(const AudioDesc &desc)` — sample rate, channels, format
- [x] `void setAudioEnabled(bool enable)` — default: true
- [x] `bool audioEnabled() const`
- [x] `enum AudioMode { Tone, Silence, LTC }`
- [x] `void setAudioMode(AudioMode mode)` — default: Tone
- [x] `AudioMode audioMode() const`
- [x] `void setChannelConfig(size_t chan, AudioGen::Config config)` — per-channel signal config (Tone mode)
- [x] `void setToneFrequency(double hz)` — convenience: all channels to sine at given freq
- [x] `void setToneAmplitude(double amplitude)` — convenience: all channels
- [x] `void setLtcLevel(float level)` — LTC output amplitude 0.0–1.0 (default: 0.5)
- [x] `void setLtcChannel(int chan)` — which channel carries LTC (default: 0). Other channels are silent. Set to -1 for all channels.
- [ ] `void setSamplesPerFrame(size_t samples)` — samples per process cycle (default: computed from video frame rate and audio sample rate via `std::round(sampleRate/fps)`). *(Deferred — current auto-computation works; LTC mode ignores this since LtcEncoder determines its own sample count per frame.)*

**Lifecycle overrides:**
- [x] Override `configure()`:
  - [x] Validate VideoDesc and AudioDesc
  - [x] Set output port VideoDesc and AudioDesc
  - [x] Set TimecodeGenerator frame rate from VideoDesc
  - [x] Create internal `AudioGen` (Tone/Silence modes) or `LtcEncoder` (LTC mode) based on audioMode
  - [ ] Pre-render initial pattern into template Image (for static patterns) *(deferred — current impl renders fresh each frame; optimizable later)*
  - [x] Initialize motion offset to 0
  - [x] Compute audio samples-per-frame from video rate and audio rate (Tone/Silence modes)
- [x] Override `process()`:
  - [x] **Timecode:** Call `TimecodeGenerator::advance()` to get current frame's TC
  - [x] **Video:** Render current frame (from template + motion offset, or generate fresh for Noise/ZonePlate)
  - [x] **Audio:** Based on audioMode:
    - [x] Tone/Silence: Call `AudioGen::generate()` for this frame's sample count
    - [x] LTC: Call `LtcEncoder::encode()` with current TC, place on configured channel(s)
  - [x] **Metadata:** Stamp timecode on Image and Frame metadata
  - [x] Create `Frame::Ptr` with Image + Audio + Metadata
  - [x] Push Frame to output port via `deliverOutput()`
  - [x] Advance motion offset
  - [x] Source node pacing: gated by downstream back-pressure. Does NOT enforce real-time timing — that's the responsibility of the sink node (e.g., RtpVideoSinkNode).
- [x] Override `stop()`: reset TimecodeGenerator and motion state

**Extended stats (via `extendedStats()`):**
- [x] `"framesGenerated"` — total frames produced
- [x] `"currentTimecode"` — current TC as string

**Internal pattern generators (private methods):**
- [x] `renderPattern(double motionOffset)` — dispatches to specific generator with current offset
- [x] `renderColorBars(double offset)` — SMPTE color bars (100% or 75%), offset scrolls horizontally
- [x] `renderRamp(double offset)` — luminance gradient, offset shifts ramp start position
- [x] `renderGrid(double offset)` — white grid lines on black, offset shifts grid position
- [x] `renderCrosshatch(double offset)` — diagonal crosshatch lines, offset shifts
- [x] `renderCheckerboard(double offset)` — alternating squares, offset shifts
- [x] `renderZonePlate(double phase)` — circular zone plate, phase rotates the pattern
- [x] `renderNoise()` — random pixel values (ignores offset, always fresh)
- [x] `renderSolid()` — solid fill (ignores offset)
- [x] All generators use `PaintEngine` for drawing operations (except ZonePlate and Noise which use direct pixel access for performance)

**Doctest:**
- [x] Generate one frame of each pattern, verify image dimensions and pixel format
- [x] Generate multiple frames with motion != 0, verify images differ between frames
- [x] Verify timecode increments correctly across frames
- [x] Verify audio output sample count matches expected samples-per-frame (Tone mode)
- [x] Verify LTC mode: output audio has samples
- [ ] Verify LTC round-trip: decode TestPatternNode LTC output with LtcDecoder, verify TC values match *(encoder→decoder round-trip tested in ltcdecoder.cpp; end-to-end via TestPatternNode not yet tested)*
- [x] Verify frame count tracks correctly (via extendedStats)
- [x] Verify static patterns (Solid/White/Black) are identical frame-to-frame regardless of motion setting

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

### 4E. FrameDemuxNode — **DONE**

Splits a Frame into its constituent Image and Audio streams. Utility node (one Frame input, one Image output, one Audio output). This is the initial, minimal demux solution for vidgen. A more general Mux/Demux system (supporting arbitrary media type combinations, fan-in/fan-out, and recombination) is future work.

**Files:**
- [x] `include/promeki/proav/framedemuxnode.h`
- [x] `src/framedemuxnode.cpp`
- [x] `tests/framedemuxnode.cpp`

**Implementation checklist:**
- [x] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [x] Constructor: one Frame input port, one Image output port, one Audio output port
- [x] Override `configure()`:
  - [x] Extract ImageDesc from input Frame port's VideoDesc, set on Image output port
  - [x] Extract AudioDesc from input Frame port, set on Audio output port
- [x] Override `process()`:
  - [x] Pull Frame from input
  - [x] Extract Image from Frame, copy Frame's Metadata to Image's Metadata (timecode, frame number, timestamp)
  - [x] Extract Audio from Frame, copy Frame's Metadata to Audio's Metadata
  - [x] Push Image to image output port
  - [x] Push Audio to audio output port
  - [x] If Frame has no audio (audio disabled), only push to image output
- [x] Doctest: split a Frame, verify Image and Audio have correct data and metadata

---

## Step 5: AV-over-IP (Phase 3C)

Already specified in [network_avoverip.md](network_avoverip.md). For vidgen, implement in this order:

1. **PrioritySocket** — UDP + DSCP/QoS (simple UdpSocket subclass)
2. **RtpPacket** — lightweight buffer-view for RTP packets (shared `Buffer::Ptr` + offset/size)
3. **RtpSession** — RTP packet send/receive (RFC 3550)
4. **RtpPayload** — abstract base + concrete payload types:
   - **RtpPayloadRawVideo** — RFC 4175 uncompressed video (ST 2110-20, line-by-line packetization)
   - **RtpPayloadJpeg** — RFC 2435 JPEG (Motion JPEG)
   - **RtpPayloadL24** — 24-bit linear audio (AES67/ST 2110-30)
   - **RtpPayloadL16** — 16-bit linear audio
5. **SdpSession** — SDP generation for stream advertisement
6. **MulticastManager** — multicast group management

PtpClock is listed in the existing plan but is NOT required for initial vidgen — we can use wall-clock timestamps initially and add PTP synchronization later.

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

These already exist in the library and will be used by the new code:

| Class | Used By | Purpose |
|---|---|---|
| `AudioGen` | TestPatternNode | Sine/silence signal generation |
| `PaintEngine` | TestPatternNode, TimecodeOverlayNode | 2D drawing on images |
| `FontPainter` | TimecodeOverlayNode | FreeType text rendering |
| `Image` | TestPatternNode, TimecodeOverlayNode | Pixel buffer container |
| `Audio` | TestPatternNode | Sample buffer container |
| `Frame` | All nodes | Aggregates image + audio + metadata |
| `AudioDesc` | TestPatternNode, RtpAudioSinkNode | Audio format description |
| `VideoDesc` | TestPatternNode, RtpVideoSinkNode | Video format description |
| `ImageDesc` | TestPatternNode | Image format description |
| `PixelFormat` | TestPatternNode, JpegEncoderNode | Pixel format database |
| `Timecode` | TimecodeGenerator, TimecodeOverlayNode, LtcEncoder | Timecode via libvtc |
| `Buffer` | JpegEncoderNode, RTP nodes | Generic memory container |
| `MemSpace` | All nodes (via Buffer) | Memory allocation abstraction with statistics |
| `ImageFile` (JPEG codec) | JpegEncoderNode | libjpeg-turbo compression |
| `ThreadPool` | MediaPipeline | Worker thread management |
| `Queue` | MediaNode (input queue) | Thread-safe buffering |

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
