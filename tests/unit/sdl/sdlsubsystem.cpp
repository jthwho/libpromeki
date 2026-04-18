/**
 * @file      sdlsubsystem.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/sdl/sdlsubsystem.h>
#include <promeki/application.h>
#include <promeki/eventloop.h>
#include <promeki/elapsedtimer.h>

#include <chrono>
#include <thread>

using namespace promeki;

TEST_CASE("SdlSubsystem: worker-thread quit wakes Application::exec()") {
        // Construct an Application + SdlSubsystem on the stack.
        // SdlSubsystem does SDL_Init and installs its IoSource on the
        // main EventLoop; a worker-thread quit must wake the loop
        // through the EventLoop's own wake fd, independent of any
        // SDL events arriving.  Regressions the old wake-callback
        // bridge was trying to handle.
        char arg0[] = "sdlsubsystem-test";
        char *argv[] = { arg0 };
        Application  app(1, argv);
        SdlSubsystem sdl;

        REQUIRE(Application::mainEventLoop() != nullptr);

        std::thread worker([] {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                Application::quit(7);
        });

        ElapsedTimer t;
        t.start();
        const int rc = app.exec();
        worker.join();

        CHECK(rc == 7);
        CHECK(t.elapsed() < 1500);
}

TEST_CASE("SdlSubsystem: instance() returns the constructed subsystem") {
        char arg0[] = "sdlsubsystem-test";
        char *argv[] = { arg0 };
        Application  app(1, argv);

        CHECK(SdlSubsystem::instance() == nullptr);
        {
                SdlSubsystem sdl;
                CHECK(SdlSubsystem::instance() == &sdl);
        }
        CHECK(SdlSubsystem::instance() == nullptr);
}
