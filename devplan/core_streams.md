# Core Streams and Serialization

**Phase:** 2 (alongside IO abstractions) — **Core phase complete.**
**Dependencies:** Phase 1 (containers), Phase 2A (IODevice)
**Depends-on-this:** Phase 4 (pipeline frame serialization), Phase 7 (ObjectBase saveState/loadState)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

**Maintenance note:** Completed items are removed from this document once merged. Only retain completed items when they provide context needed by a future phase in this same document. If something is done, trust the code and git history as the source of truth.

Provides promeki-native stream classes for binary and text I/O. These present a Qt-style API and integrate with IODevice and promeki types. DataStream and TextStream are implemented, and all std:: stream usage has been migrated out of the library (only `tests/doctest_main.cpp` retains std:: streams, required by the doctest API).

---

## Phase 2K: TextStream Promeki Type Operator Extensions

**Dependencies:** TextStream (complete)

Add free `operator<<(TextStream &, const T &)` and `operator>>(TextStream &, T &)` overloads so promeki types can be written/read with TextStream directly (e.g., `stream << myPoint << myTimecode`). Each overload lives in the type's own header, forward-declaring `TextStream` to avoid circular includes (similar to how Qt places `QTextStream` operators alongside each type). See `CODING_STANDARDS.md` for the full convention.

- [ ] `Point<T, N>` — e.g. `"(1, 2, 3)"`
- [ ] `Size2DTemplate<T>` — e.g. `"1920x1080"`
- [ ] `Rect<T>` — e.g. `"(x, y, w, h)"`
- [ ] `Rational<T>` — e.g. `"24000/1001"`
- [ ] `UUID` — string representation
- [ ] `Timecode` — SMPTE string
- [ ] `TimeStamp` — formatted representation
- [ ] `DateTime` — ISO 8601 or similar
- [ ] `FourCC` — four-character string
- [ ] `Color` — RGBA components
- [ ] `XYZColor` — X, Y, Z components
- [ ] `AudioDesc` — human-readable summary
- [ ] `ImageDesc` — human-readable summary
- [ ] `VideoDesc` — human-readable summary
- [ ] `Metadata` — key-value dump
- [ ] `Error` — name and description
- [ ] `Duration` — formatted representation
- [ ] `FrameRate` — string representation
- [ ] `StringList` — delimited output
- [ ] `List<T>` — bracketed, comma-separated (where T is streamable)
- [ ] `Map<K,V>` — bracketed key-value pairs
- [ ] `Set<T>` — bracketed, comma-separated

---

## Phase 2L: DataStream Promeki Type Serialization Extensions

**Dependencies:** DataStream (complete)
**Depends-on-this:** Phase 4 (pipeline frame serialization), Phase 7 (ObjectBase saveState/loadState)

Add `DataStream` operators for binary serialization of promeki types. Each overload lives in the type's own header, forward-declaring `DataStream` to avoid circular includes (same pattern as TextStream operators).

- [ ] `Point<T, N>` — N values of type T
- [ ] `Size2DTemplate<T>` — width, height
- [ ] `Rect<T>` — x, y, width, height
- [ ] `Rational<T>` — numerator, denominator
- [ ] `UUID` — 16 bytes (DataFormat)
- [ ] `Timecode` — frame number + mode identifier
- [ ] `TimeStamp` — internal representation
- [ ] `DateTime` — epoch + timezone info
- [ ] `Color` — RGBA components
- [ ] `XYZColor` — X, Y, Z components
- [ ] `AudioDesc` — sample rate, format, channels, etc.
- [ ] `ImageDesc` — width, height, pixel format, etc.
- [ ] `VideoDesc` — image desc + frame rate
- [ ] `Metadata` — serialize as key-value pairs
- [ ] `List<T>` — count + elements (where T is streamable)
- [ ] `Map<K,V>` — count + key-value pairs
- [ ] `Set<T>` — count + elements
- [ ] `HashMap<K,V>` — count + key-value pairs
- [ ] `HashSet<T>` — count + elements

### Extensibility
- [ ] Document pattern for user types: implement `operator<<(DataStream &, const MyType &)` and `operator>>(DataStream &, MyType &)`

---

## ObjectBase Serialization (saveState / loadState)

**Phase:** 7 (enhanced existing classes) — depends on DataStream
**Dependencies:** DataStream (complete)

Add binary state serialization to ObjectBase. Each subclass can override to save/restore its own state. Uses DataStream for portable, versioned binary format.

**Files:**
- [ ] Modify `include/promeki/objectbase.h`
- [ ] Modify `src/core/objectbase.cpp`
- [ ] `tests/objectbase_serialization.cpp`

**Implementation checklist:**
- [ ] `virtual Error saveState(DataStream &stream) const` — default: saves nothing, returns Ok
- [ ] `virtual Error loadState(DataStream &stream)` — default: reads nothing, returns Ok
- [ ] `Error saveState(Buffer &buffer) const` — convenience: creates DataStream over BufferIODevice
- [ ] `Error loadState(const Buffer &buffer)` — convenience: creates DataStream over BufferIODevice
- [ ] Convention: each class writes a version tag first, then its data
  - [ ] `stream << (uint16_t)1; // version`
  - [ ] On load: read version, handle backward compat
- [ ] Subclass pattern:
  ```
  Error MyClass::saveState(DataStream &stream) const {
      Error err = ObjectBase::saveState(stream);  // save parent state first
      if(err) return err;
      stream << (uint16_t)1;  // MyClass version
      stream << _myField1 << _myField2;
      return stream.status() == DataStream::Ok ? Error() : Error("write failed");
  }
  ```
- [ ] `PROMEKI_SIGNAL(stateLoaded)` — emitted after successful loadState
- [ ] `PROMEKI_SIGNAL(stateSaved)` — emitted after successful saveState (optional, for debugging)
- [ ] Recursive save/load: option to save/load child object states
  - [ ] `Error saveStateRecursive(DataStream &stream) const` — saves self + all children
  - [ ] `Error loadStateRecursive(DataStream &stream)` — loads self + all children
  - [ ] Children saved in order, with count prefix
- [ ] Doctest: save/load a test ObjectBase subclass, verify round-trip, test versioning, test recursive save/load

---

## Design Notes

### DataStream wire format
- Stream header: 4-byte magic (`0x50 0x4D 0x44 0x53` / "PMDS") + uint16_t version (big-endian). Current version: 1.
- Each value preceded by a one-byte TypeId tag (0x01–0x0E) for type validation on read.
- All multi-byte integers: byte-order controlled by `setByteOrder()`, default big-endian.
- Strings: `uint32_t` length prefix (byte count) + UTF-8 bytes (no null terminator). Latin1 with non-ASCII auto-converted to UTF-8.
- Buffers: `uint32_t` length prefix + raw bytes.
- Variants: TypeVariant tag + Variant::Type byte + untagged value. Complex types (DateTime, UUID, Timecode, etc.) serialized as String representation.
- Floats/doubles: IEEE 754, byte-swapped if needed.
- 256 MB sanity limit on String/Buffer reads.
- Raw byte methods (readRawData, writeRawData, skipRawData) are untagged.

### TextStream design
TextStream is an independent class (does not inherit from `std::ostream`). The formatting controls and encoding-awareness are different enough from `std::ostream` that inheritance creates more friction than value. This means all `operator<<`/`operator>>` overloads must be provided for TextStream explicitly. These overloads live in each type's own header (forward-declaring `TextStream`), not in `textstream.h`, following the Qt convention. The same applies to DataStream operators. See `CODING_STANDARDS.md` for the full convention.
