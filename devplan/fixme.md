# Existing FIXMEs

**Standards:** All fixes must follow `CODING_STANDARDS.md`. All changes require updated unit tests. See `README.md` for full requirements.

Tracked FIXME comments scattered across the codebase. Address these as they become relevant to ongoing phase work (e.g., fix the File Windows code when refactoring File to derive from IODevice in Phase 2).

---

## Windows File Implementation

**File:** `src/core/file.cpp:40`
**FIXME:** "The windows code here needs love."

The Windows `#ifdef` branch is a stub — `isOpen()` returns false, and the rest of the Windows-specific File methods are likely incomplete or missing.

- [ ] Implement Windows File backend using `CreateFile`/`ReadFile`/`WriteFile` HANDLE API
- [ ] Test on Windows (or at minimum ensure it compiles with correct stubs)
- [ ] Natural time to fix: Phase 2 File -> IODevice refactor

---

## AudioGen Planar Format Support

**File:** `src/proav/audiogen.cpp:66`
**FIXME:** "Need to set to new plane for planar."

Currently increments `data++` per channel, which only works for interleaved formats. Planar formats store each channel in a separate memory plane.

- [ ] Detect planar vs interleaved from `AudioDesc`
- [ ] For planar: advance to next plane's base pointer per channel
- [ ] For interleaved: keep current `data++` behavior
- [ ] Test with both planar and interleaved audio generation

---

## DateTime Number Word Parsing

**File:** `src/core/datetime.cpp:112`
**FIXME:** "Need to use the String::parseNumberWords()"

The `std::istringstream` was replaced with `strtoll` as part of the stream migration, but the FIXME still stands: the code should use `String::parseNumberWords()` for natural language number parsing (e.g., "three days ago") instead of bare `strtoll`.

- [ ] Implement or verify `String::parseNumberWords()` exists
- [ ] Replace `strtoll` token parsing with `String::parseNumberWords()`
- [ ] Update tests

---

## Replace Direct std Library Usage with Library Wrappers

Library classes should use the library's own container/type wrappers (`List`, `Map`, `Array`, `String`) instead of raw std types. The following violations have been identified:

### std::vector → List\<T\>

- **`src/core/bufferediodevice.cpp:149,167,211,227,240`** — Multiple `std::vector<uint8_t>` used as temporary read/collect buffers.
- **`src/proav/imagefileio_png.cpp:105`** — `std::vector<png_bytep>` for PNG row pointers.

### std::map → Map\<K,V\>

- **`src/core/string.cpp:283`** — `static const std::map<std::string, int64_t> numberWords` lookup table.
- **`src/core/datetime.cpp:78`** — `static const std::map<std::string, system_clock::duration> units` lookup table.

### std::array → Array\<T,N\>

- **`include/promeki/macaddress.h:109`** — `std::array<uint8_t, 6>` in constructor initializer.
- **`include/promeki/musicalscale.h:45`** — `using MembershipMask = std::array<int, 12>` public typedef.
- **`include/promeki/util.h:116,127,141-144,154`** — `std::array<T, 4>` in public template function signatures (`promekiCatmullRom`, `promekiBezier`, `promekiBicubic`, `promekiCubic`).
- **`src/core/system.cpp:27`** — `std::array<char, HOST_NAME_MAX>` local variable.

### Tasks

- [ ] Replace `std::vector` with `List<T>` in `src/core/bufferediodevice.cpp`
- [ ] Replace `std::vector` with `List<T>` in `src/proav/imagefileio_png.cpp`
- [ ] Replace `std::map` with `Map<K,V>` in `src/core/string.cpp` and `src/core/datetime.cpp`
- [ ] Replace `std::array` with `Array<T,N>` in `macaddress.h`, `musicalscale.h`
- [ ] Replace `std::array` with `Array<T,N>` in `util.h` template functions
- [ ] Replace `std::array` with `Array<T,N>` in `src/core/system.cpp`
- [ ] Verify all replacements compile and pass tests

