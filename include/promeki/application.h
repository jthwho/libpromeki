/**
 * @file      application.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>

#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/eventloop.h>
#include <promeki/error.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/uniqueptr.h>
#include <promeki/uuid.h>

PROMEKI_NAMESPACE_BEGIN
class Thread;
class IODevice;
class DebugServer;
class SocketAddress;
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
 *
 * @par Thread Safety
 * The static accessors are intended to be set once at startup
 * (typically in @c main before worker threads spawn) and read
 * thereafter from any thread.  Mutating setters
 * (@c setAppName, @c setAppUuid, @c setAppArguments) without
 * external synchronization is unsafe once worker threads exist.
 * @c mainThread / @c mainEventLoop are safe to call from any
 * thread.
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
                 *
                 * If a @ref QuitRequestHandler has been installed via
                 * @ref setQuitRequestHandler, it is invoked first.  When
                 * the handler returns @c true, it has accepted
                 * responsibility for eventually driving the actual quit
                 * itself (typically after some async shutdown completes)
                 * and this call neither sets @ref shouldQuit nor wakes
                 * the main @ref EventLoop.  When the handler returns
                 * @c false, or no handler is installed, the call
                 * proceeds with the default quit behaviour: the
                 * exit-code and @ref shouldQuit flag are set and the
                 * main @ref EventLoop is poked so any blocked
                 * @ref EventLoop::exec unwinds.
                 *
                 * @param exitCode The exit code (retrievable via @ref exitCode).
                 */
                static void quit(int exitCode = 0);

                /**
                 * @brief Handler signature for @ref setQuitRequestHandler.
                 *
                 * Receives the requested exit code.  Return @c true to
                 * intercept the quit (the handler will call
                 * @ref quit again, or some equivalent, once async
                 * shutdown completes) and @c false to fall through to
                 * the default quit behaviour.
                 */
                using QuitRequestHandler = std::function<bool(int exitCode)>;

                /**
                 * @brief Installs a handler invoked on every @ref quit call.
                 *
                 * Use this to defer the actual event-loop teardown until
                 * an async shutdown has completed — for example, running
                 * @ref MediaPipeline::close with @c block=false and
                 * re-invoking @ref quit from the pipeline's
                 * @c closedSignal.  Pass an empty function to clear the
                 * handler.
                 *
                 * Only one handler may be installed at a time; a second
                 * call replaces the first.  The handler runs on the
                 * calling thread of the @ref quit invocation (typically
                 * the signal-delivery thread), so it must be
                 * thread-safe — the canonical pattern is to post work
                 * to the main @ref EventLoop from inside the handler
                 * rather than doing it inline.
                 *
                 * @param handler The handler (or an empty function to clear).
                 */
                static void setQuitRequestHandler(QuitRequestHandler handler);

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

                /**
                 * @brief Environment variable consulted at construction time.
                 *
                 * If set to a non-empty spec accepted by
                 * @ref DebugServer::parseSpec (e.g. @c ":8085",
                 * @c "127.0.0.1:8085", @c "0.0.0.0:8085"), the
                 * @ref Application constructor calls
                 * @ref startDebugServer with the parsed address.  Parse
                 * or bind failures are logged at @c warn level and are
                 * never fatal.
                 */
                static const char *DebugServerEnv;

                /**
                 * @brief Starts the embedded debug HTTP server on @p address.
                 *
                 * Constructs (if needed) the @ref DebugServer, calls
                 * @ref DebugServer::installDefaultModules so the
                 * standard URL space (@c /promeki/debug ...) is mounted,
                 * and binds to @p address.  Calling this while the
                 * debug server is already listening fails with
                 * @ref Error::AlreadyOpen — call @ref stopDebugServer
                 * first to rebind.
                 *
                 * The @ref DebugServer's @ref EventLoop affinity is
                 * inherited from the calling thread at the moment of
                 * the first @c startDebugServer call (typically the
                 * main thread, when invoked from within or just after
                 * @ref Application construction).
                 */
                static Error startDebugServer(const SocketAddress &address);

                /**
                 * @brief Convenience: starts the debug server on @p port at loopback.
                 *
                 * Equivalent to @ref startDebugServer with
                 * @ref DebugServer::DefaultBindHost.  Pass an explicit
                 * @ref SocketAddress to bind to other interfaces.
                 */
                static Error startDebugServer(uint16_t port);

                /**
                 * @brief Stops the debug server, if running.
                 *
                 * Tears down the @ref DebugServer and closes any
                 * connected clients.  No-op if the debug server has
                 * never been started.  The @ref DebugServer object
                 * itself is destroyed — a subsequent
                 * @ref startDebugServer constructs a fresh one.
                 */
                static void stopDebugServer();

                /**
                 * @brief Returns the active @ref DebugServer, or @c nullptr.
                 *
                 * Useful for tests and for applications that want to
                 * mount additional routes alongside the default debug
                 * modules.
                 */
                static DebugServer *debugServer();

                /**
                 * @brief Runs the main event loop until quit() is called.
                 *
                 * Thin wrapper around @c mainEventLoop()->exec().  The
                 * Application's own @ref EventLoop is constructed with
                 * the Application itself; any @c Subsystem objects
                 * (SDL, TUI, ...) installed before this call have
                 * registered their I/O sources on that loop, so
                 * @c exec() blocks until one of them or a posted
                 * @ref quit() causes the loop to exit.
                 *
                 * @return The exit code passed to @ref quit (or the
                 *         exit code passed to @c EventLoop::quit()
                 *         directly).
                 */
                int exec();

        private:
                struct Data {
                                StringList             arguments;
                                UUID                   appUUID;
                                String                 appName;
                                Thread                *mainThread = nullptr;
                                // Atomic so the signal-watcher thread
                                // can latch a quit request while the
                                // main thread is polling
                                // @ref Application::shouldQuit.
                                Atomic<int>            exitCode{0};
                                Atomic<bool>           shouldQuit{false};
                                QuitRequestHandler     quitHandler;
                                UniquePtr<DebugServer> debugServer;
                };
                static Data &data();

                static void maybeStartDebugServerFromEnv();

                // The main-thread EventLoop.  Constructed as a member
                // of Application so subsystems installed on the stack
                // immediately after `Application app(argc, argv);`
                // have a ready-made loop to register their I/O
                // sources on.  Declared after the static data accessor
                // so it participates in member init order.
                EventLoop _eventLoop;
};

PROMEKI_NAMESPACE_END
