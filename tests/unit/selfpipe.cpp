/**
 * @file      selfpipe.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/selfpipe.h>
#include <promeki/platform.h>

#include <atomic>
#include <chrono>
#include <thread>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <poll.h>
#include <unistd.h>
#endif

using namespace promeki;

#if defined(PROMEKI_PLATFORM_POSIX)

TEST_CASE("SelfPipe: opens valid pipe with distinct fds") {
        SelfPipe pipe;
        REQUIRE(pipe.isValid());
        CHECK(pipe.readFd() >= 0);
        CHECK(pipe.writeFd() >= 0);
        CHECK(pipe.readFd() != pipe.writeFd());
}

TEST_CASE("SelfPipe: wake makes read end readable") {
        SelfPipe pipe;
        REQUIRE(pipe.isValid());

        // Before wake: poll should timeout.
        pollfd pfd;
        pfd.fd = pipe.readFd();
        pfd.events = POLLIN;
        pfd.revents = 0;
        CHECK(::poll(&pfd, 1, 0) == 0);

        pipe.wake();

        pfd.revents = 0;
        int rc = ::poll(&pfd, 1, 200);
        CHECK(rc == 1);
        CHECK((pfd.revents & POLLIN) != 0);
}

TEST_CASE("SelfPipe: drain empties the pipe") {
        SelfPipe pipe;
        REQUIRE(pipe.isValid());

        // Write several bytes then drain — subsequent poll must
        // report the pipe as not readable.
        pipe.wake();
        pipe.wake();
        pipe.wake();

        pipe.drain();

        pollfd pfd;
        pfd.fd = pipe.readFd();
        pfd.events = POLLIN;
        pfd.revents = 0;
        CHECK(::poll(&pfd, 1, 0) == 0);
}

TEST_CASE("SelfPipe: cross-thread wake") {
        SelfPipe pipe;
        REQUIRE(pipe.isValid());

        std::atomic<bool> done{false};
        std::thread       waiter([&] {
                pollfd pfd;
                pfd.fd = pipe.readFd();
                pfd.events = POLLIN;
                pfd.revents = 0;
                // Block until the other thread wakes us, or 1 s elapses.
                int rc = ::poll(&pfd, 1, 1000);
                if (rc == 1 && (pfd.revents & POLLIN)) {
                        done.store(true, std::memory_order_release);
                        pipe.drain();
                }
        });

        // Small delay so the waiter is actually blocked in poll().
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pipe.wake();

        waiter.join();
        CHECK(done.load(std::memory_order_acquire));
}

TEST_CASE("SelfPipe: write end is non-blocking (many wakes don't stall)") {
        SelfPipe pipe;
        REQUIRE(pipe.isValid());

        // Push more wakes than a default pipe buffer (64KiB on Linux)
        // can hold if each wake were one byte.  The write end is
        // O_NONBLOCK, so a saturated pipe drops extra bytes silently
        // rather than blocking the caller.
        for (int i = 0; i < 100000; ++i) pipe.wake();

        // Drain — must complete promptly.
        auto t0 = std::chrono::steady_clock::now();
        pipe.drain();
        auto dt = std::chrono::steady_clock::now() - t0;
        CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(dt).count() < 500);
}

#endif // PROMEKI_PLATFORM_POSIX
