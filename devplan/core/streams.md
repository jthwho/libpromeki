# Core Streams and Serialization

**Phase:** 2 — framework COMPLETE; extensions remain
**Dependencies:** Phase 1 (containers), Phase 2A (IODevice)
**Depends-on-this:** Phase 7 (ObjectBase saveState/loadState)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

`DataStream`, `TextStream`, and `StreamString` are implemented and used throughout the library. DataStream has full type coverage (primitives, containers, data objects, Variant symmetry, golden wire-format tests, `Result<T>` read API, extension API via `beginFrame`/`endFrame`/`readFrame`/`readFrameHeader`/`skipFrame`/`setError`). Wire format is now v3: each value is emitted as a self-describing frame (`[tag(2)][version(1)][size(4)][body]`) enabling per-type versioning and forward-compatible unknown-tag skipping. `TypeUrl = 0x46` added (Phase 4r) for `Url` round-trip via string form. See git history for the completed DataStream type expansion.

What remains: TextStream type operator overloads, and ObjectBase saveState/loadState.

### Journal — 2026-05-15: DataType registry, Variant rewrite, fromString standardization, converter consolidation (landed)

Three interlinked layers shipped in one commit:

**DataType registry** (`include/promeki/datatype.h`, `src/core/datatype.cpp`): TypeRegistry-pattern handle wrapping an immutable `Data` record per C++ type. Carries ID (alias of `DataStream::TypeId`), name, size, alignment, `type_index`, and an `Ops` table of function pointers (defaultConstruct, copyConstruct, moveConstruct, destroy, equal, toString, fromString, writeStream, readStream). `Detail::makeDefaultOps<T>` auto-populates slots via concept detection (`HasEqualityOp`, `HasMemberToString`, `HasResultFromString`, `HasDataStreamWriteV/ReadV`). Registration macros: `PROMEKI_IMPLEMENT_DATATYPE` (auto-ID) and `PROMEKI_IMPLEMENT_DATATYPE_ID` (pinned ID). New tests: `tests/unit/datatype.cpp` + `tests/unit/datatype_roundtrip.cpp` (generic round-trip across all 62 registered types; 57 round-trip, 5 skip due to no serialize op, 0 failures).

**Variant rewritten to registry-driven dispatch**: now stores a refcounted `VariantBox` payload identified by `DataType::ID` instead of `std::variant`. `readVariantPayload` collapsed from a 560-line per-type switch to a 15-line registry-driven dispatch using a new one-deep frame-header lookahead (`peekFrameHeader` + `_peekedHeaderValid`) so the path works on sequential IODevices (sockets, pipes). Wire format unchanged — still v3 framed. Deletions: `variant.tpp` and `variant_fwd.h` are gone; their content moved into `variant.h` / `variant.cpp` / `datatype.h`.

**fromString convention standardization**: UUID, UMID, FrameNumber, FrameCount, MediaDuration, DateTime, Color migrated to `Result<T> T::fromString(const String &)`. Added `Result<T> fromString` siblings to typereg wrappers (ColorModel, PixelMemLayout, PixelFormat, AncFormat) and to VariantList, VariantMap, Enum. `Detail::HasResultFromString<T>` concept in `datatype.h` auto-populates `ops.fromString` in `makeDefaultOps`. New `convertViaOpsFromString<T>` generic converter routes through `ops.fromString`; registered for every `(String, T)` pair via `registerStringToVia` / `registerStringToGroupVia`.

**Converter registry consolidation**: grouped registrations driven by role-based tuples (`NumericGroup`, `IntegerGroup`, `TypeRegFromStringGroup`, `StringRoundTripGroup`, `ToStringOnly`, `NetworkStringRoundTripGroup`). Only pairs with a real `convertOne` path are registered. `findConverter == nullptr` is now the canonical "no path" sentinel (was previously "always-fails converter"). Stripped ~200 lines of dead `if constexpr` fromString dispatch from `convertOne`.

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
- [ ] Convention: each class wraps its state in a `beginFrame`/`endFrame` pair using a stable user TypeId; the frame's per-type version field handles backward compat
  - [ ] `stream.beginFrame(TypeMyClass, 1); stream << _field1 << _field2; stream.endFrame();`
  - [ ] On load: `uint8_t ver = 0; stream.readFrame(TypeMyClass, 1, &ver);` — branch on `ver` when needed
- [ ] Subclass pattern:
  ```
  Error MyClass::saveState(DataStream &stream) const {
      Error err = ObjectBase::saveState(stream);  // save parent state first
      if(err) return err;
      stream.beginFrame(TypeMyClass, 1);
      stream << _myField1 << _myField2;
      stream.endFrame();
      return stream.toError();
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

### DataStream wire format (v3)

Wire format is frozen at v3 and protected by golden tests. Summary:

- Header: 16 bytes — `"PMDS"` magic, `uint16_t` version (big-endian), byte-order marker `'B'`/`'L'`, 9 reserved zero bytes
- Payload: self-describing frames — each value emits `[tag(uint16)][version(uint8)][size(uint32)][body]`
- The `size` field enables readers to skip past unknown tags without knowing the body layout (forward compat)
- The per-type `version` field lets individual types evolve their body encoding independently
- Strings: `uint32_t` length prefix + UTF-8 bytes (within the frame body)
- Buffers: `uint32_t` length prefix + raw bytes (within the frame body)
- Variants: no outer wrapper — writing `Variant{UUID}` is byte-identical to writing a direct `UUID`; the reader reads the frame header and dispatches; unknown tags consume the body via the size field and yield a default-constructed Variant without error
- Size2D/Rect/Point/Rational: inner values carry their own framed tags so one template operator covers all element types
- Containers: outer frame + framed `TypeUInt32` count + N fully-framed elements; 1 GiB per-frame sanity cap
- Raw byte escape hatch: `readRawData` / `writeRawData` / `skipRawData` are unframed; inside an open frame, `writeRawData` appends to the body buffer

### Extension API

Public: `beginFrame`/`endFrame` (write), `readFrame`/`readFrameHeader`/`skipFrame` (read), `setError`. User operators use these to frame their data and report errors the same way built-ins do. Any type with `operator<<` and `operator>>` overloads automatically works inside `List<T>`, `Map<K,V>`, etc., and through the `Result<T> read<T>()` API.

### TextStream design

TextStream is an independent class (does not inherit from `std::ostream`). All `operator<<`/`operator>>` overloads must be provided for TextStream explicitly. These overloads live in each type's own header, forward-declaring `TextStream`, following the Qt convention.
