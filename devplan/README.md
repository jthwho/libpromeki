# libpromeki Development Plan

## Overview

This plan builds out the consolidated `promeki` library (core, network, proav, music) and the separate `promeki-tui` and `promeki-sdl` UI libraries toward completeness. Work is organized into 7 phases ordered by dependency.

**Maintenance note:** Completed items are removed from individual phase documents once merged, unless they provide context needed by a future phase in the same document. The code and git history are the source of truth for completed work. Phase summaries in this README note what's complete at a high level.

## Documents

| Document | Phase | Description |
|---|---|---|
| [core_utilities.md](core_utilities.md) | 7 | Enhanced existing classes (Variant, String, RegEx, Result adoption) |
| [core_io.md](core_io.md) | 2 | IODevice, BufferedIODevice, FilePath, Dir, Process |
| [core_streams.md](core_streams.md) | 2, 7 | DataStream, TextStream type extensions, ObjectBase saveState/loadState |
| [network_sockets.md](network_sockets.md) | 3A | Socket layer (Abstract, TCP, UDP, Raw, TLS) |
| [network_protocols.md](network_protocols.md) | 3B | HTTP, WebSocket, higher-level protocols |
| [network_avoverip.md](network_avoverip.md) | 3C | AV-over-IP building blocks (RTP, PTP, SDP, multicast) |
| [proav_pipeline.md](proav_pipeline.md) | 4A | Pipeline framework core (MediaNode, MediaPipeline) |
| [proav_nodes.md](proav_nodes.md) | 4B | Concrete nodes (source/sink/mixer/gain/sync) |
| [proav_dsp.md](proav_dsp.md) | 4C | DSP and effects (filters, resampler, format converter) |
| [proav_optimization.md](proav_optimization.md) | 4D | Optimization and cleanup (auto processing, batch UDP, PacketTransport) |
| [vidgen.md](vidgen.md) | 3-4 | vidgen utility: test pattern nodes, streaming nodes, CLI tool |
| [tui.md](tui.md) | 5 | TUI widget completion |
| [music_theory.md](music_theory.md) | 6A, 6B | Core music theory objects |
| [music_midi.md](music_midi.md) | 6C, 6D | MIDI I/O and arrangement |
| [fixme.md](fixme.md) | ongoing | Existing FIXME comments to address during related phase work |

## Dependency Graph

```
Phase 1 (COMPLETE) ----+---> Phase 2 (COMPLETE)
                       |       |
                       |       +---> Phase 3A (COMPLETE) ──→ Phase 3C (COMPLETE) ───┐
                       |       |                                                     |
                       |       +---> Phase 4A (mostly done) ──→ Phase 4B (in progress) ──→ vidgen (COMPLETE)
                       |       |                                  |                  |
                       |       |                                  +──→ Phase 4D (Optimization/Cleanup)
                       |       |                                                     |
                       |       |                             Phase 3B (HTTP/TLS) ────┘
                       |       |
                       |       +---> Phase 7 (ObjectBase saveState/loadState via DataStream)
                       |
                       +---> Phase 5 (TUI Widgets) [mostly independent]
                       |
                       +---> Phase 6 (Music Library) [mostly independent]

Phase 7 (Cross-Cutting) -- ongoing throughout
```

### Current Focus: Optimization and Next Steps

The `vidgen` utility (video/audio test pattern generator streaming via RTP) is complete. See [vidgen.md](vidgen.md) for details and deferred items.

**Next priorities:**
1. **Phase 4D** — Remaining optimization: automatic node processing, batch UDP/kernel pacing (`sendmmsg`, `SO_MAX_PACING_RATE`), VideoEncoder/VideoDecoder pipeline abstraction, PacketTransport abstraction. Font rendering and codec system are done. See [proav_optimization.md](proav_optimization.md).
2. **Remaining Phase 4A** — Audio::ensureExclusive()/isExclusive(), MemSpace::Stats, MemSpacePool. See [proav_pipeline.md](proav_pipeline.md).
3. **Phase 4B** — File-based source/sink nodes, mixer, gain, color space conversion. See [proav_nodes.md](proav_nodes.md).

## Phasing

### Phase 1: Core Containers, Concurrency, and Utilities — COMPLETE

Phase 1A (containers), 1B (concurrency), 1C (API consistency), and 1D (utilities) are all done. Delivered: List, Map, Set, HashMap, HashSet, Deque, Stack, PriorityQueue, Span, Mutex, ReadWriteLock, WaitCondition, Atomic, Future, Promise, ThreadPool, Queue, Random, ElapsedTimer, Duration, Algorithm, Result, Pair. `Result<T>` adoption across the codebase is tracked in each phase's document where the classes are defined.

### Phase 2: IO Abstractions, Filesystem, and Streams — COMPLETE
**Prerequisites:** Phase 1 (complete)
**Documents:** `core_io.md`, `core_streams.md`

IO abstractions, filesystem utilities, DataStream, and TextStream all implemented. StreamString migrated to use TextStream/IODevice (no remaining std:: stream usage in the library). **Resource filesystem (cirf) integrated:** `File`, `FileIODevice`, and `Dir` transparently accept `:/...` resource paths served from the compiled-in cirf resource set; new `Resource` class wraps the cirf runtime mount API; libpromeki ships `:/.PROMEKI/` built-in resources (bundled FiraCode font). See `core_io.md` and `core_streams.md` for remaining extension work (TextStream/DataStream type operator extensions, ObjectBase serialization).

### Phase 3: Network Library — IN PROGRESS
**Prerequisites:** Phase 1 (complete), Phase 2 (IODevice)
**Documents:** `network_sockets.md`, `network_protocols.md`, `network_avoverip.md`

Network sources in the `promeki` library, controlled by `PROMEKI_ENABLE_NETWORK` feature flag. Raw POSIX sockets, vendored mbedTLS for TLS. Work through documents in order: sockets first, then protocols, then AV-over-IP.

**Phase 3A (Sockets) COMPLETE.** Phase 3B (HTTP/TLS) not started. **Phase 3C (AV-over-IP) COMPLETE** — PrioritySocket, RtpSession (including `sendPacketsPaced()` for ST 2110-21 pacing), RtpPacket, RtpPayload (L24, L16, RawVideo, JPEG with RFC 2435 DQT/entropy parsing), SdpSession (with insertion-order-preserving attributes), MulticastManager. PtpClock remaining.

### Phase 4: ProAV Pipeline Framework — IN PROGRESS
**Prerequisites:** Phase 1 (complete), Phase 2 (IODevice)
**Documents:** `proav_pipeline.md`, `proav_nodes.md`, `proav_dsp.md`, `proav_optimization.md`

Generalizes the existing source/sink pattern. Work through documents in order: pipeline core, then concrete nodes, then DSP.

**Phase 4A mostly complete** — MediaSink, MediaSource, MediaNodeConfig, MediaPipelineConfig, MediaNode, MediaPipeline, EncodedDesc, Image::ensureExclusive()/isExclusive(), Audio::convertTo() all done. Remaining: Audio::ensureExclusive()/isExclusive(), MemSpace::Stats, MemSpacePool. **Phase 4B in progress** — MediaIO framework completely overhauled to controller+task split; QuickTime/ISO-BMFF backend (MediaIOTask_QuickTime) complete — classic + fragmented reader/writer for .mov/.mp4, AudioBuffer FIFO, BufferPool, ProRes/H264/HEVC PixelDesc entries, Audio/Image zero-copy factories, File DIO write path (writeBulk/sync/writev/preallocate); see proav_nodes.md for full design notes. MediaIO framework completely overhauled to controller+task split: `MediaIO` (ObjectBase controller, not subclassed by backends) + `MediaIOTask` (backend interface, private virtuals, friended by MediaIO); all I/O dispatched as `MediaIOCommand` subclasses (Open/Close/Read/Write/Seek/Params/Stats) shared via `SharedPtr<MediaIOCommand, false>`; `PROMEKI_MEDIAIO_COMMAND` macro for boilerplate injection; `Strand` class added (serialized executor on ThreadPool: serial order, pool threads returned between tasks, cancelPending, waitForIdle); each MediaIO instance has its own Strand on a shared static ThreadPool; three distinct VariantDatabase types: `MediaIOConfig` (open-time), `MediaIOStats` (runtime metrics with static ID members: FramesDropped/Repeated/Late/QueueDepth/etc.), `MediaIOParams` (parameterized command params/result); backends renamed MediaIO_* → MediaIOTask_* (TPG, ImageFile, AudioFile); prefetch depth control (task default from CmdOpen, user override via setPrefetchDepth, reset on close); seek modes (SeekDefault/Exact/NearestKeyframe/KeyframeBefore/KeyframeAfter, defaultSeekMode from task); track selection (setVideoTracks/setAudioTracks, pre-open only); frame metadata IDs added (CaptureTime, PresentationTime, FrameRepeated, FrameDropped, FrameLate, FrameKeyframe, MediaDescChanged); EOF latching (_atEnd flag, cleared by seek/setStep/close); mid-stream descriptor updates (cmd.mediaDescChanged + descriptorChanged signal); stats API (sendParams + executeCmd(MediaIOCommandStats&)); writeError signal for async write errors; cancelPending() on MediaIO drains both strand queue and read result queue; Error::Cancelled added; PromiseError moved to top-level in future.h; Future<T>::result() and Future<void>::result() both try/catch PromiseError; 58 MediaIO test cases + 10 Strand test cases; docs/mediaio.dox comprehensive backend authoring guide; VideoDesc renamed to MediaDesc; `MediaIO::adoptTask()` added for injecting externally-constructed backends (needed by SDL player); `SDLPlayerTask` / `createSDLPlayer()` — SDL write-only sink backend with audio-led pacing, fast mode, notification throttle, and renderPending() for main-thread dispatch; `mediaplay` utility (`utils/mediaplay/`) that pumps any MediaIO source into the SDL player; `MediaIOTask_ImageFile` extended with default frame rate (from config) and `ConfigFrameRate` override; file I/O nodes, mixer, gain, color space, frame sync remaining. **Phase 4C (DSP) not started.** **Phase 4D partially complete** — Font rendering system (Font/FastFont/BasicFont) complete; codec system (ImageCodec/AudioCodec/JpegImageCodec encode+decode for RGB, RGBA, YUYV, UYVY, planar 4:2:2/4:2:0, NV12; VideoTestPattern, AudioTestPattern) complete; PixelFormat/PixelDesc expanded with full YCbCr format matrix (UYVY/YUYV interleaved, planar 4:2:2/4:2:0, NV12, v210, chroma siting) and RawYUV file I/O; CSC pack/unpack bugs fixed (ARGB8 double-remapping, DPX 10-bit BE paths); `StageMonoExpand` stage added to CSC pipeline (broadcasts buffer[0] to buffers[1] and [2] so Mono8_sRGB → RGBA8_sRGB no longer renders all red); `SDLVideoWidget::mapPixelDesc()` expanded with 8/16-bit RGB/BGR/RGBA/BGRA/ARGB/ABGR direct mappings, RGBA8→RGBA32 bug fixed, refactored through `uploadCurrentImage()` helper; `FrameRate::frameDuration()` (exact nanosecond frame period as Duration) and `FrameRate::samplesPerFrame()` (exact NTSC audio cadence — 1601/1602 pattern at 29.97/48k instead of constant 1602) added; `Size2D::fromString()` added for "WxH" parsing (used by config); TPG/VideoTestPattern: text burn-in (timecode + custom text overlay), bundled FiraCode font default, AvSync pattern, unified small-array image cache, default timecode `01:00:00:00` NDF, `ConfigVideoSize` key replaces separate Width/Height keys; `AudioTestPattern::AvSync` mode (tone burst on tc.frame()==0, silence otherwise, per-size cache for cadenced rates); `BurnPosition::BurnCenter` (value 6) added to `VideoTestPattern::BurnPosition` enum and `enums.h BurnPosition` Enum type (center-of-frame burn position, string name `center`); startup registration logging for MediaIO, ImageFileIO, FileFormatFactory, and Logger teardown demoted from Info to Debug level (gated on per-module `PROMEKI_DEBUG` flag); `mediaplay` utility refactored to signal-driven pipeline architecture: `Pipeline` helper drives frame movement on main EventLoop via `frameReadySignal` (no dedicated pumper thread), multi-sink fan-out (SDL player + file/sequence writer), `--no-display` headless mode, `--frame-count N` stop-after-N-frames, `--output PATH` image-sequence or single-file writer sink, `--seq-head N` sequence start frame, `--imgseq` / `--imgseq-file` sidecar generation, `list` convenience value for `--pattern`/`--audio-mode`/`--burn-position`/`--pixel-format`/`--type` options; `frameReadySignal` now fires on all read completions (success, EOF, error) so signal-driven consumers can observe terminal results; userspace packet pacing done; automatic node processing and kernel-level pacing (`sendmmsg`/`SO_MAX_PACING_RATE`/`SO_TXTIME`) remaining. **Bug fix:** `CmdLineParser::parseMain` now decodes each `argv` element as UTF-8 (was wrapping as Latin1), fixing `--burn-text` with multi-byte codepoints (regression: U+E238 via `mediaplay`). String subsystem deep audit: encoding-agnostic hash, cross-encoding find/rfind/count, operator< total-order fix, C-string overloads as UTF-8, locale-independent toUpper/toLower, substr Latin1 narrowing, fromUtf8 ASCII fast path. C++20 `std::format` integration: `String::format`/`vformat`, `std::formatter<String>`/`Char`/`Timecode`/`VariantImpl`, `PROMEKI_FORMAT_VIA_TOSTRING` macro applied to 20+ types. `std::pair<T,Error>` → `Result<T>` migration complete for Queue/Timecode/MusicalScale/Set. See `proav_optimization.md`.

### Phase 5: TUI Widget Completion
**Prerequisites:** Minimal (existing TUI framework). Mostly independent.
**Document:** `tui.md`

Can be parallelized with Phases 3-4.

### Phase 6: Music Library Completion
**Prerequisites:** Minimal (existing music framework). Mostly independent.
**Documents:** `music_theory.md`, `music_midi.md`

Can be parallelized with Phases 3-4. Work through `music_theory.md` before `music_midi.md`.

### Phase 7: Enhanced Existing Classes
**Prerequisites:** Varies
**Documents:** Tracked in `core_utilities.md` (Enhanced Existing Classes section) and `core_streams.md` (ObjectBase serialization section)

Ongoing throughout other phases. String, Variant, and RegEx enhancements. ObjectBase saveState/loadState using DataStream.

## Coding Standards and Testing Requirements

**All work in this plan must follow the project's established coding standards and testing practices.** See `CODING_STANDARDS.md` at the project root for the full specification. Key requirements:

### Coding Standards

Every new class and every modification to an existing class must follow:

- **File headers**: Doxygen `@file` block with `@copyright Howard Logic. All rights reserved.` and `See LICENSE file in the project root folder for license information.` with trailing period.
- **Include guards**: `#pragma once`, never `#ifndef`.
- **Namespace**: All code in `PROMEKI_NAMESPACE_BEGIN` / `PROMEKI_NAMESPACE_END`.
- **Naming**: PascalCase classes, camelCase methods, `_underscore` private members, `set*()` setters, bare noun getters, `is*()` predicates, `to*()` conversions, `from*()` factories.
- **Indentation**: 8-space-wide tabs. `.clang-format` enforces formatting.
- **Object categories**: Simple data objects (no `PROMEKI_SHARED_FINAL`), Shareable data objects (`PROMEKI_SHARED_FINAL` + `::Ptr` + `::List` + `::PtrList`), Functional objects (derive from `ObjectBase`, use `PROMEKI_OBJECT`). No internal `SharedPtr<Data>` — ever.
- **Error handling**: Use `Error` class, never `bool` returns for error reporting. Preferred patterns: `Result<T>` (alias for `Pair<T, Error>`), direct `Error` return, or `Error *err = nullptr` output parameter.
- **Blocking calls**: Must accept `unsigned int timeoutMs = 0` (0 = wait indefinitely). Return `Error::Timeout` on expiry.
- **Public API**: No naked `std::` types in public interfaces — wrap in `using` aliases inside the class.
- **Doxygen**: All public classes and methods documented with `/** ... */` style. `@brief` required. `@param`, `@return`, `@tparam` as applicable.
- **No `using namespace std;`** in any file. Forward declare in headers where possible.

### Testing

Every new class must have complete unit tests. Every modification to an existing class must update its tests. No exceptions.

- **Framework**: doctest, vendored in `thirdparty/doctest/`.
- **File naming**: `tests/<classname>.cpp` matching the class being tested.
- **Structure**: `TEST_CASE("ClassName")` with `SUBCASE` for each behavior.
- **Coverage expectations**: Tests must cover:
  - Default construction and valid/invalid states
  - All public methods (getters, setters, operations)
  - Edge cases (empty containers, boundary values, error conditions)
  - Round-trip operations (serialize/deserialize, parse/format, encode/decode)
  - Operator overloads (==, !=, <, +, -, <<, >>, etc.)
  - Copy/move semantics where applicable
  - Error paths (invalid input, timeout, resource exhaustion)
  - Thread safety for concurrent classes (Mutex, Queue, ThreadPool, etc.)
- **Assertions**: `CHECK()` for non-fatal, `REQUIRE()` only when subsequent checks depend on it. `doctest::Approx()` for floating-point.
- **Test executables**: `unittest-promeki` (consolidated), `unittest-tui`, `unittest-sdl`. Tests run automatically during build via CTest.
- **Build verification**: `build` command compiles and runs all tests. A class is not done until its tests pass.

---

## Cross-Platform Considerations

**WASM readiness:** All interfaces must be designed so they can work under WASM later:
- No blocking calls in public API without async alternatives
- IODevice and sockets use virtual methods for platform-specific backends
- ThreadPool degrades gracefully (thread count 0 runs tasks inline)
- Process class uses `#ifdef` platform guards
- FilePath/Dir designed for graceful degradation on Emscripten virtual FS

**Future SRT support:** Socket and pipeline interfaces designed to accommodate `SrtSocket`/`SrtSourceNode`/`SrtSinkNode` later without API changes. No SRT-specific abstractions now.

## Verification

- Each new class gets a doctest unit test (`tests/<classname>.cpp`)
- Build with `build` command; tests run automatically
- New feature modules add sources to the `promeki` library and tests to `unittest-promeki`
- TUI widgets verified via `tui-demo` (add new tabs for new widgets)
- Pipeline framework verified via a simple audio-passthrough demo
- Network verified via loopback echo tests (UDP + TCP)

## Key Decisions

- **Network IO:** Raw POSIX sockets (no libuv/asio)
- **TLS:** Vendored mbedTLS
- **AV-over-IP:** General building blocks targeting open standards (ST 2110, AES67)
- **Pipeline threading:** Thread pool by default, nodes can opt into dedicated thread or custom pool
- **Containers:** Header-only templates following `List<T>` pattern
- **Concurrency:** Wrapping `std::` primitives with Qt-style API
- **Streams:** Separate DataStream (binary) and TextStream (formatted text). DataStream for serialization/saveState, TextStream for human-readable I/O. Both operate over IODevice or in-memory buffers. Independent from `std::ostream` (own operator<< overloads)

---

## Benchmark Infrastructure

Performance-critical code (DSP, threading, network, container operations) needs benchmarks to catch regressions and validate design decisions. No benchmark infrastructure currently exists.

### Setup
- [ ] Add benchmark framework: header-only [nanobench](https://github.com/martinus/nanobench) or similar lightweight benchmarker to `thirdparty/`
- [ ] Create `benchmarks/` directory for benchmark source files
- [ ] Add `PROMEKI_BUILD_BENCHMARKS` CMake option (default OFF)
- [ ] Create `benchmark-promeki` executable (conditionally includes network/proav/music benchmarks based on feature flags)
- [ ] Add `run-benchmarks` custom CMake target (separate from `run-tests` — benchmarks are opt-in)

### Phase-specific benchmarks
- [ ] **Phase 1:** Container operations — `List`, `Map`, `HashMap`, `Set`, `HashSet` insert/lookup/iterate at various sizes. Compare to raw `std::` equivalents to verify wrapper overhead is negligible.
- [ ] **Phase 1:** `ThreadPool` — task throughput, submit latency, scaling with thread count
- [ ] **Phase 1:** `Mutex` / `ReadWriteLock` — contended vs uncontended lock/unlock overhead
- [ ] **Phase 2:** `DataStream` — serialization throughput (bytes/sec) for bulk data
- [ ] **Phase 3:** Socket throughput — TCP and UDP loopback bytes/sec, latency
- [ ] **Phase 4:** `MediaPipeline` — frame throughput for simple passthrough graph
- [ ] **Phase 4:** DSP — `AudioFilter`, `AudioResampler` samples/sec

---

## Doxygen Module Organization

Adding many new classes requires Doxygen group organization so generated docs are navigable, not a flat alphabetical wall.

### Doxygen groups — COMPLETE

All `@defgroup` definitions exist in `docs/modules.dox`: containers, concurrency, io, streams, strings, events, audio, media, math, time, util, crypto, network, proav, color, pipeline, paint, music, tui_widgets, tui_core. Color science guide at `docs/color.dox`.

### Remaining Doxygen work
- [ ] Add `@ingroup` tag to every new class's Doxygen comment
- [ ] Retrofit `@ingroup` tags onto existing classes as they are modified
- [ ] Update `docs/dataobjects.dox` to reference appropriate groups
- [ ] Verify `docs/threading.dox` stays current as concurrency classes are implemented
- [ ] Verify generated Doxygen output has navigable module tree

---

## Existing FIXMEs

10 tracked items in [fixme.md](fixme.md):

| File | Issue | Natural Phase |
|---|---|---|
| `src/core/file.cpp:40` | Windows File implementation is a stub | Phase 2 |
| `src/proav/audiogen.cpp:66` | Audio generation doesn't handle planar formats | Phase 4B |
| `src/core/datetime.cpp:112` | Should use `String::parseNumberWords()` instead of `strtoll` | Phase 7 |
| `src/proav/mediaiotask_quicktime.cpp` | LE float audio storage is lossy (promoted to s16) | Phase 4B |
| `src/core/pixeldesc.cpp` | `raw ` BGR vs RGB byte-order disagreement | Phase 4B |
| `CMakeLists.txt` | SDL incremental-rebuild misses header ABI changes | Phase 4D |
| `include/promeki/bufferpool.h` | BufferPool available but not wired into QuickTime hot path | Phase 4B |
| `src/proav/quicktime_reader.cpp` | Fragmented reader ignores `trex` defaults fallback | Phase 4B |
| `src/proav/mediaiotask_quicktime.cpp` | Compressed audio pull-rate drifts (one packet/video frame) | Phase 4B |
| `src/proav/quicktime_writer.cpp` | Compressed audio write path missing (remux blocked) | Phase 4B |
