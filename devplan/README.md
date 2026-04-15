# libpromeki Development Plan

## Overview

This plan builds out the consolidated `promeki` library (core, network, proav, music) and the separate `promeki-tui` and `promeki-sdl` UI libraries toward completeness. Work is organized into phases ordered by dependency.

**Maintenance note:** Completed items are removed from individual phase documents once merged, unless they provide context needed by pending work. The code and git history are the source of truth for completed work.

## Current Focus

All media work happens in the `MediaIO` framework and its backends. The legacy `MediaNode` / `MediaPipeline` / `*Node` layer and its two dedicated utilities (`vidgen`, `testrender`) have been deleted; the tree is clean. The next major milestone is a new `MediaPipeline` class that builds on MediaIO to describe and run full pipelines from a data object (or JSON).

**Primary work queue (in rough order):**

1. **MediaIOTask_Rtp** (see `proav_nodes.md`):
   - Writer path is SHIPPED — unified video + audio + metadata sink with MJPEG / L16 / JSON / JPEG XS payloads, SDP export, kernel pacing, per-stream TX worker threads, and timing histograms.
   - **Reader path is SHIPPED** — per-stream RTP receive threads, packet reassembly, SDP-driven auto-config (`RtpSdp` config key), video/audio/data frame aggregation, jitter buffer, and comprehensive loopback tests.
   - Remaining reader work: mid-stream descriptor discovery, RTP timestamp wrap handling.
2. **New `MediaPipeline` class** (see `proav_pipeline.md`):
   - `MediaPipelineConfig` data object describing stages + routes, with JSON `toJson()`/`fromJson()`.
   - `MediaPipeline` class that builds, opens, starts, stops, and closes a pipeline of MediaIO instances.
3. **mediaplay JSON pipeline support** (see `proav_nodes.md` → mediaplay section):
   - `--pipeline <file.json>` / `--save-pipeline` — blocked on new `MediaPipelineConfig`.
4. **JPEG XS container and RTP follow-ups** (see `fixme.md`):
   - QuickTime / ISO-BMFF `jxsm` sample entry (read + write) per ISO/IEC 21122-3 Annex C — **blocked on procuring the spec**.
   - `RtpPayloadJpegXs` slice packetization mode (K=1) for ST 2110-22.
   - Packed RGB encode path in the codec.
5. **Network optimization follow-ups** (see `proav_optimization.md`):
   - Core prerequisites SHIPPED: `UdpSocket::setPacingRate()`, `UdpSocket::writeDatagrams()` (sendmmsg batch send), `PacketTransport` abstraction with `UdpSocketTransport` and `LoopbackTransport`, `RtpSession` migrated to `PacketTransport`, `SO_TXTIME` enable path.
   - Remaining: wire `RtpPacingMode::TxTime` through `MediaIOTask_Rtp` so per-packet `SCM_TXTIME` deadlines actually get stamped onto each datagram.  DPDK transport backend as a later phase.

## Documents

| Document | Phase | Description |
|---|---|---|
| [core_utilities.md](core_utilities.md) | 7 | Enhanced existing classes (Variant, String, RegEx, Result adoption) |
| [core_io.md](core_io.md) | 2 | IODevice, FilePath, Dir, Process (COMPLETE; carries known issues) |
| [core_streams.md](core_streams.md) | 2, 7 | DataStream, TextStream extensions, ObjectBase saveState/loadState |
| [network_sockets.md](network_sockets.md) | 3A | Socket layer (Abstract, TCP, UDP, Raw, TLS) — COMPLETE |
| [network_protocols.md](network_protocols.md) | 3B | HTTP, WebSocket, higher-level protocols |
| [network_avoverip.md](network_avoverip.md) | 3C | AV-over-IP building blocks (RTP, PTP, SDP, multicast) |
| [proav_pipeline.md](proav_pipeline.md) | 4A | `MediaPipeline` class (MediaIO-based, JSON-definable) |
| [proav_nodes.md](proav_nodes.md) | 4B | MediaIO backends (all complete: TPG, ImageFile, AudioFile, QuickTime, Converter, Rtp, SDL) |
| [proav_dsp.md](proav_dsp.md) | 4C | DSP and effects (future, as Converter subclasses) |
| [proav_optimization.md](proav_optimization.md) | 4D | Network optimization (sendmmsg, kernel pacing, PacketTransport) |
| [proav_timestamps.md](proav_timestamps.md) | 4E | MediaTimeStamp, ClockDomain, EUI64, PTP/SDP integration, auto-stamping |
| [proav_framesync.md](proav_framesync.md) | 4F | Clock / FrameSync / SDLPlayer (phases 1–3 shipped; FramePacer removal pending) |
| [tui.md](tui.md) | 5 | TUI widget completion |
| [music_theory.md](music_theory.md) | 6A, 6B | Core music theory objects |
| [music_midi.md](music_midi.md) | 6C, 6D | MIDI I/O and arrangement |
| [logger_ring_buffer.md](logger_ring_buffer.md) | cross-cutting | Retain last N log messages in a lock-free ring for inclusion in crash reports |
| [benchmarking.md](benchmarking.md) | cross-cutting | `BenchmarkRunner`, `promeki-bench` utility, MediaIO stamp hooks, live telemetry |
| [fixme.md](fixme.md) | ongoing | Existing FIXME comments tracked across the tree |
| [ideas.md](ideas.md) | backlog | Exploratory ideas that need further design |

## Dependency Graph

```
Phase 1 (COMPLETE) ──┬─► Phase 2 (COMPLETE)
                     │     │
                     │     ├─► Phase 3A (COMPLETE) ─► Phase 3C (mostly complete)
                     │     │
                     │     ├─► Phase 4 (MediaIO framework + backends complete)
                     │     │     │
                     │     │     ├─► Phase 4A: MediaPipeline (MediaIO-based)
                     │     │     │
                     │     │     └─► mediaplay --pipeline JSON support
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
**Phase 3A (Sockets) COMPLETE** — including `UdpSocket::writeDatagrams()` (sendmmsg), `setPacingRate()`, and `setTxTime()`. Phase 3B (HTTP/TLS) not started. **Phase 3C (AV-over-IP) mostly complete** — PrioritySocket, `PacketTransport` + `UdpSocketTransport` + `LoopbackTransport`, RtpSession (migrated to PacketTransport, incl. `sendPacketsPaced()`, `setRemote()`, `setPacingRate()`, plus new `startReceiving()` / `stopReceiving()` receive loop), RtpPacket, RtpPayload (L24, L16, RawVideo, JPEG with RFC 2435 DQT/entropy parsing, `RtpPayloadJson` for metadata streams, **`RtpPayloadJpegXs` for RFC 9134 JPEG XS**), SdpSession (with `fromFile()` / `toFile()`, structured `RtpMap` / `FmtpParameters` accessors), MulticastManager, **MulticastReceiver**. PtpClock remaining.

### Phase 4: ProAV — MediaIO-Based Pipeline
**MediaIO framework complete.** Seven backends complete: `MediaIOTask_TPG` (test pattern generator, with `ImageDataEncoder` data stamp pass and AvSync LTC audio mode), `MediaIOTask_ImageFile` (DPX/Cineon/TGA/SGI/PNM/PNG/JPEG/RawYUV), `MediaIOTask_AudioFile`, `MediaIOTask_QuickTime`, `MediaIOTask_Converter`, the unified **`MediaIOTask_Rtp`** (writer + reader mode, MJPEG / L16 / JSON / JPEG XS payloads, SDP-driven auto-config, per-stream TX/RX threads, timing histograms), and the new **`MediaIOTask_Inspector`** (QA sink: image data band decode, audio LTC decode, A/V sync offset, continuity checks, periodic log, per-frame callbacks), plus `SDLPlayerTask` / `SDLPlayerOldTask` for display sink (with transparent compressed-video decode; see `proav_framesync.md` for the FrameSync-based vs legacy split). A **`JpegXsImageCodec`** using vendored SVT-JPEG-XS provides JPEG XS encode/decode for planar YUV 4:2:2/4:2:0 at 8/10/12-bit. The `mediaplay` utility exercises all backends end-to-end. Supporting utilities: `CRC<T>` template, `ImageDataEncoder`, `ImageDataDecoder`, format-agnostic `LtcDecoder::decode(const Audio &, channelIndex)` overload.

**Phase 4b additions (this session):** `EnumList` (ordered Variant-compatible list of runtime-typed enum values, DataStream round-trip); `InterlaceMode` TypedEnum replacing `ImageDesc`'s `bool interlaced`; `AudioTestPattern` rewrite with per-channel `EnumList<AudioPattern>` dispatch and seven new patterns (SrcProbe, ChannelId, WhiteNoise, PinkNoise, Chirp, DualTone, PcmMarker); `MediaIO::unknownConfigKeys()` + `validateConfigKeys()` with `ValidationMode::Lenient`/`Strict`; `MediaIO::Output`/`Input` semantics clarified (Output = source, Input = sink); `mediaplay` CLI renamed `-s`/`-d`/`--sc`/`--dc` + three-column config schema help layout.

**Phase 4c additions:** `MediaIOTask_ImageFile` sidecar audio — automatic Broadcast WAV sidecar alongside image sequences (write, read, seek, source hint); three new `MediaConfig` keys: `SidecarAudioEnabled` (bool, default true), `SidecarAudioPath` (String override), `AudioSource` (Enum `AudioSourceHint`); `AudioSourceHint` TypedEnum in `enums.h` (Sidecar/Embedded). Auto-sidecar `.imgseq` promoted to always-on-by-default with `SaveImgSeqEnabled` (bool, default true); sidecar filename auto-derived from sequence pattern when `SaveImgSeqPath` is empty (e.g. `shot_####.dpx` → `shot.imgseq`); mask-based reader auto-discovers the conventionally-named sidecar. `ImgSeq` gains an `audioFile` JSON field so the sidecar records the companion `.wav` filename. Sequence writer auto-creates the output directory (all missing parents). `String::truncated(maxChars)` utility added. `String::to<>()` / `toInt()` / `toUInt()` / `toDouble()` rewritten on `strtoll`/`strtoull`/`strtod`: proper `Error::Invalid` / `Error::OutOfRange` propagation, digit-group separator stripping (`_`, `,`, `'`), base-prefix detection (`0x`/`0b`/`0o`), `bool` delegation to `toBool()`. `MediaIOTask_Inspector` simplified log format and emits a structured final-report block on `close()`. Logger console formatter column order improved (timestamp and source before level).

**Phase 4e additions:** standalone `Clock` abstraction (`Clock`, `WallClock`, `SyntheticClock`) in core; `SDLAudioClock` replaces `SDLAudioPacerClock` (per-device `ClockDomain`, filtered `rateRatio()` for audio drift signalling); `FrameSync` resyncs a source stream to a destination clock's cadence via repeat/drop plus drift-corrected audio resampling, with selectable `DropOldest` / `Block` overflow policies; new `SDLPlayerTask` / `createSDLPlayer()` built on FrameSync displaces the FramePacer path, which is retained as `SDLPlayerOldTask` / `createSDLPlayerOld()` (deprecated); `MediaConfig::SdlPlayerImpl` chooses between them. See `proav_framesync.md`.

**Phase 4d additions (this session):** `MediaIOTask_Converter` no-drop write semantics — converter write path changed from hard `TryAgain` reject at capacity to a one-shot warning; frames are never silently dropped. `MediaIO::writeDepth()` / `writesAccepted()` API added: `writeDepth()` reflects the backend's preferred write pipeline depth (set via `MediaIOCommandOpen::defaultWriteDepth`); `writesAccepted()` computes available slots as `writeDepth() - pendingWrites() - task->pendingOutput()`, clamped to zero. `MediaIOTask::pendingOutput()` virtual added (default 0); `MediaIOTask_Converter` overrides it to return its output FIFO depth. `mediaplay` `Pipeline` back-pressure migrated from hardcoded `MaxInflightConverter`/`MaxInflightPerSink` counters to `writesAccepted()` queries on the downstream stage. Pipeline shutdown drain: source EOF with in-flight converter frames sets `_sourceAtEnd` and allows `drainConverter()` to flush remaining output to sinks before calling `finish(0)`. `WriteBeyondCapacity` test updated; `WritesAccepted` test added.

**Phase 4f additions:** `Enum` infrastructure overhaul — `PROMEKI_REGISTER_ENUM_TYPE` macro pins entry tables to rodata with three compile-time `static_assert` guards (non-empty, unique names/values, default present); `Enum::registerDefinition()` static-storage path added alongside the existing heap-allocating `registerType()`; `Enum::Definition` gains three parallel `StringLiteralData *` caches (`nameLit`, `entryNameLits`, `entryQualifiedLits`) so `typeName()`, `valueName()`, and in-list `toString()` return zero-copy literal-backed `String`s; two `Enum`s with the same in-list value share the same `cstr()` pointer enabling short-circuit `String ==`. `StringRegistry<Name>` migrated from phantom struct tag to `CompiledString` NTTP — `VariantDatabase<Name>`, `MediaConfig`, `Metadata`, `LibraryOptions`, `MediaIOStats`, `MediaIOParams`, `MediaIOStats`, `Config`, `ClockDomain`, `Benchmark` all updated. `PROMEKI_DECLARE_ID` macro introduced (replacing `static inline const ID = declareID(...)`) making well-known IDs `constexpr`-compatible. Phantom tag structs (`MediaConfigTag`, `LibraryOptionsTag`, `MediaIOStatsTag`, `MediaIOParamsTag`, `ConfigTag`) removed. `CompiledString` data members made public (C++20 structural type requirement for class-type NTTPs). `PROMEKI_INTENTIONAL_LEAK(ptr)` macro added to `util.h`; delegates to `__lsan_ignore_object()` under ASAN/LSAN, no-op otherwise. `etc/valgrind.supp` added with suppressions for the three intentional-leak call sites in `enum.cpp`. `stringregistry_constexpr.md` plan document removed (work complete). `CODING_STANDARDS.md` updated: well-known enum example switched to `PROMEKI_REGISTER_ENUM_TYPE`.

**Phase 4g additions:** `VideoCodec` + `AudioCodec` TypeRegistry infrastructure; `MediaPacket` shareable data object; `JpegVideoEncoder` / `JpegVideoDecoder` wrappers over `JpegImageCodec`; `JpegXsVideoEncoder` / `JpegXsVideoDecoder` wrappers over `JpegXsImageCodec`; `MediaIOTask_VideoEncoder` + `MediaIOTask_VideoDecoder` generic encoder/decoder MediaIO backends; `MediaIOTask_RawBitstream` elementary-stream file sink (h264/h265/hevc/bit extensions); CUDA utility (`include/promeki/cuda.h`); `NvencVideoEncoder` + `NvDecVideoDecoder` GPU-accelerated H.264/HEVC codec backends (gated behind `PROMEKI_ENABLE_NVENC` / `PROMEKI_ENABLE_NVDEC`; see `docs/nvenc.dox`); `typeregistry_isolation` test pinning cross-type registry isolation; `MediaConfig::CodecName` retired and replaced by typed `MediaConfig::VideoCodec` + `MediaConfig::AudioCodec`; codec string names switched to PascalCase (`H264`, `HEVC`, `JPEG`, `JPEG_XS`, `AAC`, `Opus`, `PCMI_S16LE`, etc.); `videodesc.h` / `videodesc.cpp` deleted (functionality fully covered by `MediaDesc`; basic `MediaDesc` tests migrated into `tests/mediadesc.cpp`).

The legacy `MediaNode` / `MediaPipeline` / concrete `*Node` classes and their two dedicated utilities (`vidgen`, `testrender`) have been deleted. The new `MediaPipeline` class (see `proav_pipeline.md`) is the main remaining work in this phase.

Network optimization core prerequisites — batch `sendmmsg`, kernel pacing (`SO_MAX_PACING_RATE`), `SO_TXTIME` enable path, `PacketTransport` abstraction, `RtpSession` migration — are all shipped (see `proav_optimization.md`).  Remaining optimization work is the per-packet `SCM_TXTIME` deadline computation inside `MediaIOTask_Rtp` and a future DPDK transport backend.

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

See [benchmarking.md](benchmarking.md) for the full plan. **Shipped so far:** library-native `BenchmarkRunner` + `StatsAccumulator`, the unified `promeki-bench` utility (CSC suite only, programmatically generated from `PixelDesc::registeredIDs()`), MediaIO stamp hooks (enqueue / dequeue / taskBegin / taskEnd), the `MediaIO` identifier triple (`localId` / `Name` / `UUID`), three new `MediaConfig` keys (`Name`, `Uuid`, `EnableBenchmark`), Part D live telemetry (`RateTracker`, `BytesPerSecond`/`FramesPerSecond`/drop/repeat/late counters, latency keys, `PendingOperations` from `Strand::pendingCount()`, `MediaIOStats::toString()` compact log-line renderer, urgent `stats()` dispatch), and `mediaplay --stats` / `--stats-interval`. **Still pending:** the non-CSC microbench suites (network / codec / container / concurrency / variantdatabase / histogram), Part E MediaIO end-to-end cases (blocked on `MediaPipeline`), and CI regression integration.

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

16 tracked items in [fixme.md](fixme.md):

| File | Issue | Natural Phase |
|---|---|---|
| `src/core/file.cpp:40` | Windows File implementation is a stub | Phase 2 |
| `src/proav/audiogen.cpp:66` | Audio generation doesn't handle planar formats | Phase 4 |
| `src/core/datetime.cpp:112` | Should use `String::parseNumberWords()` instead of `strtoll` | Phase 7 |
| various | Replace direct std library usage with library wrappers | Phase 7 |
| `src/proav/mediaiotask_quicktime.cpp` | LE float audio storage is lossy (promoted to s16) | Phase 4 |
| `src/core/pixeldesc.cpp` | `raw ` BGR vs RGB byte-order disagreement | Phase 4 |
| `CMakeLists.txt` | SDL incremental-rebuild misses header ABI changes | Phase 4 |
| `include/promeki/bufferpool.h` | BufferPool available but not wired into QuickTime hot path | Phase 4 |
| `src/proav/quicktime_reader.cpp` | Fragmented reader ignores `trex` defaults fallback | Phase 4 |
| `src/proav/mediaiotask_quicktime.cpp` | Compressed audio pull-rate drifts (one packet/video frame) | Phase 4 |
| `src/proav/quicktime_reader.cpp` | Minimal XMP parser only matches `bext:` prefix (blocked on core XML support) | Phase 4 / Core |
| `src/proav/jpegxsimagecodec.cpp` | JPEG XS: SVT packed-RGB validation bug (workaround via CSC to planar) | Phase 4 |
| `src/proav/jpegxsimagecodec.cpp` | JPEG XS: additional matrix/range/colour-space variants | Phase 4 |
| `src/proav/quicktime_writer.cpp` | JPEG XS: QuickTime/ISO-BMFF container support (`jxsm` sample entry) | Phase 4 |
| `src/proav/mediaiotask_rtp.cpp` | JPEG XS: RFC 9134 RTP slice packetization + fmtp generation | Phase 4 |
| `src/proav/quicktime_writer.cpp` | Compressed audio write path missing (remux blocked) | Phase 4 |
