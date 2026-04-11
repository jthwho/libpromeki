/**
 * @file      crashhandler.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

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
 * against @c /proc/self/task.
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
 * more.
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
                 * Safe to call multiple times — subsequent calls update the
                 * pre-built state but do not stack handlers.
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
};

PROMEKI_NAMESPACE_END
