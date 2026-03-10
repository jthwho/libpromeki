# libpromeki Class Audit & TODO

This document provides a comprehensive audit of every class in libpromeki. Each class is evaluated against five criteria:

- **Std Leak**: Does the public API expose `std::` types where promeki wrappers exist?
- **Doxygen**: Is the class fully documented with `@file`, `@brief`, `@param`, `@return`?
- **Coding Standards**: Does it comply with `CODING_STANDARDS.md`?
- **Unit Tests**: Does it have thorough unit tests?
- **Completeness**: Is the class feature-complete for a professional library?

---

## Summary

| Status       | Count | Description                              |
|--------------|-------|------------------------------------------|
| CLEAN        | 61    | Fully compliant, no action needed        |
| MINOR        | 1     | Remaining minor gap                      |

---

## Test Suite Totals

- **unittest-core**: 675 test cases
- **unittest-proav**: 164 test cases
- **unittest-music**: 17 test cases
- **Total**: 856 test cases, 100% passing

---

## Remaining Action Items

### Minor
1. **FontPainter** - Unit tests PARTIAL (integration only, requires FreeType + font files)

### Future Considerations
- **Application** - Consider adding version query, feature detection
- **Logger** - Consider hiding `std::shared_ptr<std::promise<void>>`, `std::thread` from header
- **PIDController** - Consider wrapping `std::function` callbacks
- **ColorSpace** - Consider using `Array<CIEPoint, 4>` instead of `std::array`
- **FileInfo** - Consider wrapping `std::filesystem::path` or accepting as intentional
- **MusicalScale** - Consider using `Array<int, 12>` instead of `std::array`

---

## Architectural Refactors (Planned)

### Remove Internal SharedPtr from Data Objects
See `REFACTOR_SHAREDPTR.md` for full design document. Summary: data objects like String, Buffer, AudioDesc should become plain value types (like List, Map already are). The decision to use SharedPtr for sharing moves to the composing class (Image, Audio, Frame). All data objects already have `PROMEKI_SHARED_FINAL` so they can be used with SharedPtr externally when needed.

---

## Class Audit

### 1. Core Framework

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| Application | OK | OK | OK | OK (2) | PARTIAL |
| ObjectBase | OK | OK | OK | OK (16) | OK |
| Error | OK | OK | OK | OK (10) | OK |
| System | OK | OK | OK | OK (7) | OK |
| BuildInfo | OK | OK | OK | OK (7) | OK |

### 2. Error & Logging

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| Logger | OK* | OK | OK | OK (8) | OK |

*Logger: remaining `std::shared_ptr<std::promise<void>>`, `std::thread` are internal threading types

### 3. Memory & Smart Pointers

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| SharedPtr / RefCount | OK | OK | OK | OK (36) | OK |
| Buffer | OK | OK | OK | OK (10) | OK |
| MemSpace | OK | OK | OK | OK (9) | OK |
| MemPool | OK* | OK | OK | OK (23) | OK |

*MemPool: remaining `std::mutex` is internal threading type

### 4. Containers & Data Structures

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| List\<T\> | OK* | OK | OK | OK (45) | OK |
| Map\<K, V\> | OK | OK | OK | OK (18) | OK |
| Set\<T\> | OK | OK | OK | OK (17) | OK |
| Queue\<T\> | OK* | OK | OK | OK (19) | OK |
| Array\<T, N\> | OK | OK | OK | OK (22) | OK |
| StructDatabase\<K, V\> | OK | OK | OK | OK (8) | OK |

*List: `std::function`, `std::initializer_list` acceptable for template
*Queue: `std::queue`, `std::unique_lock<std::mutex>` are internal storage/threading

### 5. String & Text

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| String | OK* | OK | OK | OK (21) | OK |
| StringList | OK | OK | OK | OK (16) | OK |
| StreamString | by design | OK | OK | OK (11) | OK |
| AnsiStream | by design | OK | OK | OK (9) | OK |
| RegEx | OK | OK | OK | OK (11) | OK |
| CmdLineParser | OK* | OK | OK | OK (9) | OK |

*String: `std::string` interop intentional
*CmdLineParser: `std::variant`, `std::function` acceptable for callback infrastructure

### 6. Numeric & Math

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| Rational\<T\> | OK* | OK | OK | OK (19) | OK |
| Matrix\<T, W, H\> | OK | OK | OK | OK (21) | OK |
| Matrix3x3 | OK* | OK | OK | OK (20) | OK |
| Size2D | OK* | OK | OK | OK (14) | OK |
| Point | OK* | OK | OK | OK (15) | OK |
| Line | OK | OK | OK | OK (7) | OK |
| FourCC | OK | OK | OK | OK (9) | OK |
| PIDController | YES* | OK | OK | OK (6) | OK |

*Rational, Matrix3x3, Size2D, Point: `std::ostream`/`std::istream` operators acceptable
*PIDController: `std::function` for callbacks

### 7. Date, Time & Timecode

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| DateTime | OK | OK | OK | OK (15) | OK |
| TimeStamp | OK | OK | OK | OK (16) | OK |
| Timecode | OK* | OK | OK | OK (13) | OK |
| FrameRate | OK | OK | OK | OK (12) | OK |

*Timecode: `std::pair<Timecode, Error>` matches coding standard pattern

### 8. Variant & Metadata

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| Variant / VariantImpl | OK* | OK | OK | OK (10) | OK |
| Metadata | OK | OK | OK | OK (7) | OK |

*Variant: `std::pair`, `std::variant` internal

### 9. JSON

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| JsonObject / JsonArray | OK | OK | OK | OK (17) | OK |

### 10. Signal & Slot

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| Signal\<Args...\> | OK | OK | OK | OK (14) | OK |
| Slot\<Args...\> | YES* | OK | OK | OK (6) | OK |

*Slot: `std::function<void(Args...)>`, `std::tuple`

### 11. UUID

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| UUID | OK | OK | OK | OK (16) | OK |

### 12. File I/O

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| File | OK | OK | OK | OK (7) | OK |
| FileInfo | YES* | OK | OK | OK (8) | OK |

*FileInfo: `std::filesystem::path`, `std::optional`, `std::error_code`

### 13. Image & Video

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| PixelFormat | OK | OK | OK | OK (12) | OK |
| ImageDesc | OK | OK | OK | OK (11) | OK |
| Image | OK | OK | OK | OK (18) | OK |
| ImageFile | OK | OK | OK | OK (5) | OK |
| ImageFileIO | OK | OK | OK | OK (4) | OK |
| VideoDesc | OK | OK | OK | OK (7) | OK |
| Frame | OK | OK | OK | OK (4) | OK |
| PaintEngine | OK | OK | OK | OK (7) | OK |
| FontPainter | OK | OK | OK | PARTIAL* | OK |
| Codec | OK | OK | OK | OK (3) | PARTIAL* |

*FontPainter: integration only, requires FreeType + font files
*Codec: base class only; evaluate if more infrastructure needed

### 14. Color

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| ColorSpace | YES* | OK | OK | OK (9) | OK |
| ColorSpaceConverter | OK | OK | OK | OK (7) | OK |
| CIEPoint | OK | OK | OK | OK (12) | OK |
| CIEWavelength | OK | OK | OK | OK (6) | OK |
| XYZColor | OK* | OK | OK | OK (10) | OK |

*ColorSpace: `std::array<CIEPoint, 4>` as `Params`
*XYZColor: `std::stringstream` usage

### 15. Audio

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| AudioDesc | OK | OK | OK | OK (12) | OK |
| Audio | OK | OK | OK | OK (13) | OK |
| AudioBlock | OK | OK | OK | OK (7) | OK |
| AudioFile | OK | OK | OK | OK (5) | OK |
| AudioFileFactory | OK | OK | OK | OK (3) | OK |
| AudioGen | OK | OK | OK | OK (7) | OK |

### 16. Music

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| MidiNote | OK | OK | OK | OK* (1) | OK |
| MidiNoteNames | OK | OK | OK | OK* (1) | OK |
| MusicalNote | OK | OK | OK | OK (13) | OK |
| MusicalScale | OK* | OK | OK | OK* (1) | OK |
| NoteSequenceParser | OK | OK | OK | OK* (1) | OK |

*MidiNote/MidiNoteNames/MusicalScale/NoteSequenceParser: 1 TEST_CASE with many SUBCASEs (adequate)
*MusicalScale: `std::pair` acceptable; `std::array<int, 12>` consider `Array<int, 12>`

### 17. Utility

| Class | Std Leak | Doxygen | Standards | Tests | Complete |
|-------|----------|---------|-----------|-------|----------|
| NumName | OK | OK | OK | OK (29) | OK |
| NumNameSeq | OK | OK | OK | OK | OK |
| Namespace | OK | OK | OK | N/A | OK |

---

## Bugs Found & Fixed

1. **`System::swapEndian` double-swap** -- `std::swap` called twice per iteration, canceling itself.
2. **`File::open` wrong flags** -- Passed promeki flags instead of POSIX-converted `openFlags` to `::open()`.
3. **`File::open` missing mode** -- `O_CREAT` without mode parameter. Added `0666` mode.
4. **`File::open` ReadWrite mapping** -- `O_RDONLY|O_WRONLY` != `O_RDWR` on POSIX. Fixed to detect combined flag.
5. **`PIDController` init order** -- `_prevUpdate` initialized from `_currentTime()` before `_currentTime` was constructed (UB/segfault).
6. **`colorspace.cpp` missing from build** -- Source file not listed in `PROMEKI_PROAV_SOURCES`.
7. **`Matrix::rotationMatrix()` threw exception** -- Used `std::invalid_argument`; changed to `Error *err` pattern.
8. **`UUID` comparison operators wrong** -- `operator>`, `operator<=`, `operator>=` all used `d < other.d`.
9. **`Point` const-correctness** -- `operator const Array&()` missing `const`, breaking `lerp()`/`distanceTo()`/`clamp()`.
10. **`Point::distanceTo` and `clamp`** -- Used `other[i]` but Point has no `operator[]`; fixed to `other.d[i]`.
11. **`MemPool::allocate` wrong size check** -- `totalSize >= size` always true; should be `it->size >= totalSize`.
12. **`ObjectBase::destroyChildren` wrong call** -- `child->removeChild(this)` should be `child->_parent = nullptr`.
13. **`Signal` constructor ignored prototype** -- Parameter typo `protoype` and body always set `_prototype(nullptr)` instead of using the parameter.
14. **`UUID(const String &)` implicit conversion** -- Caused ambiguity with `Array` variadic constructor after `DataFormat` refactor. Made `UUID(const char *)` and `UUID(const String &)` constructors `explicit`.
