/**
 * @file      tuisubsystem.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/application.h>
#include <promeki/eventloop.h>
#include <promeki/elapsedtimer.h>
#include <promeki/thread.h>

#include <chrono>
#include <thread>

using namespace promeki;

// Regression for the eventloop_app refactor: a worker-thread quit()
// must wake the main EventLoop promptly.  Not a TuiSubsystem test per
// se (constructing a real TuiSubsystem would grab stdin / switch the
// terminal to raw mode, which is hostile to unit tests) — but this
// codifies the behaviour the refactor was supposed to guarantee for
// any app including a TuiSubsystem: the EventLoop wake fd unblocks
// the main-thread wait without a UI-specific bridge.
TEST_CASE("Main EventLoop: worker-thread quit wakes exec() promptly") {
        char        arg0[] = "tuisubsystem-test";
        char       *argv[] = {arg0};
        Application app(1, argv);
        EventLoop   loop;

        std::thread worker([&] {
                Thread::sleepMs(30);
                loop.quit(42);
        });

        ElapsedTimer t;
        t.start();
        const int rc = loop.exec();
        worker.join();

        CHECK(rc == 42);
        CHECK(t.elapsed() < 1000);
}
