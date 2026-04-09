# ProAV Concrete Nodes

**Phase:** 4B
**Dependencies:** Phase 4A (MediaNode, MediaSink, MediaSource, MediaPipeline)
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

All nodes derive from `MediaNode`, implement `processFrame()`, and declare their sinks/sources. All config option keys use UpperCamelCase (CamelCaps), starting with an upper-case letter (see `proav_pipeline.md` for details).

**Additional nodes for vidgen:** TestPatternNode (combined video+audio+metadata generator with motion), TimecodeOverlayNode, JpegEncoderNode, FrameRateControlNode, RtpVideoSinkNode, RtpAudioSinkNode are specified in [vidgen.md](vidgen.md).

---

## MediaIO Framework — COMPLETE

Generic abstract media I/O framework providing a uniform interface for reading and writing media (video frames, audio, metadata) from containers, image sequences, and hardware I/O devices.

**Files:**
- [x] `include/promeki/mediaio.h`
- [x] `src/proav/mediaio.cpp`
- [x] `include/promeki/mediaiotask.h`
- [x] `src/proav/mediaiotask.cpp`
- [x] `include/promeki/strand.h` (header-only)
- [x] `include/promeki/mediadesc.h` (renamed from VideoDesc)
- [x] `src/proav/mediadesc.cpp`
- [x] `include/promeki/videodesc.h` (deprecated alias: `using VideoDesc = MediaDesc`)
- [x] `include/promeki/sdl/sdlplayer.h` + `src/sdl/sdlplayer.cpp` (SDLPlayerTask write backend + createSDLPlayer factory)
- [x] `utils/mediaplay/main.cpp` (mediaplay utility: pumps any MediaIO source into SDL player)
- [x] `tests/mediaio.cpp`
- [x] `tests/strand.cpp`
- [x] `docs/mediaio.dox` (subsystem and backend authoring guide)

**Design — Controller/Task split:**
- `MediaIO` is the public controller: derives from `ObjectBase`, uses `PROMEKI_OBJECT`, is **not** subclassed by backends
- `MediaIOTask` is the backend interface: plain class (no ObjectBase), pure abstract virtuals are `private`, `MediaIO` is `friend`; derived classes override private virtuals (NVI pattern enforces dispatch-only-through-MediaIO contract)
- All interaction between MediaIO and its task flows through `MediaIOCommand` objects; the task never sees MediaIO directly

**Design — Command hierarchy:**
- `MediaIOCommand` base: holds `Promise<Error>`, `RefCount`, and `Type` enum; shared via `SharedPtr<MediaIOCommand, false>` (COW disabled — polymorphic proxy path only, never cloned)
- `PROMEKI_MEDIAIO_COMMAND(NAME, TYPE_TAG)` macro injects `type()` override and an asserting `_promeki_clone()`
- Concrete commands: `MediaIOCommandOpen`, `MediaIOCommandClose`, `MediaIOCommandRead`, `MediaIOCommandWrite`, `MediaIOCommandSeek`, `MediaIOCommandParams`, `MediaIOCommandStats`
- Each command struct carries typed input fields (set by MediaIO) and output fields (set by the task)

**Design — Strand and threading:**
- `Strand` (new class): serialized executor backed by `ThreadPool`; tasks submit to a `std::deque<Entry>` under a `Mutex`; only one task runs at a time per strand, but pool threads are returned between tasks; `cancelPending()` drains the queue and fulfills each future with `Error::Cancelled`; `waitForIdle()` / `isBusy()` for idle detection; destructor waits for idle; 10 unit tests in `tests/strand.cpp`
- Each `MediaIO` instance holds a `Strand _strand{pool()}` — per-instance serialization, cross-instance concurrency on the shared `ThreadPool`
- All commands (open, close, read, write, seek, params, stats) are submitted to the strand; the calling thread blocks on the future or returns `Error::TryAgain`

**Design — Factory and registration:**
- Config-driven factory: `MediaIO::create(Config)`, `createForFileRead()`, `createForFileWrite()`, `defaultConfig(typeName)`, `enumerate(typeName)`
- Backends self-register via `PROMEKI_REGISTER_MEDIAIO(ClassName)` at static init
- `FormatDesc` struct: name, description, extensions, canRead, canWrite, canReadWrite, factory lambda, `defaultConfig` lambda, optional `canHandleDevice` content-probe callback, optional `enumerate` callback
- `MediaIO::adoptTask(MediaIOTask *)` — inject an externally-constructed task (for backends like SDLPlayerTask whose constructor args can't be expressed in a Config); transfers ownership; MediaIO must not be open and must not already own a task

**Design — Three distinct VariantDatabase types:**
- `MediaIOConfig` (tag: `MediaIOConfigTag`) — open-time configuration; keys: `ConfigFilename`, `ConfigType`, plus backend-specific IDs
- `MediaIOStats` (tag: `MediaIOStatsTag`, subclass of `VariantDatabase`) — runtime metrics from `executeCmd(MediaIOCommandStats&)`; standard static IDs: `FramesDropped`, `FramesRepeated`, `FramesLate`, `QueueDepth`, `QueueCapacity`, `BytesPerSecond`, `AverageLatencyMs`, `PeakLatencyMs`, `LastErrorMessage`
- `MediaIOParams` (tag: `MediaIOParamsTag`) — parameterized command params/result; keys entirely backend-defined

**Design — Cached state and non-blocking I/O:**
- MediaIO caches: `_mediaDesc`, `_audioDesc`, `_metadata`, `_frameRate`, `_canSeek`, `_frameCount`, `_currentFrame`, `_defaultSeekMode` — only read/written on user thread after future resolves; no mutexes needed
- `_pendingReadCount` (Atomic) tracks in-flight CmdReads to control prefetch submission
- `_readResultQueue` (Queue) holds completed CmdRead results for non-blocking delivery
- `readFrame(frame, block=true)` / `writeFrame(frame, block=true)` — blocking or `Error::TryAgain`
- `frameAvailable()` — polls whether a completed read result is queued
- `isIdle()` — delegates to `strand.isBusy()`
- `cancelPending()` — cancels strand queue + drains `_readResultQueue`

**Design — EOF latching:**
- Once any CmdRead returns `Error::EndOfFile`, `_atEnd` is set; subsequent `readFrame()` calls return EOF immediately without re-submitting to the strand
- Latch cleared by `seekToFrame()`, `setStep()` (direction change), or `close()`

**Design — Step and prefetch:**
- `setStep(int)` — changing step cancels pending reads and drains stale results; step=0 holds, step<0 reverse, step>1 fast-forward
- `setPrefetchDepth(int n)` / `prefetchDepth()` — user-settable; task's `defaultPrefetchDepth` from CmdOpen is used unless user overrides before open; override is reset on close
- `defaultSeekMode()` — resolved from task's `MediaIOCommandOpen::defaultSeekMode`; `SeekDefault` at call site resolves to this before dispatch

**Design — Mid-stream descriptor updates:**
- Backend sets `cmd.mediaDescChanged = true` and fills `cmd.updatedMediaDesc` in a CmdRead
- MediaIO copies to cache, stamps `Metadata::MediaDescChanged` on frame, emits `descriptorChanged` signal

**Design — Track selection:**
- `setVideoTracks(List<int>)` / `setAudioTracks(List<int>)` — pre-open only, returns `Error::AlreadyOpen` if called while open; passed to CmdOpen for backends with multi-track sources

**Design — Signals:**
- `errorOccurred(Error)` — generic error
- `frameReady` — emitted when a CmdRead completes on the worker
- `frameWanted` — emitted when a CmdWrite completes on the worker
- `writeError(Error)` — only way to observe async (non-blocking) write errors
- `descriptorChanged` — MediaDesc changed mid-stream

**Design — Error additions:**
- `Error::Cancelled` — new code, returned by Strand/Future when a task is cancelled
- `PromiseError` moved to top-level in `future.h` (was nested); `Future<T>::result()` and `Future<void>::result()` both wrap `future.get()` in try/catch and convert exceptions to Error return values

**Design — Frame metadata IDs (new in Metadata):**
- `FrameNumber`, `CaptureTime`, `PresentationTime`, `FrameRepeated`, `FrameDropped`, `FrameLate`, `FrameKeyframe`, `MediaDescChanged`

**Design — Open-failure cleanup contract:**
- If CmdOpen returns an error, MediaIO automatically dispatches CmdClose on the same task instance; backends must tolerate close from failed-open state

**Test coverage:**
- 60+ test cases in `tests/mediaio.cpp`, 10 test cases in `tests/strand.cpp`
- Strand: serial order, no overlap, void tasks, multiple strands concurrent, isBusy, destructor, cancelPending, cancel-empty-queue, cancel-hook-runs
- MediaIO: registry, factory (create/createForFileRead/createForFileWrite/defaultConfig/enumerate), all TPG modes (video-only, audio-only, timecode-only, full generation), error paths (writer-not-supported, nothing-enabled, invalid-pattern, read-before-open, double-open), seeking, canSeek/frameCount, step control, prefetch depth (default/user-override/clamped), defaultSeekMode, track selection, frameAvailable, reopen-same-instance, sendParams, cancelPending, stats, EOF latching, mid-stream descriptor, ImageFile round-trips (DPX/TGA), ImageFile default/override frame rate, AudioFile round-trips (WAV), AudioFile seeking, AudioFile step/EOF, AudioFile missing frame rate, device enumeration, cancellation, TPG audio cadence at 29.97/48k and 30/48k

---

## MediaIOTask_TPG Backend — COMPLETE

Read-only MediaIOTask that generates synchronized test pattern frames.

**Files:**
- [x] `include/promeki/mediaiotask_tpg.h` (renamed from `mediaio_tpg.h`)
- [x] `src/proav/mediaiotask_tpg.cpp` (renamed from `mediaio_tpg.cpp`)
- (tests in `tests/mediaio.cpp`)

**Design:**
- Derives from `MediaIOTask`, registered as "TPG" (no file extensions — generator source)
- Overrides `executeCmd(Open)` / `executeCmd(Close)` / `executeCmd(Read)`; base implementations handle all other commands
- Video: delegates to `VideoTestPattern`; solid color configured via `Color` object (`ConfigVideoSolidColor`); static patterns cached after first generate when step=0; motion applies per-frame offset scaled by step; text burn-in (timecode overlay + custom text via `ConfigVideoText`); AvSync pattern available; bundled FiraCode font is default font for overlay rendering
- `ConfigVideoSize` (Size2Du32, replaces separate Width/Height keys): parsed via `Size2D::fromString()` from config
- Audio: delegates to `AudioTestPattern` (tone, silence, LTC, and new AvSync modes)
- Audio cadence: uses `FrameRate::samplesPerFrame()` for exact NTSC cadence — 29.97/48k emits 1601/1602 pattern summing to 8008 per 5 frames (not constant 1602)
- Timecode: delegates to `TimecodeGenerator`; TC advances by `|step|` frames per CmdRead; TC stamped on both `Frame::metadata()` and `Image::metadata()` (required by TimecodeOverlayNode); default start TC is `01:00:00:00` NDF
- Infinite source: `cmd.frameCount = MediaIO::FrameCountInfinite`, `cmd.canSeek = false`
- All config via `MediaIO::Config` (VariantDatabase); all ConfigID constants declared as static members
- `FormatDesc::defaultConfig` lambda returns fully-populated default Config (all keys, video/audio/timecode groups all disabled by default)

---

## MediaIOTask_ImageFile Backend — COMPLETE

MediaIOTask backend wrapping the ImageFile / ImageFileIO subsystem for single-image file formats.

**Files:**
- [x] `include/promeki/mediaiotask_imagefile.h` (renamed from `mediaio_imagefile.h`)
- [x] `src/proav/mediaiotask_imagefile.cpp` (renamed from `mediaio_imagefile.cpp`)
- (tests in `tests/mediaio.cpp`)

**Design:**
- Derives from `MediaIOTask`, registered as "ImageFile"
- Supports DPX, Cineon, TGA, SGI, RGB, PNM, PPM, PGM, PNG, RawYUV variants
- PNG backend (`ImageFileIO_PNG`) overhauled: libpng+zlib replaced with libspng+zlib-ng; load now fully implemented; supports Mono8/Mono16-BE/LE, RGB8/16-BE/LE, RGBA8/16-BE/LE; O_DIRECT save path; gAMA metadata round-trip; no setjmp/longjmp; 17 unit tests in `tests/imagefileio_png.cpp`
- Content probing via magic-number inspection (DPX/Cineon/PNG/SGI/PNM); `FormatDesc::canHandleDevice` populated
- Default step=0 (set via `cmd.defaultStep = 0` in Open): CmdRead re-delivers the same loaded image indefinitely (hold semantics for still images in a pipeline); step≠0 for single-delivery then EndOfFile
- `cmd.frameCount = 1` for Reader, tracks write count for Writer
- `executeCmd(Open/Reader)` loads the full image immediately and builds mediaDesc; `executeCmd(Close)` releases the cached frame
- Default frame rate: `ConfigFrameRate` key (FrameRate) in Config; if not set, defaults to 24 fps; MediaDesc.frameRate is set from this so downstream consumers (e.g. audio-led pacing in SDLPlayerTask) have a valid rate

---

## MediaIOTask_AudioFile Backend — COMPLETE

MediaIOTask backend wrapping the AudioFile subsystem (libsndfile) for frame-based audio file I/O.

**Files:**
- [x] `include/promeki/mediaiotask_audiofile.h` (renamed from `mediaio_audiofile.h`)
- [x] `src/proav/mediaiotask_audiofile.cpp` (renamed from `mediaio_audiofile.cpp`)
- (tests in `tests/mediaio.cpp`)

**Design:**
- Derives from `MediaIOTask`, registered as "AudioFile"
- Supports WAV, BWF, AIFF, OGG; content probing via RIFF/FORM/OggS magic
- Frame chunking: samples per frame = `round(sampleRate / fps)`
- `cmd.canSeek = true` for open readers; `executeCmd(Seek)` delegates to `AudioFile::seekToSample()`
- `cmd.frameCount` derived from total samples / samples-per-frame (ceiling division) in CmdOpen
- Step control: after reading 1 frame worth of samples, seeks by `(step - 1)` additional frames if step≠1
- AudioDesc sourced from config keys (`ConfigAudioRate`, `ConfigAudioChannels`) on Writer; discovered from file on Reader
- `cmd.defaultSeekMode = MediaIO_SeekExact` (sample-accurate source)
- Conditionally compiled under `PROMEKI_ENABLE_AUDIO`

---

## SDLPlayerTask Backend — COMPLETE

Write-only MediaIOTask that plays frames through SDL (video widget + audio output).

**Files:**
- [x] `include/promeki/sdl/sdlplayer.h`
- [x] `src/sdl/sdlplayer.cpp`
- [x] `utils/mediaplay/main.cpp` (reference integration — pumps any MediaIO source into the player)
- (tested via integration: mediaplay utility; not unit-testable headlessly due to SDL hardware requirement)

**Design:**
- Derives from `MediaIOTask`; not registered in the format registry — instantiated via `createSDLPlayer()` free factory and injected via `MediaIO::adoptTask()`
- Paced mode (default): audio-led pacing — audio output's `waitForBuffer()` gates the write loop to keep A/V in sync; video rendered on the main thread via queued callables
- Fast mode: no pacing — writes as fast as the backend can consume (for offline rendering or benchmarks)
- Notification throttle: avoids flooding the main thread with render callables when it falls behind; drops video frames (counted in `framesDropped()`) rather than blocking the write thread
- `renderPending()`: called from the main thread event loop to flush any queued video frame to the SDL widget
- `SDLVideoWidget::mapPixelDesc()` expanded with direct 8/16-bit RGB/BGR/RGBA/BGRA/ARGB/ABGR mappings; RGBA8→SDL_PIXELFORMAT_RGBA32 bug fixed; fallback path refactored through `uploadCurrentImage()` helper

## MediaIOTask_QuickTime Backend — COMPLETE

Read/write MediaIOTask for QuickTime (.mov), MP4, and ISO-BMFF containers. Built on a self-contained `QuickTime` engine comprising a shared atom parser, a classic `.mov` reader, a fragmented MP4 reader (moof/traf/trun walking), a classic `.mov` writer, and a fragmented MP4 writer (moof+mdat fragments with per-fragment crash-safety boundaries).

**Files (all new, untracked → committed this session):**
- [x] `include/promeki/quicktime.h` — Public QuickTime engine facade (Operation, TrackType, Layout, Track, Sample, Impl, factory methods)
- [x] `src/proav/quicktime.cpp` — Factory implementations, Impl base vtable
- [x] `src/proav/quicktime_atom.h` / `quicktime_atom.cpp` — Atom parser (size/type/children; 4CC utilities)
- [x] `src/proav/quicktime_reader.h` / `quicktime_reader.cpp` — Classic + fragmented reader
- [x] `src/proav/quicktime_writer.h` / `quicktime_writer.cpp` — Classic + fragmented writer
- [x] `include/promeki/mediaiotask_quicktime.h` — MediaIOTask_QuickTime class declaration
- [x] `src/proav/mediaiotask_quicktime.cpp` — Backend implementation
- [x] `include/promeki/audiobuffer.h` / `src/proav/audiobuffer.cpp` — Ring-buffered audio FIFO with on-push format conversion
- [x] `include/promeki/bufferpool.h` / `src/core/bufferpool.cpp` — Fixed-geometry buffer pool (available, not yet wired into any hot path)
- [x] `tests/quicktime.cpp` — 818-line test file covering default construction, fixture-based reader (UYVY, ProRes, H.264+PCM, fragmented MP4), sample reads, writer round-trips (uncompressed, ProRes pass-through, variable-duration stts, keyframe flags), fragmented writer (video-only, A+V, crash-recovery), and timecode track
- [x] `tests/mediaiotask_quicktime.cpp` — 458-line test file covering registration, factory, reader (UYVY, ProRes, H.264+PCM, AAC-in-MP4, timecode, seek), and writer round-trips (video-only, video+audio, ProRes pass-through) via MediaIO interface
- [x] `tests/audiobuffer.cpp` — 268-line test file covering construction, push/pop same-format, ring wraparound, Audio push/pop, format conversion (float32→s16), error paths (NoSpace, NotSupported), drop/peek/clear, grow, move semantics
- [x] `tests/bufferpool.cpp` — 111-line test file covering construction, reserve, acquire/release, empty-pool allocation, memory reuse, size mismatch rejection, clear, and view reset

**Supporting PixelDesc entries added (pixeldesc.cpp + pixeldesc.h):**
- ProRes 422 Proxy/LT/Normal/HQ (apco/apcs/apcn/apch), ProRes 4444 (ap4h), ProRes 4444 XQ (ap4x)
- H.264 / AVC (avc1/avc3), H.265 / HEVC (hvc1/hev1)
- All tested in `tests/pixeldesc.cpp` additions.

**Supporting Audio/Image/AudioDesc additions (modified files):**
- `Audio::fromBuffer()`, `Audio::fromCompressedData()` — zero-copy and copy-from-raw compressed audio factories
- `Audio::isCompressed()`, `Audio::compressedSize()` — predicates for compressed audio objects
- `Image::fromBuffer()` — zero-copy image factory adopting an existing Buffer::Ptr as plane 0
- `AudioDesc::isCompressed()`, `AudioDesc::codecFourCC()`, `AudioDesc::setCodecFourCC()` — codec FourCC support for compressed audio descriptors
- `AudioDesc::isValid()` updated to accept compressed (non-zero FourCC) as valid even with `DataType == 0`

**QuickTime reader design:**
- Classic reader: parses ftyp, moov, mvhd, trak (tkhd, mdia, minf, stbl: stts/stsc/stsz/stss/stco/co64/ctts/elst), tmcd, udta. Resolves fourcc→PixelDesc/AudioDesc via PixelDesc/AudioDesc type registries.
- Fragmented reader: scans for moof/traf/tfhd/tfdt/trun boxes after moov; per-fragment sample entries merged into the track's flat sample index. Incomplete tail fragments are silently discarded (crash-recovery).
- `readBulk` DIO path used for all sample data reads above 4×alignment.

**QuickTime writer design:**
- Classic layout: writes mdat payload first (each sample appended at current file offset), then at `finalize()` builds moov with stts (run-length encoded), stsc (all-in-one-chunk), stsz, stco (absolute offsets patched), stss (sync-sample list). mdat size patched by seeking back to the size field after moov is complete.
- Fragmented layout: writes ftyp + init-moov (minimal moov with mvex/trex) at open; each `flush()` emits a moof+mdat pair; `finalize()` flushes any residual fragment and closes the file. Each fragment is self-describing: default_base_is_moof flag in tfhd so sample offsets are relative to the moof atom, not the file start.
- Audio compression in trun: for fragmented audio (PCM), emits one trun entry per chunk rather than per sample (audio-trun compression). For classic PCM audio, emits one stsc/stco/stsz entry per chunk.

**Container metadata (udta) design — COMPLETE (2026-04-08):**

`QuickTime::setContainerMetadata(Metadata)` stores the container metadata; `QuickTimeWriter::appendUdta()` serializes it into a `udta` box inside `moov` (classic layout) or the init-moov (fragmented layout). The box is skipped entirely when nothing is set. Two parallel encodings live side-by-side:

- **Standard text fields → classic QuickTime ©-atoms:** Title→©nam, Comment→©cmt, Date→©day, Artist→©ART, Copyright→©cpy, Software→©too, Album→©alb, Genre→©gen, Description→©des. Payload format: `[u16 textLen][u16 language=0][text bytes]`.
- **BWF-ish fields → XMP packet in an `XMP_` box:** Originator, OriginatorReference, OriginationDateTime, UMID are emitted under the Adobe BWF bext namespace (`http://ns.adobe.com/bwf/bext/1.0/`). `OriginationDateTime` is split into `bext:originationDate` + `bext:originationTime` at the ISO-8601 `T` separator. UMID is stored as hex text via `bext:umid`. The XMP packet uses the standard `xpacket` wrapper with the Adobe magic id.
- **Reader side:** `QuickTimeReader::parseUdta` handles both encodings. ©-atoms map back to their Metadata IDs as before. An `XMP_` box is extracted via `extractBextElement(xmp, "umid")` etc. — a minimal substring-based XMP reader that finds `<bext:localName>...</bext:localName>`, handles XML entities (`&amp;`/`&lt;`/`&gt;`/`&quot;`/`&apos;`) and tolerates attributes + whitespace on the opening tag. UMID is recomposed as a typed `UMID` via `UMID::fromString`; OriginationDateTime is recombined from the date + time parts.

**Supporting classes added this round:**
- `promeki::UMID` — SMPTE 330M Unique Material Identifier, Basic (32B) and Extended (64B) forms; `UMID::generate(Length = Extended)` populates a random material number, Extended fills the Source Pack time/date from `gmtime_r` and sets the Organization field to `"MEKI"` as a persistent libpromeki signature embedded in the UMID bytes themselves. Registered as `Variant::TypeUMID` for round-tripping through Metadata.
- `Metadata::applyMediaIOWriteDefaults()` — called by `MediaIO::open(Writer|ReadWrite)` to populate standard write-time defaults via `setIfMissing`: `Date` (UTC YYYY-MM-DD), `OriginationDateTime` (UTC ISO-8601), `Software` (Application::appName if set else `"libpromeki (https://howardlogic.com)"`), `Originator` (`"libpromeki howardlogic.com"`, 26 chars, fits BWF 32-char cap), `OriginatorReference` (fresh UUIDv7 string), `UMID` (fresh Extended). Caller-set values always win.
- `VariantDatabase::setIfMissing(ID, Variant)` — generic primitive used by `applyMediaIOWriteDefaults`.
- Writer propagation: `MediaIOTask_ImageFile` stashes container metadata in `_writeContainerMetadata` and merges it into each frame (image sequences are standalone files); `MediaIOTask_AudioFile` merges `cmd.pendingMetadata` into `_audioDesc.metadata()` before `setDesc` so the libsndfile backend emits Software/Date/etc. into WAV/BWF; `MediaIOTask_QuickTime` forwards via `_qt.setContainerMetadata()`.

**Known issues / FIXMEs (all tracked in fixme.md):**
- Little-endian float audio storage is lossy (promoted to s16 at write time)
- `raw ` 24-bit BGR/RGB byte-order player disagreement (mplayer vs VLC/ffmpeg)
- CMake incremental-rebuild gap for SDL library when core headers change ABI
- BufferPool available but not wired into QuickTimeReader hot path
- Fragmented reader ignores `trex` defaults fallback (only reads `tfhd` overrides)
- Compressed audio pull-rate drifts (one packet per video frame, not dts-aligned)
- Compressed audio write path is missing (remux-style workflows blocked)
- **XMP parser only matches the `bext:` prefix** — a third-party XMP packet that binds the bext namespace URI under a different prefix will not round-trip. Blocked on adding proper XML support to the core library.

**Verified end-to-end:** `mediaplay --burn --size 1920x1080 --pattern ColorBars --audio-mode AvSync --duration 5 --output /tmp/test.mov` produces a fragmented MP4 that plays in VLC and ffprobes cleanly with video (`rawvideo`) and audio (`pcm_s16le`).

---

## AudioSourceNode

Reads audio from AudioFile and outputs frames.

**Files:**
- [ ] `include/promeki/audiosourcenode.h`
- [ ] `src/proav/audiosourcenode.cpp`
- [ ] `tests/audiosourcenode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: creates one audio output port
- [ ] `void setFilePath(const FilePath &path)` — or `String` path
- [ ] `void setAudioFile(AudioFile *file)` — use existing AudioFile
- [ ] `void setLooping(bool loop)` — loop at end of file
- [ ] Override `build()`:
  - [ ] Open AudioFile, read AudioDesc
  - [ ] Set output port AudioDesc from file
- [ ] Override `start()`: seek to beginning (or specified start)
- [ ] Override `processFrame()`:
  - [ ] Read next block of audio from file
  - [ ] Create `Frame::Ptr` with `Audio` data
  - [ ] Push to output via source
  - [ ] Handle EOF (stop or loop)
- [ ] Override `stop()`: close file
- [ ] `PROMEKI_SIGNAL(endOfFile)`
- [ ] Doctest: open file, process frames, verify audio data, EOF handling

---

## AudioSinkNode

Writes audio frames to AudioFile.

**Files:**
- [ ] `include/promeki/audiosinknode.h`
- [ ] `src/proav/audiosinknode.cpp`
- [ ] `tests/audiosinknode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: creates one audio input port
- [ ] `void setFilePath(const FilePath &path)`
- [ ] `void setAudioDesc(const AudioDesc &desc)` — output file format
- [ ] Override `build()`:
  - [ ] Negotiate format with input port
  - [ ] Create/open output AudioFile
- [ ] Override `processFrame()`:
  - [ ] Pull `Frame::Ptr` from input sink
  - [ ] Extract `Audio` data
  - [ ] Write to AudioFile
- [ ] Override `stop()`: finalize and close file
- [ ] Doctest: write frames, verify file output

---

## ImageSourceNode

Reads image sequences and outputs video frames.

**Note:** Image file backends are now available — DPX (read+write, all pixel formats, embedded audio+metadata, DIO), Cineon (read-only, 10-bit RGB), TGA (read+write, RGBA8), SGI (read+write, 6 formats, RLE), PNM (read+write, PPM/PGM P5/P6). `ImageFile` now carries `Frame` (image+audio+metadata). These backends are the foundation for this node.

**Files:**
- [ ] `include/promeki/imagesourcenode.h`
- [ ] `src/proav/imagesourcenode.cpp`
- [ ] `tests/imagesourcenode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: creates one video output port
- [ ] `void setSequencePath(const String &pattern)` — e.g., "frame_%04d.dpx"
- [ ] `void setFrameRange(FrameNumber start, FrameNumber end)`
- [ ] `void setLooping(bool loop)`
- [ ] Override `build()`:
  - [ ] Detect image format from first frame (use `ImageFile::lookupByExtension()` or `ImageFileIO::probe()`)
  - [ ] Set output port VideoDesc/ImageDesc
- [ ] Override `processFrame()`:
  - [ ] Read next image in sequence via `ImageFile::load()`
  - [ ] Push `frame()` from `ImageFile` to output port
  - [ ] Handle end of sequence
- [ ] Override `stop()`
- [ ] `PROMEKI_SIGNAL(endOfSequence)`
- [ ] Doctest: read image sequence, verify frame data

---

## ImageSinkNode

Writes video frames as image sequences.

**Files:**
- [ ] `include/promeki/imagesinknode.h`
- [ ] `src/proav/imagesinknode.cpp`
- [ ] `tests/imagesinknode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: creates one video input port
- [ ] `void setSequencePath(const String &pattern)`
- [ ] `void setStartFrame(FrameNumber frame)`
- [ ] Override `build()`: validate output path, create directory if needed
- [ ] Override `processFrame()`:
  - [ ] Pull `Frame::Ptr` from input
  - [ ] Wrap in `ImageFile`, call `save()`
  - [ ] Write to numbered file
- [ ] Override `stop()`
- [ ] Doctest: write image sequence, verify files

---

## AudioMixerNode

N-input audio mixer with per-input gain.

**Files:**
- [ ] `include/promeki/audiomixernode.h`
- [ ] `src/proav/audiomixernode.cpp`
- [ ] `tests/audiomixernode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] `void setInputCount(int count)` — creates N audio input ports
- [ ] Constructor: creates one audio output port
- [ ] `void setInputGain(int inputIndex, double gain)` — per-input gain (linear, 1.0 = unity)
- [ ] `double inputGain(int inputIndex) const`
- [ ] Override `build()`:
  - [ ] Verify all inputs have compatible AudioDesc (sample rate, format)
  - [ ] Output AudioDesc matches input (or is configured explicitly)
- [ ] Override `processFrame()`:
  - [ ] Pull frames from all inputs
  - [ ] Allocate output Audio buffer, mix into it: sum samples with per-input gain
  - [ ] Handle missing inputs gracefully (silence)
  - [ ] Clip/saturate output if needed
  - [ ] Push mixed frame to output
  - [ ] Note: mixer always allocates a fresh output buffer (N→1 reduction, no single input to detach)
- [ ] Doctest: mix 2 sine waves, verify output amplitude

---

## AudioGainNode

Simple gain adjustment node.

**Files:**
- [ ] `include/promeki/audiogainnode.h`
- [ ] `src/proav/audiogainnode.cpp`
- [ ] `tests/audiogainnode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one audio input, one audio output
- [ ] `void setGain(double gain)` — linear gain (1.0 = unity)
- [ ] `void setGainDb(double db)` — gain in decibels
- [ ] `double gain() const`
- [ ] `double gainDb() const`
- [ ] Override `build()`: output AudioDesc = input AudioDesc
- [ ] Override `processFrame()`:
  - [ ] Pull frame from input
  - [ ] Call `audio.modify()` then `audio->ensureExclusive()` (COW detach if shared, no-op in linear pipeline)
  - [ ] Apply gain to all samples in place
  - [ ] Push to output
- [ ] Doctest: apply gain, verify output levels

---

## ColorModelConvertNode

Image color model conversion.

**Files:**
- [ ] `include/promeki/colormodelconvertnode.h`
- [ ] `src/proav/colormodelconvertnode.cpp`
- [ ] `tests/colormodelconvertnode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one video input, one video output
- [ ] `void setOutputColorModel(ColorModel model)`
- [ ] `ColorModel outputColorModel() const`
- [ ] Override `build()`:
  - [ ] Read input PixelDesc's ColorModel
  - [ ] Set output PixelDesc with target ColorModel
  - [ ] Pre-compute conversion matrix/LUT
- [ ] Override `processFrame()`:
  - [ ] Pull frame from input
  - [ ] Call `img.modify()` then `img->ensureExclusive()` (COW detach if shared)
  - [ ] Apply color model conversion in place via Color conversion
  - [ ] Push to output
- [ ] Doctest: convert between known color models, verify pixel values

---

## FrameSyncNode

Audio/video synchronization using Timecode.

**Files:**
- [ ] `include/promeki/framesyncnode.h`
- [ ] `src/proav/framesyncnode.cpp`
- [ ] `tests/framesyncnode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one audio input, one video input, one audio output, one video output
- [ ] `void setTimecodeMode(Timecode::Mode mode)`
- [ ] `void setSyncSource(enum { Audio, Video, External })` — which stream is master
- [ ] `void setMaxDrift(Duration maxDrift)` — acceptable A/V offset before correction
- [ ] Override `build()`: validate input formats
- [ ] Override `processFrame()`:
  - [ ] Read timecodes from incoming frames
  - [ ] Compare A/V timecodes
  - [ ] Drop/duplicate frames to maintain sync
  - [ ] Report drift
- [ ] `PROMEKI_SIGNAL(driftDetected, Duration)` — emitted when drift exceeds threshold
- [ ] `PROMEKI_SIGNAL(syncAchieved)`
- [ ] Doctest: feed frames with known timecodes, verify sync behavior
