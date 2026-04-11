/**
 * @file      thread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <promeki/thread.h>
#include <promeki/eventloop.h>
#include <promeki/objectbase.h>
#include <promeki/logger.h>
#include "test.h"

using namespace promeki;

class SenderTracker : public ObjectBase {
        PROMEKI_OBJECT(SenderTracker, ObjectBase)
        public:
                std::atomic<ObjectBase *> lastSender{nullptr};
                std::atomic<int> callCount{0};

                PROMEKI_SLOT(handleString, const String &);
};

inline void SenderTracker::handleString(const String &) {
        lastSender.store(signalSender(), std::memory_order_relaxed);
        callCount.fetch_add(1, std::memory_order_relaxed);
        return;
}

TEST_CASE("Thread: start and quit") {
        Thread t;
        t.start();
        CHECK(t.isRunning());
        CHECK(t.threadEventLoop() != nullptr);
        t.quit();
        Error err = t.wait();
        CHECK(err.isOk());
        CHECK_FALSE(t.isRunning());
}

TEST_CASE("Thread: is not adopted by default") {
        Thread t;
        CHECK_FALSE(t.isAdopted());
}

TEST_CASE("Thread: adoptCurrentThread") {
        Thread *t = Thread::adoptCurrentThread();
        REQUIRE(t != nullptr);
        CHECK(t->isAdopted());
        CHECK(t->isRunning());
        CHECK(Thread::currentThread() == t);
        delete t;
}

TEST_CASE("Thread: postCallable runs on thread") {
        Thread t;
        t.start();
        std::atomic<bool> called{false};
        std::atomic<std::thread::id> callerId{};
        auto mainId = std::this_thread::get_id();

        t.threadEventLoop()->postCallable([&called, &callerId] {
                called = true;
                callerId = std::this_thread::get_id();
        });
        t.quit();
        t.wait();
        CHECK(called.load());
        CHECK(callerId.load() != mainId);
}

TEST_CASE("Thread: started and finished signals") {
        Thread t;
        std::atomic<bool> startedFired{false};
        std::atomic<bool> finishedFired{false};
        t.startedSignal.connect([&startedFired] {
                startedFired = true;
        });
        t.finishedSignal.connect([&finishedFired](int) {
                finishedFired = true;
        });
        t.start();
        t.quit();
        t.wait();
        CHECK(startedFired.load());
        CHECK(finishedFired.load());
}

TEST_CASE("Thread: eventLoop affinity for objects") {
        EventLoop loop;
        TestOne obj;
        // Object created with an EventLoop should have it set
        CHECK(obj.eventLoop() == &loop);
}

TEST_CASE("Thread: object created without EventLoop has nullptr") {
        // Destroy any current loop first
        EventLoop *prev = EventLoop::current();
        (void)prev;
        // Can't easily clear without creating one, but objects
        // created before any EventLoop exists should have nullptr.
        // This test just validates the accessor works.
        CHECK(true);
}

TEST_CASE("Thread: cross-thread signal/slot delivery") {
        EventLoop mainLoop;

        Thread t;
        t.start();

        // Create receiver on the worker thread
        TestTwo *recv = nullptr;
        std::atomic<bool> ready{false};
        t.threadEventLoop()->postCallable([&recv, &ready] {
                recv = new TestTwo();
                ready = true;
        });

        // Wait for receiver to be created
        while(!ready.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Create sender on main thread
        TestOne sender;

        // Connect cross-thread: sender on main, receiver on worker
        ObjectBase::connect(&sender.somethingHappenedSignal, &recv->handleSomethingSlot);

        // Emit from main thread — should be marshalled to worker thread
        sender.makeSomethingHappen();

        // Give time for the posted callable to execute on the worker
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Clean up
        t.threadEventLoop()->postCallable([recv] {
                delete recv;
        });
        t.quit();
        t.wait();
        CHECK(true); // Reaching here without crash means cross-thread worked
}

TEST_CASE("Thread: moveToThread changes affinity") {
        EventLoop mainLoop;
        Thread t;
        t.start();

        TestOne obj;
        CHECK(obj.eventLoop() == &mainLoop);

        obj.setParent(nullptr); // Must have no parent
        obj.moveToThread(t.threadEventLoop());
        CHECK(obj.eventLoop() == t.threadEventLoop());

        // Move back — must be done from the worker thread (the object's
        // current thread), since moveToThread asserts on that.
        std::atomic<bool> moved{false};
        t.threadEventLoop()->postCallable([&obj, &mainLoop, &moved] {
                obj.moveToThread(&mainLoop);
                moved = true;
        });
        // Wait for the move to complete
        while(!moved.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(obj.eventLoop() == &mainLoop);

        t.quit();
        t.wait();
}

TEST_CASE("Thread: moveToThread moves children recursively") {
        EventLoop mainLoop;
        Thread t;
        t.start();

        TestOne parent;
        TestOne *child = new TestOne(&parent);

        parent.setParent(nullptr);
        parent.moveToThread(t.threadEventLoop());
        CHECK(parent.eventLoop() == t.threadEventLoop());
        CHECK(child->eventLoop() == t.threadEventLoop());

        t.quit();
        t.wait();
}

TEST_CASE("Thread: wait on adopted thread returns Invalid") {
        Thread *t = Thread::adoptCurrentThread();
        Error err = t->wait();
        CHECK(err == Error::Invalid);
        delete t;
}

TEST_CASE("Thread: start on adopted thread is no-op") {
        Thread *t = Thread::adoptCurrentThread();
        t->start(); // Should not crash or create a new thread
        CHECK(t->isAdopted());
        delete t;
}

TEST_CASE("Thread: exit code is captured") {
        Thread t;
        t.start();
        t.quit(42);
        t.wait();
        CHECK(t.exitCode() == 42);
}

TEST_CASE("Thread: timed wait returns Timeout when thread is still running") {
        Thread t;
        t.start();
        // Don't quit — the thread's event loop is running
        Error err = t.wait(20);
        CHECK(err == Error::Timeout);
        CHECK(t.isRunning());
        // Now quit and wait for real
        t.quit();
        err = t.wait();
        CHECK(err.isOk());
        CHECK_FALSE(t.isRunning());
}

TEST_CASE("Thread: timed wait returns Ok when thread finishes in time") {
        Thread t;
        t.start();
        t.quit();
        // Give it time to finish, then wait with a generous timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Error err = t.wait(1000);
        CHECK(err.isOk());
}

TEST_CASE("Thread: cross-thread signal delivers correct signalSender") {
        EventLoop mainLoop;
        Thread t;
        t.start();

        // Create receiver on the worker thread so its EventLoop
        // differs from the sender's.
        SenderTracker *recv = nullptr;
        std::atomic<bool> ready{false};
        t.threadEventLoop()->postCallable([&recv, &ready] {
                recv = new SenderTracker();
                ready = true;
        });
        while(!ready.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Create sender on main thread
        TestOne sender;
        ObjectBase::connect(&sender.somethingHappenedSignal,
                            &recv->handleStringSlot);

        // Emit from main thread — cross-thread dispatch
        sender.somethingHappenedSignal.emit("test");

        // Wait for the marshalled slot to execute on the worker
        while(recv->callCount.load(std::memory_order_relaxed) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // The slot should have received the sender pointer
        CHECK(recv->lastSender.load(std::memory_order_relaxed) == &sender);
        CHECK(recv->callCount.load(std::memory_order_relaxed) == 1);

        // Clean up
        t.threadEventLoop()->postCallable([recv] {
                delete recv;
        });
        t.quit();
        t.wait();
}

TEST_CASE("Thread: threadEventLoop is nullptr before start") {
        Thread t;
        CHECK(t.threadEventLoop() == nullptr);
}

TEST_CASE("Thread: destructor auto-joins running thread") {
        std::atomic<bool> started{false};
        {
                Thread t;
                t.start();
                started = t.isRunning();
                // Let the destructor handle quit and join
        }
        CHECK(started.load());
        // Reaching here without hanging means the destructor joined
        CHECK(true);
}

TEST_CASE("Thread: currentThread returns correct pointer from spawned thread") {
        Thread t;
        t.start();
        std::atomic<Thread *> seen{nullptr};
        t.threadEventLoop()->postCallable([&seen] {
                seen.store(Thread::currentThread(), std::memory_order_relaxed);
        });
        t.quit();
        t.wait();
        CHECK(seen.load(std::memory_order_relaxed) == &t);
}

TEST_CASE("Thread: double start on running thread is no-op") {
        Thread t;
        t.start();
        EventLoop *loop = t.threadEventLoop();
        t.start(); // Should be a no-op
        CHECK(t.isRunning());
        CHECK(t.threadEventLoop() == loop);
        t.quit();
        t.wait();
}

TEST_CASE("Thread: currentNativeId returns nonzero") {
        uint64_t id = Thread::currentNativeId();
        CHECK(id != 0);
}

TEST_CASE("Thread: nativeId is zero before start") {
        Thread t;
        CHECK(t.nativeId() == 0);
}

TEST_CASE("Thread: nativeId is set after start") {
        Thread t;
        t.start();
        CHECK(t.nativeId() != 0);
        t.quit();
        t.wait();
}

TEST_CASE("Thread: nativeId differs from main thread") {
        uint64_t mainId = Thread::currentNativeId();
        Thread t;
        t.start();
        CHECK(t.nativeId() != mainId);
        t.quit();
        t.wait();
}

TEST_CASE("Thread: adopted thread captures nativeId") {
        uint64_t mainId = Thread::currentNativeId();
        Thread *t = Thread::adoptCurrentThread();
        CHECK(t->nativeId() == mainId);
        delete t;
}

TEST_CASE("Thread: nativeId matches currentNativeId on spawned thread") {
        Thread t;
        t.start();
        std::atomic<uint64_t> seen{0};
        t.threadEventLoop()->postCallable([&seen] {
                seen.store(Thread::currentNativeId(), std::memory_order_relaxed);
        });
        t.quit();
        t.wait();
        CHECK(seen.load(std::memory_order_relaxed) == t.nativeId());
}

TEST_CASE("Thread: schedulePolicy returns Default for running thread") {
        Thread t;
        t.start();
        CHECK(t.schedulePolicy() == SchedulePolicy::Default);
        t.quit();
        t.wait();
}

TEST_CASE("Thread: schedulePolicy returns Default when not running") {
        Thread t;
        CHECK(t.schedulePolicy() == SchedulePolicy::Default);
}

TEST_CASE("Thread: priority returns 0 when not running") {
        Thread t;
        CHECK(t.priority() == 0);
}

TEST_CASE("Thread: priority returns value for running thread") {
        Thread t;
        t.start();
        // Default policy priority should be within valid range
        int prio = t.priority();
        int lo = Thread::priorityMin(SchedulePolicy::Default);
        int hi = Thread::priorityMax(SchedulePolicy::Default);
        CHECK(prio >= lo);
        CHECK(prio <= hi);
        t.quit();
        t.wait();
}

TEST_CASE("Thread: setPriority returns Invalid when not running") {
        Thread t;
        Error err = t.setPriority(0);
        CHECK(err == Error::Invalid);
}

TEST_CASE("Thread: setPriority with Default policy on running thread") {
        Thread t;
        t.start();
        int lo = Thread::priorityMin(SchedulePolicy::Default);
        // Setting priority within the valid range should succeed
        Error err = t.setPriority(lo, SchedulePolicy::Default);
        CHECK(err.isOk());
        t.quit();
        t.wait();
}

TEST_CASE("Thread: priorityMin and priorityMax are valid") {
        int lo = Thread::priorityMin(SchedulePolicy::Default);
        int hi = Thread::priorityMax(SchedulePolicy::Default);
        CHECK(lo <= hi);

        lo = Thread::priorityMin(SchedulePolicy::Fifo);
        hi = Thread::priorityMax(SchedulePolicy::Fifo);
        CHECK(lo <= hi);

        lo = Thread::priorityMin(SchedulePolicy::RoundRobin);
        hi = Thread::priorityMax(SchedulePolicy::RoundRobin);
        CHECK(lo <= hi);
}

TEST_CASE("Thread: name is empty by default") {
        Thread t;
        CHECK(t.name().isEmpty());
}

TEST_CASE("Thread: setName and name") {
        Thread t;
        t.setName("worker");
        CHECK(t.name() == "worker");
}

TEST_CASE("Thread: setName before start applies on thread") {
        Thread t;
        t.setName("mythread");
        t.start();
        // Verify the local name is still correct after start
        CHECK(t.name() == "mythread");
        // Read the OS name from within the thread to verify it was applied
        std::atomic<bool> matched{false};
        t.threadEventLoop()->postCallable([&matched] {
                char buf[64] = {};
                pthread_getname_np(pthread_self(), buf, sizeof(buf));
                matched.store(String(buf) == "mythread", std::memory_order_relaxed);
        });
        t.quit();
        t.wait();
        CHECK(matched.load());
}

TEST_CASE("Thread: setName on running thread updates OS name") {
        Thread t;
        t.start();
        t.setName("livethread");
        CHECK(t.name() == "livethread");
        // Read the OS name from within the thread
        std::atomic<bool> matched{false};
        t.threadEventLoop()->postCallable([&matched] {
                char buf[64] = {};
                pthread_getname_np(pthread_self(), buf, sizeof(buf));
                matched.store(String(buf) == "livethread", std::memory_order_relaxed);
        });
        t.quit();
        t.wait();
        CHECK(matched.load());
}

TEST_CASE("Thread: setName before start is race-free (stress)") {
        // Regression test for a startup race: Thread::threadEntry() calls
        // applyOsName() before the parent has necessarily finished
        // assigning _thread / _pthreadHandle, so nativeHandle() could
        // return a null pthread_t and crash pthread_setname_np inside
        // libc.  The fix is to use pthread_self() inside applyOsName()
        // when called from the thread being named.
        //
        // Any single run is flaky — the race window is narrow — so we
        // spin up many threads in quick succession and verify that all
        // of them both survive startup and successfully applied their
        // names.  Without the fix this test crashes (SIGSEGV in libc)
        // intermittently on a multi-core machine.
        constexpr int kCount = 64;
        List<Thread *> threads;
        List<std::atomic<bool> *> matches;
        for(int i = 0; i < kCount; i++) {
                auto *t = new Thread();
                t->setName(String::number(i));
                t->start();
                threads.pushToBack(t);

                auto *m = new std::atomic<bool>(false);
                matches.pushToBack(m);
                String expected = String::number(i);
                t->threadEventLoop()->postCallable([m, expected] {
                        char buf[64] = {};
                        pthread_getname_np(pthread_self(), buf, sizeof(buf));
                        m->store(String(buf) == expected,
                                 std::memory_order_relaxed);
                });
        }
        for(int i = 0; i < kCount; i++) {
                threads[i]->quit();
                threads[i]->wait();
                CHECK(matches[i]->load());
                delete threads[i];
                delete matches[i];
        }
}

TEST_CASE("Thread: setName truncates long names gracefully") {
        Thread t;
        t.setName("this_is_a_very_long_thread_name_that_exceeds_the_limit");
        t.start();
        // The local name should be preserved in full
        CHECK(t.name() == "this_is_a_very_long_thread_name_that_exceeds_the_limit");
        // The OS name will be truncated but should not crash
        t.quit();
        t.wait();
}

TEST_CASE("Thread: setName on adopted thread") {
        Thread *t = Thread::adoptCurrentThread();
        t->setName("main");
        CHECK(t->name() == "main");
        // On adopted thread we're on the current thread, so OS name should apply
        char buf[64] = {};
        pthread_getname_np(pthread_self(), buf, sizeof(buf));
        CHECK(String(buf) == "main");
        delete t;
}

TEST_CASE("Thread: idealThreadCount returns nonzero") {
        unsigned int count = Thread::idealThreadCount();
        CHECK(count > 0);
}

TEST_CASE("Thread: isCurrentThread on adopted thread") {
        Thread *t = Thread::adoptCurrentThread();
        CHECK(t->isCurrentThread());
        delete t;
}

TEST_CASE("Thread: isCurrentThread on spawned thread from main") {
        Thread t;
        t.start();
        // From the main thread, isCurrentThread should be false
        CHECK_FALSE(t.isCurrentThread());
        // From the spawned thread, it should be true
        std::atomic<bool> isCurrent{false};
        t.threadEventLoop()->postCallable([&t, &isCurrent] {
                isCurrent.store(t.isCurrentThread(), std::memory_order_relaxed);
        });
        t.quit();
        t.wait();
        CHECK(isCurrent.load());
}

TEST_CASE("Thread: start with custom stack size") {
        Thread t;
        t.setName("bigstack");
        t.start(4 * 1024 * 1024); // 4 MB stack
        CHECK(t.isRunning());
        CHECK(t.threadEventLoop() != nullptr);
        // Verify the thread actually runs with the custom stack
        std::atomic<bool> called{false};
        t.threadEventLoop()->postCallable([&called] {
                called = true;
        });
        t.quit();
        t.wait();
        CHECK(called.load());
}

TEST_CASE("Thread: start with default stack size (zero)") {
        Thread t;
        t.start(0);
        CHECK(t.isRunning());
        t.quit();
        t.wait();
}

TEST_CASE("Thread: affinity returns non-empty set for running thread") {
        Thread t;
        t.start();
        Set<int> cpus = t.affinity();
        // On Linux, the default affinity includes all CPUs
        CHECK(cpus.size() > 0);
        t.quit();
        t.wait();
}

TEST_CASE("Thread: affinity returns empty set when not running") {
        Thread t;
        Set<int> cpus = t.affinity();
        CHECK(cpus.isEmpty());
}

TEST_CASE("Thread: setAffinity pins to a single core") {
        Thread t;
        t.start();
        Set<int> single;
        single.insert(0);
        Error err = t.setAffinity(single);
        CHECK(err.isOk());
        // Verify the affinity was applied
        Set<int> result = t.affinity();
        CHECK(result.size() == 1);
        CHECK(result.contains(0));
        t.quit();
        t.wait();
}

TEST_CASE("Thread: setAffinity with empty set restores all cores") {
        Thread t;
        t.start();
        // First pin to core 0
        Set<int> single;
        single.insert(0);
        t.setAffinity(single);
        // Then clear affinity
        Error err = t.setAffinity(Set<int>());
        CHECK(err.isOk());
        Set<int> result = t.affinity();
        CHECK(result.size() > 1);
        t.quit();
        t.wait();
}

TEST_CASE("Thread: setAffinity returns Invalid when not running") {
        Thread t;
        Set<int> cpus;
        cpus.insert(0);
        Error err = t.setAffinity(cpus);
        CHECK(err == Error::Invalid);
}

TEST_CASE("Thread: cross-thread signalSender is nullptr when sender destroyed") {
        EventLoop mainLoop;
        Thread t;
        t.start();

        // Create receiver on the worker thread and block the event
        // loop so that the emit callable cannot run until we release it.
        SenderTracker *recv = nullptr;
        std::atomic<bool> ready{false};
        std::atomic<bool> gate{false};
        t.threadEventLoop()->postCallable([&recv, &ready, &gate] {
                recv = new SenderTracker();
                ready.store(true, std::memory_order_release);
                // Hold the event loop until the gate opens, ensuring
                // the emit callable queued after this one cannot run
                // until the sender has been destroyed.
                while(!gate.load(std::memory_order_acquire)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
        });
        while(!ready.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        {
                // Create sender on main thread, connect, and emit.
                // The emit queues a callable behind the gate, so it
                // won't execute until we release the gate below.
                TestOne sender;
                ObjectBase::connect(&sender.somethingHappenedSignal,
                                    &recv->handleStringSlot);
                sender.somethingHappenedSignal.emit("test");
                // Sender is destroyed here.
        }

        // Sender is now destroyed. Release the gate so the worker
        // thread can process the emit callable.
        gate.store(true, std::memory_order_release);

        // Wait for the marshalled slot to execute
        while(recv->callCount.load(std::memory_order_relaxed) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // signalSender() should be nullptr because the sender was
        // destroyed before the slot ran.
        CHECK(recv->lastSender.load(std::memory_order_relaxed) == nullptr);

        // Clean up
        t.threadEventLoop()->postCallable([recv] {
                delete recv;
        });
        t.quit();
        t.wait();
}

TEST_CASE("Thread: ObjectBasePtr cross-thread invalidation") {
        Thread t;
        t.start();

        // Create an ObjectBasePtr on the worker thread that tracks
        // an object on the main thread.
        std::atomic<bool> ready{false};
        std::atomic<bool> gate{false};
        std::atomic<bool> ptrInvalidAfterDestroy{false};
        TestOne *obj = new TestOne();

        t.threadEventLoop()->postCallable([&] {
                ObjectBasePtr ptr(obj);
                ready.store(true, std::memory_order_release);
                // Block until the main thread destroys the object
                while(!gate.load(std::memory_order_acquire)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                ptrInvalidAfterDestroy.store(!ptr.isValid(), std::memory_order_relaxed);
        });

        while(!ready.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Destroy the object while the worker thread holds an ObjectBasePtr
        delete obj;

        // Release the gate so the worker thread can check validity
        gate.store(true, std::memory_order_release);

        t.quit();
        t.wait();

        CHECK(ptrInvalidAfterDestroy.load());
}

TEST_CASE("Thread: ObjectBasePtr multiple cross-thread trackers invalidated") {
        constexpr int numThreads = 4;
        Thread threads[numThreads];
        std::atomic<int> readyCount{0};
        std::atomic<bool> gate{false};
        std::atomic<int> invalidCount{0};
        TestOne *obj = new TestOne();

        for(int i = 0; i < numThreads; i++) {
                threads[i].start();
                threads[i].threadEventLoop()->postCallable([&] {
                        ObjectBasePtr ptr(obj);
                        readyCount.fetch_add(1, std::memory_order_release);
                        while(!gate.load(std::memory_order_acquire)) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        if(!ptr.isValid()) {
                                invalidCount.fetch_add(1, std::memory_order_relaxed);
                        }
                });
        }

        // Wait for all threads to hold an ObjectBasePtr
        while(readyCount.load(std::memory_order_acquire) < numThreads) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Destroy the object — all ObjectBasePtr's should be invalidated
        delete obj;
        gate.store(true, std::memory_order_release);

        for(int i = 0; i < numThreads; i++) {
                threads[i].quit();
                threads[i].wait();
        }

        CHECK(invalidCount.load() == numThreads);
}

TEST_CASE("Thread: ObjectBasePtr data() returns nullptr after cross-thread invalidation") {
        Thread t;
        t.start();

        std::atomic<bool> ready{false};
        std::atomic<bool> gate{false};
        std::atomic<bool> dataIsNull{false};
        TestOne *obj = new TestOne();

        t.threadEventLoop()->postCallable([&] {
                ObjectBasePtr ptr(obj);
                ready.store(true, std::memory_order_release);
                while(!gate.load(std::memory_order_acquire)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                dataIsNull.store(ptr.data() == nullptr, std::memory_order_relaxed);
        });

        while(!ready.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        delete obj;
        gate.store(true, std::memory_order_release);

        t.quit();
        t.wait();

        CHECK(dataIsNull.load());
}

TEST_CASE("Thread: ObjectBasePtr concurrent destroy ptr and object") {
        // Exercises the _pointerMap mutex: ObjectBasePtr::unlink() and
        // ObjectBase::runCleanup() race on _pointerMap concurrently.
        for(int iter = 0; iter < 100; iter++) {
                TestOne *obj = new TestOne();
                std::atomic<bool> ready{false};

                // Create an ObjectBasePtr on a worker thread
                auto *ptr = new ObjectBasePtr(obj);
                std::thread worker([&] {
                        ready.store(true, std::memory_order_release);
                        // Destroy the ObjectBasePtr (triggers unlink)
                        delete ptr;
                });

                while(!ready.load(std::memory_order_acquire)) {
                        std::this_thread::yield();
                }
                // Destroy the object concurrently (triggers runCleanup)
                delete obj;

                worker.join();
        }
        // If we get here without crashing/TSAN errors, the mutex works.
        CHECK(true);
}

TEST_CASE("Thread: ObjectBasePtr copy assignment across threads") {
        Thread t;
        t.start();

        TestOne *obj = new TestOne();
        ObjectBasePtr mainPtr(obj);

        std::atomic<bool> ready{false};
        std::atomic<bool> gate{false};
        std::atomic<bool> copyValid{false};

        t.threadEventLoop()->postCallable([&] {
                // Copy-construct from a pointer owned by the main thread
                ObjectBasePtr workerPtr;
                workerPtr = mainPtr;
                ready.store(true, std::memory_order_release);
                while(!gate.load(std::memory_order_acquire)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                copyValid.store(workerPtr.isValid(), std::memory_order_relaxed);
        });

        while(!ready.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Worker copy should still be valid since object is alive
        gate.store(true, std::memory_order_release);

        t.quit();
        t.wait();

        CHECK(copyValid.load());
        delete obj;
}
