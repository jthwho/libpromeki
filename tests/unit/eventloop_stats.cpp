/**
 * @file      eventloop_stats.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <promeki/application.h>
#include <promeki/event.h>
#include <promeki/eventloop.h>
#include <promeki/objectbase.h>
#include <promeki/objectbase.tpp>
#include <promeki/thread.h>
#include <promeki/timestamp.h>

using namespace promeki;

// Local Event subclass with a stable type ID so we can verify
// per-event-type bucketing.
namespace {

class StatsTestEventA : public Event {
        public:
                static const Type Type;
                StatsTestEventA() : Event(Type) {}
};

class StatsTestEventB : public Event {
        public:
                static const Type Type;
                StatsTestEventB() : Event(Type) {}
};

const Event::Type StatsTestEventA::Type = Event::registerType("StatsTestA");
const Event::Type StatsTestEventB::Type = Event::registerType("StatsTestB");

class StatsTestReceiver : public ObjectBase {
                PROMEKI_OBJECT(StatsTestReceiver, ObjectBase)
        public:
                StatsTestReceiver(ObjectBase *parent = nullptr) : ObjectBase(parent) {}

                int aCount = 0;
                int bCount = 0;
                int timerCount = 0;

                void event(Event *e) override {
                        if (e->type() == StatsTestEventA::Type) {
                                aCount++;
                                e->accept();
                                return;
                        }
                        if (e->type() == StatsTestEventB::Type) {
                                bCount++;
                                e->accept();
                                return;
                        }
                        ObjectBase::event(e);
                }

                void timerEvent(TimerEvent *) override { timerCount++; }
};

} // namespace

TEST_CASE("EventLoopStats: peekStats returns invalid with no monitor") {
        EventLoop loop;
        EventLoop::Report r = loop.peekStats();
        // No monitor → no measurement; Duration fields stay at their
        // default (invalid).
        CHECK_FALSE(r.wallElapsed.isValid());
        CHECK_FALSE(loop.hasMonitor());
}

TEST_CASE("EventLoopStats: installMonitor flips hasMonitor") {
        EventLoop loop;
        loop.installMonitor(Duration::fromMilliseconds(50));
        CHECK(loop.hasMonitor());
        loop.removeMonitor();
        CHECK_FALSE(loop.hasMonitor());
}

TEST_CASE("EventLoopStats: setName / name round-trip") {
        EventLoop loop;
        CHECK(loop.name().isEmpty());
        loop.setName("worker");
        CHECK(loop.name() == "worker");
}

TEST_CASE("EventLoopStats: bucket counts match dispatched work") {
        EventLoop          loop;
        StatsTestReceiver  receiver;
        loop.setName("buckets");
        loop.installMonitor(Duration::fromSeconds(10));

        // Drive a known mix of work, then quit.
        const int callableCount = 5;
        for (int i = 0; i < callableCount; i++) {
                loop.postCallable([] {
                        // Spend enough time to register past 0.05%.
                        TimeStamp start = TimeStamp::now();
                        while (start.elapsedMicroseconds() < 200) {
                                std::this_thread::yield();
                        }
                });
        }
        const int aPosts = 3;
        const int bPosts = 2;
        for (int i = 0; i < aPosts; i++) loop.postEvent(&receiver, new StatsTestEventA());
        for (int i = 0; i < bPosts; i++) loop.postEvent(&receiver, new StatsTestEventB());
        loop.startTimer(
                1,
                [&loop, &receiver]() {
                        // One short timer fire then quit.
                        receiver.timerCount++;
                        loop.quit();
                },
                /*singleShot=*/true);

        loop.exec();

        EventLoop::Report r = loop.consumeStats();
        CHECK(r.loopName == "buckets");
        // Under the partition rule the aggregate @c callables /
        // @c events buckets carry only the unlabeled remainder.
        // The unlabeled callables here are the postCallable lambdas
        // (no Label supplied), and every event has a registered
        // type so @c eventsCount is zero.
        CHECK(r.callablesCount == callableCount);
        CHECK(r.eventsCount == 0);
        CHECK(r.timersCount >= 1);

        // Per-event-type bucket present and matches; sum across
        // entries gives the total events dispatched.
        auto itA = r.eventsByType.find(static_cast<int>(StatsTestEventA::Type));
        REQUIRE(itA != r.eventsByType.end());
        CHECK(itA->second.count == aPosts);
        auto itB = r.eventsByType.find(static_cast<int>(StatsTestEventB::Type));
        REQUIRE(itB != r.eventsByType.end());
        CHECK(itB->second.count == bPosts);
        int64_t totalEventsByType = 0;
        for (const auto &[t, es] : r.eventsByType) totalEventsByType += es.count;
        CHECK(totalEventsByType == aPosts + bPosts);

        // Bucket sum should not exceed wallElapsed (modulo overhead
        // clamp).  Under the partition rule the sum must include
        // both the aggregate buckets and every per-key map entry.
        // Allow generous slack — this is a correctness check, not
        // a tight bound.
        int64_t sumNs = r.sleep.nanoseconds() + r.queueWait.nanoseconds() + r.timers.nanoseconds() +
                        r.events.nanoseconds() + r.callables.nanoseconds() + r.io.nanoseconds() +
                        r.overhead.nanoseconds();
        for (const auto &[t, es] : r.eventsByType) sumNs += es.elapsed.nanoseconds();
        for (const auto &[id, es] : r.callablesByLabel) sumNs += es.elapsed.nanoseconds();
        CHECK(sumNs <= r.wallElapsed.nanoseconds() + 1'000'000); // 1ms slop

        loop.removeMonitor();
}

TEST_CASE("EventLoopStats: peekStats is non-destructive") {
        EventLoop          loop;
        StatsTestReceiver  receiver;
        loop.installMonitor(Duration::fromSeconds(10));

        for (int i = 0; i < 4; i++) loop.postEvent(&receiver, new StatsTestEventA());
        loop.postCallable([&loop] { loop.quit(); });
        loop.exec();

        EventLoop::Report r1 = loop.peekStats();
        EventLoop::Report r2 = loop.peekStats();
        // Events have a registered type so they go into
        // eventsByType; the aggregate eventsCount stays at zero.
        // peekStats is non-destructive — both reads match.
        auto sumEventsByType = [](const EventLoop::Report &r) {
                int64_t s = 0;
                for (const auto &[t, es] : r.eventsByType) s += es.count;
                return s;
        };
        CHECK(sumEventsByType(r1) == 4);
        CHECK(sumEventsByType(r2) == 4);
        CHECK(r1.callablesCount == r2.callablesCount);

        EventLoop::Report consumed = loop.consumeStats();
        CHECK(sumEventsByType(consumed) == 4);

        // After consumeStats the next peek should observe zeroed
        // accumulators.
        EventLoop::Report after = loop.peekStats();
        CHECK(sumEventsByType(after) == 0);
        CHECK(after.callablesCount == 0);

        loop.removeMonitor();
}

TEST_CASE("EventLoopStats: removeMonitor stops timer + invalidates snapshot") {
        EventLoop loop;
        loop.installMonitor(Duration::fromMilliseconds(50));
        CHECK(loop.hasMonitor());
        loop.removeMonitor();
        CHECK_FALSE(loop.hasMonitor());

        EventLoop::Report r = loop.consumeStats();
        // Monitor removed → no measurement; Duration fields stay at
        // their default (invalid).
        CHECK_FALSE(r.wallElapsed.isValid());
        CHECK(r.callablesCount == 0);
}

TEST_CASE("EventLoopStats: reconfigure swaps reporter + cadence") {
        EventLoop                 loop;
        std::atomic<int>          callsA{0};
        std::atomic<int>          callsB{0};
        EventLoop::ReportFunction fnA = [&callsA](const EventLoop::Report &) { callsA++; };
        EventLoop::ReportFunction fnB = [&callsB](const EventLoop::Report &) { callsB++; };

        // Run #1: monitor with fnA at 15 ms cadence; quit after
        // 80 ms so the sampling timer fires several times.  The
        // single-shot quit timer is what gives processTimers
        // multiple chances to run during the loop.
        loop.installMonitor(Duration::fromMilliseconds(15), fnA);
        loop.startTimer(
                80,
                [&loop] { loop.quit(); },
                /*singleShot=*/true);
        loop.exec();

        int callsAAfterFirst = callsA.load();
        CHECK(callsAAfterFirst >= 1);

        // Reconfigure to fnB at the same cadence.
        loop.installMonitor(Duration::fromMilliseconds(15), fnB);
        loop.startTimer(
                80,
                [&loop] { loop.quit(); },
                /*singleShot=*/true);
        loop.exec();

        loop.removeMonitor();

        // fnB has fired and fnA has not been invoked again.
        CHECK(callsB.load() >= 1);
        CHECK(callsA.load() == callsAAfterFirst);
}

TEST_CASE("EventLoopStats: monitor survives loop destruction") {
        // Heap-allocate the loop on a worker thread and tear it
        // down with a monitor still installed.  ASan/TSan in CI
        // should catch any late timer fire landing on freed state.
        std::atomic<bool> ready{false};
        std::atomic<bool> done{false};
        std::thread       worker([&ready, &done] {
                EventLoop *loop = new EventLoop();
                loop->installMonitor(Duration::fromMilliseconds(5));
                loop->postCallable([loop] { loop->quit(); });
                ready.store(true);
                loop->exec();
                delete loop;
                done.store(true);
        });

        // Spin until the worker has completed; failure mode is a
        // crash in the worker, not a hang here.
        for (int i = 0; i < 200 && !done.load(); i++) BasicThread::sleepMs(5);
        worker.join();
        CHECK(done.load());
}

TEST_CASE("EventLoopStats: cross-thread installMonitor") {
        Thread *t = new Thread();
        t->setName("xthread-stats");
        t->start();
        REQUIRE(t->threadEventLoop() != nullptr);

        std::atomic<int>          calls{0};
        std::atomic<uint64_t>     reporterTid{0};
        EventLoop::ReportFunction fn = [&calls, &reporterTid](const EventLoop::Report &) {
                calls++;
                reporterTid.store(BasicThread::currentNativeId());
        };

        t->threadEventLoop()->installMonitor(Duration::fromMilliseconds(15), fn);
        // Drive enough idle wallclock for the sampler to fire.
        BasicThread::sleepMs(120);
        t->quit();
        t->wait();

        CHECK(calls.load() >= 1);
        CHECK(reporterTid.load() != 0);
        CHECK(reporterTid.load() != BasicThread::currentNativeId());
        delete t;
}

TEST_CASE("EventLoopStats: Application::startEventLoopMonitors auto-installs on new loops") {
        // Arm the auto-install hook, then construct a Thread.  The
        // Thread's loop should self-install at construction time
        // and start firing the reporter from its own thread.
        std::atomic<int>          calls{0};
        std::atomic<uint64_t>     reporterTid{0};
        EventLoop::ReportFunction fn = [&calls, &reporterTid](const EventLoop::Report &) {
                calls++;
                reporterTid.store(BasicThread::currentNativeId());
        };
        Application::startEventLoopMonitors(Duration::fromMilliseconds(15), fn);
        CHECK(Application::eventLoopMonitorsEnabled());

        Thread *t = new Thread();
        t->setName("auto-install-test");
        t->start();
        REQUIRE(t->threadEventLoop() != nullptr);

        BasicThread::sleepMs(120);
        t->quit();
        t->wait();

        CHECK(calls.load() >= 1);
        CHECK(reporterTid.load() != BasicThread::currentNativeId());
        delete t;

        // Disable the hook so other tests are not affected.
        Application::stopEventLoopMonitors();
        CHECK_FALSE(Application::eventLoopMonitorsEnabled());
}

TEST_CASE("EventLoopStats: labeled postCallable buckets per label") {
        EventLoop loop;
        loop.installMonitor(Duration::fromSeconds(10));

        const EventLoop::Label fastLabel("stats-test-fast");
        const EventLoop::Label slowLabel("stats-test-slow");

        const int fastPosts = 5;
        const int slowPosts = 3;
        for (int i = 0; i < fastPosts; i++) {
                loop.postCallable(fastLabel, [] {});
        }
        for (int i = 0; i < slowPosts; i++) {
                loop.postCallable(slowLabel, [] {
                        // Spend enough time to register past 0.05%.
                        TimeStamp start = TimeStamp::now();
                        while (start.elapsedMicroseconds() < 200) {
                                std::this_thread::yield();
                        }
                });
        }
        // One unlabeled post for control.
        bool unlabeledRan = false;
        loop.postCallable([&unlabeledRan, &loop] {
                unlabeledRan = true;
                loop.quit();
        });
        loop.exec();

        EventLoop::Report r = loop.consumeStats();
        CHECK(unlabeledRan);
        // Under the partition rule the aggregate callablesCount
        // counts ONLY unlabeled posts.  Labeled fastPosts /
        // slowPosts are entirely in callablesByLabel.
        CHECK(r.callablesCount == 1);

        auto itFast = r.callablesByLabel.find(fastLabel.id());
        REQUIRE(itFast != r.callablesByLabel.end());
        CHECK(itFast->second.count == fastPosts);

        auto itSlow = r.callablesByLabel.find(slowLabel.id());
        REQUIRE(itSlow != r.callablesByLabel.end());
        CHECK(itSlow->second.count == slowPosts);

        // Slow label should consume more wallclock than fast,
        // assuming the spin actually elapsed.
        CHECK(itSlow->second.elapsed.nanoseconds() >= itFast->second.elapsed.nanoseconds());

        // Reverse-lookup recovers the label name registered by
        // the StringRegistry probe.
        CHECK(EventLoop::Label::fromId(fastLabel.id()).name() == "stats-test-fast");
        CHECK(EventLoop::Label::fromId(slowLabel.id()).name() == "stats-test-slow");

        // Sum of labeled posts plus the unlabeled aggregate equals
        // the total dispatch count.
        int64_t labeledTotal = 0;
        for (const auto &[id, es] : r.callablesByLabel) labeledTotal += es.count;
        CHECK(labeledTotal == fastPosts + slowPosts);
        CHECK(labeledTotal + r.callablesCount == fastPosts + slowPosts + 1);

        loop.removeMonitor();
}

TEST_CASE("EventLoopStats: same-id Label dedups across postCallable sites") {
        EventLoop loop;
        loop.installMonitor(Duration::fromSeconds(10));

        // Two distinct Item constructions of the same string
        // must produce the same id and therefore land in the
        // same bucket.
        const EventLoop::Label a("dedup-test");
        const EventLoop::Label b("dedup-test");
        REQUIRE(a.id() == b.id());

        for (int i = 0; i < 3; i++) loop.postCallable(a, [] {});
        for (int i = 0; i < 4; i++) loop.postCallable(b, [] {});
        loop.postCallable([&loop] { loop.quit(); });
        loop.exec();

        EventLoop::Report r = loop.consumeStats();
        auto              it = r.callablesByLabel.find(a.id());
        REQUIRE(it != r.callablesByLabel.end());
        CHECK(it->second.count == 7);
        loop.removeMonitor();
}

namespace {

class SignalEmitter : public ObjectBase {
                PROMEKI_OBJECT(SignalEmitter, ObjectBase)
        public:
                SignalEmitter(ObjectBase *parent = nullptr) : ObjectBase(parent) {}
                PROMEKI_SIGNAL(statsTestPing, int);
};

class SignalReceiver : public ObjectBase {
                PROMEKI_OBJECT(SignalReceiver, ObjectBase)
        public:
                SignalReceiver(ObjectBase *parent = nullptr) : ObjectBase(parent) {}
                std::atomic<int> received{0};
                void             onPing(int) { received++; }
};

} // namespace

TEST_CASE("EventLoopStats: cross-thread signal emits land in a labeled callable bucket") {
        // Receiver lives on a worker thread; emit from the main
        // thread.  The bridge lambda's postCallable must tag the
        // posted callable with the signal's prototype string,
        // producing a single per-prototype bucket regardless of
        // emit count.
        Thread *worker = new Thread();
        worker->setName("sig-stats");
        worker->start();
        REQUIRE(worker->threadEventLoop() != nullptr);

        worker->threadEventLoop()->installMonitor(Duration::fromSeconds(10));

        SignalEmitter   emitter;
        SignalReceiver *receiver = nullptr;
        // Construct the receiver on the worker thread so its
        // EventLoop affinity matches.  Use postCallable to do so
        // and wait for confirmation.
        std::atomic<bool> ready{false};
        worker->threadEventLoop()->postCallable([&ready, &receiver]() {
                receiver = new SignalReceiver();
                ready.store(true);
        });
        for (int i = 0; i < 200 && !ready.load(); i++) BasicThread::sleepMs(2);
        REQUIRE(ready.load());

        emitter.statsTestPingSignal.connect(
                [receiver](int v) { receiver->onPing(v); }, receiver);

        const int emits = 6;
        for (int i = 0; i < emits; i++) emitter.statsTestPingSignal.emit(i);

        // Give the worker time to dispatch all posted callables.
        for (int i = 0; i < 200 && receiver->received.load() < emits; i++) BasicThread::sleepMs(2);
        CHECK(receiver->received.load() == emits);

        EventLoop::Report r = worker->threadEventLoop()->consumeStats();

        // Resolve the prototype string back through the registry
        // and verify a single bucket exists with the expected
        // count.
        EventLoop::Label expected{SignalEmitter::statsTestPingSignalName};
        auto             it = r.callablesByLabel.find(expected.id());
        REQUIRE(it != r.callablesByLabel.end());
        CHECK(it->second.count == emits);

        // Tear down receiver on its own thread.
        worker->threadEventLoop()->postCallable([receiver]() { delete receiver; });
        worker->threadEventLoop()->removeMonitor();
        worker->quit();
        worker->wait();
        delete worker;
}

TEST_CASE("EventLoopStats: sleep bucket dominates an idle loop") {
        // When the loop sits idle in poll() / queueWait, the sleep
        // (or queueWait) bucket should dominate.  We don't assert
        // a specific percentage — scheduler noise makes that flaky
        // — but we assert at least one of the wait buckets is
        // non-zero after a real interval of idle time.
        EventLoop loop;
        loop.installMonitor(Duration::fromSeconds(10));
        loop.startTimer(
                80,
                [&loop] { loop.quit(); },
                /*singleShot=*/true);
        loop.exec();
        EventLoop::Report r = loop.consumeStats();
        CHECK((r.sleep.nanoseconds() > 0 || r.queueWait.nanoseconds() > 0));
        loop.removeMonitor();
}
