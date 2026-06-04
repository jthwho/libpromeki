/**
 * @file      crashhandler.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Installs signal handlers for fatal crash signals.
 * @ingroup util
 *
 * When installed, catches SIGSEGV, SIGABRT, SIGBUS, SIGFPE, and SIGILL,
 * and writes a crash report to stderr and a log file before re-raising
 * the signal for core dump generation.
 *
 * The signal handler is designed to be as signal-safe as possible:
 * all header output uses raw @c write() calls, the crash log path is
 * pre-built at install time (no heap allocation until the stack
 * trace stage), and thread enumeration on Linux uses direct syscalls
 * against @c /proc/self/task.  The report is written to stderr and the
 * log file simultaneously, and the very first line printed to stderr is
 * the path of the log file being written — so the location is known even
 * if a later stage stalls.
 *
 * The crash report includes:
 * - Signal name and number
 * - PID and timestamp
 * - Crashing thread TID and name
 * - List of all threads with their TIDs and names
 * - Stack trace via @c backtrace, with C++ symbols demangled by
 *   @c abi::__cxa_demangle (falls back to raw frames via
 *   @c backtrace_symbols_fd if allocation fails)
 * - Path to the saved crash log file
 *
 * The stack-trace stage is not strictly async-signal-safe (it uses
 * @c backtrace_symbols and @c abi::__cxa_demangle, both of which
 * allocate), but it runs after all critical header and thread
 * information has already been written via signal-safe primitives.
 * A failure during demangling costs us a prettier trace — nothing
 * more.  Because that allocation can deadlock outright when the
 * crashing thread already holds the malloc arena lock (the classic
 * heap-corruption @c SIGABRT), the crash path arms a @c SIGALRM
 * watchdog around it: on timeout the handler unwinds back to the
 * trace site, dumps the raw frame addresses with signal-safe
 * primitives, and lets the rest of the report finish.  The timeout is
 * @ref LibraryOptions::CrashStackTraceTimeout seconds (default 10; 0
 * disables the watchdog).  The log file is also @c fsync()'d
 * immediately before the stack trace so the report so far is durable
 * even in the worst case.
 *
 * After the stack trace, a best-effort @c promekiLogSync() flushes
 * any buffered logger output.
 *
 * Controlled by @ref LibraryOptions::CrashHandler (default @c true)
 * and wired into @ref Application automatically.
 *
 * @par Example
 * @code
 * // Automatic via Application (default):
 * Application app(argc, argv);
 * // crash handlers are now active
 *
 * // Manual control:
 * CrashHandler::install();
 * // ...
 * CrashHandler::uninstall();
 * @endcode
 */
class CrashHandler {
        public:
                /**
                 * @brief Installs crash signal handlers.
                 *
                 * Registers handlers for SIGSEGV, SIGABRT, SIGBUS, SIGFPE,
                 * and SIGILL.  Pre-builds the crash log file path from the
                 * current @ref Application::appName and process ID.
                 *
                 * If @ref LibraryOptions::CoreDumps is true, also raises
                 * RLIMIT_CORE to the hard limit.
                 *
                 * Snapshots the following process state into static
                 * buffers so the signal handler can write a report
                 * without touching any non-signal-safe APIs:
                 * - Command line (@ref Application::arguments)
                 * - Hostname, working directory, uname info
                 * - Environment (if @ref LibraryOptions::CaptureEnvironment)
                 * - @ref LibraryOptions values
                 * - The set of registered @ref MemSpace entries
                 *   (metadata only — counter values are read live
                 *   at crash time via atomic loads)
                 *
                 * State that changes after install() — such as
                 * @ref MemSpace instances registered by a subsystem
                 * later in startup — will not appear in crash reports
                 * until the snapshot is refreshed.  Use
                 * @ref Application::refreshCrashHandler to re-snapshot.
                 *
                 * Safe to call multiple times — subsequent calls update
                 * the pre-built state but do not stack handlers.
                 *
                 * @see Application::refreshCrashHandler
                 * @see MemSpace::registerData
                 */
                static void install();

                /**
                 * @brief Restores default signal dispositions.
                 *
                 * Resets all five crash signals to SIG_DFL.
                 */
                static void uninstall();

                /**
                 * @brief Returns true if crash handlers are currently installed.
                 * @return true if install() has been called without a matching uninstall().
                 */
                static bool isInstalled();

                /**
                 * @brief Callback invoked from the crash signal handler.
                 *
                 * @warning Runs in async-signal context on the crashing thread.
                 * It MUST be async-signal-safe: only raw @c write / @c tcsetattr
                 * and the like — no heap allocation, no locks, no @ref String /
                 * @ref AnsiStream.  Keep it tiny.  @p userdata is the pointer
                 * passed to @ref addCleanupHandler and must outlive the
                 * registration.
                 */
                using CleanupCallback = void (*)(void *userdata);

                /**
                 * @brief Registers an async-signal-safe cleanup run before the crash report.
                 *
                 * Cleanup handlers fire at the very top of the crash signal
                 * handler — before any report output — so a subsystem can put
                 * the world back into a sane state first.  The motivating case
                 * is a TUI restoring the terminal (leaving the alternate
                 * screen, showing the cursor, disabling raw mode) so the crash
                 * report is actually visible and the shell is usable afterward.
                 *
                 * Handlers run in registration order.  They fire only on the
                 * real crash path (a re-raised fatal signal); @ref writeTrace
                 * deliberately does not run them, so taking a diagnostic
                 * snapshot never tears down a live TUI's terminal state.
                 *
                 * @param cb       The async-signal-safe callback (see
                 *                 @ref CleanupCallback).  Must be non-null.
                 * @param userdata Opaque pointer passed to @p cb; must outlive
                 *                 the registration.
                 * @return A handle @c >= 0 on success, or @c -1 if @p cb is null
                 *         or the fixed handler table is full.
                 */
                static int addCleanupHandler(CleanupCallback cb, void *userdata);

                /**
                 * @brief Removes a cleanup handler registered via @ref addCleanupHandler.
                 *
                 * Must be called before @p userdata is destroyed.  Unknown or
                 * already-removed handles are ignored.
                 *
                 * @param handle The handle returned by @ref addCleanupHandler.
                 */
                static void removeCleanupHandler(int handle);

                /**
                 * @brief Writes a diagnostic trace report to a unique file.
                 *
                 * Produces the same report a crash would produce — process
                 * identity, build info, OS info, memory stats, thread list,
                 * stack trace, memory map — but without re-raising a signal.
                 * Intended for programmatic diagnostic snapshots from inside
                 * the application (e.g. when detecting a soft failure or an
                 * inconsistent state).
                 *
                 * The trace is written to:
                 * @verbatim
                 * <crashLogDir>/promeki-trace-<appname>-<pid>-<seqno>.log
                 * @endverbatim
                 * where @c seqno is an internal atomic counter that makes
                 * each call's filename unique within the process.
                 *
                 * May be called before @ref install(); if so, some of the
                 * snapshotted fields (host, working directory, uname data)
                 * will be missing from the report but the trace itself is
                 * still written.  Calling @ref install() once on startup
                 * ensures a complete report.
                 *
                 * This function is @em not async-signal-safe — it allocates
                 * via String and calls @c abi::__cxa_demangle in the stack
                 * trace path.  Do not call it from a signal handler.
                 *
                 * @param reason Optional human-readable reason for the trace.
                 *        If non-null, appears in the trace header.
                 */
                static void writeTrace(const char *reason = nullptr);

                /**
                 * @brief Returns whether writeTrace() echoes to stderr.
                 * @return true if console output is enabled (the default).
                 */
                static bool consoleTraceEnabled();

                /**
                 * @brief Enables or disables stderr output from writeTrace().
                 *
                 * When disabled, writeTrace() still writes the trace to a log
                 * file but suppresses the stderr echo.  Has no effect on the
                 * signal handler crash path, which always writes to stderr.
                 *
                 * @param enabled true to echo traces to stderr, false to suppress.
                 */
                static void setConsoleTraceEnabled(bool enabled);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
