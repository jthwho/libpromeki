# libpromeki Development Plan

## Overview

This plan builds out the consolidated `promeki` library (core, network, proav, music) and the separate `promeki-tui` and `promeki-sdl` UI libraries toward completeness. Work is organized into 7 phases ordered by dependency.

**Maintenance note:** Completed items are removed from individual phase documents once merged, unless they provide context needed by a future phase in the same document. The code and git history are the source of truth for completed work. Phase summaries in this README note what's complete at a high level.

## Documents

| Document | Phase | Description |
|---|---|---|
| [core_utilities.md](core_utilities.md) | 7 | Enhanced existing classes (Variant, String, RegEx, Result adoption) |
| [core_io.md](core_io.md) | 2 | IODevice, BufferedIODevice, FilePath, Dir, Process |
| [core_streams.md](core_streams.md) | 2, 7 | DataStream, TextStream, ObjectBase saveState/loadState, std:: stream migration |
| [network_sockets.md](network_sockets.md) | 3A | Socket layer (Abstract, TCP, UDP, Raw, TLS) |
| [network_protocols.md](network_protocols.md) | 3B | HTTP, WebSocket, higher-level protocols |
| [network_avoverip.md](network_avoverip.md) | 3C | AV-over-IP building blocks (RTP, PTP, SDP, multicast) |
| [proav_pipeline.md](proav_pipeline.md) | 4A | Pipeline framework core (MediaNode, MediaGraph, MediaPipeline) |
| [proav_nodes.md](proav_nodes.md) | 4B | Concrete nodes (source/sink/mixer/gain/sync) |
| [proav_dsp.md](proav_dsp.md) | 4C | DSP and effects (filters, resampler, format converter) |
| [proav_optimization.md](proav_optimization.md) | 4D | Optimization and cleanup (BitmapFont, codec system, auto processing) |
| [vidgen.md](vidgen.md) | 3-4 | vidgen utility: test pattern nodes, streaming nodes, CLI tool |
| [tui.md](tui.md) | 5 | TUI widget completion |
| [music_theory.md](music_theory.md) | 6A, 6B | Core music theory objects |
| [music_midi.md](music_midi.md) | 6C, 6D | MIDI I/O and arrangement |
| [fixme.md](fixme.md) | ongoing | Existing FIXME comments to address during related phase work |

## Dependency Graph

```
Phase 1 (COMPLETE) ----+---> Phase 2 (IO/FS complete, streams complete, StreamString refactor remaining)
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

**Recent work:** **Library consolidation (complete):** The four separate shared libraries (`promeki-core`, `promeki-network`, `promeki-proav`, `promeki-music`) have been merged into a single `promeki` shared library. `promeki-tui` and `promeki-sdl` remain separate. CMakeLists.txt rewritten with `PROMEKI_ENABLE_*` feature flags and `PROMEKI_USE_SYSTEM_*` vendored-vs-system options. Generated `build/include/promeki/config.h` provides compile-time feature detection. Headers flattened from `promeki/core/`, `promeki/proav/`, `promeki/network/`, `promeki/music/` subdirectories into `promeki/`. Sources reorganized into `src/core/`, `src/proav/`, `src/network/`, `src/music/`. Test executables consolidated to `unittest-promeki` (network tests in `tests/network/`). All documentation, Doxygen groups (removed `core_` prefix), devplan files, README, and CODING_STANDARDS updated. `pixeldesc_proav.cpp` merged back into `pixeldesc.cpp`. Dead test file `image.cpp` removed, `image2.cpp` renamed to `image.cpp`. **New `StringRegistry<Tag>` (thread-safe append-only string-to-ID registry), `VariantDatabase<Tag>` (named Variant store with JSON/DataStream/TextStream serialization), and `Config` alias (`VariantDatabase<ConfigTag>`) added to promeki. Two new `Error` codes: `IdNotFound` and `ConversionFailed`. New `AudioLevel` class (dBFS value type with linear conversion) in promeki. AudioGen and TestPatternNode refactored to use AudioLevel instead of raw linear amplitude. vidgen CLI uses `--audio-level`/`--ltc-level` in dBFS. SDP attribute ordering fixed. RTP sender pacing (userspace) implemented. `#ifdef PROMEKI_HAVE_NETWORK` removed from RTP sink node headers. `Metadata` refactored to inherit from `VariantDatabase<MetadataTag>` (X-macro enum removed, well-known keys are now `static inline const ID` members). `Variant::operator==`/`operator!=` added with three-tier cross-type comparison. `VariantDatabase::operator==`/`operator!=` added. `Size2DTemplate` and `TimeStamp` gain `operator==`/`operator!=`. **Phase 4D partial:** `ImageCodec`/`AudioCodec` abstract bases with string-based name registry, `JpegImageCodec` (libjpeg-turbo, quality/subsampling, registered as "jpeg"), `VideoTestPattern` (11 patterns, dual-mode create/render, motion, fromString/toString), `AudioTestPattern` (Tone/Silence/LTC, configure/create/render, fromString/toString). `JpegEncoderNode` and `TestPatternNode` refactored to delegate to these new classes. `Codec` class removed (was a placeholder). **Color/ColorModel refactoring (complete):** Major refactoring of the color system across 6 phases: `XYZColor`, `CIEPoint`, and `CIEWavelengthTable` consolidated into flat include/promeki/ directory; new `ColorModel` class is the single source of truth for color model + space (ID-based construct-on-first-use registry avoids static init ordering issues); `Color` refactored from `uint8_t` RGBA to `float[4]` + `ColorModel` (model-aware serialization via `toString()`/`fromString()`, `uint8_t` constructor preserved for backward compat); `PixelFormat` CompType deconflated to `Comp0/1/2` (no longer implies RGB channel semantics); old `ColorSpace`/`ColorSpaceConverter` classes removed; 19 color models added including DCI-P3, Adobe RGB, ACES AP0/AP1, YCbCr Rec.2020; 2187 test assertions across 217 test cases; color science guide page at `docs/color.dox`. `core_color` Doxygen group added. **TypeRegistry pattern (complete):** Introduced the TypeRegistry design pattern (documented at `docs/typeregistry.dox`) and retrofitted it onto `ColorModel` and `MemSpace`: enum IDs replace ad-hoc constants, `registerType()`/`registerData()` enable user-defined entries at runtime, `Data`/`Ops` structs are public in headers so callers can populate them, registries use `Map` (construct-on-first-use singletons), `StructDatabase` dependency removed from `MemSpace`. `Color(ColorModel::ID, float, float, float, float)` constructor added to resolve overload ambiguity with the `ColorModel` and `uint8_t` overloads. **FrameRate well-known rate expansion:** Five new well-known rates added to `PROMEKI_WELL_KNOWN_FRAME_RATES`: `FPS_4795` (47.95, 48000/1001), `FPS_48` (48, 48/1), `FPS_100` (100, 100/1), `FPS_11988` (119.88, 120000/1001), `FPS_120` (120, 120/1). `wellKnownRate()` refactored from a cached `_rate` member to an on-demand computation that compares the reduced rational against all well-known entries, enabling non-stored rationals (e.g. 30000/1000) to correctly match (e.g. FPS_30). `fromString()` documentation updated to list all accepted rate strings including the 5 new ones. **Image system refactor (complete):** The old monolithic polymorphic `PixelFormat` class has been split into two TypeRegistry value types: `PixelFormat` (memory layout only: component count, bit depths, bytes per block, planes, sampling, `byteOffset` per component) and `PixelDesc` (full pixel description: `PixelFormat` + `ColorModel` + per-component semantic ranges + compression info with `encodeSources`/`decodeTargets` + paint engine factory). Both classes live in `include/promeki/` (moved out of proav). Descriptive well-known IDs added (`RGBA8_sRGB_Full`, `YUV8_422_Rec709_Limited`, etc.). `Variant` support added for all four TypeRegistry types (`ColorModel`, `MemSpace`, `PixelFormat`, `PixelDesc`) with full toString/fromString/integer conversion. `PaintEngine::createPixel(Color)` is now color-model-aware (converts the Color to the PixelDesc's target ColorModel before mapping to 16-bit components). `registeredIDs()` added to all four TypeRegistry types. ~55 consumer files (headers, sources, tests, utilities) migrated. Old `pixelformat_old.h`, `pixelformat_old.cpp`, `pixelformat_rgb8.cpp`, `pixelformat_rgba8.cpp`, `pixelformat_jpeg.cpp` removed.

**Next priorities:**
1. **Phase 4D** — Optimization and cleanup: BitmapFont (fast native-format font rendering), video codec abstraction, automatic node processing, batch UDP/kernel pacing (`sendmmsg`, `SO_MAX_PACING_RATE`). Userspace pacing fallback is done. See [proav_optimization.md](proav_optimization.md).
2. **Remaining Phase 4A** — Audio::ensureExclusive()/isExclusive(), MemSpace::Stats, MemSpacePool. See [proav_pipeline.md](proav_pipeline.md).
3. **Phase 4B** — File-based source/sink nodes, mixer, gain, color space conversion. See [proav_nodes.md](proav_nodes.md).

## Phasing

### Phase 1: Core Containers, Concurrency, and Utilities — COMPLETE

Phase 1A (containers), 1B (concurrency), 1C (API consistency), and 1D (utilities) are all done. Delivered: List, Map, Set, HashMap, HashSet, Deque, Stack, PriorityQueue, Span, Mutex, ReadWriteLock, WaitCondition, Atomic, Future, Promise, ThreadPool, Queue, Random, ElapsedTimer, Duration, Algorithm, Result, Pair. `Result<T>` adoption across the codebase is tracked in each phase's document where the classes are defined.

### Phase 2: IO Abstractions, Filesystem, and Streams — IN PROGRESS
**Prerequisites:** Phase 1 (complete)
**Documents:** `core_io.md`, `core_streams.md`

Establish a uniform byte-oriented IO interface that network sockets, files, and pipes can all implement. Add filesystem utilities. Add DataStream (binary serialization) and TextStream (formatted text I/O) — both operate over IODevice or in-memory buffers. DataStream is the foundation for ObjectBase saveState/loadState (Phase 7). IODevice is the base class for Phase 3 sockets and Phase 4 pipeline file I/O.

**IO and Filesystem (COMPLETE):** IODevice, BufferedIODevice, BufferIODevice, FilePath, Dir, File (refactored to BufferedIODevice), FileInfo, Process, Buffer size model, Error enhancements, Terminal error reporting.

**DataStream (COMPLETE):** Binary serialization stream over IODevice with wire format versioning, per-value type tags, and byte-order control. BufferIODevice provides in-memory backing.

**TextStream (COMPLETE):** Formatted text I/O with encoding awareness, manipulators, and multiple backing stores.

**Remaining:** StreamString refactor (last std:: stream usage — still uses `std::streambuf`/`std::ostream`).

### Phase 3: Network Library — IN PROGRESS
**Prerequisites:** Phase 1 (complete), Phase 2 (IODevice)
**Documents:** `network_sockets.md`, `network_protocols.md`, `network_avoverip.md`

Network sources in the `promeki` library, controlled by `PROMEKI_ENABLE_NETWORK` feature flag. Raw POSIX sockets, vendored mbedTLS for TLS. Work through documents in order: sockets first, then protocols, then AV-over-IP.

**Phase 3A (Sockets) COMPLETE.** Phase 3B (HTTP/TLS) not started. **Phase 3C (AV-over-IP) COMPLETE** — PrioritySocket, RtpSession (including `sendPacketsPaced()` for ST 2110-21 pacing), RtpPacket, RtpPayload (L24, L16, RawVideo, JPEG with RFC 2435 DQT/entropy parsing), SdpSession (with insertion-order-preserving attributes), MulticastManager. PtpClock remaining.

### Phase 4: ProAV Pipeline Framework — IN PROGRESS
**Prerequisites:** Phase 1 (complete), Phase 2 (IODevice)
**Documents:** `proav_pipeline.md`, `proav_nodes.md`, `proav_dsp.md`, `proav_optimization.md`

Generalizes the existing source/sink pattern. Work through documents in order: pipeline core, then concrete nodes, then DSP.

**Phase 4A mostly complete** — MediaPort, MediaNode, MediaLink, MediaGraph, MediaPipeline, EncodedDesc all done. Remaining: Audio::ensureExclusive()/isExclusive(), MemSpace::Stats, MemSpacePool. **Phase 4B in progress** — vidgen nodes (TestPatternNode, TimecodeOverlayNode, JpegEncoderNode, FrameDemuxNode, RtpVideoSinkNode, RtpAudioSinkNode) complete; file I/O nodes, mixer, gain, color space, frame sync remaining. **Phase 4C (DSP) not started.** **Phase 4D (Optimization) partially started** — userspace packet pacing done (`RtpSession::sendPacketsPaced()`, used by `RtpVideoSinkNode` for ST 2110-21 style pacing); BitmapFont, codec abstraction, automatic node processing, kernel-level pacing (`sendmmsg`/`SO_MAX_PACING_RATE`/`SO_TXTIME`) remaining. See `proav_optimization.md`.

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

### New Doxygen groups
- [ ] `@defgroup containers` — List, Map, Set, HashMap, HashSet, Deque, Stack, PriorityQueue, Span, Queue, Array
- [ ] `@defgroup concurrency` — Mutex, ReadWriteLock, WaitCondition, Atomic, Future, Promise, ThreadPool
- [ ] `@defgroup io` — IODevice, BufferedIODevice, File, FilePath, Dir, FileInfo, Process
- [ ] `@defgroup streams` — DataStream, TextStream
- [ ] `@defgroup strings` — String, RegEx, AnsiStream, StreamString
- [ ] `@defgroup events` — EventLoop, Event, TimerEvent, Thread, ObjectBase
- [x] `@defgroup audio` — AudioLevel, AudioDesc, AudioGen (added to `docs/modules.dox`)
- [ ] `@defgroup media` — ImageDesc, VideoDesc, Image, Audio, Frame, Buffer, PixelFormat
- [ ] `@defgroup math` — Point, Size2D, Rect, Rational, Matrix3x3
- [x] `@defgroup color` — Color, ColorModel, XYZColor, CIEPoint, CIEWavelengthTable (added to `docs/modules.dox`; `docs/color.dox` color science guide page added)
- [ ] `@defgroup time` — Timecode, TimeStamp, DateTime, Duration, ElapsedTimer
- [ ] `@defgroup util` — Variant, Error, Random, Algorithm, UUID, FourCC, Metadata, Env
- [ ] `@defgroup network` — AbstractSocket, TcpSocket, TcpServer, UdpSocket, RawSocket, SocketAddress
- [ ] `@defgroup protocols` — HttpClient, HttpRequest, HttpResponse, WebSocket, SslSocket, SslContext
- [ ] `@defgroup avoverip` — PrioritySocket, PtpClock, RtpSession, RtpPayload, SdpSession, MulticastManager
- [ ] `@defgroup pipeline` — MediaNode, MediaPort, MediaLink, MediaGraph, MediaPipeline
- [ ] `@defgroup nodes` — AudioSourceNode, AudioSinkNode, ImageSourceNode, etc.
- [ ] `@defgroup dsp` — AudioFilter, AudioResampler, AudioFormatConverter
- [ ] `@defgroup music` — Interval, Chord, ChordProgression, Key, Tempo, TempoMap, etc.
- [ ] `@defgroup midi` — MidiEvent, MidiTrack, MidiFile, Instrument, Track, Arrangement
- [ ] `@defgroup tui_widgets` — All TUI widgets

### Implementation
- [ ] Add `@ingroup` tag to every new class's Doxygen comment
- [ ] Retrofit `@ingroup` tags onto existing classes as they are modified
- [ ] Create `docs/modules.dox` with all `@defgroup` definitions and descriptions
- [ ] Update `docs/dataobjects.dox` to reference appropriate groups
- [ ] Verify `docs/threading.dox` stays current as concurrency classes are implemented
- [ ] Verify generated Doxygen output has navigable module tree

---

## Existing FIXMEs

There are 3 remaining FIXME comments in the codebase. These are tracked in [fixme.md](fixme.md) and should be addressed as they become relevant to ongoing phase work. Summary:

| File | Issue | Natural Phase |
|---|---|---|
| `src/core/file.cpp:44` | Windows File implementation is a stub | Phase 2 (File -> IODevice) |
| `src/proav/audiogen.cpp:64` | Audio generation doesn't handle planar formats | Phase 4B |
| `src/core/datetime.cpp:108` | Should use `String::parseNumberWords()` instead of `strtoll` | Phase 7 (String enhancements) |
