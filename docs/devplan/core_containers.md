# Core Containers and API Consistency

**Phase:** 1A, 1C
**Dependencies:** None
**Pattern reference:** `include/promeki/core/list.h`, `include/promeki/core/map.h`, `include/promeki/core/set.h`
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

## 1A. New Container Wrappers — COMPLETE

All new containers implemented, tested, and documented. Each follows the `List<T>` pattern: header-only template in `include/promeki/core/`, Qt-style method names, iterators, doctest unit test.

| Class | Header | Test | Notes |
|-------|--------|------|-------|
| `Pair<A,B>` | `include/promeki/core/pair.h` | `tests/pair.cpp` | Simple value type, structured bindings support |
| `Result<T>` | `include/promeki/core/result.h` | `tests/result.cpp` | `using` alias for `Pair<T, Error>` + helpers |
| `HashMap<K,V>` | `include/promeki/core/hashmap.h` | `tests/hashmap.cpp` | Shareable, wraps `std::unordered_map` |
| `HashSet<T>` | `include/promeki/core/hashset.h` | `tests/hashset.cpp` | Shareable, wraps `std::unordered_set`, set ops |
| `Deque<T>` | `include/promeki/core/deque.h` | `tests/deque.cpp` | Shareable, wraps `std::deque` |
| `Stack<T>` | `include/promeki/core/stack.h` | `tests/stack.cpp` | Simple value type, wraps `std::stack` |
| `PriorityQueue<T>` | `include/promeki/core/priorityqueue.h` | `tests/priorityqueue.cpp` | Simple value type, custom comparator support |
| `Span<T>` | `include/promeki/core/span.h` | `tests/span.cpp` | Non-owning view, wraps `std::span` |

---

## 1C. API Consistency Audit — COMPLETE

All existing containers brought to parity:

- **Set**: Added `revBegin()`, `revEnd()`, `constRevBegin()`, `constRevEnd()`, `swap()`, `unite()`, `intersect()`, `subtract()` + tests
- **Map**: Added `revBegin()`, `revEnd()`, `constRevBegin()`, `constRevEnd()`, `swap()` + tests
- **Queue**: Added `isEmpty()` + test
- **List**: Added `forEach()`, `indexOf()`, `lastIndexOf()`, `count()`, `mid()` + tests
- **All containers**: Verified consistent `forEach()`, naming conventions, copy/move semantics

---

## Remaining: Adopting Result\<T\> Across the Codebase

To be done as each class is implemented in its respective phase. Once a class exists, its `std::pair<T, Error>` returns should use `Result<T>`:

- [ ] `SocketAddress::fromString()` — `Result<SocketAddress>` (Phase 3A)
- [ ] `SdpSession::fromString()` — `Result<SdpSession>` (Phase 3C)
- [ ] `Interval::fromName()` — `Result<Interval>` (Phase 6A)
- [ ] `Chord::fromName()` — `Result<Chord>` (Phase 6A)
- [ ] `ChordProgression::fromRomanNumerals()` — `Result<ChordProgression>` (Phase 6A)
- [ ] `Key::fromName()` — `Result<Key>` (Phase 6A)
- [ ] `TimeSignature::fromString()` — `Result<TimeSignature>` (Phase 6A)
- [ ] `Tempo::fromMarking()` — `Result<Tempo>` (Phase 6A)
- [ ] `Dynamics::fromName()` — `Result<Dynamics>` (Phase 6A)
- [ ] `MidiFile::readFromFile()`, `readFromBuffer()` — `Result<MidiFile>` (Phase 6C)
- [ ] `MidiFile::writeToBuffer()` — `Result<Buffer>` (Phase 6C)
- [ ] `Instrument::fromGMProgram()`, `fromName()` — `Result<Instrument>` (Phase 6D)
- [ ] `Track::fromMidiTrack()` — `Result<Track>` (Phase 6D)
- [ ] `Arrangement::fromMidiFile()` — `Result<Arrangement>` (Phase 6D)
- [ ] `Future<T>::result()` — `Result<T>` (Phase 1B)
- [ ] `Queue<T>::pop()` — `Result<T>` (Phase 7)
- [ ] `MediaLink::pullFrame()`, `tryPullFrame()` — `Result<Frame::Ptr>` (Phase 4A)
- [ ] Existing codebase: migrate `std::pair<T, Error>` returns (e.g., `Timecode::fromString()`) to `Result<T>` (Phase 7)
