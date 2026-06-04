# libpromeki Development Plan

This directory tracks pending work on the consolidated `promeki`
library and its companions (`promeki-tui`, `promeki-sdl`). Completed
work is **not** kept here — code and git history are the source of
truth. Each plan document is meant to read as "what's still open"
plus enough context for the open items to be intelligible.

## Layout

```
devplan/
├── README.md            (this file — index + current focus + dep graph)
├── fixme/               Tracked FIXME comments (one file per item)
├── core/                Core library work
│   ├── io.md            IODevice, File, FilePath, Dir (Phase 2 carry-ins)
│   ├── streams.md       TextStream type ops, ObjectBase saveState/loadState
│   ├── datastream-consolidation.md  DataStream / DataType / PROMEKI_DATATYPE refactor
│   ├── utilities.md     String / Variant / RegEx enhancements
│   ├── ownership.md     Heap-ownership Phase C migration backlog
│   ├── logger_ring_buffer.md  Crash-handler-readable retained log
│   └── systemcow-mediaio-allocator.md  MemfdRegion + SystemCow Buffer + MediaIOAllocator
├── network/             Network library work
│   ├── sockets.md       Phase 3A — complete; deferred items
│   ├── avoverip.md      Phase 3C — PtpClock + RTP follow-ups
│   ├── 2110.md          SMPTE ST 2110 conformance plan (-10/-20/-21/-30/-31/-40)
│   ├── pcap.md          pcap/pcapng reader + L2/L3/L4 demux + SDP-labeled ST 2110-40 decode (P1-P4 shipped 2026-06-02; P5 writer deferred)
│   ├── srt.md           Phase 3D — SRT shipped; MediaIO backend + bonded listener deferred
│   ├── rtmp.md          Phase 3F/5 — RTMP / RTMPS publisher + subscriber (Phases 0-5 shipped; Phase 6 docs/CMake next)
│   └── tls.md           mbedTLS audit follow-ups — OCSP, 4.x upgrade triggers, deferred items
├── proav/               ProAV / MediaIO subsystem
│   ├── pipeline.md      MediaPipeline class follow-ups
│   ├── planner.md       MediaPipelinePlanner future work
│   ├── backends.md      Per-backend remaining work
│   ├── capabilities_audit.md  describe / proposeInput / proposeOutput audit
│   ├── optimization.md  Network transmit (sendmmsg, kernel pacing, TXTIME)
│   ├── timestamps.md    MediaTimeStamp / ClockDomain follow-ups
│   ├── framesync.md     COMPLETE; stub retained
│   ├── inspector-pcm-marker-decoder.md  COMPLETE; stub retained
│   ├── transcription.md TranscriptionEngine / WhisperCpp follow-ups (streaming, CUDA, diarization)
│   ├── dsp.md           Deferred audio DSP backends
│   ├── nvenc.md         NVENC / NVDEC follow-up work
│   ├── video-signal-carriers.md  VideoPortRef / SdiSignalConfig / HdmiSignalConfig / VideoReferenceConfig
│   ├── ntv2.md          AJA NTV2 SDI / HDMI MediaIO backend (build scaffolding shipped)
│   ├── v4l2-m2m-codec.md  V4l2VideoEncoder/Decoder + V4l2M2mCodec engine (shipped 2026-06-01)
│   └── quicktime.md     QuickTime writer drain-at-close (deferred)
├── music/               Phase 6
│   ├── theory.md        Phase 6A/6B — unstarted
│   └── midi.md          Phase 6C/6D — unstarted
├── tui/                 Phase 5
│   └── widgets.md       TUI widgets to build
├── infra/               Cross-cutting infrastructure
│   ├── benchmarking.md  BenchmarkRunner / promeki-bench remaining suites
│   ├── promeki-test.md  Functional test runner (shipped 2026-05-05)
│   ├── audit.md         2026-04-25 audit findings register (90 open)
│   ├── qemu-cross-testing.md  qemu-user wiring for cross-build CI/CD (unstarted)
│   ├── cross-rpi4.md    Raspberry Pi 4 cross-compile (sysroot + build shipped 2026-06-01)
│   └── valgrind.md      COMPLETE; stub retained
└── demos/
    └── promeki-pipeline.md  Vue 3 / Vue Flow demo (all phases shipped)
```

## Current focus

22. **TUI infrastructure + terminal write-path consolidation (SHIPPED 2026-06-04)** —
   `AnsiStream` expanded with the full modern terminal escape vocabulary:
   `MouseTracking` / `CursorStyle` enums, `Strikethrough` style, capability-aware
   `setForeground(Color, ColorSupport)` / `setBackground(Color, ColorSupport)`,
   static `resetSeq` / `styleSeq` / `foregroundSeq` / `backgroundSeq` / `foreground256Seq`
   / `foregroundRGBSeq` / `hyperlinkSeq` sequence builders (used by logger formatter and
   TUI cell flusher), `write(int64_t/uint64_t/double)` overloads, `cursorNextLine` /
   `cursorPrevLine` / `setCursorColumn` / `cursorHome` / `setCursorStyle` cursor primitives,
   screen-clear variants (`clearScreenAndHome/BeforeCursor/AfterCursor/clearScrollback`),
   line/character insert+delete, `resetScrollingRegion`, mouse tracking with `MouseTracking`
   level enum, bracketed paste, focus reporting, synchronized output (mode 2026), OSC 8
   hyperlinks, OSC 2 window title, OSC 52 clipboard, `softReset`/`hardReset`, `bell`.
   `Terminal` now owns the single ordered write path: a `File` adopting the output fd
   (write-buffered via new `BufferedIODevice` write-buffering layer) plus an `AnsiStream`
   layered over it; `ansiStream()` accessor exposes it; `TuiSubsystem` binds its `_ansiStream`
   reference from `_terminal.ansiStream()` rather than constructing a second stream over
   stdout.  `Terminal` gained `enableFocusReporting` / `disableFocusReporting` + five RAII
   guards (`RawModeGuard`, `AlternateScreenGuard`, `MouseTrackingGuard`, `BracketedPasteGuard`,
   `FocusReportingGuard`) and `emergencyRestore()` (async-signal-safe single-`write` restore).
   `CrashHandler` gained an async-signal-safe cleanup handler registry
   (`addCleanupHandler` / `removeCleanupHandler`); `TuiSubsystem` registers
   `Terminal::emergencyRestore` as a hook so a fatal crash leaves the terminal usable and
   the crash report readable.  `BufferedIODevice` gained write buffering: `setWriteBuffered`,
   `isWriteBuffered`, `setWriteBufferCapacity`, `writeBufferCapacity`, an abstract
   `writeToDevice()` virtual (all concrete subclasses migrated), and `bufferedBytesPending()`;
   `File` migrated `write()` to `writeToDevice()`, gained `File(FileHandle, mode, ownsHandle)`
   fd-adoption constructor, and its `pos()` accounts for pending buffered bytes.
   `EventLoop` gained an internal `EventLoopPollCache` that rebuilds the `pollfd` array only
   when IoSources change, avoiding O(N) rebuild on every wake.  `TuiInputParser` gained
   `ParsedEvent::Paste` + `ParsedEvent::FocusIn/FocusOut` types, bracketed-paste accumulation
   with 8 MiB guard, and `\033[I` / `\033[O` focus-event decoding.  `TuiSubsystem` gained
   `dispatchPasteEvent` / `dispatchWindowFocusEvent` / `crashRestore` static hook + `isWindowFocused()`.
   `Widget` gained `windowFocusEvent(WindowFocusEvent *)` virtual; new `WindowFocusEvent`
   class (`Event::registerType("WindowFocus")`).  Logger console formatter migrated from
   hand-rolled `"\033[...]m"` literals to `AnsiStream::foregroundSeq` / `styleSeq` /
   `resetSeq`.  `screen.cpp` color-emission helpers removed; `setForeground(Color, ColorSupport)`
   / `setBackground(Color, ColorSupport)` used directly.  Tests: 6 write-buffering cases
   (`bufferediodevice.cpp`), 3 File fd-adoption cases (`file.cpp`), focus toggle + RAII
   guards (`terminal.cpp`), cleanup handler registry + fire-ordering in fork crash test
   (`crashhandler.cpp`), bracketed-paste + focus events (`tui/inputparser.cpp`),
   `WindowFocusEvent` + `windowFocusEvent` dispatch (`tui/widget.cpp`), 40+ new
   `AnsiStream` cases.

1. **MediaPipeline polish** — `docs/mediapipeline.dox` authoring
   guide, `docs/mediaplay.dox` grammar reference. The functional test
   runner (`utils/promeki-test/`) is shipped and covers roundtrip,
   codec, audio, RTP, and FrameBridge suites; CI integration and the
   FrameBridge `acceptPending` bug remain open. See
   [proav/pipeline.md](proav/pipeline.md),
   [proav/backends.md](proav/backends.md), and
   [infra/promeki-test.md](infra/promeki-test.md).
2. **RTP follow-ups** — mid-stream descriptor discovery, RTP
   timestamp wrap, ST 2110-20 10/12-bit pgroup, L24, ST 2110-40,
   per-packet `SCM_TXTIME` deadlines via `RtpPacingMode::TxTime`.
   See [proav/backends.md](proav/backends.md) and
   [proav/optimization.md](proav/optimization.md).
3. **JPEG XS container + RTP** — QuickTime / ISO-BMFF `jxsm` sample
   entry (blocked on procuring ISO/IEC 21122-3:2024) and RFC 9134
   slice mode K=1. See [`fixme/`](fixme/).
4. **Audit remediation R2–R6** — 90 open findings from the
   2026-04-25 audit. R1 is complete. See
   [infra/audit.md](infra/audit.md).
5. **Codec abstraction follow-ups** — generic `configKeys()`
   discovery, drain hook for stateful temporal codecs, GPU-resident
   bridges. See [proav/backends.md](proav/backends.md) and
   [proav/planner.md](proav/planner.md).
6. **ARM / cross-build robustness** — `PROMEKI_CONFIG_FILE` preset
   system, `cmake/configs/cross-aarch64-linux.cmake` + toolchain, and
   per-feature `#if PROMEKI_ENABLE_*` header guards shipped
   (2026-05-15). Raspberry Pi 4 cross-compile (Trixie sysroot, GCC-14,
   `cross-rpi4.cmake` preset, `scripts/build-rpi4-sysroot.sh`) shipped
   2026-06-01 with full clean build green. Next: run on hardware, wire
   qemu-user CI lane. See [infra/cross-rpi4.md](infra/cross-rpi4.md)
   and [infra/qemu-cross-testing.md](infra/qemu-cross-testing.md).
20. **V4L2 M2M codec backend (SHIPPED 2026-06-01)** — `V4l2M2mCodec`
   shared engine + `V4l2VideoEncoder` + `V4l2VideoDecoder` ("V4L2"
   backend, H264/HEVC, NV12/NV16/P010). Profile/level, VUI colorimetry,
   HDR static metadata, caption-SEI bitstream surgery. DMABUF zero-copy
   primitives in the engine (proven on vicodec); backend wiring and Pi4
   / VCU bring-up are next. See
   [proav/v4l2-m2m-codec.md](proav/v4l2-m2m-codec.md).
21. **DMABUF buffer backend + V4L2MediaIO zero-copy (SHIPPED 2026-06-01)**
   — `DmabufBufferImpl`, `DmaHeap`, `MemDomain::Dmabuf` / `MemSpace::Dmabuf`,
   `BufferImpl::ReleaseCallback`. `V4l2MediaIO` auto-selects dma-buf
   zero-copy for uncompressed captures (EXPBUF probe); compressed formats
   stay MMAP. udev rule in `etc/udev-rules/`. See
   [proav/backends.md](proav/backends.md).
8. **AJA NTV2 build scaffolding** — `thirdparty/libajantv2` submodule
   (ntv2_18_0_0), `PROMEKI_ENABLE_NTV2` CMake option, wired into
   `promeki` as PRIVATE link target (2026-05-16). MediaIO backend
   (`NTV2MediaIO`) lands in a follow-up changeset. See
   [proav/backends.md](proav/backends.md) and
   [proav/ancdata.md](proav/ancdata.md) Phase 5.
7. **DataStream / DataType consolidation** — Phases 1-3 complete
   (2026-05-16): `PROMEKI_DATATYPE` macro, `DataTypeID` enum, 47 types
   migrated, `enum DataStream::Type` and `Variant::TypeXxx` aliases
   removed. Phase 4 cleanup (SFINAE traits, `docs/dataobjects.dox`,
   `CODING_STANDARDS.md`) remains open.
   See [`core/datastream-consolidation.md`](core/datastream-consolidation.md).
9. **Validity sentinels on `TimeStamp` / `Duration` / `DateTime`**
   — SHIPPED 2026-05-17. `INT64_MIN` as `Invalid`; default-construct
   = invalid; `Duration::zero()` for explicit zero; arithmetic
   propagates invalid; `MediaTimeStamp::isValid()` requires both
   domain and inner `TimeStamp` to be valid; `MediaTimeStamp::nanoseconds()`
   convenience accessor added.  All consuming sites updated
   (`PacingGate`, `EventLoop`, `RtpSession`, all MediaIO backends).
   See [`proav/timestamps.md`](proav/timestamps.md).
10. **`AudioBuffer` MediaTimeStamp flow + `PcmAudioPayload` push/pop**
   — SHIPPED 2026-05-18. Anchor queue threads PTS through the ring
   FIFO; filter-delay correction back-adjusts resampled anchors;
   `resamplerSampleDelta()` exposes drift accounting;
   `push(PcmAudioPayload)` / `popPayload` / `popWaitPayload` /
   `nextSamplePts()` added; RTP audio packetizer migrated.
   See [`proav/timestamps.md`](proav/timestamps.md).
11. **`std::atomic` → `promeki::Atomic` migration + concurrency additions**
   — SHIPPED 2026-05-18. All `std::atomic<T>` / `std::memory_order_*`
   uses in our code migrated to `Atomic<T>` / `MemoryOrder`.  New
   additions to `atomic.h`: `MemoryOrder` enum + `toStdMemoryOrder()`,
   `atomicThreadFence()`, explicit-order overloads on all `Atomic<T>`
   methods, `compareExchangeWeak`, `AtomicRef<T>` (wraps
   `std::atomic_ref<T>`), `AtomicFlag` (wraps `std::atomic_flag`).
   New header `once.h`: `OnceFlag` + `callOnce` wrapping
   `std::once_flag` / `std::call_once`.  New in `uniqueptr.h`:
   `UniquePtr<T[]>` array specialization + `uniquePointerCast`.
   Tests: `once.cpp` (new), `atomic.cpp` extended, `uniqueptr.cpp`
   extended.  Audit finding #19 partial: `compareExchangeWeak` added;
   `requires` constraint on arithmetic ops remains open.
12. **Submodule auto-init system**
   — SHIPPED 2026-05-18. `cmake/PromekiSubmodules.cmake` maps each
   `thirdparty/` submodule to the CMake feature flag(s) that require
   it and runs `git submodule update --init --recursive` on first
   configure.  Mirror URL rewriting via `PROMEKI_MIRRORS_FILE` or
   well-known per-user / system config paths (shared search order with
   the companion script).  `scripts/mirror-thirdparty.py` handles
   GitLab project auto-create + `git push --mirror` for self-hosted
   mirrors; reads the same CMake-syntax config file.
   `cmake/mirrors.example.cmake` documents the config format.
   Replaces deleted `scripts/mirrors.conf` + `scripts/setup-mirrors.sh`.
14. **Speech-to-text (TranscriptionEngine + WhisperCpp Phase 1)** —
   SHIPPED 2026-05-25. `TranscriptionEngine` abstract base + backend
   registry; `Transcript` / `TranscriptWord` / `TranscriptList` value
   types; `SubtitleCueBuilder` cue-shaping layer; `MediaConfig`
   Transcription* + SubtitleCue* keys; `TranscriptionMode` /
   `TranscriptionChannelMode` enums; `Metadata::Transcript` key;
   vendored `whisper.cpp` v1.8.4 `WhisperCpp` backend (CPU, batch
   only); `Dir::models()` + `LibraryOptions::ModelsDir` convention;
   `promeki-fetch-model` CLI (SHA-256-verified Hugging Face downloader);
   `docs/whisper.md`.  Streaming mode, CUDA backend, diarization, and
   HTTP-streaming downloads are deferred.
   See [proav/transcription.md](proav/transcription.md).
15. **FLAC / Vorbis / MP3 via libsndfile** — SHIPPED 2026-05-25.
   Five new vendored submodules (`libogg`, `libflac`, `libvorbis`,
   `mpg123`, `lame`); three new feature flags
   (`PROMEKI_ENABLE_FLAC/VORBIS/MP3`); `audiofile_libsndfile.cpp`
   extended for `flac`, `mp3`, `ogg`, `oga`, `mpeg` extensions.
   `AudioDataEncoder` gained `leadInBits` (0..32 bit cells of constant
   `+A` pre-roll; absorbs MP3/Vorbis onset-transient erosion).
   `AudioDataDecoder` gained `expectedAmplitude` (quarter-amplitude
   threshold for the sustained-positive sync-edge filter; rejects
   codec pre-echo wobbles).  Tests: `tests/unit/audiofile_codecs.cpp`
   (8 cases: factory routing, testmedia reader checks, end-to-end
   encoder/decoder round-trips through FLAC/Vorbis/MP3).
   All codec deps shipped as shared objects — see item 17.
   See [proav/backends.md](proav/backends.md).
16. **`enums.h` split + optional display labels** — SHIPPED 2026-05-26.
   The monolithic `include/promeki/enums.h` (3806 lines) was split into
   17 per-subsystem headers (`enums_anc.h`, `enums_audio.h`,
   `enums_clock.h`, `enums_codec.h`, `enums_color.h`, `enums_jxs.h`,
   `enums_mediaio.h`, `enums_ndi.h`, `enums_network.h`, `enums_rtmp.h`,
   `enums_rtp.h`, `enums_st2110.h`, `enums_subtitle.h`,
   `enums_timecode.h`, `enums_tpg.h`, `enums_transcription.h`,
   `enums_video.h`) so editing one group's enums no longer rebuilds
   every consumer in the tree; all include sites updated.  `Enum` also
   gained an **optional, presentational** display-label facility:
   `Enum::Entry::displayName` (third entry-row field) and
   `Enum::Definition::displayName`, the new
   `PROMEKI_REGISTER_ENUM_TYPE_DISPLAY` macro (plain
   `PROMEKI_REGISTER_ENUM_TYPE` forwards with `nullptr`), and accessors
   `valueDisplayName()` / `typeDisplayName()` / `Type::displayName()` /
   static `displayNameOf()`.  Labels are never parsed, looked up, or
   serialized — `lookup`/`valueOf`/`toString`/the `"Type::Value"` wire
   form always use the programmatic name — and are zero-cost when
   unused (literal caches allocated only when a label is registered).
   `CODING_STANDARDS.md` § Well-Known Enums updated for both changes.
   Audit finding #29 (entry triple-declaration sync) is *not* addressed
   by this work and remains open.
17. **Static → shared vendored deps + `$ORIGIN` RPATH** — SHIPPED 2026-05-26.
   LGPL and codec deps (libsndfile, libmpg123, libmp3lame, libFLAC,
   libogg, libvorbis, libopus, fdk-aac) switched from static archives
   absorbed into `libpromeki.so` to standalone shared objects shipped
   beside it in `lib/`.  `libpromeki_srt.so` likewise changed from a
   `ld -r` + `objcopy --localize-symbols` static archive to a
   `c++ -shared --exclude-libs` shared object — same mbedTLS-3.6
   isolation guarantees, simpler toolchain requirements.  Every binary
   and library now carries an `$ORIGIN`-relative RPATH so `build/bin/`
   + `build/lib/` form a self-contained, relocatable bundle.  A
   `cmake/promeki_stage_shared_deps.cmake` POST_BUILD helper stages the
   vendored `.so` families (preserving symlink chains) into `build/lib/`
   so the build tree mirrors the install layout.  `PROMEKI_FORCE_BUNDLED_LIBS`
   CMake option available to switch to legacy `DT_RPATH` if needed.
19. **mDNS/DNS-SD + St2110TrafficCalc + ObjectBase EventLoop refactor** — SHIPPED 2026-05-29.
   *mDNS:* Native mDNS/DNS-SD (RFC 6762/6763) built on `UdpSocket` + `MulticastManager`.
   `MdnsManager` (Thread subclass) owns dual IPv4+IPv6 sockets, multicast joins on
   `224.0.0.251` / `ff02::fb`, `IP_PKTINFO`/`IPV6_PKTINFO` ingress attribution, and fan-out
   under mutex to registered consumers. Consumer classes: `MdnsBrowser` (PTR-driven cache,
   continuous-query backoff RFC 6762 §5.2, KAS RFC 6762 §7.1, cache-flush semantics),
   `MdnsTypeBrowser` (RFC 6763 §9 meta-query `_services._dns-sd._udp.local.`),
   `MdnsResolver` (one-shot wrapper over `MdnsBrowser`), `MdnsPublisher` (full state machine
   Idle→Probing→Announcing→Published→Conflicted/Withdrawing; auto-rename RFC 6762 §9;
   KAS on responder side). Value types: `MdnsServiceType`, `MdnsTxtRecord` (RFC 6763 §6,
   `Presence` enum), `MdnsServiceInstance`, `MdnsRecord` (publish-side RR + named factories).
   Wire layer: `MdnsPacket` + mjansson/mdns single-header parser (new `thirdparty/mdns`
   submodule); `mdnsBuildAnnounce`/`mdnsBuildGoodbye`/`mdnsBuildProbe` outbound encoders;
   `mdnsname.h` label escape/unescape helpers (RFC 1035 §5.1). `Application::mdnsManager()`
   lazy-create global. Feature flag `PROMEKI_ENABLE_MDNS`. DataTypes: 0x75–0x77.
   ~150 doctest cases across 12 per-class test files.
   *St2110TrafficCalc:* `St2110TrafficCalc` aggregator + `promeki-2110-calc` CLI — given
   (VideoFormat, sampling/depth or PixelFormat, senderType) derives the complete ST 2110-21
   Result (packet geometry, rates, VRX/CMAX, TRO_DEFAULT, TP/TROFF params); 8 test cases.
   *ObjectBase EventLoop refactor:* `eventLoop()` now out-of-line; derived from
   `_thread->threadEventLoop()` rather than a cached `_eventLoop` pointer captured at
   construction — eliminates stale-pointer risk after `moveToThread()`. `UdpSocket` gains
   `setReceivePktInfo(bool)` + `readDatagramWithIfIndex()` for per-packet ingress attribution.
   *Mirror script:* `scripts/mirror-thirdparty.py` enhanced with multi-host GitLab token
   support and `PROMEKI_MIRROR_APIS` environment variable for selective API activation.
18. **`MediaIOParams` transactional get/set + build infra** — SHIPPED 2026-05-27.
   `MediaIOParams` replaces the old `VariantDatabase`-based `(name, params)`
   verb form with an ordered `Get`/`Set` block (`include/promeki/mediaioparams.h`).
   `CommandMediaIO` owns the apply loop; backends implement `getParam` /
   `setParam` / `validateParam` hooks. Atomic semantics: validate-all-up-front,
   abort-before-anything-applied, best-effort rollback (reverse-order prior
   restore) on mid-apply failure; aborted actions report `Error::TransactionAborted`
   (new error code). `RtpMediaIO` migrated from `GetSdp` verb to `get(ParamSdp)`.
   Tests: `tests/unit/mediaioparams.cpp` (13 cases / 66 assertions, covering
   ordering, non-atomic failure isolation, atomic validation abort, mid-apply
   rollback, unreadable-prior rollback skip, failing-restore tolerance, empty
   block, and pre-open short-circuit). Also shipped: `cmake/PromekiSplitDebug.cmake`
   + `PromekiStripSo.cmake` (DWARF split to `build/lib-debug/`);
   `cmake/configs/proav-embedded.cmake` (no software codecs / fonts / audio
   preset); `examples/downstream/` (runnable `find_package` + `add_subdirectory`
   example); `PROMEKI_ENABLE_SRC` / `PROMEKI_ENABLE_FREETYPE` / `PROMEKI_ENABLE_JPEG`
   header guards in `FrameSync`, `SubtitleRenderer`, `VideoTestPattern`,
   `JpegVideoCodec` so those headers compile cleanly with the corresponding
   feature disabled; `docs/building.md` updated with split-debug and
   downstream-consumption sections.
13. **`BasicThread` + `Thread` refactor** — SHIPPED 2026-05-18.
   `BasicThread` (Pimpl, move-only, no `ObjectBase`) owns OS thread,
   scheduling, affinity, naming, and static helpers (`sleepMs/Us/Ns`,
   `sleep(Duration)`, `yield`, `idealThreadCount`, `currentNativeId`,
   `setCurrentThreadName`, `priorityMin/Max`). `Thread` refactored to
   wrap a `BasicThread` member; `Thread::start()` now returns `Error`
   and bails cleanly on OS failure (no deadlock). All `std::thread`
   consumers converted: `ThreadPool` workers, `Logger` worker,
   `SignalHandler` watcher, `NdiDiscovery`, `NdiMediaIO` capture,
   `V4l2MediaIO` video/audio, `SdlPlayer` pull, `RtpChaosShim`
   endpoints — each with a unique OS-level name and a `nextInstanceId<Tag>()`-backed
   `instanceID()` accessor. `ObjectBase` now explicitly deletes
   copy/move; 9 derived classes shed redundant explicit deletes.
   `BasicThread::detach()` removed as unsafe under Pimpl.

## Phase status (overview)

| Phase | Topic                                | Status                              |
| :---: | ------------------------------------ | ----------------------------------- |
| 1     | Core containers / concurrency        | COMPLETE                            |
| 2     | IO / filesystem / streams            | COMPLETE (carry-ins in `core/`)     |
| 3A    | Sockets                              | COMPLETE                            |
| 3B    | HTTP / WebSocket / TLS               | COMPLETE                            |
| 3C    | AV-over-IP (RTP / SDP / multicast)   | mostly complete; PtpClock pending   |
| 3E    | mDNS / DNS-SD                        | COMPLETE (see network/mdns.md; NSEC, per-iface sockets, Windows pktinfo deferred) |
| 3D    | SRT (Secure Reliable Transport)      | shipped; SrtMediaIO backend deferred |
| 4     | ProAV — MediaIO framework + backends | framework + 18 backends shipped;<br>NTV2 scaffolding landed; backend pending; follow-ups in `proav/` |
| 4A    | MediaPipeline                        | shipped; docs + tests pending       |
| 4M    | MediaPipelinePlanner                 | shipped (single-hop + 2-hop codec)  |
| 5     | TUI widgets                          | unstarted                           |
| 6     | Music library                        | unstarted                           |
| 7     | Cross-cutting (Result, Variant, …)   | ongoing                             |

## Dependency graph (high level)

```
Phase 1 ──┬─► Phase 2 ──┬─► Phase 3A ─► Phase 3B
          │             │              ├► Phase 3C ── PtpClock (open)
          │             │              └► Phase 3D (SRT) ── SrtMediaIO backend (open)
          │             │
          │             ├─► Phase 4 (MediaIO + backends + MediaPipeline)
          │             │       │
          │             │       ├─► proav/backends.md follow-ups
          │             │       ├─► proav/optimization.md (TXTIME, DPDK)
          │             │       └─► proav/quicktime.md (drain-at-close)
          │             │
          │             └─► Phase 7 (ObjectBase saveState/loadState)
          │
          ├─► Phase 5 (TUI widgets)         [independent]
          └─► Phase 6 (Music library)       [independent]

Phase 7 (cross-cutting) — ongoing
```

## Standards and testing

All work follows `CODING_STANDARDS.md` at the repo root. Every new
class requires complete doctest unit tests; every modification to an
existing class updates its tests. Test framework is doctest, vendored
under `thirdparty/doctest/`. Three test executables —
`unittest-promeki`, `unittest-tui`, `unittest-sdl` — built and run by
`build check`. The `scripts/precommit.sh` gate runs configure →
format-check → `-Werror` build → `check` → doxygen.

## Conventions

- Plan docs are pruned as work lands. The shape we want is: brief
  "what shipped" statement, then a checklist or bullet list of open
  items. Anything explanatory enough to call "design notes" should
  live in `docs/*.dox`, not here.
- Class names mentioned in plan docs reflect the **current**
  codebase. The MediaIO ports/strategies/factory refactor renamed
  `MediaIOTask_X` → `XMediaIO`; older docs caught in transitional
  states should be updated when revisited.
- When closing a plan doc, leave a one-paragraph stub pointing at
  `docs/` (or git history) instead of deleting it, so readers
  searching for the topic land on the canonical reference.
