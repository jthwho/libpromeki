# Core Streams and Serialization

**Phase:** 2 — framework COMPLETE; extensions remain
**Dependencies:** Phase 1 (containers), Phase 2A (IODevice)
**Depends-on-this:** Phase 7 (ObjectBase saveState/loadState)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

`DataStream`, `TextStream`, and `StreamString` are implemented and used throughout the library. DataStream has full type coverage (primitives, containers, data objects, Variant symmetry, golden wire-format tests, `Result<T>` read API, extension API via `writeTag`/`readTag`/`setError`). See git history for the completed DataStream type expansion.

What remains: TextStream type operator overloads, and ObjectBase saveState/loadState.

---

## Phase 2K: TextStream Promeki Type Operator Extensions

**Dependencies:** TextStream (complete)

Add free `operator<<(TextStream &, const T &)` and `operator>>(TextStream &, T &)` overloads so promeki types can be written/read with TextStream directly. Each overload lives in the type's own header, forward-declaring `TextStream` to avoid circular includes (Qt convention). See `CODING_STANDARDS.md`.

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
- [ ] `MediaDesc` — human-readable summary
- [ ] `Metadata` — key-value dump
- [ ] `Error` — name and description
- [ ] `Duration` — formatted representation
- [ ] `FrameRate` — string representation
- [ ] `VideoFormat` — SMPTE or generic string
- [ ] `StringList` — delimited output
- [ ] `List<T>` — bracketed, comma-separated (where T is streamable)
- [ ] `Map<K,V>` — bracketed key-value pairs
- [ ] `Set<T>` — bracketed, comma-separated

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

## Design Notes (retained for reference)

### DataStream wire format (v1)

Wire format is frozen and protected by golden tests. Summary:

- Header: 16 bytes — `"PMDS"` magic, `uint16_t` version (big-endian), byte-order marker `'B'`/`'L'`, 9 reserved zero bytes
- Payload: tagged values, one-byte `TypeId` preceding each value; tag ranges are documented in `datastream.h`
- Strings: `uint32_t` length prefix + UTF-8 bytes
- Buffers: `uint32_t` length prefix + raw bytes
- Variants: no outer wrapper — writing `Variant{UUID}` is byte-identical to writing a direct `UUID`; the reader peeks the tag and dispatches
- Size2D/Rect/Point/Rational: inner values carry their own tags so one template operator covers all element types
- Containers: outer tag + `TypeUInt32` count + N fully-tagged elements; 256 MiB sanity cap
- Raw byte escape hatch: `readRawData` / `writeRawData` / `skipRawData` are untagged

### Extension API

Public: `writeTag`, `readTag`, `setError`. User operators use these to frame their data and report errors the same way built-ins do. Any type with `operator<<` and `operator>>` overloads automatically works inside `List<T>`, `Map<K,V>`, etc., and through the `Result<T> read<T>()` API.

### TextStream design

TextStream is an independent class (does not inherit from `std::ostream`). All `operator<<`/`operator>>` overloads must be provided for TextStream explicitly. These overloads live in each type's own header, forward-declaring `TextStream`, following the Qt convention.
