# Debugging and Diagnostics {#debugging}

Build types, debug logging, and crash handling in libpromeki.

This page explains the tools libpromeki provides for diagnosing problems
at development time and in production. It covers:

- [Build Types](#debug_build_types) — choosing the right CMake build type
- [The Logger System](#debug_logging) — the Logger system and convenience macros
- [Per-Module Debug Logging](#debug_promekidebug) — per-module debug logging with `PROMEKI_DEBUG`
- [Crash Handling](#debug_crashhandler) — automatic crash reports for fatal signals

---

## Build Types {#debug_build_types}

libpromeki defines three CMake build types. The build type controls
optimisation level, debug symbols, and whether per-module debug
logging (`promekiDebug()`) is compiled in.

| Build type | Optimisation | Debug symbols | `promekiDebug()` | `NDEBUG` | Typical use |
|------------|--------------|---------------|------------------|----------|-------------|
| **Debug** | `-O0` | Yes | Compiled in | Not set | Step-through debugging, sanitizers |
| **DevRelease** (default) | `-O3` | Yes (`-g`) | Compiled in | Set | Day-to-day development, CI, profiling |
| **Release** | `-O3` | No | Compiled out | Set | Distribution / final packaging |

### DevRelease — the recommended default {#debug_build_devrelease}

When no `CMAKE_BUILD_TYPE` is specified, the build defaults to
**DevRelease**. This gives you release-level performance (`-O3`)
plus two features that are invaluable during development and
production troubleshooting:

- **Debug symbols** (`-g`) — so crash reports, core dumps, and
  profilers show meaningful function names and line numbers.
- **`promekiDebug()` compiled in** — the per-module debug logging
  system is available and can be activated at runtime via the
  `PROMEKI_DEBUG` environment variable without recompiling.

`NDEBUG` is still set, so `assert()` macros are disabled and the
library runs at full speed.

```sh
# These are equivalent — DevRelease is the default:
cmake -B build
cmake -B build -DCMAKE_BUILD_TYPE=DevRelease
```

### Debug {#debug_build_debug}

The **Debug** build type disables optimisation entirely and enables
debug symbols. Use it when you need to step through code in a
debugger (GDB, LLDB) or run sanitizers (ASan, TSan, UBSan).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```

### Release {#debug_build_release}

The **Release** build type enables full optimisation with no debug
symbols and compiles out all `promekiDebug()` calls. Use it for
final distribution builds where binary size and performance are
paramount and debug logging overhead must be zero.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

---

## The Logger System {#debug_logging}

All logging in libpromeki goes through the `Logger` class. The
logger is thread-safe and asynchronous — log messages are enqueued
and written by a dedicated background thread so the calling thread
is never blocked on I/O.

### Convenience Macros {#debug_logging_macros}

In practice you will almost always use the convenience macros rather
than calling `Logger` methods directly:

| Macro | Level | When to use |
|-------|-------|-------------|
| `promekiDebug(fmt, ...)` | Debug | Per-module diagnostic output (see [Per-Module Debug Logging](#debug_promekidebug)) |
| `promekiInfo(fmt, ...)` | Info | Normal operational messages |
| `promekiWarn(fmt, ...)` | Warn | Recoverable problems worth investigating |
| `promekiErr(fmt, ...)` | Err | Errors that affect correctness |

All macros accept `printf`-style format strings:

```cpp
promekiInfo("Opened %s (%d x %d)", path.cstr(), w, h);
promekiWarn("Buffer underrun at %s", tc.toString().first().cstr());
promekiErr("Failed to bind socket: %s", err.desc().cstr());
```

Every log line automatically includes a timestamp, source file and
line number, log level, and the name of the calling thread.

### Configuring the Logger {#debug_logging_config}

The default logger writes to the console (stderr). You can also
direct output to a file and adjust the minimum log level:

```cpp
// Send log output to a file (in addition to the console)
Logger::defaultLogger().setLogFile("/tmp/myapp.log");

// Only show warnings and errors on the console
Logger::defaultLogger().setLogLevel(Logger::Warn);

// Disable console output entirely (file-only logging)
Logger::defaultLogger().setConsoleLoggingEnabled(false);
```

You can also install custom formatters for file and console output:

```cpp
Logger::defaultLogger().setFileFormatter([](const Logger::LogFormat &fmt) -> String {
        return String::sprintf("[%c] %s",
                Logger::levelToChar(fmt.entry->level),
                fmt.entry->msg.cstr());
});
```

### Flushing Log Output {#debug_logging_sync}

Because logging is asynchronous, the log queue may contain
unwritten messages when your program exits or when you need to
inspect a log file mid-run. Use `sync()` to block until the
queue is drained:

```cpp
promekiInfo("About to do something risky...");
promekiLogSync();  // block until all queued messages are written
```

---

## Per-Module Debug Logging (promekiDebug) {#debug_promekidebug}

The `promekiDebug()` macro provides fine-grained, per-module debug
logging that can be compiled in but only activated at runtime for
the modules you care about. This lets you instrument the library
heavily without any runtime cost until you need it.

### How It Works {#debug_promekidebug_how}

Each source file that wants to emit debug output places the
`PROMEKI_DEBUG` macro near the top, inside the `promeki`
namespace:

```cpp
#include <promeki/myclass.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MyClass)

MyClass::MyClass() {
        promekiDebug("MyClass(%p): created, size %d", (void *)this, _size);
}
```

The `PROMEKI_DEBUG(Name)` macro registers the module name with the
debug database. At startup, the library checks the
`PROMEKI_DEBUG` environment variable and enables logging for any
module whose name appears in the comma-separated list.

### Activating Debug Output {#debug_promekidebug_activate}

Set the `PROMEKI_DEBUG` environment variable to a comma-separated
list of module names:

```sh
# Enable debug output for ThreadPool and MediaIO:
PROMEKI_DEBUG=ThreadPool,MediaIO ./myapp

# Enable debug output for everything (if modules are named):
PROMEKI_DEBUG=ThreadPool,Thread,Logger,MediaIO,MediaIOTask_Rtp ./myapp
```

When enabled, `promekiDebug()` messages appear alongside normal
log output at the `Debug` level. When disabled, the module's
`promekiDebug()` calls skip the logging call entirely — just a
branch on a local `bool`.

### What Happens in Release Builds? {#debug_promekidebug_compiled_out}

In a **Release** build, `PROMEKI_DEBUG_ENABLE` is not defined, so
the `promekiDebug()` macro expands to nothing. The compiler
eliminates all debug logging calls entirely — zero overhead.

If you set the `PROMEKI_DEBUG` environment variable in a Release
build, the library will print a warning:

```
[W] PROMEKI_DEBUG is set but promekiDebug() messages are compiled out.
    Rebuild with -DCMAKE_BUILD_TYPE=DevRelease or Debug.
```

### Benchmarking with PROMEKI_BENCHMARK {#debug_promekidebug_benchmarks}

Two companion macros let you time sections of code, gated on the
same per-module enable flag:

```cpp
void MyClass::processFrame(const Frame &frame) {
        PROMEKI_BENCHMARK_BEGIN(processFrame)
        // ... expensive work ...
        PROMEKI_BENCHMARK_END(processFrame)
        // prints: "[MyClass] processFrame took 0.003217000 sec"
}
```

Like `promekiDebug()`, these are compiled out in Release builds
and only run when the module's debug flag is enabled.

---

## Crash Handling {#debug_crashhandler}

The `CrashHandler` class installs signal handlers for five
fatal POSIX signals: `SIGSEGV`, `SIGABRT`, `SIGBUS`,
`SIGFPE`, and `SIGILL`. When a crash occurs, it writes a
detailed report to stderr and to a log file, then re-raises the
signal so the OS can generate a core dump.

### What the Crash Report Contains {#debug_crash_report}

- Signal name and number (e.g. `SIGSEGV`)
- Fault address and signal code
- Process ID and ISO 8601 UTC timestamp
- Crashing thread's TID and name
- List of all threads with TIDs and names
- Demangled C++ stack trace
- OS info (kernel version, architecture)
- Memory usage and resource limits
- Memory map (`/proc/self/maps` on Linux)
- Environment snapshot (if enabled)
- Registered `MemSpace` allocation counters

The crash log is written to:

```
<tempdir>/promeki-crash-<appname>-<pid>.log
```

The directory can be overridden via `LibraryOptions::CrashLogDir`
or the `PROMEKI_OPT_CrashLogDir` environment variable.

### Automatic Installation via Application {#debug_crash_auto}

If your program uses the `Application` class, crash handlers
are installed automatically. No extra code is needed:

```cpp
#include <promeki/application.h>

int main(int argc, char **argv) {
        Application app(argc, argv);

        // Crash handlers are now active.
        // If the process crashes, a report is written automatically.

        return app.exec();
}
```

The `Application` constructor checks
`LibraryOptions::CrashHandler` (default `true`). To disable
it, set the option before constructing `Application` or via the
environment:

```cpp
// Programmatically
LibraryOptions::instance().set(LibraryOptions::CrashHandler, false);
Application app(argc, argv);

// Or via environment
// export PROMEKI_OPT_CrashHandler=false
```

### Manual Installation {#debug_crash_manual}

If your program does not use `Application` (e.g. a library
consumer that has its own main loop), you can install the crash
handler directly:

```cpp
#include <promeki/crashhandler.h>

int main(int argc, char **argv) {
        // Install crash handlers as early as possible
        CrashHandler::install();

        // ... your application code ...

        // Optional: remove handlers before exit
        CrashHandler::uninstall();
        return 0;
}
```

`install()` is safe to call multiple times — subsequent calls
refresh the snapshotted process state (hostname, thread list,
MemSpace entries, etc.) without stacking signal handlers.

### Refreshing the Snapshot {#debug_crash_refresh}

The crash handler snapshots process state at `install()` time so
it can write the report from inside a signal handler without
calling non-signal-safe functions. If your application registers
new `MemSpace` entries or changes the app name after startup,
call `Application::refreshCrashHandler()` (or just call
`CrashHandler::install()` again) to update the snapshot:

```cpp
Application app(argc, argv);
app.setAppName("my-tool");   // automatically refreshes

// If you register MemSpaces later:
MemSpace::registerData(...);
Application::refreshCrashHandler();
```

### Diagnostic Traces Without Crashing {#debug_crash_trace}

You can produce the same report a crash would generate — without
actually crashing — using `CrashHandler::writeTrace()`:

```cpp
// Something unexpected happened; capture a diagnostic snapshot
if(unexpectedState) {
        CrashHandler::writeTrace("unexpected state in pipeline stage 3");
}
```

Each call writes to a unique file:

```
<tempdir>/promeki-trace-<appname>-<pid>-<seqno>.log
```

This is useful for capturing the state of a long-running process
without stopping it.

### Enabling Core Dumps {#debug_crash_coredumps}

By default the crash handler does not modify the core dump
resource limit. To enable core dumps, set
`LibraryOptions::CoreDumps` to `true`:

```cpp
// Programmatically
LibraryOptions::instance().set(LibraryOptions::CoreDumps, true);

// Or via environment
// export PROMEKI_OPT_CoreDumps=true
```

When enabled, `install()` raises `RLIMIT_CORE` to the hard limit
so the kernel generates a core file on crash. Combined with the
`-g` debug symbols from **DevRelease** builds, you get a core dump
that GDB/LLDB can make full use of.

### CrashHandler Library Options {#debug_crash_options}

All crash-related options can be set via code or environment
variables:

| Option | Env variable | Type | Default | Description |
|--------|--------------|------|---------|-------------|
| `LibraryOptions::CrashHandler` | `PROMEKI_OPT_CrashHandler` | bool | true | Install crash signal handlers |
| `LibraryOptions::CoreDumps` | `PROMEKI_OPT_CoreDumps` | bool | false | Raise `RLIMIT_CORE` for core dumps |
| `LibraryOptions::CrashLogDir` | `PROMEKI_OPT_CrashLogDir` | String | (empty = temp dir) | Directory for crash/trace log files |
| `LibraryOptions::CaptureEnvironment` | `PROMEKI_OPT_CaptureEnvironment` | bool | true | Include env vars in crash reports |

---

## Quick Reference {#debug_summary}

### Environment Variables {#debug_summary_env}

| Variable | Purpose | Example |
|----------|---------|---------|
| `PROMEKI_DEBUG` | Enable per-module debug logging | `PROMEKI_DEBUG=ThreadPool,MediaIO` |
| `PROMEKI_OPT_CrashHandler` | Enable/disable crash handlers | `PROMEKI_OPT_CrashHandler=false` |
| `PROMEKI_OPT_CoreDumps` | Enable core dumps | `PROMEKI_OPT_CoreDumps=true` |
| `PROMEKI_OPT_CrashLogDir` | Override crash log directory | `PROMEKI_OPT_CrashLogDir=/var/log/myapp` |
| `PROMEKI_OPT_CaptureEnvironment` | Include env in crash reports | `PROMEKI_OPT_CaptureEnvironment=false` |

### Typical Debugging Workflow {#debug_summary_workflow}

```sh
# 1. Build with DevRelease (the default — just run cmake)
cmake -B build
cmake --build build

# 2. Run with debug logging for the subsystem you're investigating
PROMEKI_DEBUG=ThreadPool ./build/myapp

# 3. If a crash occurs, check the crash log
cat /tmp/promeki-crash-myapp-12345.log

# 4. For deeper investigation, enable core dumps
PROMEKI_OPT_CoreDumps=true ./build/myapp
# After crash:
gdb ./build/myapp core
```
