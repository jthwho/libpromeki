# Logger Ring Buffer + Crash Log Integration

**Phase:** Cross-cutting (logger + crash handler)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

## Motivation

When a crash occurs, the last few log messages before the fault are usually the highest-value debugging information available — they tell you what the program was *doing* when it died, which the stack trace alone can't convey (e.g. which file it was parsing, which frame it was encoding, which network peer it was talking to).

The existing `CrashHandler` captures build info, OS info, memory, thread list, stack trace, and memory map (see `include/promeki/crashhandler.h`), but has no access to recent log history because `Logger` has no retained-history mechanism.

Adding a bounded ring buffer to `Logger` that the crash handler can walk at crash time closes this gap.

## Design Constraints

1. **Crash-handler-readable.** The ring buffer must be walkable from inside a signal handler without taking the logger's mutex — a crash-time deadlock on the logger's internal lock is strictly worse than missing recent log lines.
2. **Lock-free writer.** The logger's worker thread is the sole writer. The crash handler is a rare opportunistic reader. A single-producer / single-reader lock-free ring is the right primitive.
3. **Bounded memory.** Pre-allocated fixed-size storage. No heap allocation in the hot log path beyond what the logger already does.
4. **Best-effort read.** At crash time, a torn or partially-written entry is acceptable as long as it can't cause the crash handler to itself crash — reading stale/garbage data is fine, dereferencing a half-updated pointer is not.
5. **Configurable.** Size, enable/disable, and "minimum severity to retain" all go through `LibraryOptions`.
6. **Zero cost when disabled.** If the ring buffer is disabled the logger's fast path must pay no extra per-message cost.

## Proposed API

### Logger additions

```cpp
class Logger {
public:
        /// A retained log entry, as copied into the ring buffer.
        struct Retained {
                DateTime        ts;
                LogLevel        level;
                const char *    file;   ///< Static pointer; lifetime = program.
                int             line;
                uint64_t        threadId;
                char            msg[RetainedMsgLen]; ///< NUL-terminated; may be truncated.
        };

        /// Sets the ring buffer capacity.  Must be called before
        /// the first log message is enqueued.  A value of 0 disables
        /// retention.  Controlled by @ref LibraryOptions::LogRetention.
        void setRetentionCapacity(size_t entries);

        /// Copies the currently-retained entries into @p out in
        /// chronological order (oldest first).  Safe to call from
        /// any thread at any time.  Not async-signal-safe.
        void retainedEntries(List<Retained> &out) const;

        /// Async-signal-safe: writes the retained entries directly
        /// to a file descriptor in the format used by the crash
        /// handler's report body.  Walks the ring without taking
        /// any locks.
        void writeRetainedEntries(int fd) const;
};
```

Notes:
- `RetainedMsgLen` is a compile-time constant (e.g. 256 bytes). Longer messages are truncated in the ring — the full message still goes through the logger's normal console / file path.
- `file` is stored as the raw `const char *` from the call site (via `__FILE_NAME__` / `sourceFileName`). These pointers live for the entire program lifetime so they're safe to read from a signal handler.

### Ring buffer internals

```cpp
// Inside Logger (private):
std::atomic<uint64_t>   _retainSeq{0};    ///< Monotonic entry index.
Retained *              _retainRing = nullptr;
size_t                  _retainCap  = 0;
```

Writer (logger worker thread):
1. Atomically increment `_retainSeq` to claim a slot.
2. Compute `slot = (seq - 1) % _retainCap`.
3. Fill `_retainRing[slot]` with the entry data.
4. Release-fence before a future read of the entry (implicit via the atomic).

Reader (crash handler):
1. Load `_retainSeq` with acquire ordering → `seq`.
2. Compute how many entries to walk: `min(seq, _retainCap)`.
3. Walk them oldest-first. For each, copy the entry to a local buffer and sanity-check before printing (e.g. file pointer non-null, msg first byte in printable range).
4. Skip anything that fails sanity.

The race is: a writer may be mid-copy on the slot we're reading. Acceptable outcomes are "we see the old entry" or "we see the new entry"; "we crash dereferencing garbage" is not. Sanity checks on file pointer and msg content defend against the torn-read case.

### LibraryOptions additions

```cpp
/// @brief int — number of log messages retained in the ring buffer
/// for inclusion in crash reports (0 = disabled, default 128).
static inline const ID LogRetention = declareID("LogRetention",
        VariantSpec().setType(Variant::TypeS32)
                .setDefault(int32_t(128))
                .setMin(0).setMax(4096)
                .setDescription("Log messages retained for crash reports."));

/// @brief Enum @ref LogLevel — minimum severity retained in the
/// crash ring buffer (Info by default, so Debug chatter is excluded).
static inline const ID LogRetentionLevel = declareID("LogRetentionLevel",
        VariantSpec().setType(Variant::TypeEnum)
                .setDefault(Logger::Info)
                .setEnumType(Logger::LogLevelType)
                .setDescription("Minimum severity retained for crash reports."));
```

Wired into `Application::Application()` alongside the existing `CrashHandler::install()` call so the ring buffer is allocated and active before the first log message.

### CrashHandler integration

`writeReportBody` gains a new section between "Resource Limits" and "Thread":

```
--- Recent Log ---
2026-04-10T17:00:01Z  I thread.cpp:142      [worker-0] starting frame 42
2026-04-10T17:00:01Z  W mediaio.cpp:88      [worker-0] backpressure (queue=15)
2026-04-10T17:00:02Z  E rtpreader.cpp:203   [logger]  packet loss detected
```

Implemented by calling `Logger::defaultLogger().writeRetainedEntries(fd)` with both stderr and the log file. If retention is disabled or the ring is empty, the section is omitted.

## Tasks

- [ ] `Logger::Retained` struct with fixed-size `msg[]`.
- [ ] Ring buffer allocation (`setRetentionCapacity`).
- [ ] Lock-free enqueue from the logger worker thread at the same point the entry is passed to formatters.
- [ ] `retainedEntries(List &)` — mutex-taking reader for normal-context callers (tests).
- [ ] `writeRetainedEntries(int fd)` — async-signal-safe reader for `CrashHandler`.
- [ ] Sanity-check on torn reads (file pointer validity, msg bounds).
- [ ] `LibraryOptions::LogRetention` + `LogRetentionLevel` IDs.
- [ ] `Application::Application()` wires up the ring buffer before `CrashHandler::install()`.
- [ ] `CrashHandler::writeReportBody` emits `--- Recent Log ---` section.
- [ ] Unit test: log N messages, call `retainedEntries`, verify oldest-first order and correct truncation when N > capacity.
- [ ] Unit test: concurrent logger threads writing while a reader walks — no crashes, no invalid data returned.
- [ ] Unit test: `writeRetainedEntries(fd)` produces the expected format into a temp file.
- [ ] Crashtest demo update: log a few messages before crashing, verify the recent-log section appears in the crash report.
- [ ] Doxygen: update `Logger` class doc and add `@par` example showing the retained-entries API.

## Open Questions

1. **Should `file` be stored as a copied string** (safer against bugs where `__FILE__` pointers might somehow not be static, e.g. generated code) or the raw pointer (smaller, faster)? Preference: raw pointer — `__FILE__` is always a string literal. Revisit if we ever add plugin/JIT code.
2. **Per-thread ring** (simpler to walk, requires TLS) vs **global ring** (one place, crossable thread boundaries)? Preference: global ring — the value of recent log context is inter-thread.
3. **Include the thread name per entry?** The logger already caches thread names in `_threadNames`. Storing a pointer into that map is unsafe (the map can rehash). Best option: store the thread ID and let the crash handler resolve via `readThreadName()` at dump time.

## Related Work

- Cross-references `CrashHandler` (`include/promeki/crashhandler.h`, `src/core/crashhandler.cpp`).
- Depends on `Logger` (`include/promeki/logger.h`, `src/core/logger.cpp`).
- Adds two entries to `LibraryOptions` (`include/promeki/libraryoptions.h`).
