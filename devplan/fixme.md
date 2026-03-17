# Existing FIXMEs

**Standards:** All fixes must follow `CODING_STANDARDS.md`. All changes require updated unit tests. See `README.md` for full requirements.

Tracked FIXME comments scattered across the codebase. Address these as they become relevant to ongoing phase work (e.g., fix the File Windows code when refactoring File to derive from IODevice in Phase 2).

---

## Windows File Implementation

**File:** `src/file.cpp:44`
**FIXME:** "The windows code here needs love."

The Windows `#ifdef` branch is a stub — `isOpen()` returns false, and the rest of the Windows-specific File methods are likely incomplete or missing.

- [ ] Implement Windows File backend using `CreateFile`/`ReadFile`/`WriteFile` HANDLE API
- [ ] Test on Windows (or at minimum ensure it compiles with correct stubs)
- [ ] Natural time to fix: Phase 2 File -> IODevice refactor

---

## ~~AudioDesc Metadata Comparison~~ (DONE)

**File:** `include/promeki/proav/audiodesc.h`

- [x] `operator==` now includes metadata comparison
- [x] Added `formatEquals()` for format-only comparison (type, rate, channels)
- [x] Added `operator==` to `Metadata` (via `toJson()`) and `JsonObject`
- [x] AudioFile write check uses `formatEquals()` since audio buffers don't carry file-level metadata
- [x] Tests added for `Metadata::operator==`, `JsonObject::operator==`, and `AudioDesc` equality variants

---

## ~~AudioFile libsndfile Readability Check~~ (DONE)

**File:** `src/audiofile_libsndfile.cpp`

- [x] `fileIsReadable()` now validates `info.channels > 0`, `info.samplerate > 0`, and `info.format != 0`
- [x] Added `memset` initialization of `SF_INFO` before `sf_open`
- [x] Returns false for files with unsupported configurations

---

## AudioGen Planar Format Support

**File:** `src/audiogen.cpp:64`
**FIXME:** "Need to set to new plane for planar."

Currently increments `data++` per channel, which only works for interleaved formats. Planar formats store each channel in a separate memory plane.

- [ ] Detect planar vs interleaved from `AudioDesc`
- [ ] For planar: advance to next plane's base pointer per channel
- [ ] For interleaved: keep current `data++` behavior
- [ ] Test with both planar and interleaved audio generation

---

## DateTime Number Word Parsing

**File:** `src/datetime.cpp:108`
**FIXME:** "Need to use the String::parseNumberWords()"

The `std::istringstream` was replaced with `strtoll` as part of the stream migration, but the FIXME still stands: the code should use `String::parseNumberWords()` for natural language number parsing (e.g., "three days ago") instead of bare `strtoll`.

- [ ] Implement or verify `String::parseNumberWords()` exists
- [ ] Replace `strtoll` token parsing with `String::parseNumberWords()`
- [ ] Update tests

---

## ~~FileInfo::suffix() Crashes on Extensionless Files~~ (DONE)

**File:** `include/promeki/core/fileinfo.h`

- [x] Guard against empty extension before calling `substr(1)`
- [x] Tests added for extensionless files, dotfiles, and compound extensions

---

## PixelFormat Memory Space Validation

**File:** `src/pixelformat_old.cpp:217`
**FIXME:** "We don't check the image is in a memory space we can access. Need to work out a way to dispatch to different fill() functions for different memory spaces."

- [ ] Add memory space check before calling fill function
- [ ] Design dispatch mechanism for per-memory-space fill implementations
- [ ] Consider if this ties into the pipeline framework's buffer handling (Phase 4)
- [ ] Update tests

---

## Dead Test File: tests/image.cpp

**File:** `tests/image.cpp`

`tests/image.cpp` is not listed in `UNITTEST_PROAV_SOURCES` in CMakeLists.txt and is never compiled. `tests/image2.cpp` appears to be its replacement. The file contains image creation, fill, PaintEngine, FontPainter, and PNG save tests that overlap with `image2.cpp` and `paintengine.cpp`.

- [ ] Verify no unique test coverage exists in `tests/image.cpp` that isn't already in `image2.cpp` or `paintengine.cpp`
- [ ] Delete `tests/image.cpp` once confirmed redundant

