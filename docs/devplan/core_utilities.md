# Core Utilities and Enhanced Existing Classes

**Phase:** 1D (utilities), 7 (enhanced existing classes)
**Dependencies:** Phase 1A containers for Algorithm
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

## Random

Wraps `<random>`. Utility class, not a data object.

**Files:**
- [ ] `include/promeki/random.h`
- [ ] `src/random.cpp`
- [ ] `tests/random.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<random>`), namespace
- [ ] Private: `std::mt19937` engine, seeded from `std::random_device` by default
- [ ] `randomInt(int min, int max)` — uniform integer distribution, inclusive
- [ ] `randomInt64(int64_t min, int64_t max)` — 64-bit variant
- [ ] `randomDouble(double min, double max)` — uniform real distribution
- [ ] `randomFloat(float min, float max)` — float variant
- [ ] `randomBytes(size_t count)` — returns `Buffer` of random bytes
- [ ] `randomBool()` — 50/50 true/false
- [ ] `seed(uint64_t seed)` — manual seed for reproducibility
- [ ] Static `global()` — returns thread-local global instance
- [ ] Doctest: range checks, reproducibility with same seed, randomBytes length

---

## ElapsedTimer

Wraps `std::chrono::steady_clock`. Utility class.

**Files:**
- [ ] `include/promeki/elapsedtimer.h`
- [ ] `tests/elapsedtimer.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<chrono>`), namespace
- [ ] Private: `std::chrono::steady_clock::time_point _start`
- [ ] Constructor: starts automatically
- [ ] `start()` — records start time
- [ ] `restart()` — records start time, returns elapsed since previous start (in ms)
- [ ] `elapsed()` — returns milliseconds since start as `int64_t`
- [ ] `elapsedUs()` — returns microseconds since start
- [ ] `elapsedNs()` — returns nanoseconds since start
- [ ] `hasExpired(int64_t ms)` — returns true if elapsed >= ms
- [ ] `isValid()` — returns true if `start()` has been called
- [ ] `invalidate()` — resets to invalid state
- [ ] Doctest: basic timing, restart returns elapsed, hasExpired, invalid state

---

## Duration

Wraps `std::chrono::duration`. Simple value type — no PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/duration.h`
- [ ] `tests/duration.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<chrono>`), namespace
- [ ] Internal storage: `std::chrono::nanoseconds`
- [ ] Static factories: `fromHours()`, `fromMinutes()`, `fromSeconds()`, `fromMilliseconds()`, `fromMicroseconds()`, `fromNanoseconds()`
- [ ] `hours()` — returns `int64_t`
- [ ] `minutes()` — returns `int64_t` (total, not remainder)
- [ ] `seconds()` — returns `int64_t`
- [ ] `milliseconds()` — returns `int64_t`
- [ ] `microseconds()` — returns `int64_t`
- [ ] `nanoseconds()` — returns `int64_t`
- [ ] `toSecondsDouble()` — returns `double`
- [ ] Arithmetic: `operator+`, `operator-`, `operator*` (scalar), `operator/` (scalar)
- [ ] Comparison: `operator==`, `operator!=`, `operator<`, `operator>`, `operator<=`, `operator>=`
- [ ] `isZero()` — returns bool
- [ ] `isNegative()` — returns bool
- [ ] `toString()` — human-readable (e.g., "1h 23m 45s")
- [ ] Doctest: construction, arithmetic, conversions, comparison, toString

---

## Algorithm

Header-only free functions operating on promeki containers.

**Files:**
- [ ] `include/promeki/algorithm.h`
- [ ] `tests/algorithm.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] `sorted(Container)` — returns sorted copy
- [ ] `sorted(Container, Compare)` — returns sorted copy with custom comparator
- [ ] `filtered(Container, Predicate)` — returns copy with only matching elements
- [ ] `mapped(Container, Transform)` — returns container of transformed elements (deduced return type)
- [ ] `allOf(Container, Predicate)` — returns bool
- [ ] `anyOf(Container, Predicate)` — returns bool
- [ ] `noneOf(Container, Predicate)` — returns bool
- [ ] `forEach(Container, Callable)` — applies callable to each element
- [ ] `accumulate(Container, Init, BinaryOp)` — fold/reduce
- [ ] `minElement(Container)` — returns iterator to minimum
- [ ] `maxElement(Container)` — returns iterator to maximum
- [ ] `contains(Container, Value)` — generic contains
- [ ] Works with `List`, `Set`, `Map`, `Deque`, and new containers via iterators
- [ ] Doctest: each function with List and at least one other container type

---

## Enhanced Existing Classes (Phase 7)

These enhancements are ongoing throughout other phases.

---

### Variant Enhancements

- [ ] Add `HashMap<String, Variant>` type support (map variant)
- [ ] Add `List<Variant>` type support (list/array variant)
- [ ] Add `Buffer` type support
- [ ] `toJson()` — convert variant to `JsonObject`/`JsonArray`
- [ ] `fromJson(const JsonObject &)` — construct variant from JSON
- [ ] `fromJson(const JsonArray &)` — construct variant from JSON array
- [ ] Round-trip tests: Variant -> JSON -> Variant
- [ ] Update `VariantImpl` template for new types
- [ ] Update tests

---

### String Enhancements

- [ ] `arg(const String &)` — Qt-style `String("Hello %1").arg(name)`, replaces lowest numbered `%N` placeholder
- [ ] `arg(int)`, `arg(double)` — numeric overloads with optional format/precision
- [ ] `number(int)` — static factory, returns String representation
- [ ] `number(double, int precision = 6)` — static factory
- [ ] `toInt(Error *err = nullptr)` — parse to int
- [ ] `toDouble(Error *err = nullptr)` — parse to double
- [ ] `toFloat(Error *err = nullptr)` — parse to float
- [ ] `toInt64(Error *err = nullptr)` — parse to int64_t
- [ ] Update tests for all new String methods

---

### RegEx Enhancements

- [ ] `matchAll(const String &subject)` — returns `List` of all matches
- [ ] `captureGroups()` — returns `List<String>` of capture groups from last match
- [ ] Named capture support: `(?P<name>...)` syntax
- [ ] `namedCapture(const String &name)` — returns named capture from last match
- [ ] Update tests
