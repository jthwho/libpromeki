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

- [ ] `String::format` migration — migrate `String::sprintf` and `String::arg` call sites over time (neither is deprecated yet; just opportunistic)

---

## RegEx Enhancements

- [ ] `matchAll(const String &subject)` — returns `List` of all matches
- [ ] `captureGroups()` — returns `List<String>` of capture groups from last match
- [ ] Named capture support: `(?P<name>...)` syntax
- [ ] `namedCapture(const String &name)` — returns named capture from last match
- [ ] Update tests

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
- [x] Tests in `tests/crashhandler.cpp`
