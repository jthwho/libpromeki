# Enhanced Existing Classes

**Phase:** 7 (ongoing)
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

String, Variant, Color/ColorModel, StringRegistry/VariantDatabase/Config, Metadata, Size2DTemplate/TimeStamp, and the C++20 `std::format` integration are done. `SdpSession` was added as a Variant type (`TypeSdpSession`) with String-to-SdpSession conversion via `SdpSession::fromString()`. TimeStamp gained `promeki::Duration` interop operators (`+=`, `-=`, `+`, `-`, and `durationBetween`). See git history for the completed work. The items below are the remaining opportunistic enhancements.

---

## Result\<T\> Adoption

Opportunistic migration: any `std::pair<T, Error>` return still in the codebase should be converted to `Result<T>` when the class is next touched. No new code should return `std::pair<T, Error>`.

---

## Variant Enhancements

Needed to make `MediaConfig`/`MediaPipelineConfig` JSON round-trip work across every type a pipeline config might carry.

- [ ] `HashMap<String, Variant>` type support (map variant)
- [ ] `List<Variant>` type support (list/array variant)
- [ ] `Buffer` type support
- [ ] `toJson()` — convert variant to `JsonObject`/`JsonArray`
- [ ] `fromJson(const JsonObject &)` — construct variant from JSON
- [ ] `fromJson(const JsonArray &)` — construct variant from JSON array
- [ ] Round-trip tests: Variant → JSON → Variant for every registered Variant type (`PixelDesc`, `MediaDesc`, `FrameRate`, `Color`, `SocketAddress`, `UUID`, `UMID`, etc.)
- [ ] Update `VariantImpl` template for new types

---

## String Enhancements

- [x] `String::fromByteCount(uint64_t bytes, int maxDecimals = 3)` — metric (1000-based) human-readable byte formatting (B/KB/MB/GB/TB/PB/EB), trailing zeros trimmed
- [x] `String::fromByteCount(uint64_t bytes, int maxDecimals, const ByteCountStyle &style)` — same with explicit metric/binary style selection; `ByteCountStyle` provides compile-time type checking
- [ ] `String::format` migration — migrate `String::sprintf` and `String::arg` call sites over time (neither is deprecated yet; just opportunistic)

---

## RegEx Enhancements

- [ ] `matchAll(const String &subject)` — returns `List` of all matches
- [ ] `captureGroups()` — returns `List<String>` of capture groups from last match
- [ ] Named capture support: `(?P<name>...)` syntax
- [ ] `namedCapture(const String &name)` — returns named capture from last match
- [ ] Update tests

---

## TypedEnum CRTP — DONE

- [x] `TypedEnum<Derived>` template in `include/promeki/enum.h` — CRTP base pinning an `Enum` to a compile-time type; default ctor, `int` ctor, `String` ctor; publicly inherits `Enum` so slicing to `Enum` is safe and Variant/VariantDatabase compatibility is preserved
- [x] All 11 well-known enums in `include/promeki/enums.h` migrated from `struct X { static inline const Enum Y; }` pattern to `class X : public TypedEnum<X>` with external `inline const X::Y{val}` definitions
- [x] New `ByteCountStyle` enum added to `enums.h` (Metric=0, Binary=1, default Metric) — used by `String::fromByteCount`
- [x] `CODING_STANDARDS.md` updated with a "Well-Known Enums" section documenting the pattern, placement, function-signature guidance, and slicing-safe backward compatibility
- [x] Tests: `ByteCountStyle` slicing, integer ctor, name ctor exercised in `tests/string.cpp`

---

## Enum Enhancements

- [ ] `Enum::IDList` / `Enum::registeredTypes()` — already present; opportunistic: add `registeredValues(Type)` convenience if needed by pipeline config tooling

---

## Env Enhancements — DONE

- [x] `Env::list()` — returns all process environment variables as `Map<String, String>`
- [x] `Env::list(const RegEx &filter)` — returns filtered subset by name regex
- [x] Tests added in `tests/env.cpp`

---

## LibraryOptions — DONE

- [x] `LibraryOptions` — `VariantDatabase<LibraryOptionsTag>` singleton; options declared with `VariantSpec` (type, default, description)
- [x] `LibraryOptions::loadFromEnvironment()` — scans `PROMEKI_OPT_*` env vars, type-coerces via `VariantSpec::parseString`, warns on unknown names
- [x] Options: `CrashHandler` (bool, default true), `CoreDumps` (bool, default false), `CrashLogDir` (String, default empty), `CaptureEnvironment` (bool, default true)
- [x] `TerminationSignalHandler` (bool, default true) — controls whether `Application` installs `SignalHandler` on construction
- [x] `SignalDoubleTapExit` (bool, default true) — controls whether a second termination signal delivery calls `std::_Exit`
- [x] Wired into `Application` constructor; loads env overrides before `CrashHandler::install()`
- [x] Tests in `tests/libraryoptions.cpp`

---

## CrashHandler — DONE

- [x] Signal-safe crash handler for SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL
- [x] Writes crash report to stderr and pre-built log file path (no heap allocation in handler signal-safe stage)
- [x] Reports: signal name/number, PID, ISO 8601 UTC timestamp (hand-rolled, signal-safe), crashing thread TID/name, all thread TIDs/names (Linux: via `/proc/self/task` + `getdents64`)
- [x] Demangled C++ backtrace via `abi::__cxa_demangle` (gated on `PROMEKI_HAVE_CXA_DEMANGLE`; falls back to `backtrace_symbols_fd` if allocation fails)
- [x] OS info via `uname` (sysname, release, version, machine)
- [x] Memory stats via `getrusage` + `sysinfo` (resident set, virtual size, swap)
- [x] Resource limits via `getrlimit` (core, data, stack, open files, virtual memory)
- [x] CPU register dump from `ucontext_t` (x86_64 and aarch64)
- [x] `/proc/self/maps` memory map dump
- [x] Environment snapshot (gated on `LibraryOptions::CaptureEnvironment`)
- [x] `LibraryOptions` state snapshot appended to each report
- [x] `writeTrace(const char *reason)` — non-crash diagnostic snapshot with atomic seqno for unique filenames
- [x] Core dump support: raises `RLIMIT_CORE` to hard limit when `LibraryOptions::CoreDumps` is true
- [x] Non-POSIX stub provided for portability
- [x] Wired into `Application` constructor/destructor; `setAppName()` re-installs to pick up new name in log path
- [x] `Application::writeTrace()` forwarder added
- [x] Install-time snapshot includes registered `MemSpace` entries (id, name, `Stats*`); signal handler dumps per-space counters via direct atomic loads (async-signal-safe). `MaxMemSpaceNameLen=64`, warns on truncation.
- [x] `Application::refreshCrashHandler()` — re-runs `CrashHandler::install()` if installed, refreshing the snapshot to pick up MemSpaces registered after initial install
- [x] Application forwarders: `installCrashHandler()`, `uninstallCrashHandler()`, `isCrashHandlerInstalled()`
- [x] Tests in `tests/crashhandler.cpp`

---

## SignalHandler — DONE

- [x] `SignalHandler` class in `include/promeki/signalhandler.h` / `src/core/signalhandler.cpp`
- [x] POSIX: `sigaction` + self-pipe trick; dedicated `promeki-signals` watcher thread reads pipe and runs quit logic in normal thread context (no locks, no allocation in signal path)
- [x] Handles SIGINT, SIGTERM, SIGHUP, SIGQUIT on POSIX; CTRL_C_EVENT, CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT on Windows
- [x] Calls `Application::quit(128+signo)` and posts `QuitItem` to `Application::mainEventLoop()` on delivery
- [x] Double-tap force-exit: second delivery of any caught signal calls `std::_Exit(128+signo)` (controlled by `LibraryOptions::SignalDoubleTapExit`)
- [x] `install()` / `uninstall()` / `isInstalled()` — idempotent, safe to call multiple times
- [x] Application forwarders: `installSignalHandlers()`, `uninstallSignalHandlers()`, `areSignalHandlersInstalled()`
- [x] Wired into `Application` constructor/destructor (gated on `LibraryOptions::TerminationSignalHandler`)
- [x] `mediaplay` ad-hoc `std::signal(SIGINT/SIGTERM)` calls removed; library-wide handler takes over
- [x] Tests in `tests/signalhandler.cpp` (lifecycle, double-install, Application forwarders, SIGINT end-to-end with EventLoop wakeup on POSIX)
