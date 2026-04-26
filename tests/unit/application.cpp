/**
 * @file      application.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/application.h>
#include <promeki/eventloop.h>
#include <promeki/iodevice.h>
#include <promeki/thread.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace promeki;

TEST_CASE("Application: static accessors work without an instance") {
        Application::setAppName("no-instance");
        CHECK(Application::appName() == "no-instance");
        Application::setAppName("");
}

TEST_CASE("Application: arguments captured from argc/argv") {
        char arg0[] = "myapp";
        char arg1[] = "--verbose";
        char arg2[] = "input.wav";
        char *argv[] = { arg0, arg1, arg2 };
        Application app(3, argv);
        REQUIRE(Application::arguments().size() == 3);
        CHECK(Application::arguments()[0] == "myapp");
        CHECK(Application::arguments()[1] == "--verbose");
        CHECK(Application::arguments()[2] == "input.wav");
}

TEST_CASE("Application: arguments empty after destruction") {
        {
                char arg0[] = "test";
                char *argv[] = { arg0 };
                Application app(1, argv);
                CHECK_FALSE(Application::arguments().isEmpty());
        }
        CHECK(Application::arguments().isEmpty());
}

TEST_CASE("Application: setAppName and appName roundtrip") {
        Application::setAppName("test-application");
        CHECK(Application::appName() == "test-application");
        Application::setAppName("");
}

TEST_CASE("Application: setAppUUID and appUUID roundtrip") {
        UUID id("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
        Application::setAppUUID(id);
        CHECK(Application::appUUID() == id);
        CHECK(Application::appUUID().isValid());
        Application::setAppUUID(UUID());
}

TEST_CASE("Application: stdinDevice returns readable device") {
        IODevice *dev = Application::stdinDevice();
        REQUIRE(dev != nullptr);
        CHECK(dev->isOpen());
        CHECK(dev->isReadable());
}

TEST_CASE("Application: stdoutDevice returns writable device") {
        IODevice *dev = Application::stdoutDevice();
        REQUIRE(dev != nullptr);
        CHECK(dev->isOpen());
        CHECK(dev->isWritable());
}

TEST_CASE("Application: stderrDevice returns writable device") {
        IODevice *dev = Application::stderrDevice();
        REQUIRE(dev != nullptr);
        CHECK(dev->isOpen());
        CHECK(dev->isWritable());
}

TEST_CASE("Application: not copyable or movable") {
        CHECK_FALSE(std::is_copy_constructible_v<Application>);
        CHECK_FALSE(std::is_move_constructible_v<Application>);
        CHECK_FALSE(std::is_copy_assignable_v<Application>);
        CHECK_FALSE(std::is_move_assignable_v<Application>);
}

TEST_CASE("Application: quit and shouldQuit") {
        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);

        CHECK_FALSE(Application::shouldQuit());
        CHECK(Application::exitCode() == 0);

        Application::quit();
        CHECK(Application::shouldQuit());
        CHECK(Application::exitCode() == 0);
}

TEST_CASE("Application: quit with non-zero exit code") {
        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);

        CHECK_FALSE(Application::shouldQuit());
        Application::quit(42);
        CHECK(Application::shouldQuit());
        CHECK(Application::exitCode() == 42);
}

TEST_CASE("Application: exitCode default is zero") {
        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);

        CHECK(Application::exitCode() == 0);
        CHECK_FALSE(Application::shouldQuit());
}

TEST_CASE("Application: quit-request handler intercepts and suppresses") {
        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);

        int observedCode = -1;
        Application::setQuitRequestHandler([&observedCode](int code) -> bool {
                observedCode = code;
                return true; // suppress default quit
        });

        Application::quit(17);
        CHECK(observedCode == 17);
        // Handler returned true — shouldQuit must NOT have been set,
        // and exitCode must NOT have been overwritten.
        CHECK_FALSE(Application::shouldQuit());
        CHECK(Application::exitCode() == 0);

        Application::setQuitRequestHandler(nullptr);
}

TEST_CASE("Application: quit-request handler falls through on false") {
        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);

        int observedCode = -1;
        Application::setQuitRequestHandler([&observedCode](int code) -> bool {
                observedCode = code;
                return false; // let default quit run
        });

        Application::quit(9);
        CHECK(observedCode == 9);
        CHECK(Application::shouldQuit());
        CHECK(Application::exitCode() == 9);

        Application::setQuitRequestHandler(nullptr);
}

TEST_CASE("Application: mainEventLoop is reachable from a non-main thread before any same-thread query") {
        // Regression: SIGINT delivery happens on the signal watcher
        // thread.  If `Application::mainEventLoop` is queried from
        // there before any same-thread caller has primed the cache,
        // the adopted Thread must still surface the main EventLoop
        // so that `Application::quit` can wake the loop.
        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);

        std::atomic<EventLoop *> seenLoop{nullptr};
        std::thread other([&]() {
                seenLoop.store(Application::mainEventLoop());
        });
        other.join();
        CHECK(seenLoop.load() != nullptr);
        CHECK(seenLoop.load() == Application::mainEventLoop());
}

TEST_CASE("Application: quit from another thread wakes the main EventLoop") {
        // Regression: matches the SIGINT path — Application::quit
        // invoked on a non-main thread must unblock
        // mainEventLoop()->exec() in time for `Application::exec` to
        // return.
        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);

        std::thread quitter([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                Application::quit(7);
        });

        const int code = app.exec();
        quitter.join();
        CHECK(code == 7);
        CHECK(Application::shouldQuit());
        CHECK(Application::exitCode() == 7);
}

TEST_CASE("Application: quit-request handler cleared with nullptr") {
        char arg0[] = "test";
        char *argv[] = { arg0 };
        Application app(1, argv);

        int callCount = 0;
        Application::setQuitRequestHandler([&callCount](int) -> bool {
                ++callCount;
                return true;
        });
        Application::quit(1);
        CHECK(callCount == 1);
        CHECK_FALSE(Application::shouldQuit());

        // Clear and try again — handler should not run, default path
        // engages.
        Application::setQuitRequestHandler(nullptr);
        Application::quit(2);
        CHECK(callCount == 1); // unchanged
        CHECK(Application::shouldQuit());
        CHECK(Application::exitCode() == 2);
}
