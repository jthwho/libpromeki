/**
 * @file      application.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/uuid.h>

PROMEKI_NAMESPACE_BEGIN
class Thread;
class EventLoop;
class IODevice;
PROMEKI_NAMESPACE_END

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Application-wide state for the promeki library.
 * @ingroup events
 *
 * Provides global application identity (name, UUID, command-line arguments)
 * used by other parts of the library such as UUID v3/v5 generation.
 *
 * All accessors are static and work whether or not an instance has been
 * constructed.  Optionally, an Application object can be created on the
 * stack in main() to capture argc/argv and register as the current
 * instance (similar to QApplication):
 *
 * @code
 * int main(int argc, char **argv) {
 *         Application app(argc, argv);
 *         Application::setAppName("myapp");
 *         // ...
 * }
 * @endcode
 *
 * If no instance is created, the static accessors operate on
 * default-constructed internal state.
 */
class Application {
        public:
                /**
                 * @brief Constructs and registers an Application instance.
                 * @param argc Argument count from main().
                 * @param argv Argument vector from main().
                 *
                 * Stores the command-line arguments and sets this as the
                 * current Application.  Only one instance should exist at
                 * a time.
                 */
                Application(int argc, char **argv);

                /** @brief Destroys the Application and clears the current instance. */
                ~Application();

                Application(const Application &) = delete;
                Application(Application &&) = delete;
                Application &operator=(const Application &) = delete;
                Application &operator=(Application &&) = delete;

                /**
                 * @brief Returns the command-line arguments.
                 * @return The arguments passed to the constructor, or an empty
                 *         list if no instance was created.
                 */
                static const StringList &arguments();

                /**
                 * @brief Returns the application UUID used as a namespace for UUID v3/v5 generation.
                 * @return The application namespace UUID.
                 */
                static const UUID &appUUID();

                /**
                 * @brief Sets the application UUID used as a namespace for UUID v3/v5 generation.
                 * @param uuid The namespace UUID to use.
                 */
                static void setAppUUID(const UUID &uuid);

                /**
                 * @brief Returns the application name used for UUID v3/v5 generation.
                 * @return The application name string.
                 */
                static const String &appName();

                /**
                 * @brief Sets the application name used for UUID v3/v5 generation.
                 * @param name The application name to use.
                 */
                static void setAppName(const String &name);

                /**
                 * @brief Returns the Thread object for the main (application) thread.
                 * @return The main thread, or nullptr if no Application was constructed.
                 */
                static Thread *mainThread();

                /**
                 * @brief Returns the EventLoop for the main thread.
                 * @return The main thread's EventLoop, or nullptr if the user
                 *         hasn't created one yet.
                 */
                static EventLoop *mainEventLoop();

                /**
                 * @brief Returns an IODevice wrapping C stdin.
                 *
                 * The returned device is a lazy-initialized static local
                 * FileIODevice opened for ReadOnly.  It does not own the
                 * FILE pointer.
                 *
                 * @return A non-owning IODevice for stdin.
                 */
                static IODevice *stdinDevice();

                /**
                 * @brief Returns an IODevice wrapping C stdout.
                 *
                 * The returned device is a lazy-initialized static local
                 * FileIODevice opened for WriteOnly.  It does not own the
                 * FILE pointer.
                 *
                 * @return A non-owning IODevice for stdout.
                 */
                static IODevice *stdoutDevice();

                /**
                 * @brief Returns an IODevice wrapping C stderr.
                 *
                 * The returned device is a lazy-initialized static local
                 * FileIODevice opened for WriteOnly.  It does not own the
                 * FILE pointer.
                 *
                 * @return A non-owning IODevice for stderr.
                 */
                static IODevice *stderrDevice();

                /**
                 * @brief Installs termination signal handlers.
                 *
                 * Convenience forwarder to @ref SignalHandler::install.
                 * Normally you do not need to call this — the
                 * @ref Application constructor installs the handler
                 * automatically when
                 * @ref LibraryOptions::TerminationSignalHandler is
                 * @c true (the default).
                 *
                 * Must be called from the main thread before any
                 * application threads are spawned so the signal mask
                 * is inherited by children.
                 *
                 * @see SignalHandler::install
                 */
                static void installSignalHandlers();

                /**
                 * @brief Uninstalls the termination signal handlers.
                 *
                 * Convenience forwarder to @ref SignalHandler::uninstall.
                 * The @ref Application destructor calls this
                 * automatically.
                 *
                 * @see SignalHandler::uninstall
                 */
                static void uninstallSignalHandlers();

                /**
                 * @brief Returns true if termination signal handlers are installed.
                 * @return true if @ref installSignalHandlers has been
                 *         called without a matching
                 *         @ref uninstallSignalHandlers.
                 *
                 * @see SignalHandler::isInstalled
                 */
                static bool areSignalHandlersInstalled();

                /**
                 * @brief Installs the crash signal handlers.
                 *
                 * Convenience forwarder to @ref CrashHandler::install.
                 * Normally you do not need to call this — the
                 * @ref Application constructor installs the handler
                 * automatically when @ref LibraryOptions::CrashHandler
                 * is @c true (the default).
                 *
                 * @see CrashHandler::install
                 */
                static void installCrashHandler();

                /**
                 * @brief Uninstalls the crash signal handlers.
                 *
                 * Convenience forwarder to @ref CrashHandler::uninstall.
                 * The @ref Application destructor calls this automatically.
                 *
                 * @see CrashHandler::uninstall
                 */
                static void uninstallCrashHandler();

                /**
                 * @brief Returns true if the crash handler is installed.
                 * @return true if @ref installCrashHandler has been
                 *         called without a matching
                 *         @ref uninstallCrashHandler.
                 *
                 * @see CrashHandler::isInstalled
                 */
                static bool isCrashHandlerInstalled();

                /**
                 * @brief Re-snapshots the crash handler's install-time state.
                 *
                 * @ref CrashHandler takes a snapshot of various process
                 * state at install time (command line, environment,
                 * library options, registered MemSpaces, ...) so the
                 * signal handler can write a report without touching
                 * any non-signal-safe APIs.  State that changes after
                 * install() — such as newly registered MemSpaces or
                 * updated LibraryOptions — will not appear in crash
                 * reports until the snapshot is refreshed.
                 *
                 * This method refreshes the snapshot if (and only if)
                 * the crash handler is currently installed.  If the
                 * crash handler was never installed or has been
                 * explicitly uninstalled, this is a no-op — it will
                 * not install the handler.
                 *
                 * Typical use: call once during application startup
                 * after all subsystems have registered their custom
                 * MemSpaces.
                 *
                 * @code
                 * int main(int argc, char **argv) {
                 *         Application app(argc, argv);
                 *         initMyGpuBackend();      // registers a "GPU" MemSpace
                 *         initMySharedMemPool();   // registers another MemSpace
                 *         Application::refreshCrashHandler();
                 *         // ... run event loop ...
                 * }
                 * @endcode
                 *
                 * @see CrashHandler::install, MemSpace::registerData
                 */
                static void refreshCrashHandler();

                /**
                 * @brief Writes a diagnostic trace report to a unique file.
                 *
                 * Convenience forwarder to @ref CrashHandler::writeTrace.
                 * Useful for snapshotting application state (build info,
                 * OS info, memory usage, thread list, stack trace) from
                 * code paths that detect a soft failure or an unexpected
                 * condition worth investigating later.
                 *
                 * @param reason Optional human-readable reason for the trace.
                 *
                 * @see CrashHandler::writeTrace
                 */
                static void writeTrace(const char *reason = nullptr);

                /**
                 * @brief Requests the application to quit.
                 * @param exitCode The exit code (retrievable via exitCode()).
                 */
                static void quit(int exitCode = 0);

                /**
                 * @brief Returns true if quit() has been called.
                 * @return true if the application should exit.
                 */
                static bool shouldQuit();

                /**
                 * @brief Returns the exit code set by quit().
                 * @return The exit code, or 0 if quit() was never called.
                 */
                static int exitCode();

        private:
                struct Data {
                        StringList      arguments;
                        UUID            appUUID;
                        String          appName;
                        Thread          *mainThread = nullptr;
                        int             exitCode = 0;
                        bool            shouldQuit = false;
                };
                static Data &data();
};

PROMEKI_NAMESPACE_END
