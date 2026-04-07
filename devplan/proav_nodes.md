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
- 58 test cases in `tests/mediaio.cpp`, 10 test cases in `tests/strand.cpp`
- Strand: serial order, no overlap, void tasks, multiple strands concurrent, isBusy, destructor, cancelPending, cancel-empty-queue, cancel-hook-runs
- MediaIO: registry, factory (create/createForFileRead/createForFileWrite/defaultConfig/enumerate), all TPG modes (video-only, audio-only, timecode-only, full generation), error paths (writer-not-supported, nothing-enabled, invalid-pattern, read-before-open, double-open), seeking, canSeek/frameCount, step control, prefetch depth (default/user-override/clamped), defaultSeekMode, track selection, frameAvailable, reopen-same-instance, sendParams, cancelPending, stats, EOF latching, mid-stream descriptor, ImageFile round-trips (DPX/TGA), AudioFile round-trips (WAV), AudioFile seeking, AudioFile step/EOF, device enumeration, cancellation

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
- Video: delegates to `VideoTestPattern`; solid color configured via `Color` object (`ConfigVideoSolidColor`); static patterns cached after first generate when step=0; motion applies per-frame offset scaled by step
- Audio: delegates to `AudioTestPattern` (tone, silence, ltc modes)
- Timecode: delegates to `TimecodeGenerator`; TC advances by `|step|` frames per CmdRead; TC stamped on both `Frame::metadata()` and `Image::metadata()` (required by TimecodeOverlayNode)
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
- Content probing via magic-number inspection (DPX/Cineon/PNG/SGI/PNM); `FormatDesc::canHandleDevice` populated
- Default step=0 (set via `cmd.defaultStep = 0` in Open): CmdRead re-delivers the same loaded image indefinitely (hold semantics for still images in a pipeline); step≠0 for single-delivery then EndOfFile
- `cmd.frameCount = 1` for Reader, tracks write count for Writer
- `executeCmd(Open/Reader)` loads the full image immediately and builds mediaDesc; `executeCmd(Close)` releases the cached frame

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
