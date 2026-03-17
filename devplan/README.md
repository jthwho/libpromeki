# libpromeki Development Plan

## Overview

This plan builds out all four existing libraries (core, proav, music, tui) toward completeness and adds a new network library (`promeki-network`). Work is organized into 7 phases ordered by dependency.

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

**Next priorities:**
1. **Phase 4D** — Optimization and cleanup: BitmapFont (fast native-format font rendering), video codec abstraction, automatic node processing, batch UDP/kernel pacing. See [proav_optimization.md](proav_optimization.md).
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

New shared library `promeki-network` with CMake option `PROMEKI_BUILD_NETWORK`. Raw POSIX sockets, vendored mbedTLS for TLS. Work through documents in order: sockets first, then protocols, then AV-over-IP.

**Phase 3A (Sockets) COMPLETE.** Phase 3B (HTTP/TLS) not started. **Phase 3C (AV-over-IP) COMPLETE** — PrioritySocket, RtpSession, RtpPacket, RtpPayload (L24, L16, RawVideo, JPEG with RFC 2435 DQT/entropy parsing), SdpSession, MulticastManager. PtpClock remaining.

### Phase 4: ProAV Pipeline Framework — IN PROGRESS
**Prerequisites:** Phase 1 (complete), Phase 2 (IODevice)
**Documents:** `proav_pipeline.md`, `proav_nodes.md`, `proav_dsp.md`, `proav_optimization.md`

Generalizes the existing source/sink pattern. Work through documents in order: pipeline core, then concrete nodes, then DSP.

**Phase 4A mostly complete** — MediaPort, MediaNode, MediaLink, MediaGraph, MediaPipeline, EncodedDesc all done. Remaining: Audio::ensureExclusive()/isExclusive(), MemSpace::Stats, MemSpacePool. **Phase 4B in progress** — vidgen nodes (TestPatternNode, TimecodeOverlayNode, JpegEncoderNode, FrameDemuxNode, RtpVideoSinkNode, RtpAudioSinkNode) complete; file I/O nodes, mixer, gain, color space, frame sync remaining. **Phase 4C (DSP) not started.** **Phase 4D (Optimization)** planned — see `proav_optimization.md`.

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
- **Test executables**: `unittest-core`, `unittest-proav`, `unittest-music`, `unittest-network` (new). Tests run automatically during build via CTest.
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
- New libraries need CMake target + test executable (e.g., `unittest-network`)
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
- [ ] Create `benchmark-core`, `benchmark-proav`, `benchmark-network` executables
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

Adding many new classes and a new library (`promeki-network`) requires Doxygen group organization so generated docs are navigable, not a flat alphabetical wall.

### New Doxygen groups
- [ ] `@defgroup core_containers` — List, Map, Set, HashMap, HashSet, Deque, Stack, PriorityQueue, Span, Queue, Array
- [ ] `@defgroup core_concurrency` — Mutex, ReadWriteLock, WaitCondition, Atomic, Future, Promise, ThreadPool
- [ ] `@defgroup core_io` — IODevice, BufferedIODevice, File, FilePath, Dir, FileInfo, Process
- [ ] `@defgroup core_streams` — DataStream, TextStream
- [ ] `@defgroup core_strings` — String, RegEx, AnsiStream, StreamString
- [ ] `@defgroup core_events` — EventLoop, Event, TimerEvent, Thread, ObjectBase
- [ ] `@defgroup core_media` — AudioDesc, ImageDesc, VideoDesc, Image, Audio, Frame, Buffer, PixelFormat
- [ ] `@defgroup core_math` — Point, Size2D, Rect, Rational, Matrix3x3, Color, ColorSpace
- [ ] `@defgroup core_time` — Timecode, TimeStamp, DateTime, Duration, ElapsedTimer
- [ ] `@defgroup core_util` — Variant, Error, Random, Algorithm, UUID, FourCC, Metadata, Env
- [ ] `@defgroup net_sockets` — AbstractSocket, TcpSocket, TcpServer, UdpSocket, RawSocket, SocketAddress
- [ ] `@defgroup net_protocols` — HttpClient, HttpRequest, HttpResponse, WebSocket, SslSocket, SslContext
- [ ] `@defgroup net_avoverip` — PrioritySocket, PtpClock, RtpSession, RtpPayload, SdpSession, MulticastManager
- [ ] `@defgroup proav_pipeline` — MediaNode, MediaPort, MediaLink, MediaGraph, MediaPipeline
- [ ] `@defgroup proav_nodes` — AudioSourceNode, AudioSinkNode, ImageSourceNode, etc.
- [ ] `@defgroup proav_dsp` — AudioFilter, AudioResampler, AudioFormatConverter
- [ ] `@defgroup music_theory` — Interval, Chord, ChordProgression, Key, Tempo, TempoMap, etc.
- [ ] `@defgroup music_midi` — MidiEvent, MidiTrack, MidiFile, Instrument, Track, Arrangement
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

There are 4 remaining FIXME comments in the codebase. These are tracked in [fixme.md](fixme.md) and should be addressed as they become relevant to ongoing phase work. Summary:

| File | Issue | Natural Phase |
|---|---|---|
| `src/file.cpp:44` | Windows File implementation is a stub | Phase 2 (File -> IODevice) |
| `src/audiogen.cpp:64` | Audio generation doesn't handle planar formats | Phase 4B |
| `src/datetime.cpp:108` | Should use `String::parseNumberWords()` instead of `strtoll` | Phase 7 (String enhancements) |
| `src/pixelformat_old.cpp:217` | No memory space validation in `fill()` | Phase 4 |
