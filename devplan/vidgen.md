# vidgen — Video Test Pattern Generator Utility

**Goal:** Build a command-line utility (`vidgen`) that generates video and audio test patterns, burns in timecode, and streams via RTP as either uncompressed (ST 2110-20) or Motion JPEG. The utility serves as a testbed for the proav pipeline and network libraries.

**Dependencies:** Phase 4A (pipeline), Phase 4B (nodes), Phase 3A (sockets), Phase 3C (RTP/SDP)

---

## Progress

**vidgen is complete.** All pipeline nodes, streaming infrastructure, and the CLI utility are implemented and working.

**One optional item remains:**
- FrameRateControlNode (not required for vidgen — useful for file-output pipelines and benchmarking)

### Deferred Items from Completed Work

- **MediaNode**: Fatal messages propagating to MediaPipeline to stop the pipeline (requires pipeline processing loop integration)
- **MediaNode**: MediaSink/MediaSource connection lists are unprotected — safe only if the pipeline graph is never mutated while running. Read-write lock needed for hot-reconfiguration.
- **TestPatternNode**: `setSamplesPerFrame()` override (current auto-computation works; LTC mode uses LtcEncoder's own sample count)
- **TestPatternNode**: Pre-render optimization for static patterns (current impl renders fresh each frame)
- **TestPatternNode**: End-to-end LTC round-trip test via LtcDecoder (encoder→decoder tested separately in ltcdecoder.cpp)
- **LtcEncoder**: Multi-format `encode(tc, AudioDesc)` and int8_t→target format conversion
- **LtcDecoder**: `forward` field in DecodedTimecode (pending libvtc callback direction support), Audio→int8_t conversion, chunked decoding test
- **JpegEncoderNode**: Forces 4:2:2 subsampling for RFC 2435 type 1 compatibility. YUV input format support (YUYV, UYVY, planar 4:2:2, planar 4:2:0, NV12) is now resolved — JpegImageCodec::encode() handles all these formats and JpegEncoderNode delegates to it automatically.

---

## Step 4: Remaining Pipeline Nodes

These nodes supplement the existing Phase 4B nodes in [proav_nodes.md](proav_nodes.md).

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
- [ ] Override `build()`: pass through ImageDesc, compute frame interval from rate
- [ ] Override `processFrame()`:
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

---

## vidgen Utility — COMPLETE

Command-line utility at `utils/vidgen/main.cpp`. Builds a MediaPipeline from command-line options and runs it. Supports ST 2110 (uncompressed) and Motion JPEG transport modes, optional TC burn, audio (tone/silence/LTC), SDP output, verbose stats. See `vidgen --help` for full CLI reference.

---

## Future Extensions

These are natural next steps after vidgen is working:

- **General Mux/Demux system** — generalized Mux and Demux nodes that handle arbitrary media type fan-in/fan-out, breaking out and recombining sub-streams (e.g., extracting Image from Frame, processing it, and recombining with original Audio into a new Frame). FrameDemuxNode is the minimal initial solution; the general system replaces it.
- **JSON pipeline definition** — `MediaPipelineConfig::toJson()` / `MediaPipelineConfig::fromJson()` to define pipelines as JSON documents. The node type registry and MediaNodeConfig options map (see `proav_pipeline.md` design constraint) are the foundation for this. Includes Variant conversions for AudioDesc, VideoDesc, ImageDesc, SocketAddress, etc.
- **PtpClock integration** — lock RTP timestamps to PTP grandmaster for true ST 2110 compliance
- **RtpSourceNode** — receive RTP streams into the pipeline (enables monitoring/recording)
- **SrtSocket / SrtSinkNode** — SRT streaming output
- **NDI output** — via vendor SDK wrapper
- **Video file output** — complement file-based source nodes from Phase 4B
- **Text overlay node** — general text overlay (not just timecode)
- **Multi-stream** — multiple video/audio essences from single vidgen instance
- **HTTP/WebSocket SDP server** — serve SDP via HTTP for NMOS-style discovery
- ~~**Built-in default font**~~ — done: FiraCode bundled, `--tc-font` is optional
