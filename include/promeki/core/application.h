/**
 * @file      core/application.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/stringlist.h>
#include <promeki/core/uuid.h>

PROMEKI_NAMESPACE_BEGIN
class Thread;
class EventLoop;
class IODevice;
PROMEKI_NAMESPACE_END

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Application-wide state for the promeki library.
 * @ingroup core_events
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

        private:
                struct Data {
                        StringList      arguments;
                        UUID            appUUID;
                        String          appName;
                        Thread          *mainThread = nullptr;
                };
                static Data &data();
};

PROMEKI_NAMESPACE_END
