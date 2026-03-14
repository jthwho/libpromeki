# MIDI I/O and Arrangement

**Phase:** 6C, 6D
**Dependencies:** Phase 6A (music theory classes: Interval, Chord, Key, Tempo, TempoMap, TimeSignature, Dynamics, Articulation)
**Library:** `promeki-music`
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

## 6C. MIDI I/O

---

### MidiEvent

Data object representing a single MIDI event. PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/music/midievent.h`
- [ ] `src/music/midievent.cpp`
- [ ] `tests/midievent.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `enum Type { NoteOn, NoteOff, ControlChange, ProgramChange, PitchBend, ChannelPressure, PolyPressure, SysEx, Meta }`
- [ ] `enum MetaType { TrackName, Tempo, TimeSignature, KeySignature, EndOfTrack, Text, Copyright, Lyrics }`
- [ ] Constructor from type + timestamp + data
- [ ] `uint32_t timestamp() const` — in ticks
- [ ] `void setTimestamp(uint32_t ticks)`
- [ ] `Type type() const`
- [ ] `uint8_t channel() const` — 0-15
- [ ] `void setChannel(uint8_t ch)`
- [ ] Note events:
  - [ ] `uint8_t note() const` — MIDI note number (0-127)
  - [ ] `uint8_t velocity() const` — 0-127
  - [ ] `static MidiEvent noteOn(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity)`
  - [ ] `static MidiEvent noteOff(uint32_t tick, uint8_t channel, uint8_t note, uint8_t velocity = 0)`
- [ ] Control Change:
  - [ ] `uint8_t controller() const` — CC number (0-127)
  - [ ] `uint8_t controlValue() const` — CC value (0-127)
  - [ ] `static MidiEvent controlChange(uint32_t tick, uint8_t channel, uint8_t controller, uint8_t value)`
- [ ] Program Change:
  - [ ] `uint8_t program() const`
  - [ ] `static MidiEvent programChange(uint32_t tick, uint8_t channel, uint8_t program)`
- [ ] Pitch Bend:
  - [ ] `int16_t pitchBendValue() const` — -8192 to 8191
  - [ ] `static MidiEvent pitchBend(uint32_t tick, uint8_t channel, int16_t value)`
- [ ] Meta events:
  - [ ] `MetaType metaType() const`
  - [ ] `Buffer metaData() const`
  - [ ] `static MidiEvent tempo(uint32_t tick, double bpm)`
  - [ ] `static MidiEvent timeSignature(uint32_t tick, int numerator, int denominator)`
  - [ ] `static MidiEvent trackName(uint32_t tick, const String &name)`
- [ ] `bool isNote() const` — NoteOn or NoteOff
- [ ] `bool isChannel() const` — channel message vs meta/sysex
- [ ] `bool isMeta() const`
- [ ] `operator<` — sort by timestamp
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: construct each event type, static factories, sorting by timestamp

---

### MidiTrack

Ordered sequence of MidiEvents. Data object with PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/music/miditrack.h`
- [ ] `src/music/miditrack.cpp`
- [ ] `tests/miditrack.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `void addEvent(const MidiEvent &event)` — inserts in timestamp order
- [ ] `void removeEvent(int index)`
- [ ] `MidiEvent event(int index) const`
- [ ] `int eventCount() const`
- [ ] `List<MidiEvent> events() const` — all events, sorted by timestamp
- [ ] `String name() const` — from TrackName meta event
- [ ] `void setName(const String &name)` — sets/updates TrackName meta event
- [ ] `uint32_t duration() const` — tick of last event
- [ ] `double durationSeconds(uint16_t ticksPerBeat, double bpm) const`
- [ ] Filtering:
  - [ ] `List<MidiEvent> noteEvents() const`
  - [ ] `List<MidiEvent> controlEvents() const`
  - [ ] `List<MidiEvent> eventsOfType(MidiEvent::Type type) const`
  - [ ] `List<MidiEvent> eventsInRange(uint32_t startTick, uint32_t endTick) const`
- [ ] `void transpose(int semitones)` — transpose all note events
- [ ] `void setChannel(uint8_t channel)` — set channel for all channel events
- [ ] `void quantize(uint32_t gridTicks)` — snap events to grid
- [ ] `void clear()`
- [ ] `bool isEmpty() const`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: add events, ordering, filtering, transpose, quantize

---

### MidiFile

Read/write Standard MIDI Files (SMF). Data object with PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/music/midifile.h`
- [ ] `src/music/midifile.cpp`
- [ ] `tests/midifile.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `enum Format { SingleTrack = 0, MultiTrack = 1, MultiSong = 2 }`
- [ ] `static std::pair<MidiFile, Error> readFromFile(const String &path)`
- [ ] `static std::pair<MidiFile, Error> readFromFile(const FilePath &path)`
- [ ] `Error writeToFile(const String &path) const`
- [ ] `Error writeToFile(const FilePath &path) const`
- [ ] `static std::pair<MidiFile, Error> readFromBuffer(const Buffer &data)`
- [ ] `std::pair<Buffer, Error> writeToBuffer() const`
- [ ] `Format format() const`, `void setFormat(Format)`
- [ ] `uint16_t ticksPerBeat() const`, `void setTicksPerBeat(uint16_t)` — resolution (typical: 480)
- [ ] `List<MidiTrack> tracks() const`
- [ ] `MidiTrack track(int index) const`
- [ ] `int trackCount() const`
- [ ] `void addTrack(const MidiTrack &track)`
- [ ] `void removeTrack(int index)`
- [ ] `double durationSeconds() const` — duration of longest track (requires tempo extraction)
- [ ] SMF parser internals:
  - [ ] MThd header: format, track count, division
  - [ ] MTrk chunks: variable-length delta times, event parsing
  - [ ] Running status support
  - [ ] SysEx and Meta event handling
- [ ] SMF writer internals:
  - [ ] Write MThd header
  - [ ] Write MTrk chunks with delta-time encoding
  - [ ] Running status optimization
- [ ] Doctest: create MIDI file, write to buffer, read back, verify tracks and events. Also test with known .mid file if available.

---

## 6D. Arrangement

---

### Instrument

Name and MIDI mapping. Simple value type.

**Files:**
- [ ] `include/promeki/music/instrument.h`
- [ ] `src/music/instrument.cpp`
- [ ] `tests/instrument.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `String name() const`, `void setName(const String &)`
- [ ] `uint8_t midiProgram() const`, `void setMidiProgram(uint8_t)` — General MIDI program (0-127)
- [ ] `uint8_t midiChannel() const`, `void setMidiChannel(uint8_t)` — 0-15 (9 = drums)
- [ ] `bool isDrumKit() const` — true if channel 9
- [ ] `String family() const` — e.g., "Strings", "Brass", "Woodwinds"
- [ ] General MIDI instrument lookup:
  - [ ] `static std::pair<Instrument, Error> fromGMProgram(uint8_t program)` — name from GM spec
  - [ ] `static std::pair<Instrument, Error> fromName(const String &name)` — lookup by name
- [ ] Common instruments:
  - [ ] `static Instrument piano()`, `static Instrument acousticGuitar()`, `static Instrument electricGuitar()`
  - [ ] `static Instrument bass()`, `static Instrument drums()`
  - [ ] `static Instrument violin()`, `static Instrument cello()`
- [ ] `operator==`, `operator!=`
- [ ] Doctest: construction, GM lookup, common instruments

---

### Track

Instrument + sequence of musical notes. Data object with PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/music/track.h`
- [ ] `src/music/track.cpp`
- [ ] `tests/track.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `Instrument instrument() const`, `void setInstrument(const Instrument &)`
- [ ] `String name() const`, `void setName(const String &)`
- [ ] `MusicalNote::List notes() const` — ordered list of notes
- [ ] `void addNote(const MusicalNote &note)`
- [ ] `void removeNote(int index)`
- [ ] `void insertNote(int index, const MusicalNote &note)`
- [ ] `int noteCount() const`
- [ ] `MusicalNote note(int index) const`
- [ ] `double duration() const` — total duration in beats
- [ ] `void transpose(int semitones)` — transpose all notes
- [ ] `void transpose(const Interval &interval)`
- [ ] `void setDynamics(const Dynamics &dynamics)` — apply to all notes
- [ ] `MidiTrack toMidiTrack(uint16_t ticksPerBeat) const` — convert to MIDI
- [ ] `static std::pair<Track, Error> fromMidiTrack(const MidiTrack &midi, uint16_t ticksPerBeat)` — convert from MIDI
- [ ] `bool isEmpty() const`
- [ ] `void clear()`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: add notes, transpose, toMidiTrack round-trip

---

### Arrangement

Full score: list of tracks + TempoMap + Key. Data object with PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/music/arrangement.h`
- [ ] `src/music/arrangement.cpp`
- [ ] `tests/arrangement.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `String title() const`, `void setTitle(const String &)`
- [ ] `String composer() const`, `void setComposer(const String &)`
- [ ] `Key key() const`, `void setKey(const Key &)`
- [ ] `TempoMap tempoMap() const`, `void setTempoMap(const TempoMap &)`
- [ ] Tracks:
  - [ ] `List<Track> tracks() const`
  - [ ] `Track track(int index) const`
  - [ ] `int trackCount() const`
  - [ ] `void addTrack(const Track &track)`
  - [ ] `void removeTrack(int index)`
  - [ ] `void insertTrack(int index, const Track &track)`
  - [ ] `Track *trackByName(const String &name)`
  - [ ] `Track *trackByInstrument(const Instrument &instrument)`
- [ ] `double duration() const` — duration of longest track in beats
- [ ] `double durationSeconds() const` — using tempoMap
- [ ] `int measureCount() const` — number of measures
- [ ] Transpose:
  - [ ] `void transpose(int semitones)` — transpose all tracks and key
  - [ ] `void transpose(const Interval &interval)`
- [ ] MIDI conversion:
  - [ ] `MidiFile toMidiFile(uint16_t ticksPerBeat = 480) const`
  - [ ] `static std::pair<Arrangement, Error> fromMidiFile(const MidiFile &midi)`
- [ ] `bool isEmpty() const`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: build arrangement, toMidiFile round-trip, transpose, duration calculation
