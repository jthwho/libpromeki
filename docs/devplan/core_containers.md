# Core Containers and API Consistency

**Phase:** 1A, 1C
**Dependencies:** None
**Pattern reference:** `include/promeki/list.h`, `include/promeki/map.h`, `include/promeki/set.h`
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

## 1A. New Container Wrappers

Each follows the `List<T>` pattern: header-only template in `include/promeki/`, Qt-style method names, iterators, doctest unit test. Shareable containers get `PROMEKI_SHARED_FINAL`, `::Ptr`, `::List`, `::PtrList`.

---

### Pair\<A,B\>

Wraps `std::pair<A,B>`. Simple value type — no PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/pair.h`
- [ ] `tests/pair.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<utility>`), namespace
- [ ] Wrap `std::pair<A,B>` as private member
- [ ] Type aliases: `FirstType`, `SecondType`
- [ ] Constructors: default, from `A` + `B`, from `std::pair<A,B>`
- [ ] `first()` — reference access to first element
- [ ] `first() const` — const reference
- [ ] `second()` — reference access to second element
- [ ] `second() const` — const reference
- [ ] `setFirst(const A &)`, `setSecond(const B &)`
- [ ] `operator==`, `operator!=`, `operator<`
- [ ] `toStdPair()` — returns `const std::pair<A,B> &`
- [ ] Implicit conversion from `std::pair<A,B>` for interop
- [ ] `swap(Pair &other)`
- [ ] Static `make(A, B)` — factory (like `std::make_pair`)
- [ ] Structured bindings support: specialize `std::tuple_size`, `std::tuple_element`, `get<>` so `auto [a, b] = pair;` works
- [ ] Doctest: construction, accessors, comparison, structured bindings, std::pair interop

### Result\<T\>

Convenience alias for the `Pair<T, Error>` pattern used throughout the library for fallible factory methods and parse operations. Not a separate class — just a `using` alias with helper methods.

**Files:**
- [ ] `include/promeki/result.h`
- [ ] `tests/result.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<promeki/pair.h>`, `<promeki/error.h>`), namespace
- [ ] `template <typename T> using Result = Pair<T, Error>;`
- [ ] Free function `Result<T> makeResult(T value)` — wraps value with `Error::Ok`
- [ ] Free function `Result<T> makeError(Error err)` — wraps default-constructed T with error
- [ ] Convenience accessors (free functions or via Pair specialization):
  - [ ] `value(const Result<T> &)` — alias for `first()`
  - [ ] `error(const Result<T> &)` — alias for `second()`
  - [ ] `isOk(const Result<T> &)` — shorthand for `second().isOk()`
  - [ ] `isError(const Result<T> &)` — shorthand for `second().isError()`
- [ ] Structured bindings still work: `auto [val, err] = someFactory();`
- [ ] Doctest: makeResult, makeError, isOk/isError, structured bindings, use with from*() pattern

### Adopting Result\<T\> Across the Plan

Once `Result<T>` exists, all `std::pair<T, Error>` return types throughout the plan become `Result<T>`:

- [ ] `IODevice::seek()` — already returns `Error` (no change, not a pair)
- [ ] `SocketAddress::fromString()` — `Result<SocketAddress>`
- [ ] `SdpSession::fromString()` — `Result<SdpSession>`
- [ ] `Interval::fromName()` — `Result<Interval>`
- [ ] `Chord::fromName()` — `Result<Chord>`
- [ ] `ChordProgression::fromRomanNumerals()` — `Result<ChordProgression>`
- [ ] `Key::fromName()` — `Result<Key>`
- [ ] `TimeSignature::fromString()` — `Result<TimeSignature>`
- [ ] `Tempo::fromMarking()` — `Result<Tempo>`
- [ ] `Dynamics::fromName()` — `Result<Dynamics>`
- [ ] `MidiFile::readFromFile()`, `readFromBuffer()` — `Result<MidiFile>`
- [ ] `MidiFile::writeToBuffer()` — `Result<Buffer>`
- [ ] `Instrument::fromGMProgram()`, `fromName()` — `Result<Instrument>`
- [ ] `Track::fromMidiTrack()` — `Result<Track>`
- [ ] `Arrangement::fromMidiFile()` — `Result<Arrangement>`
- [ ] `Future<T>::result()` — `Result<T>`
- [ ] `Queue<T>::pop()` — `Result<T>`
- [ ] `MediaLink::pullFrame()`, `tryPullFrame()` — `Result<Frame::Ptr>`
- [ ] Existing codebase: migrate `std::pair<T, Error>` returns (e.g., `Timecode::fromString()`) to `Result<T>`

---

### HashMap\<K,V\>

Wraps `std::unordered_map<K,V>`.

**Files:**
- [ ] `include/promeki/hashmap.h`
- [ ] `tests/hashmap.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Wrap `std::unordered_map<K,V>` as private member
- [ ] Type aliases: `Key`, `Value`, `Iterator`, `ConstIterator`
- [ ] `size()`, `isEmpty()`, `clear()`
- [ ] `contains(const K &key)` — returns bool
- [ ] `insert(const K &key, const V &value)`
- [ ] `remove(const K &key)` — returns bool (true if removed)
- [ ] `value(const K &key)` — returns `V` (default-constructed if missing)
- [ ] `value(const K &key, const V &defaultValue)` — returns value or default
- [ ] `operator[](const K &key)` — reference access
- [ ] `keys()` — returns `List<K>`
- [ ] `values()` — returns `List<V>`
- [ ] `begin()`, `end()`, `constBegin()`, `constEnd()`
- [ ] `forEach(Callable)` — iterate with callback
- [ ] `swap(HashMap &other)`
- [ ] `operator==`, `operator!=`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Register in `VariantImpl` if needed
- [ ] Doctest: insert/remove, contains, keys/values, empty state, copy, iteration

---

### HashSet\<T\>

Wraps `std::unordered_set<T>`.

**Files:**
- [ ] `include/promeki/hashset.h`
- [ ] `tests/hashset.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Wrap `std::unordered_set<T>` as private member
- [ ] Type aliases: `Value`, `Iterator`, `ConstIterator`
- [ ] `size()`, `isEmpty()`, `clear()`
- [ ] `contains(const T &value)` — returns bool
- [ ] `insert(const T &value)`
- [ ] `remove(const T &value)` — returns bool
- [ ] `toList()` — returns `List<T>`
- [ ] `begin()`, `end()`, `constBegin()`, `constEnd()`
- [ ] `forEach(Callable)`
- [ ] `swap(HashSet &other)`
- [ ] `unite(const HashSet &other)` — set union
- [ ] `intersect(const HashSet &other)` — set intersection
- [ ] `subtract(const HashSet &other)` — set difference
- [ ] `operator==`, `operator!=`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: insert/remove, contains, toList, set operations, empty state

---

### Deque\<T\>

Wraps `std::deque<T>`.

**Files:**
- [ ] `include/promeki/deque.h`
- [ ] `tests/deque.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Wrap `std::deque<T>` as private member
- [ ] Type aliases: `Value`, `Iterator`, `ConstIterator`
- [ ] `size()`, `isEmpty()`, `clear()`
- [ ] `pushToFront(const T &value)`
- [ ] `pushToBack(const T &value)`
- [ ] `popFromFront()` — returns `T`
- [ ] `popFromBack()` — returns `T`
- [ ] `front()`, `back()` — reference access
- [ ] `operator[](size_t index)` — reference access
- [ ] `at(size_t index)` — bounds-checked access
- [ ] `begin()`, `end()`, `constBegin()`, `constEnd()`
- [ ] `revBegin()`, `revEnd()`, `constRevBegin()`, `constRevEnd()`
- [ ] `forEach(Callable)`
- [ ] `swap(Deque &other)`
- [ ] `operator==`, `operator!=`
- [ ] PROMEKI_SHARED_FINAL, `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: push/pop front and back, indexing, empty state, iteration

---

### Stack\<T\>

Wraps `std::stack<T>` (or `std::deque<T>` directly for iterator access).

**Files:**
- [ ] `include/promeki/stack.h`
- [ ] `tests/stack.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Wrap `std::stack<T>` as private member
- [ ] `size()`, `isEmpty()`, `clear()`
- [ ] `push(const T &value)`
- [ ] `pop()` — returns `T`
- [ ] `top()` — reference access
- [ ] `constTop()` — const reference access
- [ ] `swap(Stack &other)`
- [ ] `operator==`, `operator!=`
- [ ] Doctest: push/pop, top, empty state, LIFO order verification

---

### PriorityQueue\<T\>

Wraps `std::priority_queue<T>`. Not thread-safe; for use inside synchronized contexts.

**Files:**
- [ ] `include/promeki/priorityqueue.h`
- [ ] `tests/priorityqueue.cpp`

**Implementation checklist:**
- [ ] Header guard, includes, namespace
- [ ] Template parameters: `T`, optional `Compare = std::less<T>`
- [ ] Wrap `std::priority_queue<T, std::vector<T>, Compare>` as private member
- [ ] `size()`, `isEmpty()`
- [ ] `push(const T &value)`
- [ ] `pop()` — returns `T` (highest priority)
- [ ] `top()` — const reference to highest priority
- [ ] `swap(PriorityQueue &other)`
- [ ] Doctest: push/pop ordering, empty state, custom comparator

---

### Span\<T\>

Wraps `std::span<T>`. Non-owning view — no PROMEKI_SHARED_FINAL.

**Files:**
- [ ] `include/promeki/span.h`
- [ ] `tests/span.cpp`

**Implementation checklist:**
- [ ] Header guard, includes (`<span>`), namespace
- [ ] Wrap `std::span<T>` as private member
- [ ] Constructors: from pointer+size, from `List<T>`, from `Array<T,N>`, from C array
- [ ] `size()`, `isEmpty()`
- [ ] `front()`, `back()` — reference access
- [ ] `operator[](size_t index)` — reference access
- [ ] `data()` — raw pointer
- [ ] `subspan(size_t offset, size_t count)` — returns new `Span<T>`
- [ ] `first(size_t count)`, `last(size_t count)`
- [ ] `begin()`, `end()`, `constBegin()`, `constEnd()`
- [ ] `revBegin()`, `revEnd()`, `constRevBegin()`, `constRevEnd()`
- [ ] `forEach(Callable)`
- [ ] Doctest: construction from various sources, subspan, empty state, iteration

---

## 1C. API Consistency Audit

Bring all existing containers to parity with each other.

---

### Set Additions

- [ ] `revBegin()` — reverse iterator (List has this, Set doesn't)
- [ ] `revEnd()` — reverse iterator
- [ ] `constRevBegin()` — const reverse iterator
- [ ] `constRevEnd()` — const reverse iterator
- [ ] `swap(Set &other)`
- [ ] `unite(const Set &other)` — set union (returns new Set or modifies in place)
- [ ] `intersect(const Set &other)` — set intersection
- [ ] `subtract(const Set &other)` — set difference
- [ ] Update tests for new Set methods

---

### Map Additions

- [ ] `revBegin()` — reverse iterator
- [ ] `revEnd()` — reverse iterator
- [ ] `constRevBegin()` — const reverse iterator
- [ ] `constRevEnd()` — const reverse iterator
- [ ] `swap(Map &other)`
- [ ] Update tests for new Map methods

---

### Queue Additions

- [ ] `isEmpty()` — canonical emptiness check (has `size()` but missing this)
- [ ] Update tests for Queue isEmpty()

---

### List Additions

- [ ] `indexOf(const T &value)` — returns index or -1
- [ ] `lastIndexOf(const T &value)` — returns index or -1
- [ ] `count(const T &value)` — count occurrences of value
- [ ] `mid(size_t pos, size_t length)` — sublist extraction
- [ ] Update tests for new List methods

---

### All Containers Verification

- [ ] Verify consistent `forEach()` support across List, Set, Map, Queue, and all new containers
- [ ] Verify consistent naming conventions across all containers
- [ ] Verify all containers have proper copy/move constructors and assignment operators
