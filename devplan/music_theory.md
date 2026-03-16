# Music Theory Objects

**Phase:** 6A, 6B
**Dependencies:** Minimal (existing music framework)
**Library:** `promeki-music`
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

## 6A. Core Music Theory

---

### Interval

Musical interval. Simple value type — no PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/music/interval.h`
- [ ] `src/music/interval.cpp`
- [ ] `tests/interval.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `enum Quality { Perfect, Major, Minor, Augmented, Diminished }`
- [ ] `enum Number { Unison = 1, Second, Third, Fourth, Fifth, Sixth, Seventh, Octave }`
- [ ] Constructor from `Quality` + `Number`
- [ ] Constructor from semitone count
- [ ] `static Result<Interval> fromName(const String &name)` — e.g., "P5", "m3", "M7", "A4", "d5"
- [ ] `int semitones() const`
- [ ] `Quality quality() const`
- [ ] `Number number() const`
- [ ] `String name() const` — e.g., "P5"
- [ ] `String longName() const` — e.g., "Perfect Fifth"
- [ ] `Interval invert() const` — complement (P5 -> P4, M3 -> m6)
- [ ] `bool isConsonant() const`
- [ ] `bool isDissonant() const`
- [ ] `bool isCompound() const` — greater than octave
- [ ] `Interval simple() const` — reduce compound to simple (M9 -> M2)
- [ ] `operator==`, `operator!=`, `operator<`
- [ ] `operator+(const Interval &)` — add intervals
- [ ] Doctest: construction, fromName, semitones, invert, compound/simple, name round-trip

---

### Chord

Chord defined by root note and intervals. Data object with PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/music/chord.h`
- [ ] `src/music/chord.cpp`
- [ ] `tests/chord.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `enum Type { Major, Minor, Diminished, Augmented, Major7, Minor7, Dominant7, Diminished7, HalfDiminished7, Augmented7, Sus2, Sus4, Add9, Power }`
- [ ] Constructor from `MusicalNote` root + `Type`
- [ ] Constructor from `MusicalNote` root + `List<Interval>` (custom voicing)
- [ ] `static Result<Chord> fromName(const String &name)` — e.g., "Cmaj7", "Am", "F#dim"
- [ ] `MusicalNote root() const`
- [ ] `Type type() const`
- [ ] `String name() const` — e.g., "Cmaj7"
- [ ] `String fullName() const` — e.g., "C Major Seventh"
- [ ] `List<Interval> intervals() const`
- [ ] `List<MusicalNote> notes() const` — notes in the chord
- [ ] `List<uint8_t> midiNotes(int octave) const` — MIDI note numbers at given octave
- [ ] `Chord invert(int inversion) const` — 1st, 2nd, 3rd inversion
- [ ] `int inversion() const` — 0 = root position
- [ ] `Chord transpose(const Interval &interval) const`
- [ ] `Chord transpose(int semitones) const`
- [ ] `bool contains(const MusicalNote &note) const`
- [ ] `operator==`, `operator!=`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: construction, fromName, notes, midiNotes, inversions, transpose

---

### ChordProgression

Sequence of chords with durations. Data object with PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/music/chordprogression.h`
- [ ] `src/music/chordprogression.cpp`
- [ ] `tests/chordprogression.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Nested `ChordEntry`: `Chord` + duration (in beats)
- [ ] `void addChord(const Chord &chord, double durationBeats)`
- [ ] `void insertChord(int index, const Chord &chord, double durationBeats)`
- [ ] `void removeChord(int index)`
- [ ] `int chordCount() const`
- [ ] `Chord chordAt(int index) const`
- [ ] `Chord chordAtBeat(double beat) const` — find chord at given beat position
- [ ] `double totalDuration() const` — total beats
- [ ] `static Result<ChordProgression> fromRomanNumerals(const String &numerals, const Scale &scale)` — e.g., "I IV V I"
- [ ] `String toRomanNumerals(const Scale &scale) const`
- [ ] `ChordProgression transpose(int semitones) const`
- [ ] `ChordProgression transpose(const Interval &interval) const`
- [ ] `List<ChordEntry> entries() const`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: build progression, fromRomanNumerals, chordAtBeat, transpose

---

### Key

Root note + mode. Simple value type.

**Files:**
- [ ] `include/promeki/music/key.h`
- [ ] `src/music/key.cpp`
- [ ] `tests/key.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Constructor from `MusicalNote` root + `Scale::Mode` (or enum: Major, Minor, etc.)
- [ ] `static Result<Key> fromName(const String &name)` — e.g., "C major", "A minor", "F# dorian"
- [ ] `MusicalNote root() const`
- [ ] `Scale::Mode mode() const`
- [ ] `Scale scale() const` — returns the full scale for this key
- [ ] `Key relativeKey() const` — relative major/minor (C major <-> A minor)
- [ ] `Key parallelKey() const` — parallel major/minor (C major <-> C minor)
- [ ] `List<int> signature() const` — number of sharps (positive) or flats (negative)
- [ ] `int sharps() const`, `int flats() const`
- [ ] `String name() const` — e.g., "C major"
- [ ] `bool contains(const MusicalNote &note) const` — is note in this key
- [ ] `List<Chord> diatonicChords() const` — triads built on each scale degree
- [ ] `List<Chord> seventhChords() const` — seventh chords on each scale degree
- [ ] `operator==`, `operator!=`
- [ ] Doctest: construction, relative/parallel keys, signature, diatonic chords

---

### TimeSignature

Simple value type.

**Files:**
- [ ] `include/promeki/music/timesignature.h`
- [ ] `src/music/timesignature.cpp`
- [ ] `tests/timesignature.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Constructor from `int beatsPerMeasure`, `int beatUnit`
- [ ] `int beatsPerMeasure() const` — numerator (e.g., 4 in 4/4)
- [ ] `int beatUnit() const` — denominator (e.g., 4 in 4/4, meaning quarter note)
- [ ] `bool isCompound() const` — e.g., 6/8, 9/8, 12/8
- [ ] `bool isSimple() const`
- [ ] `bool isDuple() const` — 2 or 6
- [ ] `bool isTriple() const` — 3 or 9
- [ ] `bool isQuadruple() const` — 4 or 12
- [ ] `String toString() const` — e.g., "4/4"
- [ ] `static Result<TimeSignature> fromString(const String &str)` — e.g., "3/4"
- [ ] Common constants: `Common` (4/4), `CutTime` (2/2), `Waltz` (3/4)
- [ ] `operator==`, `operator!=`
- [ ] Doctest: construction, compound detection, toString/fromString

---

### Tempo

Simple value type.

**Files:**
- [ ] `include/promeki/music/tempo.h`
- [ ] `src/music/tempo.cpp`
- [ ] `tests/tempo.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Constructor from `double bpm`
- [ ] `double bpm() const`
- [ ] `void setBpm(double bpm)`
- [ ] `Duration beatDuration() const` — duration of one beat
- [ ] `Duration measureDuration(const TimeSignature &ts) const` — duration of one measure
- [ ] `double beatsPerSecond() const`
- [ ] `double secondsPerBeat() const`
- [ ] `String marking() const` — Italian tempo marking (e.g., "Allegro")
- [ ] `static Result<Tempo> fromMarking(const String &marking)` — e.g., "Andante" -> ~76-108 bpm (midpoint)
- [ ] Common constants: `Largo` (50), `Adagio` (70), `Andante` (92), `Moderato` (108), `Allegro` (132), `Presto` (168)
- [ ] `operator==`, `operator!=`, `operator<`
- [ ] Doctest: construction, durations, markings

---

### TempoMap

Tempo and time signature changes over time. Data object with PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/music/tempomap.h`
- [ ] `src/music/tempomap.cpp`
- [ ] `tests/tempomap.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `void addTempoChange(double beat, const Tempo &tempo)`
- [ ] `void addTimeSignatureChange(double beat, const TimeSignature &ts)`
- [ ] `void removeTempoChange(double beat)`
- [ ] `void removeTimeSignatureChange(double beat)`
- [ ] `Tempo tempoAt(double beat) const`
- [ ] `TimeSignature timeSignatureAt(double beat) const`
- [ ] `double beatToTime(double beat) const` — beat position to absolute time (seconds)
- [ ] `double timeToBeat(double timeSeconds) const` — absolute time to beat position
- [ ] `int beatToMeasure(double beat) const` — which measure number
- [ ] `double measureToBeat(int measure) const` — first beat of measure
- [ ] `List<std::pair<double, Tempo>> tempoChanges() const`
- [ ] `List<std::pair<double, TimeSignature>> timeSignatureChanges() const`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: single tempo, tempo change, time signature change, beatToTime/timeToBeat round-trip

---

### Rhythm

Pattern of durations and rests. Data object with PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/music/rhythm.h`
- [ ] `src/music/rhythm.cpp`
- [ ] `tests/rhythm.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Nested `Beat` struct: `double duration` (in beat units), `bool isRest`
- [ ] `void addBeat(double duration, bool isRest = false)`
- [ ] `void addNote(double duration)` — convenience: addBeat(duration, false)
- [ ] `void addRest(double duration)` — convenience: addBeat(duration, true)
- [ ] `int beatCount() const`
- [ ] `Beat beat(int index) const`
- [ ] `double totalDuration() const` — sum of all beat durations
- [ ] `List<Beat> beats() const`
- [ ] `void clear()`
- [ ] `Rhythm repeat(int times) const` — repeat pattern N times
- [ ] `Rhythm concatenate(const Rhythm &other) const`
- [ ] Common patterns:
  - [ ] `static Rhythm straight(int beats)` — equal duration notes
  - [ ] `static Rhythm dotted(int beats)` — dotted rhythm
  - [ ] `static Rhythm swung(int beats)` — swing feel (2/3 + 1/3 split)
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: build pattern, totalDuration, repeat, common patterns

---

## 6B. Performance and Dynamics

---

### Dynamics

Enum-based dynamic markings with velocity mapping.

**Files:**
- [ ] `include/promeki/music/dynamics.h`
- [ ] `src/music/dynamics.cpp`
- [ ] `tests/dynamics.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `enum Level { ppp, pp, p, mp, mf, f, ff, fff }`
- [ ] Constructor from `Level`
- [ ] Constructor from MIDI velocity: `Dynamics(uint8_t velocity)`
- [ ] `Level level() const`
- [ ] `uint8_t velocity() const` — MIDI velocity (0-127)
- [ ] `static Dynamics fromVelocity(uint8_t velocity)` — maps velocity range to Level
- [ ] `String name() const` — e.g., "pp", "mf"
- [ ] `String longName() const` — e.g., "pianissimo", "mezzo-forte"
- [ ] `static Result<Dynamics> fromName(const String &name)`
- [ ] `double linearGain() const` — 0.0 to 1.0 for audio processing
- [ ] `operator==`, `operator!=`, `operator<`
- [ ] Velocity mappings: ppp=16, pp=33, p=49, mp=64, mf=80, f=96, ff=112, fff=127
- [ ] Doctest: construction, velocity mapping, fromVelocity, name

---

### Articulation

Enum-based articulation markings.

**Files:**
- [ ] `include/promeki/music/articulation.h`
- [ ] `src/music/articulation.cpp`
- [ ] `tests/articulation.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `enum Type { Normal, Legato, Staccato, Staccatissimo, Tenuto, Accent, Marcato, Sforzando }`
- [ ] Constructor from `Type`
- [ ] `Type type() const`
- [ ] `String name() const` — e.g., "Staccato"
- [ ] `String symbol() const` — Unicode symbol if available
- [ ] `double durationScale() const` — how much to shorten/lengthen note:
  - [ ] Normal = 1.0, Legato = 1.0, Staccato = 0.5, Staccatissimo = 0.25, Tenuto = 1.0
- [ ] `double velocityScale() const` — how much to boost/reduce velocity:
  - [ ] Normal = 1.0, Accent = 1.3, Marcato = 1.4, Sforzando = 1.5
- [ ] `bool affectsDuration() const`
- [ ] `bool affectsVelocity() const`
- [ ] `operator==`, `operator!=`
- [ ] Doctest: construction, scales, name
