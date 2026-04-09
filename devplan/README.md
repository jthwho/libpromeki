# libpromeki Development Plan

## Overview

This plan builds out the consolidated `promeki` library (core, network, proav, music) and the separate `promeki-tui` and `promeki-sdl` UI libraries toward completeness. Work is organized into phases ordered by dependency.

**Maintenance note:** Completed items are removed from individual phase documents once merged, unless they provide context needed by pending work. The code and git history are the source of truth for completed work.

## Current Focus

The `MediaNode` / old `MediaPipeline` framework is **deprecated**. All new media work happens in the `MediaIO` framework and its backends. Once the new capabilities exist (see below), a new `MediaPipeline` class builds on top of MediaIO to describe and run full pipelines from a data object (or JSON). The legacy `MediaNode` classes, `vidgen` utility, and any `*Node` sources stay in the tree only until migration is complete, then are deleted in a single sweep.

**Primary work queue (in rough order):**

1. **New MediaIO backends** (see `proav_nodes.md`):
   - `MediaIOTask_Converter` — **complete** (initial version): ReadWrite CSC, JPEG encode/decode, audio sample-format conversion via `Image::convert()` / `JpegImageCodec` / `Audio::convertTo()`. Stateful temporal codecs and explicit ColorModel/sample-rate knobs remain future work.
   - `MediaIOTask_RtpVideo` — bidirectional RTP video TX/RX. Replaces `RtpVideoSinkNode`, adds receive.
   - `MediaIOTask_RtpAudio` — bidirectional RTP audio TX/RX. Replaces `RtpAudioSinkNode`, adds receive.
   - `ImageFileIO_JPEG` — JPEG read/write for the existing `ImageFile` backend, with pass-through for already-compressed JPEG frames.
2. **New `MediaPipeline` class** (see `proav_pipeline.md`):
   - `MediaPipelineConfig` data object describing stages + routes, with JSON `toJson()`/`fromJson()`.
   - `MediaPipeline` class that builds, opens, starts, stops, and closes a pipeline of MediaIO instances.
3. **mediaplay overhaul** (see `proav_nodes.md` → mediaplay section):
   - **Landed:** `--in` / `--incfg` / `--convert` / `--convertcfg` / `--out` / `--outcfg Key:Value` grammar with type-aware parsing against each backend's `defaultConfig`.  `list` sentinel works automatically for any `Enum` or `PixelDesc` key.  `--help` autogenerates every backend's key schema from the live MediaIO registry.  Positional `mediaplay <in> <out>` shortcuts work when `<in>` is an existing file (sequence masks and non-existent paths are treated as outputs).  Fan-out via repeated `--out`.
   - Pending: `--pipeline <file.json>` / `--save-pipeline` — blocked on new `MediaPipelineConfig`.
4. **Network optimization** (see `proav_optimization.md`):
   - `UdpSocket::setPacingRate()` (kernel pacing via `SO_MAX_PACING_RATE`).
   - `UdpSocket::writeDatagrams()` (batch send via `sendmmsg`).
   - `PacketTransport` abstraction (prereq for `MediaIOTask_RtpVideo`/`RtpAudio` and future DPDK).
5. **Legacy removal:** Delete `utils/vidgen/`, every `*Node` class, the old `MediaPipeline`/`MediaNode` files, and their tests — one large cleanup commit after migration is verified.

## Documents

| Document | Phase | Description |
|---|---|---|
| [core_utilities.md](core_utilities.md) | 7 | Enhanced existing classes (Variant, String, RegEx, Result adoption) |
| [core_io.md](core_io.md) | 2 | IODevice, FilePath, Dir, Process (COMPLETE; carries known issues) |
| [core_streams.md](core_streams.md) | 2, 7 | DataStream, TextStream extensions, ObjectBase saveState/loadState |
| [network_sockets.md](network_sockets.md) | 3A | Socket layer (Abstract, TCP, UDP, Raw, TLS) — COMPLETE |
| [network_protocols.md](network_protocols.md) | 3B | HTTP, WebSocket, higher-level protocols |
| [network_avoverip.md](network_avoverip.md) | 3C | AV-over-IP building blocks (RTP, PTP, SDP, multicast) |
| [proav_pipeline.md](proav_pipeline.md) | 4A | **NEW** `MediaPipeline` class (MediaIO-based, JSON-definable) |
| [proav_nodes.md](proav_nodes.md) | 4B | MediaIO backends (existing + new: Converter, RtpVideo, RtpAudio, JPEG ImageFile) |
| [proav_dsp.md](proav_dsp.md) | 4C | DSP and effects (future, as Converter subclasses) |
| [proav_optimization.md](proav_optimization.md) | 4D | Network optimization (sendmmsg, kernel pacing, PacketTransport) |
| [vidgen.md](vidgen.md) | 3-4 | **DEPRECATED** (to be removed with the rest of the MediaNode era) |
| [tui.md](tui.md) | 5 | TUI widget completion |
| [music_theory.md](music_theory.md) | 6A, 6B | Core music theory objects |
| [music_midi.md](music_midi.md) | 6C, 6D | MIDI I/O and arrangement |
| [fixme.md](fixme.md) | ongoing | Existing FIXME comments tracked across the tree |

## Dependency Graph

```
Phase 1 (COMPLETE) ──┬─► Phase 2 (COMPLETE)
                     │     │
                     │     ├─► Phase 3A (COMPLETE) ─► Phase 3C (mostly complete) ─┐
                     │     │                                                      │
                     │     ├─► Phase 4 (MediaIO framework complete)               │
                     │     │     │                                                │
                     │     │     ├─► MediaIO backends: Converter, RtpVideo, ◄─────┘
                     │     │     │   RtpAudio, JPEG ImageFile
                     │     │     │
                     │     │     ├─► Phase 4A: new MediaPipeline (MediaIO-based)
                     │     │     │
                     │     │     ├─► mediaplay CLI overhaul
                     │     │     │
                     │     │     └─► Legacy MediaNode / vidgen removal
                     │     │
                     │     ├─► Phase 4D (network optimization)
                     │     │     │
                     │     │     └─► PacketTransport ◄─┐
                     │     │                           │
                     │     │     Phase 3B (HTTP/TLS) ──┘
                     │     │
                     │     └─► Phase 7 (ObjectBase saveState/loadState via DataStream)
                     │
                     ├─► Phase 5 (TUI Widgets)          [mostly independent]
                     │
                     └─► Phase 6 (Music Library)        [mostly independent]

Phase 7 (Cross-Cutting) — ongoing throughout
```

## Phasing

### Phase 1: Core Containers, Concurrency, Utilities — COMPLETE
Delivered: List, Map, Set, HashMap, HashSet, Deque, Stack, PriorityQueue, Span, Mutex, ReadWriteLock, WaitCondition, Atomic, Future, Promise, ThreadPool, Queue, Random, ElapsedTimer, Duration, Algorithm, Result, Pair.

### Phase 2: IO Abstractions, Filesystem, Streams — COMPLETE
IO abstractions, filesystem utilities, DataStream, and TextStream all implemented. Resource filesystem (cirf) integrated: `File`, `FileIODevice`, and `Dir` transparently accept `:/` resource paths. libpromeki ships `:/.PROMEKI/` built-in resources (bundled FiraCode font). Remaining extension work lives in `core_streams.md` (TextStream type operators, ObjectBase serialization).

### Phase 3: Network Library — IN PROGRESS
**Phase 3A (Sockets) COMPLETE.** Phase 3B (HTTP/TLS) not started. **Phase 3C (AV-over-IP) mostly complete** — PrioritySocket, RtpSession (incl. `sendPacketsPaced()`), RtpPacket, RtpPayload (L24, L16, RawVideo, JPEG with RFC 2435 DQT/entropy parsing), SdpSession, MulticastManager. PtpClock remaining.

### Phase 4: ProAV — MediaIO-Based Pipeline
**MediaIO framework complete.** Four backends complete: `MediaIOTask_TPG`, `MediaIOTask_ImageFile`, `MediaIOTask_AudioFile`, `MediaIOTask_QuickTime`, plus `SDLPlayerTask` for display sink. The `mediaplay` utility exercises all of them end-to-end.

**The old `MediaNode` / `MediaPipeline` / concrete `*Node` classes are deprecated** — they still work and `vidgen` still uses them, but no new work targets them. The new `MediaPipeline` class is being built on top of MediaIO (see `proav_pipeline.md`), and the missing MediaIO backends (`Converter`, `RtpVideo`, `RtpAudio`, JPEG ImageFile extension) are queued up in `proav_nodes.md`.

Network optimization (batch `sendmmsg`, kernel pacing, `PacketTransport` abstraction) is tracked in `proav_optimization.md` and is a prereq for the new RTP backends.

DSP (audio filters, resampler, format converter) is deferred — it will land later as `MediaIOTask_Converter` subclasses. See `proav_dsp.md`.

### Phase 5: TUI Widget Completion
Mostly independent. See `tui.md`. Can be parallelized with Phase 3/4.

### Phase 6: Music Library Completion
Mostly independent. Work through `music_theory.md` before `music_midi.md`.

### Phase 7: Enhanced Existing Classes
Ongoing throughout other phases. String, Variant, and RegEx enhancements, plus ObjectBase saveState/loadState via DataStream. See `core_utilities.md` and `core_streams.md`.

---

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
- **Coverage expectations**: Tests must cover default/invalid states, all public methods, edge cases, round-trip operations, operator overloads, copy/move semantics, error paths, and thread safety where applicable.
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

**Future SRT support:** Socket and pipeline interfaces designed to accommodate `SrtSocket` later without API changes. No SRT-specific abstractions now.

## Key Decisions

- **Network IO:** Raw POSIX sockets (no libuv/asio)
- **TLS:** Vendored mbedTLS
- **AV-over-IP:** General building blocks targeting open standards (ST 2110, AES67)
- **Pipeline unit:** `MediaIO` instance. No node wrapper. Conversions are Converter MediaIOs.
- **Pipeline topology:** Declarative config (stages + routes) → JSON-serializable → `MediaPipeline::build()` instantiates and wires
- **Threading:** Per-MediaIO `Strand` on a shared `ThreadPool`. Signal-driven frame movement, no dedicated pumper thread.
- **Containers:** Header-only templates following `List<T>` pattern
- **Concurrency:** Wrapping `std::` primitives with Qt-style API
- **Streams:** Separate DataStream (binary) and TextStream (formatted text). Both operate over IODevice or in-memory buffers, independent of `std::ostream`.

---

## Benchmark Infrastructure

Performance-critical code (DSP, threading, network, container operations) needs benchmarks to catch regressions and validate design decisions. No benchmark infrastructure currently exists.

### Setup
- [ ] Add benchmark framework: header-only [nanobench](https://github.com/martinus/nanobench) or similar to `thirdparty/`
- [ ] `benchmarks/` directory for benchmark source files
- [ ] `PROMEKI_BUILD_BENCHMARKS` CMake option (default OFF)
- [ ] `benchmark-promeki` executable
- [ ] `run-benchmarks` custom CMake target (opt-in, separate from `run-tests`)

### Targets
- [ ] Container operations (List, Map, HashMap, Set, HashSet) vs raw `std::` equivalents
- [ ] ThreadPool throughput and submit latency
- [ ] Mutex / ReadWriteLock contended vs uncontended overhead
- [ ] DataStream serialization throughput
- [ ] Socket throughput (TCP / UDP loopback)
- [ ] MediaPipeline frame throughput for simple passthrough topology
- [ ] CSC conversion throughput per pixel-format pair

---

## Doxygen Module Organization

### Doxygen groups — COMPLETE

All `@defgroup` definitions exist in `docs/modules.dox`: containers, concurrency, io, streams, strings, events, audio, media, math, time, util, crypto, network, proav, color, pipeline, paint, music, tui_widgets, tui_core. Color science guide at `docs/color.dox`. MediaIO authoring guide at `docs/mediaio.dox`.

### Remaining Doxygen work
- [ ] Add `@ingroup` tag to every new class's Doxygen comment
- [ ] Retrofit `@ingroup` tags onto existing classes as they are modified
- [ ] `docs/mediapipeline.dox` — authoring guide, JSON schema, worked examples (new)
- [ ] `docs/mediaplay.dox` — new CLI grammar reference
- [ ] Verify generated Doxygen output has navigable module tree

---

## Existing FIXMEs

11 tracked items in [fixme.md](fixme.md):

| File | Issue | Natural Phase |
|---|---|---|
| `src/core/file.cpp:40` | Windows File implementation is a stub | Phase 2 |
| `src/proav/audiogen.cpp:66` | Audio generation doesn't handle planar formats | Phase 4 |
| `src/core/datetime.cpp:112` | Should use `String::parseNumberWords()` instead of `strtoll` | Phase 7 |
| `src/proav/mediaiotask_quicktime.cpp` | LE float audio storage is lossy (promoted to s16) | Phase 4 |
| `src/core/pixeldesc.cpp` | `raw ` BGR vs RGB byte-order disagreement | Phase 4 |
| `CMakeLists.txt` | SDL incremental-rebuild misses header ABI changes | Phase 4 |
| `include/promeki/bufferpool.h` | BufferPool available but not wired into QuickTime hot path | Phase 4 |
| `src/proav/quicktime_reader.cpp` | Fragmented reader ignores `trex` defaults fallback | Phase 4 |
| `src/proav/mediaiotask_quicktime.cpp` | Compressed audio pull-rate drifts (one packet/video frame) | Phase 4 |
| `src/proav/quicktime_writer.cpp` | Compressed audio write path missing (remux blocked) | Phase 4 |
| `src/proav/quicktime_reader.cpp` | Minimal XMP parser only matches `bext:` prefix (blocked on core XML support) | Phase 4 / Core |
