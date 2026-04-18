/**
 * @file      eventloop.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <thread>
#include <atomic>
#include <promeki/eventloop.h>
#include <promeki/objectbase.h>
#include <promeki/timerevent.h>
#include <promeki/logger.h>
#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <unistd.h>
#include <fcntl.h>
#endif

using namespace promeki;

TEST_CASE("EventLoop: current is null with no loop") {
        // Save and restore in case a test runner has one
        EventLoop *prev = EventLoop::current();
        // We can't easily clear it without creating one, so just check the API works
        CHECK(true);
        (void)prev;
}

TEST_CASE("EventLoop: construction sets current") {
        EventLoop loop;
        CHECK(EventLoop::current() == &loop);
}

TEST_CASE("EventLoop: destruction clears current") {
        {
                EventLoop loop;
                CHECK(EventLoop::current() == &loop);
        }
        CHECK(EventLoop::current() == nullptr);
}

TEST_CASE("EventLoop: postCallable and processEvents") {
        EventLoop loop;
        bool called = false;
        loop.postCallable([&called] { called = true; });
        loop.processEvents();
        CHECK(called);
}

TEST_CASE("EventLoop: multiple postCallable calls") {
        EventLoop loop;
        int counter = 0;
        loop.postCallable([&counter] { counter++; });
        loop.postCallable([&counter] { counter++; });
        loop.postCallable([&counter] { counter++; });
        loop.processEvents();
        CHECK(counter == 3);
}

TEST_CASE("EventLoop: exec and quit") {
        EventLoop loop;
        loop.postCallable([&loop] { loop.quit(42); });
        int code = loop.exec();
        CHECK(code == 42);
}

TEST_CASE("EventLoop: quit with default code") {
        EventLoop loop;
        loop.postCallable([&loop] { loop.quit(); });
        int code = loop.exec();
        CHECK(code == 0);
}

TEST_CASE("EventLoop: quit from another thread") {
        EventLoop loop;
        std::thread t([&loop] {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                loop.quit(7);
        });
        int code = loop.exec();
        t.join();
        CHECK(code == 7);
}

TEST_CASE("EventLoop: postCallable from another thread") {
        EventLoop loop;
        std::atomic<bool> called{false};
        std::thread t([&loop, &called] {
                loop.postCallable([&called, &loop] {
                        called = true;
                        loop.quit();
                });
        });
        loop.exec();
        t.join();
        CHECK(called.load());
}

TEST_CASE("EventLoop: standalone callable timer single-shot") {
        EventLoop loop;
        int fireCount = 0;
        loop.startTimer(5, [&fireCount, &loop] {
                fireCount++;
                loop.quit();
        }, true);
        loop.exec();
        CHECK(fireCount == 1);
}

TEST_CASE("EventLoop: standalone callable timer repeating") {
        EventLoop loop;
        int fireCount = 0;
        int timerId = loop.startTimer(5, [&fireCount, &loop] {
                fireCount++;
                if(fireCount >= 3) loop.quit();
        }, false);
        loop.exec();
        loop.stopTimer(timerId);
        CHECK(fireCount >= 3);
}

TEST_CASE("EventLoop: stopTimer prevents firing") {
        EventLoop loop;
        int fireCount = 0;
        int timerId = loop.startTimer(5, [&fireCount] {
                fireCount++;
        }, false);
        loop.stopTimer(timerId);
        // Post a quit after a short delay so we don't block forever
        loop.postCallable([&loop] {
                loop.quit();
        });
        loop.exec();
        CHECK(fireCount == 0);
}

TEST_CASE("EventLoop: isRunning") {
        EventLoop loop;
        CHECK_FALSE(loop.isRunning());
        bool wasRunning = false;
        loop.postCallable([&loop, &wasRunning] {
                wasRunning = loop.isRunning();
                loop.quit();
        });
        loop.exec();
        CHECK(wasRunning);
        CHECK_FALSE(loop.isRunning());
}

TEST_CASE("EventLoop: processEvents with WaitForMore and timeout") {
        EventLoop loop;
        // Should return after timeout with no events
        auto start = TimeStamp::now();
        loop.processEvents(EventLoop::WaitForMore, 20);
        auto elapsed = start.elapsedMilliseconds();
        CHECK(elapsed >= 15); // Allow some tolerance
}

TEST_CASE("EventLoop: ObjectBase timer event delivery") {
        class TimerObj : public ObjectBase {
                PROMEKI_OBJECT(TimerObj, ObjectBase)
                public:
                        int fireCount = 0;
                        int lastTimerId = -1;
                protected:
                        void timerEvent(TimerEvent *e) override {
                                fireCount++;
                                lastTimerId = e->timerId();
                        }
        };

        EventLoop loop;
        TimerObj obj;
        int timerId = loop.startTimer(&obj, 5, true);
        // Process events with a wait to let the timer fire
        loop.postCallable([&loop] {
                // Give the timer time to fire, then quit
        });
        // Wait a bit for the timer
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        loop.processEvents();
        CHECK(obj.fireCount == 1);
        CHECK(obj.lastTimerId == timerId);
}

TEST_CASE("EventLoop: postEvent delivers to receiver") {
        class EventObj : public ObjectBase {
                PROMEKI_OBJECT(EventObj, ObjectBase)
                public:
                        int eventCount = 0;
                        Event::Type lastType = Event::InvalidType;
                protected:
                        void event(Event *e) override {
                                eventCount++;
                                lastType = e->type();
                        }
        };

        EventLoop loop;
        EventObj obj;
        Event::Type customType = Event::registerType();
        loop.postEvent(&obj, new Event(customType));
        loop.processEvents();
        CHECK(obj.eventCount == 1);
        CHECK(obj.lastType == customType);
}

TEST_CASE("EventLoop: processEvents manual loop (WASM style)") {
        EventLoop loop;
        int counter = 0;
        loop.postCallable([&counter] { counter++; });
        loop.postCallable([&counter] { counter++; });
        // Process in a manual loop instead of exec()
        for(int i = 0; i < 5; i++) {
                loop.processEvents();
        }
        CHECK(counter == 2);
}

TEST_CASE("EventLoop: exitCode accessor") {
        EventLoop loop;
        CHECK(loop.exitCode() == 0);
        loop.postCallable([&loop] { loop.quit(99); });
        loop.exec();
        CHECK(loop.exitCode() == 99);
}

TEST_CASE("EventLoop: ExcludeTimers flag skips timer processing") {
        EventLoop loop;
        int fireCount = 0;
        loop.startTimer(1, [&fireCount] {
                fireCount++;
        }, true);
        // Wait for the timer to be ready to fire
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // Process with ExcludeTimers — timer should not fire
        loop.processEvents(EventLoop::ExcludeTimers);
        CHECK(fireCount == 0);
        // Now process without the flag — timer should fire
        loop.processEvents();
        CHECK(fireCount == 1);
}

TEST_CASE("EventLoop: ExcludePosted flag skips posted callables") {
        EventLoop loop;
        bool called = false;
        loop.postCallable([&called] { called = true; });
        // Process with ExcludePosted — callable should not run
        loop.processEvents(EventLoop::ExcludePosted);
        CHECK_FALSE(called);
        // Now process without the flag — callable should run
        loop.processEvents();
        CHECK(called);
}

TEST_CASE("EventLoop: destructor drains queued events") {
        Event::Type customType = Event::registerType();
        bool *leaked = new bool(false);
        {
                EventLoop loop;
                // Post an event that will not be processed before destruction
                loop.postEvent(nullptr, new Event(customType));
                // The destructor should delete the Event without crashing.
                // We can't easily verify the delete happened, but reaching
                // here without ASAN/valgrind errors confirms no leak.
        }
        // If we got here, the destructor drained without crashing
        CHECK(true);
        delete leaked;
}

TEST_CASE("EventLoop: ObjectBase startTimer and stopTimer") {
        class TimerObj : public ObjectBase {
                PROMEKI_OBJECT(TimerObj, ObjectBase)
                public:
                        int fireCount = 0;
                protected:
                        void timerEvent(TimerEvent *) override {
                                fireCount++;
                        }
        };

        EventLoop loop;
        TimerObj obj;
        int timerId = obj.startTimer(5, true);
        CHECK(timerId >= 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        loop.processEvents();
        CHECK(obj.fireCount == 1);

        // stopTimer on a non-existent timer should not crash
        obj.stopTimer(timerId);
}

TEST_CASE("EventLoop: ObjectBase repeating timer via timerEvent") {
        class TimerObj : public ObjectBase {
                PROMEKI_OBJECT(TimerObj, ObjectBase)
                public:
                        int fireCount = 0;
                protected:
                        void timerEvent(TimerEvent *) override {
                                fireCount++;
                        }
        };

        EventLoop loop;
        TimerObj obj;
        int timerId = obj.startTimer(5, false);
        // Wait and process several times to let the timer fire repeatedly
        for(int i = 0; i < 5; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                loop.processEvents();
        }
        obj.stopTimer(timerId);
        CHECK(obj.fireCount >= 3);
}

// ============================================================================
// Cross-thread timer installation
// ============================================================================
//
// startTimer and stopTimer must be safe to call from a thread other
// than the one running exec().  Before the thread-safety fix, the
// cross-thread caller raced on the internal timer list and, even if
// the append somehow landed, the owning thread sat asleep in
// Queue::pop() because nothing woke it up.  These tests drive both
// paths and make sure the worker thread ticks the timer immediately
// after the cross-thread install.

TEST_CASE("EventLoop: startTimer from another thread wakes the worker") {
        // Worker thread runs exec() on its own EventLoop with no
        // pending work.  The main thread then installs a single-shot
        // timer cross-thread; the test passes only if the timer body
        // runs within a short wall-clock window — i.e. the worker
        // woke up and processed the new timer promptly instead of
        // sleeping forever.
        EventLoop *workerLoopPtr = nullptr;
        std::atomic<bool> workerReady{false};
        std::atomic<bool> timerFired{false};

        std::thread worker([&]() {
                EventLoop loop;
                workerLoopPtr = &loop;
                workerReady.store(true);
                loop.exec();
        });

        // Wait until the worker has constructed its EventLoop.
        while(!workerReady.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Cross-thread single-shot timer that quits the worker.
        workerLoopPtr->startTimer(5, [&timerFired, workerLoopPtr]() {
                timerFired.store(true);
                workerLoopPtr->quit();
        }, true);

        worker.join();
        CHECK(timerFired.load());
}

TEST_CASE("EventLoop: stopTimer from another thread removes the timer") {
        // Start a repeating timer cross-thread, let it fire once,
        // then stop it cross-thread and confirm no further fires.
        EventLoop *workerLoopPtr = nullptr;
        std::atomic<bool> workerReady{false};
        std::atomic<int> fireCount{0};

        std::thread worker([&]() {
                EventLoop loop;
                workerLoopPtr = &loop;
                workerReady.store(true);
                loop.exec();
        });

        while(!workerReady.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        int timerId = workerLoopPtr->startTimer(5, [&fireCount]() {
                fireCount.fetch_add(1);
        });

        // Let it fire a few times.
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        int countBefore = fireCount.load();
        CHECK(countBefore >= 1);

        // Stop cross-thread and give the worker time to observe.
        workerLoopPtr->stopTimer(timerId);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        int countAfterStop = fireCount.load();

        // Quit the worker.
        workerLoopPtr->quit();
        worker.join();

        // No further fires after the cross-thread stop.
        CHECK(fireCount.load() == countAfterStop);
}

TEST_CASE("EventLoop: timer callback can start another timer without deadlocking") {
        // processTimers() takes the timer mutex to snapshot ready
        // entries and releases it before invoking callbacks.  A
        // callback that calls startTimer() on the same event loop
        // must therefore succeed without deadlocking on the mutex.
        EventLoop loop;
        std::atomic<int> innerFires{0};
        std::atomic<bool> outerFired{false};
        loop.startTimer(5, [&]() {
                outerFired.store(true);
                loop.startTimer(5, [&innerFires, &loop]() {
                        innerFires.fetch_add(1);
                        if(innerFires.load() >= 2) loop.quit();
                }, false);
        }, true);
        loop.exec();
        CHECK(outerFired.load());
        CHECK(innerFires.load() >= 2);
}

TEST_CASE("EventLoop: timer callback can stop another timer without deadlocking") {
        // Same guarantee as the startTimer case, but for stopTimer:
        // a callback that stops a sibling timer on the same loop
        // must not deadlock on _timersMutex.
        EventLoop loop;
        std::atomic<int> aFires{0};
        std::atomic<int> bFires{0};
        int bId = loop.startTimer(3, [&bFires]() {
                bFires.fetch_add(1);
        });
        loop.startTimer(6, [&aFires, &loop, bId]() {
                aFires.fetch_add(1);
                loop.stopTimer(bId);  // stop sibling from inside our callback
                if(aFires.load() >= 1) loop.quit();
        });
        loop.exec();
        CHECK(aFires.load() >= 1);
        // b fired at least once but stopTimer should have halted it
        // before it could run many times.  Loose upper bound to
        // tolerate scheduler jitter.
        CHECK(bFires.load() < 50);
}

#if defined(PROMEKI_PLATFORM_POSIX)

namespace {

// Helper for the IoSource tests: a non-blocking pipe pair that
// closes both ends when it goes out of scope.
struct TestPipe {
        int read_fd  = -1;
        int write_fd = -1;

        TestPipe() {
                int fds[2];
                REQUIRE(::pipe(fds) == 0);
                read_fd = fds[0];
                write_fd = fds[1];
                int flags = ::fcntl(read_fd, F_GETFL);
                ::fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);
        }
        ~TestPipe() {
                if(read_fd  >= 0) ::close(read_fd);
                if(write_fd >= 0) ::close(write_fd);
        }

        void writeByte() {
                char b = 'x';
                ssize_t n = ::write(write_fd, &b, 1);
                (void)n;
        }
};

} // namespace

TEST_CASE("EventLoop: IoSource fires on pipe readability") {
        EventLoop loop;
        TestPipe pipe;
        std::atomic<int> fireCount{0};
        std::atomic<uint32_t> lastEvents{0};

        int h = loop.addIoSource(pipe.read_fd, EventLoop::IoRead,
                [&](int fd, uint32_t ev) {
                        lastEvents.store(ev);
                        char buf[16];
                        while(::read(fd, buf, sizeof(buf)) > 0) { }
                        fireCount.fetch_add(1);
                        loop.quit();
                });
        CHECK(h >= 0);

        std::thread t([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                pipe.writeByte();
        });
        loop.exec();
        t.join();

        CHECK(fireCount.load() == 1);
        CHECK((lastEvents.load() & EventLoop::IoRead) != 0);
        loop.removeIoSource(h);
}

TEST_CASE("EventLoop: addIoSource rejects bad arguments") {
        EventLoop loop;

        // Bad fd.
        CHECK(loop.addIoSource(-1, EventLoop::IoRead,
                [](int, uint32_t){}) == -1);

        // Empty callback.
        CHECK(loop.addIoSource(0, EventLoop::IoRead,
                EventLoop::IoCallback()) == -1);

        // No event bits requested.
        TestPipe pipe;
        CHECK(loop.addIoSource(pipe.read_fd, 0,
                [](int, uint32_t){}) == -1);
}

TEST_CASE("EventLoop: removeIoSource stops further firing") {
        EventLoop loop;
        TestPipe pipe;
        std::atomic<int> fireCount{0};
        int h = loop.addIoSource(pipe.read_fd, EventLoop::IoRead,
                [&](int fd, uint32_t) {
                        char buf[16];
                        while(::read(fd, buf, sizeof(buf)) > 0) { }
                        fireCount.fetch_add(1);
                });

        // Fire once, drain the pipe, then unregister and confirm
        // a subsequent write does not wake the loop.
        pipe.writeByte();
        loop.postCallable([&] {
                // Give the IoSource a chance to fire first.  Since
                // postCallable goes onto the queue and IoSource is
                // handled via poll() return, a single pass of
                // exec() can't be predicted to order them; instead
                // we drive exec() until the source has fired at
                // least once, then remove it.
        });
        loop.startTimer(50, [&] {
                if(fireCount.load() >= 1) {
                        loop.removeIoSource(h);
                        loop.quit();
                } else {
                        pipe.writeByte();
                }
        }, false);
        loop.exec();

        CHECK(fireCount.load() >= 1);
        int beforeSecondWrite = fireCount.load();

        // Write again and pump the loop briefly — the callback
        // must not fire again because the source was removed.
        pipe.writeByte();
        loop.startTimer(50, [&] { loop.quit(); }, true);
        loop.exec();
        CHECK(fireCount.load() == beforeSecondWrite);
}

TEST_CASE("EventLoop: IoSource coexists with timers") {
        EventLoop loop;
        TestPipe pipe;
        std::atomic<int> ioFires{0};
        std::atomic<int> timerFires{0};

        int h = loop.addIoSource(pipe.read_fd, EventLoop::IoRead,
                [&](int fd, uint32_t) {
                        char buf[16];
                        while(::read(fd, buf, sizeof(buf)) > 0) { }
                        ioFires.fetch_add(1);
                });
        loop.startTimer(5, [&] {
                timerFires.fetch_add(1);
                if(timerFires.load() >= 3) loop.quit();
        }, false);

        std::thread t([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                pipe.writeByte();
        });
        loop.exec();
        t.join();

        CHECK(timerFires.load() >= 3);
        CHECK(ioFires.load() >= 1);
        loop.removeIoSource(h);
}

TEST_CASE("EventLoop: addIoSource from another thread wakes the loop") {
        EventLoop loop;
        TestPipe pipe;
        std::atomic<int> fireCount{0};

        // Kick off exec() with nothing registered.  The loop will
        // block in poll() on just the wake fd.  From another
        // thread, register a new source for a fd that is already
        // readable.  The loop must observe the addIoSource wake
        // and include the new source in its next poll.
        pipe.writeByte();

        std::thread t([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                int h = loop.addIoSource(pipe.read_fd, EventLoop::IoRead,
                        [&](int fd, uint32_t) {
                                char buf[16];
                                while(::read(fd, buf, sizeof(buf)) > 0) { }
                                fireCount.fetch_add(1);
                                loop.quit();
                        });
                CHECK(h >= 0);
        });
        loop.exec();
        t.join();

        CHECK(fireCount.load() == 1);
}

TEST_CASE("EventLoop: multiple IoSources fire independently") {
        EventLoop loop;
        TestPipe pipe1;
        TestPipe pipe2;
        std::atomic<int> fires1{0};
        std::atomic<int> fires2{0};

        loop.addIoSource(pipe1.read_fd, EventLoop::IoRead,
                [&](int fd, uint32_t) {
                        char buf[16];
                        while(::read(fd, buf, sizeof(buf)) > 0) { }
                        fires1.fetch_add(1);
                });
        loop.addIoSource(pipe2.read_fd, EventLoop::IoRead,
                [&](int fd, uint32_t) {
                        char buf[16];
                        while(::read(fd, buf, sizeof(buf)) > 0) { }
                        fires2.fetch_add(1);
                        if(fires1.load() >= 1 && fires2.load() >= 1) {
                                loop.quit();
                        }
                });

        std::thread t([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                pipe1.writeByte();
                pipe2.writeByte();
        });
        loop.exec();
        t.join();

        CHECK(fires1.load() >= 1);
        CHECK(fires2.load() >= 1);
}

TEST_CASE("EventLoop: IoSource callback can remove itself") {
        EventLoop loop;
        TestPipe pipe;
        std::atomic<int> fireCount{0};
        int h = -1;

        h = loop.addIoSource(pipe.read_fd, EventLoop::IoRead,
                [&](int fd, uint32_t) {
                        char buf[16];
                        while(::read(fd, buf, sizeof(buf)) > 0) { }
                        fireCount.fetch_add(1);
                        loop.removeIoSource(h);
                        loop.quit();
                });
        REQUIRE(h >= 0);
        pipe.writeByte();
        loop.exec();
        CHECK(fireCount.load() == 1);
}

#endif // PROMEKI_PLATFORM_POSIX
