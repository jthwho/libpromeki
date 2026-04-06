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
- [x] `include/promeki/mediadesc.h` (renamed from VideoDesc)
- [x] `src/proav/mediadesc.cpp`
- [x] `include/promeki/videodesc.h` (deprecated alias: `using VideoDesc = MediaDesc`)
- [x] `tests/mediaio.cpp`

**Design:**
- `MediaIO` derives from `ObjectBase`, uses `PROMEKI_OBJECT`
- Config-driven factory: `MediaIO::create(Config)`, `createForFileRead()`, `createForFileWrite()`, `defaultConfig(typeName)`
- Backends self-register via `PROMEKI_REGISTER_MEDIAIO(ClassName)` at static init
- `FormatDesc` struct: name, description, extensions, canRead, canWrite, factory lambda, optional `canHandleDevice` content-probe callback (used by `createForFileRead()` as extension-miss fallback)
- Open modes: `NotOpen`, `Reader`, `Writer`
- Base-class open/close lifecycle: `open()` calls `onOpen()`, `close()` calls `onClose()`; backends override `onOpen()`/`onClose()` (not `open()`/`close()`)
- Base-class frame I/O: `readFrame()` calls `onReadFrame()`, `writeFrame()` calls `onWriteFrame()`; both guard for open state and correct mode
- Debug assert in `~MediaIO()` catches backends that fail to call `close()` in their destructor
- Virtual API: `mediaDesc()`, `setMediaDesc()`, `frameRate()`, `audioDesc()`, `setAudioDesc()`, `metadata()`, `setMetadata()`, `readFrame()`, `writeFrame()`, `canSeek()`, `seekToFrame()`, `frameCount()` (`int64_t` with sentinel constants `FrameCountUnknown`/`FrameCountInfinite`/`FrameCountError`), `currentFrame()`
- Step control: `step()` / `setStep(int)` (default 1); backends override `setStep()` to react to direction/speed changes; 0 = hold/re-read, negative = reverse
- `errorOccurred(Error)` signal for async error reporting
- 40 test cases covering registry, factory, all TPG modes, error paths, seeking, base-class accessors, step control, FrameCountInfinite, ImageFile round-trips, AudioFile round-trips, and content probing

---

## MediaIO_TPG Backend — COMPLETE

Read-only MediaIO source that generates synchronized test pattern frames.

**Files:**
- [x] `include/promeki/mediaio_tpg.h`
- [x] `src/proav/mediaio_tpg.cpp`
- (tests in `tests/mediaio.cpp`)

**Design:**
- Derives from `MediaIO`, registered as "TPG" (no file extensions — generator source)
- Overrides `onOpen()`/`onClose()` (base class manages lifecycle); destructor calls `close()` before member cleanup
- Video: delegates to `VideoTestPattern`; solid color configured via `Color` object (single `ConfigVideoSolidColor` key, replaces former R/G/B uint16_t triple); static patterns cached after first render when step=0; motion applies per-frame offset scaled by step
- Audio: delegates to `AudioTestPattern` (tone, silence, ltc modes)
- Timecode: delegates to `TimecodeGenerator`; TC advances by `|step|` frames per read (step=0 holds); TC stamped on both `Frame::metadata()` and `Image::metadata()` (required by TimecodeOverlayNode)
- Infinite source: `frameCount()` == `FrameCountInfinite`, `canSeek()` == false
- `setStep()` overridden to control timecode advance rate and cache invalidation
- All config via `MediaIO::Config` (VariantDatabase); all ConfigID constants declared as static members
- `FormatDesc::defaultConfig` lambda returns fully-populated default Config (all keys, including video/audio/timecode groups, all disabled by default)

---

## MediaIO_ImageFile Backend — COMPLETE

MediaIO backend wrapping the ImageFile / ImageFileIO subsystem for single-image file formats.

**Files:**
- [x] `include/promeki/mediaio_imagefile.h`
- [x] `src/proav/mediaio_imagefile.cpp`
- (tests in `tests/mediaio.cpp`)

**Design:**
- Derives from `MediaIO`, registered as "ImageFile"
- Supports DPX, Cineon, TGA, SGI, RGB, PNM, PPM, PGM, PNG, RawYUV variants
- Content probing via magic-number inspection (DPX/Cineon/PNG/SGI/PNM); `FormatDesc::canHandleDevice` populated
- Default step=0: `readFrame()` re-delivers the same loaded image indefinitely (hold semantics for still images in a pipeline); set step≠0 for single-delivery then EOF
- `frameCount()` returns 1 while open as Reader, `_currentFrame` while Writer, 0 when closed
- `onOpen(Reader)` loads the full image immediately and builds `_mediaDesc` from it
- Destructor calls `close()` before member cleanup

---

## MediaIO_AudioFile Backend — COMPLETE

MediaIO backend wrapping the AudioFile subsystem (libsndfile) for frame-based audio file I/O.

**Files:**
- [x] `include/promeki/mediaio_audiofile.h`
- [x] `src/proav/mediaio_audiofile.cpp`
- (tests in `tests/mediaio.cpp`)

**Design:**
- Derives from `MediaIO`, registered as "AudioFile"
- Supports WAV, BWF, AIFF, OGG; content probing via RIFF/FORM/OggS magic
- Frame chunking: samples per frame = `round(sampleRate / fps)`
- `canSeek()` returns true for open readers; `seekToFrame()` delegates to `AudioFile::seekToSample()`
- `frameCount()` is derived from total samples / samples-per-frame (ceiling division)
- Step control: after reading 1 frame worth of samples, seeks by `(step - 1)` additional frames if step≠1
- AudioDesc sourced from config keys (`ConfigAudioRate`, `ConfigAudioChannels`) or a pre-set `setMediaDesc()` call
- Destructor calls `close()` before member cleanup

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
