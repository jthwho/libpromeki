# ProAV Concrete Nodes

**Phase:** 4B
**Dependencies:** Phase 4A (MediaNode, MediaPort, MediaGraph, MediaPipeline)
**Library:** `promeki-proav`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

All nodes derive from `MediaNode`, implement `process()`, and declare their input/output ports.

---

## AudioSourceNode

Reads audio from AudioFile and outputs frames.

**Files:**
- [ ] `include/promeki/proav/proav/audiosourcenode.h`
- [ ] `src/audiosourcenode.cpp`
- [ ] `tests/audiosourcenode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: creates one audio output port
- [ ] `void setFilePath(const FilePath &path)` — or `String` path
- [ ] `void setAudioFile(AudioFile *file)` — use existing AudioFile
- [ ] `void setLooping(bool loop)` — loop at end of file
- [ ] Override `configure()`:
  - [ ] Open AudioFile, read AudioDesc
  - [ ] Set output port AudioDesc from file
- [ ] Override `start()`: seek to beginning (or specified start)
- [ ] Override `process()`:
  - [ ] Read next block of audio from file
  - [ ] Create `Frame::Ptr` with `Audio` data
  - [ ] Push to output port via MediaLink
  - [ ] Handle EOF (stop or loop)
- [ ] Override `stop()`: close file
- [ ] `PROMEKI_SIGNAL(endOfFile)`
- [ ] Doctest: open file, process frames, verify audio data, EOF handling

---

## AudioSinkNode

Writes audio frames to AudioFile.

**Files:**
- [ ] `include/promeki/proav/proav/audiosinknode.h`
- [ ] `src/audiosinknode.cpp`
- [ ] `tests/audiosinknode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: creates one audio input port
- [ ] `void setFilePath(const FilePath &path)`
- [ ] `void setAudioDesc(const AudioDesc &desc)` — output file format
- [ ] Override `configure()`:
  - [ ] Negotiate format with input port
  - [ ] Create/open output AudioFile
- [ ] Override `process()`:
  - [ ] Pull `Frame::Ptr` from input port via MediaLink
  - [ ] Extract `Audio` data
  - [ ] Write to AudioFile
- [ ] Override `stop()`: finalize and close file
- [ ] Doctest: write frames, verify file output

---

## ImageSourceNode

Reads image sequences and outputs video frames.

**Files:**
- [ ] `include/promeki/proav/proav/imagesourcenode.h`
- [ ] `src/imagesourcenode.cpp`
- [ ] `tests/imagesourcenode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: creates one video output port
- [ ] `void setSequencePath(const String &pattern)` — e.g., "frame_%04d.dpx"
- [ ] `void setFrameRange(FrameNumber start, FrameNumber end)`
- [ ] `void setLooping(bool loop)`
- [ ] Override `configure()`:
  - [ ] Detect image format from first frame
  - [ ] Set output port VideoDesc/ImageDesc
- [ ] Override `process()`:
  - [ ] Read next image in sequence
  - [ ] Create `Frame::Ptr` with `Image` data
  - [ ] Push to output port
  - [ ] Handle end of sequence
- [ ] Override `stop()`
- [ ] `PROMEKI_SIGNAL(endOfSequence)`
- [ ] Doctest: read image sequence, verify frame data

---

## ImageSinkNode

Writes video frames as image sequences.

**Files:**
- [ ] `include/promeki/proav/proav/imagesinknode.h`
- [ ] `src/imagesinknode.cpp`
- [ ] `tests/imagesinknode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: creates one video input port
- [ ] `void setSequencePath(const String &pattern)`
- [ ] `void setStartFrame(FrameNumber frame)`
- [ ] Override `configure()`: validate output path, create directory if needed
- [ ] Override `process()`:
  - [ ] Pull `Frame::Ptr` from input
  - [ ] Extract `Image` data
  - [ ] Write to numbered file
- [ ] Override `stop()`
- [ ] Doctest: write image sequence, verify files

---

## AudioMixerNode

N-input audio mixer with per-input gain.

**Files:**
- [ ] `include/promeki/proav/proav/audiomixernode.h`
- [ ] `src/audiomixernode.cpp`
- [ ] `tests/audiomixernode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] `void setInputCount(int count)` — creates N audio input ports
- [ ] Constructor: creates one audio output port
- [ ] `void setInputGain(int inputIndex, double gain)` — per-input gain (linear, 1.0 = unity)
- [ ] `double inputGain(int inputIndex) const`
- [ ] Override `configure()`:
  - [ ] Verify all inputs have compatible AudioDesc (sample rate, format)
  - [ ] Output AudioDesc matches input (or is configured explicitly)
- [ ] Override `process()`:
  - [ ] Pull frames from all inputs
  - [ ] Mix: sum samples with per-input gain
  - [ ] Handle missing inputs gracefully (silence)
  - [ ] Clip/saturate output if needed
  - [ ] Push mixed frame to output
- [ ] Doctest: mix 2 sine waves, verify output amplitude

---

## AudioGainNode

Simple gain adjustment node.

**Files:**
- [ ] `include/promeki/proav/audiogainnode.h`
- [ ] `src/audiogainnode.cpp`
- [ ] `tests/audiogainnode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one audio input, one audio output
- [ ] `void setGain(double gain)` — linear gain (1.0 = unity)
- [ ] `void setGainDb(double db)` — gain in decibels
- [ ] `double gain() const`
- [ ] `double gainDb() const`
- [ ] Override `configure()`: output AudioDesc = input AudioDesc
- [ ] Override `process()`:
  - [ ] Pull frame from input
  - [ ] Apply gain to all samples
  - [ ] Push to output
- [ ] Doctest: apply gain, verify output levels

---

## ColorSpaceConvertNode

Image color space conversion.

**Files:**
- [ ] `include/promeki/proav/colorspaceconvertnode.h`
- [ ] `src/colorspaceconvertnode.cpp`
- [ ] `tests/colorspaceconvertnode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one video input, one video output
- [ ] `void setOutputColorSpace(const ColorSpace &cs)`
- [ ] `ColorSpace outputColorSpace() const`
- [ ] Override `configure()`:
  - [ ] Read input port ColorSpace
  - [ ] Set output port with target ColorSpace
  - [ ] Pre-compute conversion matrix/LUT
- [ ] Override `process()`:
  - [ ] Pull frame from input
  - [ ] Apply color space conversion (use existing `ColorSpaceConverter`)
  - [ ] Push to output
- [ ] Leverage existing `ColorSpaceConverter` class
- [ ] Doctest: convert between known color spaces, verify pixel values

---

## FrameSyncNode

Audio/video synchronization using Timecode.

**Files:**
- [ ] `include/promeki/proav/framesyncnode.h`
- [ ] `src/framesyncnode.cpp`
- [ ] `tests/framesyncnode.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one audio input, one video input, one audio output, one video output
- [ ] `void setTimecodeMode(Timecode::Mode mode)`
- [ ] `void setSyncSource(enum { Audio, Video, External })` — which stream is master
- [ ] `void setMaxDrift(Duration maxDrift)` — acceptable A/V offset before correction
- [ ] Override `configure()`: validate input formats
- [ ] Override `process()`:
  - [ ] Read timecodes from incoming frames
  - [ ] Compare A/V timecodes
  - [ ] Drop/duplicate frames to maintain sync
  - [ ] Report drift
- [ ] `PROMEKI_SIGNAL(driftDetected, Duration)` — emitted when drift exceeds threshold
- [ ] `PROMEKI_SIGNAL(syncAchieved)`
- [ ] Doctest: feed frames with known timecodes, verify sync behavior
