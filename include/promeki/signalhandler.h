/**
 * @file      signalhandler.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Installs handlers for termination signals (Ctrl-C, kill, etc.).
 * @ingroup util
 *
 * When installed, catches SIGINT, SIGTERM, SIGHUP, and SIGQUIT on POSIX
 * systems (and CTRL_C_EVENT, CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT on
 * Windows) and turns them into a clean application shutdown:
 *
 *  1. @ref Application::quit is called with exit code @c 128+signo so
 *     any pump loop that polls @ref Application::shouldQuit observes
 *     the quit request.
 *  2. A @c QuitItem is posted to @ref Application::mainEventLoop so
 *     any thread blocked in @ref EventLoop::exec wakes immediately
 *     with the same exit code.
 *
 * On POSIX the handler uses the classic self-pipe trick: a
 * @c sigaction hook forwards the incoming signal number as a single
 * byte through a pipe, and a dedicated thread (named
 * @c promeki-signals) reads the pipe and invokes the rest of the
 * quit logic in a fully normal thread context.  Only the one byte
 * @c write goes through the async-signal handler, so no locks are
 * taken and no allocation happens inside the signal path.  Because
 * the delivery mechanism is a process-wide @c sigaction rather than
 * @c sigwait, the handler works correctly regardless of which thread
 * the kernel picks to receive the signal or the order in which the
 * application's threads are spawned.
 *
 * @par Double-signal force-exit
 * By default, a second delivery of any caught termination signal
 * calls @c std::_Exit(128+signo) so an unresponsive event loop can
 * still be killed with a second Ctrl-C.  Set
 * @ref LibraryOptions::SignalDoubleTapExit to @c false to disable
 * this behaviour — useful for command-line tools that want a single
 * Ctrl-C to abort immediately, or for applications that want to
 * handle multiple signals themselves.
 *
 * Controlled by @ref LibraryOptions::TerminationSignalHandler
 * (default @c true) and wired into @ref Application automatically.
 *
 * @par Example
 * @code
 * // Automatic via Application (default):
 * int main(int argc, char **argv) {
 *         Application app(argc, argv);
 *         // SIGINT/SIGTERM/... now translate to Application::quit()
 *         return Application::mainEventLoop()->exec();
 * }
 *
 * // Manual control:
 * SignalHandler::install();
 * // ...
 * SignalHandler::uninstall();
 * @endcode
 *
 * @see Application::quit
 * @see CrashHandler for fatal signal (SIGSEGV/SIGABRT/...) handling.
 */
class SignalHandler {
        public:
                /**
                 * @brief Installs termination signal handlers.
                 *
                 * On POSIX, creates a self-pipe, registers an async
                 * @c sigaction handler for SIGINT/SIGTERM/SIGHUP/SIGQUIT
                 * that writes the signal number to the pipe, and
                 * starts the dedicated reader thread that runs the
                 * rest of the quit logic in normal context.
                 *
                 * On Windows, registers a @c SetConsoleCtrlHandler
                 * callback.
                 *
                 * Safe to call multiple times — subsequent calls are
                 * no-ops while the handler is already installed.
                 */
                static void install();

                /**
                 * @brief Uninstalls the termination signal handlers.
                 *
                 * On POSIX, restores the previous @c sigaction
                 * dispositions, pokes the reader thread with a
                 * sentinel byte, joins it, and closes the self-pipe.
                 * On Windows, removes the @c SetConsoleCtrlHandler
                 * callback.
                 *
                 * Safe to call when the handler is not installed.
                 *
                 * @warning Must not be called from the reader thread
                 *          itself.
                 */
                static void uninstall();

                /**
                 * @brief Returns true if the signal handler is currently installed.
                 * @return true if @ref install has been called without
                 *         a matching @ref uninstall.
                 */
                static bool isInstalled();

                /** @brief Callback type for @ref subscribe. */
                using Callback = std::function<void(int signo)>;

                /**
                 * @brief Subscribes a callback for a non-termination signal.
                 *
                 * Lets other subsystems (e.g. TuiSubsystem for
                 * SIGWINCH) route signals through the single
                 * @ref SignalHandler self-pipe watcher thread instead
                 * of installing their own @c sigaction hooks.  The
                 * callback runs on the watcher thread in normal
                 * context — it may take locks, allocate, and post
                 * onto event loops.
                 *
                 * On first subscription for @p signo the @c sigaction
                 * hook is installed; the hook stays registered until
                 * @ref uninstall.  Multiple subscribers for the same
                 * signal are dispatched in subscription order.
                 *
                 * @param signo  The signal number (must not be one of
                 *               the reserved termination signals —
                 *               @c SIGINT / @c SIGTERM / @c SIGHUP /
                 *               @c SIGQUIT).
                 * @param cb     Callback invoked with @p signo each
                 *               time the signal arrives.
                 * @return       Handle @c >= 0 on success; @c -1 if
                 *               @p signo is reserved, @p cb is empty,
                 *               the handler is not installed, or the
                 *               @c sigaction call failed.
                 *
                 * @note Subscribers installed before @ref install are
                 *       rejected — install the handler first.
                 *       @ref Application constructs with the handler
                 *       already installed, which is the expected
                 *       subscription entry point.
                 */
                static int subscribe(int signo, Callback cb);

                /**
                 * @brief Removes a subscription previously installed via @ref subscribe.
                 *
                 * Idempotent: unknown handles are silently ignored.
                 * The underlying @c sigaction hook stays installed for
                 * the life of this @ref SignalHandler install — a
                 * stray signal arriving for a signal with no live
                 * subscribers is drained and dropped.
                 *
                 * @param handle The handle returned by @ref subscribe.
                 */
                static void unsubscribe(int handle);
};

PROMEKI_NAMESPACE_END
