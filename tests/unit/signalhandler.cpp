/**
 * @file      signalhandler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/signalhandler.h>
#include <promeki/application.h>
#include <promeki/libraryoptions.h>
#include <promeki/eventloop.h>
#include <promeki/thread.h>
#include <promeki/elapsedtimer.h>
#include <promeki/platform.h>

#include <chrono>
#include <thread>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <csignal>
#include <unistd.h>
#endif

using namespace promeki;

// ============================================================================
// Basic lifecycle
// ============================================================================

TEST_CASE("SignalHandler: install and uninstall") {
        const bool wasInstalled = SignalHandler::isInstalled();
        SignalHandler::uninstall();
        CHECK_FALSE(SignalHandler::isInstalled());
        SignalHandler::install();
        CHECK(SignalHandler::isInstalled());
        SignalHandler::uninstall();
        CHECK_FALSE(SignalHandler::isInstalled());
        if (wasInstalled) SignalHandler::install();
}

TEST_CASE("SignalHandler: double install is a no-op") {
        const bool wasInstalled = SignalHandler::isInstalled();
        SignalHandler::uninstall();
        SignalHandler::install();
        SignalHandler::install();
        CHECK(SignalHandler::isInstalled());
        SignalHandler::uninstall();
        CHECK_FALSE(SignalHandler::isInstalled());
        if (wasInstalled) SignalHandler::install();
}

TEST_CASE("SignalHandler: uninstall when not installed is safe") {
        const bool wasInstalled = SignalHandler::isInstalled();
        SignalHandler::uninstall();
        CHECK_FALSE(SignalHandler::isInstalled());
        SignalHandler::uninstall();
        CHECK_FALSE(SignalHandler::isInstalled());
        if (wasInstalled) SignalHandler::install();
}

// ============================================================================
// Forwarding from Application
// ============================================================================

TEST_CASE("Application: signal handler forwarders") {
        const bool wasInstalled = SignalHandler::isInstalled();
        Application::uninstallSignalHandlers();
        CHECK_FALSE(Application::areSignalHandlersInstalled());
        Application::installSignalHandlers();
        CHECK(Application::areSignalHandlersInstalled());
        CHECK(SignalHandler::isInstalled());
        Application::uninstallSignalHandlers();
        CHECK_FALSE(Application::areSignalHandlersInstalled());
        if (wasInstalled) SignalHandler::install();
}

// ============================================================================
// End-to-end: delivering SIGINT should trigger Application::quit and wake
// the main EventLoop.  POSIX-only.
// ============================================================================

#if defined(PROMEKI_PLATFORM_POSIX)

TEST_CASE("SignalHandler: SIGINT translates to Application::quit and wakes EventLoop") {
        // Disable the double-tap escape hatch — we raise SIGINT exactly
        // once but want to be sure a repeat during this test cannot
        // force _Exit.  Restore the option at the end.
        const bool savedDoubleTap = LibraryOptions::instance().getAs<bool>(LibraryOptions::SignalDoubleTapExit);
        LibraryOptions::instance().set(LibraryOptions::SignalDoubleTapExit, false);

        // Construct an Application so Application::mainEventLoop() is
        // populated for this test.  The ctor also installs the signal
        // handler (and the crash handler), both of which the dtor
        // tears down.
        char        arg0[] = "signalhandler-test";
        char       *argv[] = {arg0};
        Application app(1, argv);

        REQUIRE(Application::areSignalHandlersInstalled());

        // Create an EventLoop on the main thread and populate
        // Thread::threadEventLoop's cross-thread cache by asking for
        // it from this (the main) thread.  Without this step,
        // Application::mainEventLoop() returns nullptr because
        // adoptCurrentThread() does not create a loop on its own.
        EventLoop mainLoop;
        REQUIRE(Application::mainThread() != nullptr);
        REQUIRE(Application::mainThread()->threadEventLoop() == &mainLoop);
        REQUIRE(Application::mainEventLoop() == &mainLoop);

        // Application::quit state should start clean.  Previous tests
        // in this binary (see tests/application.cpp) may have poked
        // it; construction of a new Application resets the shared
        // Data block.
        CHECK_FALSE(Application::shouldQuit());

        // Raise SIGINT against ourselves.  SignalHandler installs a
        // process-wide sigaction rather than masking signals at
        // individual threads, so the kernel may dispatch the signal
        // to any unblocked thread — including this one.  Either way,
        // asyncSignalHandler forwards the signal number through the
        // self-pipe, and the promeki-signals watcher drains the pipe
        // and runs deliverQuit() in normal thread context, which
        // calls Application::quit(128+SIGINT) and EventLoop::quit on
        // the main loop.
        REQUIRE(kill(getpid(), SIGINT) == 0);

        // Poll for the quit state with a bounded timeout.  The
        // watcher runs in its own thread so there is no guaranteed
        // ordering, but one second is comfortably longer than the
        // hand-off should ever take.
        ElapsedTimer timer;
        timer.start();
        while (!Application::shouldQuit() && timer.elapsed() < 1000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        CHECK(Application::shouldQuit());
        CHECK(Application::exitCode() == 128 + SIGINT);

        // The main event loop should also have been marked for quit:
        // calling exec() on it now should return immediately with the
        // forwarded exit code rather than blocking.
        timer.start();
        int rc = Application::mainEventLoop()->exec();
        CHECK(rc == 128 + SIGINT);
        CHECK(timer.elapsed() < 1000);

        // Restore the double-tap option for subsequent tests.
        LibraryOptions::instance().set(LibraryOptions::SignalDoubleTapExit, savedDoubleTap);

        // Stop the SignalHandler watcher thread *before* mainLoop
        // goes out of scope — the watcher caches the main thread's
        // EventLoop pointer via Thread::threadEventLoop and may still
        // wake it on a stray late-delivered signal.  ~Application
        // would do this for us, but it runs after mainLoop is already
        // gone (LIFO destruction of locals).  Manual uninstall here
        // ensures the watcher is joined while mainLoop is still
        // alive; ~Application's uninstall becomes a no-op afterwards.
        SignalHandler::uninstall();
}

// ============================================================================
// Custom signal subscription (SIGUSR1)
// ============================================================================

TEST_CASE("SignalHandler: subscribe rejects invalid arguments") {
        const bool wasInstalled = SignalHandler::isInstalled();
        SignalHandler::install();

        // Termination signals are reserved.
        CHECK(SignalHandler::subscribe(SIGINT, [](int) {}) == -1);
        CHECK(SignalHandler::subscribe(SIGTERM, [](int) {}) == -1);

        // Empty callback.
        CHECK(SignalHandler::subscribe(SIGUSR1, SignalHandler::Callback()) == -1);

        // Subscribe before install is refused.
        SignalHandler::uninstall();
        CHECK(SignalHandler::subscribe(SIGUSR1, [](int) {}) == -1);

        if (wasInstalled) SignalHandler::install();
}

TEST_CASE("SignalHandler: subscribe dispatches the callback on signal delivery") {
        const bool wasInstalled = SignalHandler::isInstalled();
        SignalHandler::install();

        std::atomic<int> count{0};
        std::atomic<int> lastSigno{0};
        int              h = SignalHandler::subscribe(SIGUSR1, [&](int signo) {
                lastSigno.store(signo, std::memory_order_relaxed);
                count.fetch_add(1, std::memory_order_relaxed);
        });
        REQUIRE(h >= 0);

        REQUIRE(::kill(::getpid(), SIGUSR1) == 0);

        ElapsedTimer timer;
        timer.start();
        while (count.load() < 1 && timer.elapsed() < 1000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(count.load() >= 1);
        CHECK(lastSigno.load() == SIGUSR1);

        SignalHandler::unsubscribe(h);
        if (!wasInstalled) SignalHandler::uninstall();
}

TEST_CASE("SignalHandler: unsubscribe stops further callbacks") {
        const bool wasInstalled = SignalHandler::isInstalled();
        SignalHandler::install();

        std::atomic<int> count{0};
        int h = SignalHandler::subscribe(SIGUSR1, [&](int) { count.fetch_add(1, std::memory_order_relaxed); });
        REQUIRE(h >= 0);

        REQUIRE(::kill(::getpid(), SIGUSR1) == 0);
        ElapsedTimer timer;
        timer.start();
        while (count.load() < 1 && timer.elapsed() < 1000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(count.load() >= 1);

        SignalHandler::unsubscribe(h);
        int before = count.load();

        // A second SIGUSR1 after unsubscribe must not invoke the
        // now-dead callback.  Give the watcher thread time to drain
        // the pipe — if it had dispatched, count would have grown.
        REQUIRE(::kill(::getpid(), SIGUSR1) == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(count.load() == before);

        if (!wasInstalled) SignalHandler::uninstall();
}

TEST_CASE("SignalHandler: multiple subscribers receive the same signal") {
        const bool wasInstalled = SignalHandler::isInstalled();
        SignalHandler::install();

        std::atomic<int> a{0};
        std::atomic<int> b{0};
        int ha = SignalHandler::subscribe(SIGUSR1, [&](int) { a.fetch_add(1, std::memory_order_relaxed); });
        int hb = SignalHandler::subscribe(SIGUSR1, [&](int) { b.fetch_add(1, std::memory_order_relaxed); });
        REQUIRE(ha >= 0);
        REQUIRE(hb >= 0);
        CHECK(ha != hb);

        REQUIRE(::kill(::getpid(), SIGUSR1) == 0);
        ElapsedTimer timer;
        timer.start();
        while ((a.load() < 1 || b.load() < 1) && timer.elapsed() < 1000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(a.load() >= 1);
        CHECK(b.load() >= 1);

        SignalHandler::unsubscribe(ha);
        SignalHandler::unsubscribe(hb);
        if (!wasInstalled) SignalHandler::uninstall();
}

#endif // PROMEKI_PLATFORM_POSIX
