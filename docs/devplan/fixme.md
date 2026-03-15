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

## Thread::threadEventLoop() Cross-Thread Bug

**File:** `src/thread.cpp:178`
**FIXME:** "This only works when called from the adopted thread itself. Cross-thread callers will get the wrong EventLoop (or nullptr) because EventLoop::current() is thread-local."

**Fixed in Phase 1B.** `threadEventLoop()` now caches the EventLoop pointer in `_threadLoop` when called from the adopted thread itself. Cross-thread callers see the cached value. `quit()` also uses `threadEventLoop()` for consistency.

- [x] Have `threadEventLoop()` lazily cache the EventLoop pointer for adopted threads
- [x] `threadEventLoop()` returns cached pointer for cross-thread callers
- [x] `quit()` uses `threadEventLoop()` instead of duplicating adopted-thread logic

---

## AudioDesc Metadata Comparison

**File:** `include/promeki/proav/audiodesc.h:277`
**FIXME:** "We don't currently compare metadata" in `operator==`.

- [ ] Include metadata in `operator==` comparison
- [ ] Decide if metadata equality should be strict or optional (separate `equals()` with flags?)
- [ ] Update tests

---

## AudioFile libsndfile Readability Check

**File:** `src/audiofile_libsndfile.cpp:312`
**FIXME:** "Have a look at the info struct and decide if we actually can read it."

`fileIsReadable()` opens the file and immediately returns true without inspecting the `SF_INFO` struct (channels, sample rate, format).

- [ ] Check `info.channels > 0`, `info.samplerate > 0`, and supported format
- [ ] Return false for files with unsupported configurations
- [ ] Update tests

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

Currently uses `std::istringstream` to parse integer tokens. Should use `String::parseNumberWords()` for natural language number parsing (e.g., "three days ago").

- [ ] Implement or verify `String::parseNumberWords()` exists
- [ ] Replace `std::istringstream(token) >> count` with `String::parseNumberWords()`
- [ ] This also eliminates one `std::istringstream` usage (see `core_streams.md` migration)
- [ ] Update tests

---

## PixelFormat Memory Space Validation

**File:** `src/pixelformat_old.cpp:217`
**FIXME:** "We don't check the image is in a memory space we can access. Need to work out a way to dispatch to different fill() functions for different memory spaces."

- [ ] Add memory space check before calling fill function
- [ ] Design dispatch mechanism for per-memory-space fill implementations
- [ ] Consider if this ties into the pipeline framework's buffer handling (Phase 4)
- [ ] Update tests

---

## Platform-Specific RNG

**File:** `src/util.cpp:28`
**FIXME:** "Move over platform specific RNG stuff"

`promekiRand()` uses `fallbackRand()` instead of platform-specific cryptographic RNG (`/dev/urandom`, `CryptGenRandom`, `getrandom()`).

- [ ] Natural time to fix: Phase 1D when implementing the `Random` class
- [ ] `Random` class should use platform RNG for seeding; `promekiRand()` can delegate to `Random::randomBytes()` or use the same platform backend
- [ ] Update tests
