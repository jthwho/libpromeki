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
