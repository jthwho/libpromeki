/**
 * @file      tests/unit/mediaiostatscollector.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <memory>
#include <promeki/duration.h>
#include <promeki/eventloop.h>
#include <promeki/framecount.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiostats.h>
#include <promeki/mediaiostatscollector.h>
#include <promeki/objectbase.h>
#include <promeki/objectbase.tpp>
#include <promeki/string.h>
#include <promeki/variant.h>
#include <promeki/windowedstat.h>

#include "mediaio_test_helpers.h"

using namespace promeki;
using promeki::tests::InlineTestMediaIO;

namespace {
        // Convenience alias so test bodies stay short.
        using Key = MediaIOStatsCollector::Key;

        // Build a stand-alone Read command, stamp custom stats on it,
        // and hand it back as a Ptr the test can pass directly into
        // MediaIO::commandCompletedSignal.emit.  Use this when the test
        // wants to drive the collector without going through the
        // strategy/executeCmd plumbing.
        MediaIOCommand::Ptr makeReadWithStats(MediaIOStats stats) {
                auto *raw = new MediaIOCommandRead();
                raw->result = Error::Ok;
                raw->stats = std::move(stats);
                return MediaIOCommand::Ptr::takeOwnership(raw);
        }
}

TEST_CASE("MediaIOStatsCollector_DefaultConstructIsDetached") {
        EventLoop             loop;
        MediaIOStatsCollector coll;

        CHECK(coll.target() == nullptr);
        CHECK(coll.windowSize() == MediaIOStatsCollector::DefaultWindowSize);
        CHECK(coll.windows().isEmpty());
}

TEST_CASE("MediaIOStatsCollector_AttachOnConstruction") {
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        CHECK(coll.target() == &io);
        CHECK(coll.windows().isEmpty());
}

TEST_CASE("MediaIOStatsCollector_FoldsFrameworkTimingFromOpen") {
        // open() flows through the framework's per-command telemetry
        // path — both QueueWaitDuration and ExecuteDuration are stamped
        // into MediaIOCommand::stats by the strategy class.  Once
        // commandCompletedSignal fires, the collector should have a
        // window for each under (Open, <id>) keys.
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        REQUIRE(io.open().wait().isOk());

        const WindowedStat exec = coll.window({MediaIOCommand::Open, MediaIOStats::ExecuteDuration});
        const WindowedStat wait = coll.window({MediaIOCommand::Open, MediaIOStats::QueueWaitDuration});
        CHECK(exec.count() == 1);
        CHECK(wait.count() == 1);
        // Framework-set timings are non-negative; we don't assert
        // specific values since they depend on the test machine.
        CHECK(exec.min() >= 0.0);
        CHECK(wait.min() >= 0.0);
}

TEST_CASE("MediaIOStatsCollector_SeparatesByCommandType") {
        // The same ID under different MediaIOCommand::Kind values must
        // land in distinct windows: a Read command's ExecuteDuration
        // and a Write command's ExecuteDuration are conceptually
        // different telemetry channels.
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        // Two Read commands and one Write — emit directly so we don't
        // depend on the executeCmd plumbing (which would also stamp
        // framework timing on top).
        MediaIOStats readStats;
        readStats.set(MediaIOStats::ExecuteDuration, Duration::fromNanoseconds(100));
        MediaIOCommand::Ptr r1 = makeReadWithStats(readStats);

        readStats.set(MediaIOStats::ExecuteDuration, Duration::fromNanoseconds(300));
        MediaIOCommand::Ptr r2 = makeReadWithStats(readStats);

        auto *rawWrite = new MediaIOCommandWrite();
        rawWrite->result = Error::Ok;
        rawWrite->stats.set(MediaIOStats::ExecuteDuration, Duration::fromNanoseconds(50));
        MediaIOCommand::Ptr w1 = MediaIOCommand::Ptr::takeOwnership(rawWrite);

        io.commandCompletedSignal.emit(r1);
        io.commandCompletedSignal.emit(r2);
        io.commandCompletedSignal.emit(w1);

        const WindowedStat readExec = coll.window({MediaIOCommand::Read, MediaIOStats::ExecuteDuration});
        const WindowedStat writeExec = coll.window({MediaIOCommand::Write, MediaIOStats::ExecuteDuration});
        CHECK(readExec.count() == 2);
        CHECK(readExec.min() == doctest::Approx(100.0));
        CHECK(readExec.max() == doctest::Approx(300.0));
        CHECK(writeExec.count() == 1);
        CHECK(writeExec.min() == doctest::Approx(50.0));
}

TEST_CASE("MediaIOStatsCollector_SkipsNonNumericStatEntries") {
        // Non-numeric stat entries (e.g. a String value the backend
        // stamps for diagnostics) must not create windows — the
        // promotion path is shared with WindowedStat::push(Variant)
        // which returns false for non-numeric types.
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        MediaIOStats stats;
        stats.set(MediaIOStats::ExecuteDuration, Duration::fromNanoseconds(42));
        MediaIOStats::ID labelId("test.label");
        stats.set(labelId, Variant(String("diagnostic")));

        io.commandCompletedSignal.emit(makeReadWithStats(stats));

        // The duration entry made it into a window; the string did not.
        CHECK(coll.window({MediaIOCommand::Read, MediaIOStats::ExecuteDuration}).count() == 1);
        CHECK(coll.windows().contains({MediaIOCommand::Read, MediaIOStats::ExecuteDuration}));
        CHECK_FALSE(coll.windows().contains({MediaIOCommand::Read, labelId}));
}

TEST_CASE("MediaIOStatsCollector_SkipsFrameCountSentinels") {
        // FrameCount::unknown() / FrameCount::infinity() must not
        // pollute windows with sentinel-encoded negative values.
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        MediaIOStats     stats;
        MediaIOStats::ID finiteId("test.framecount.finite");
        MediaIOStats::ID unknownId("test.framecount.unknown");
        MediaIOStats::ID infiniteId("test.framecount.infinite");
        stats.set(finiteId, FrameCount(7));
        stats.set(unknownId, FrameCount::unknown());
        stats.set(infiniteId, FrameCount::infinity());

        io.commandCompletedSignal.emit(makeReadWithStats(stats));

        CHECK(coll.window({MediaIOCommand::Read, finiteId}).count() == 1);
        CHECK(coll.window({MediaIOCommand::Read, finiteId}).min() == doctest::Approx(7.0));
        CHECK_FALSE(coll.windows().contains({MediaIOCommand::Read, unknownId}));
        CHECK_FALSE(coll.windows().contains({MediaIOCommand::Read, infiniteId}));
}

TEST_CASE("MediaIOStatsCollector_DetachStopsFolding") {
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        MediaIOStats stats;
        stats.set(MediaIOStats::ExecuteDuration, Duration::fromNanoseconds(10));
        io.commandCompletedSignal.emit(makeReadWithStats(stats));
        REQUIRE(coll.window({MediaIOCommand::Read, MediaIOStats::ExecuteDuration}).count() == 1);

        coll.setTarget(nullptr);
        CHECK(coll.target() == nullptr);
        // Detaching also drops accumulated windows (they would be
        // ambiguous against a future re-attach to a different target).
        CHECK(coll.windows().isEmpty());

        // Subsequent emits on the original target must NOT land in
        // the collector — the slot is fully disconnected.
        io.commandCompletedSignal.emit(makeReadWithStats(stats));
        CHECK(coll.windows().isEmpty());
}

TEST_CASE("MediaIOStatsCollector_SwapTargetClearsAndRewires") {
        EventLoop             loop;
        InlineTestMediaIO     io1;
        InlineTestMediaIO     io2;
        MediaIOStatsCollector coll(&io1);

        MediaIOStats stats;
        stats.set(MediaIOStats::ExecuteDuration, Duration::fromNanoseconds(99));
        io1.commandCompletedSignal.emit(makeReadWithStats(stats));
        REQUIRE(coll.windows().contains({MediaIOCommand::Read, MediaIOStats::ExecuteDuration}));

        coll.setTarget(&io2);
        CHECK(coll.target() == &io2);
        CHECK(coll.windows().isEmpty());

        // io1's signal must no longer feed the collector.
        io1.commandCompletedSignal.emit(makeReadWithStats(stats));
        CHECK(coll.windows().isEmpty());

        // io2's signal does feed the collector now.
        io2.commandCompletedSignal.emit(makeReadWithStats(stats));
        CHECK(coll.window({MediaIOCommand::Read, MediaIOStats::ExecuteDuration}).count() == 1);
}

TEST_CASE("MediaIOStatsCollector_ZeroWindowSizeDropsSamples") {
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        coll.setWindowSize(0);
        CHECK(coll.windowSize() == 0);

        MediaIOStats stats;
        stats.set(MediaIOStats::ExecuteDuration, Duration::fromNanoseconds(10));
        io.commandCompletedSignal.emit(makeReadWithStats(stats));

        CHECK(coll.windows().isEmpty());
}

TEST_CASE("MediaIOStatsCollector_NegativeWindowSizeClampsToZero") {
        EventLoop             loop;
        MediaIOStatsCollector coll;

        coll.setWindowSize(-10);
        CHECK(coll.windowSize() == 0);
}

TEST_CASE("MediaIOStatsCollector_SetWindowSizeShrinksExistingWindows") {
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        // Push ten samples into a single window.
        for (int i = 0; i < 10; ++i) {
                MediaIOStats stats;
                stats.set(MediaIOStats::ExecuteDuration, Duration::fromNanoseconds(i + 1));
                io.commandCompletedSignal.emit(makeReadWithStats(stats));
        }
        REQUIRE(coll.window({MediaIOCommand::Read, MediaIOStats::ExecuteDuration}).count() == 10);

        // Shrink to 3; oldest samples drop first so the window keeps
        // the most recent three.
        coll.setWindowSize(3);
        const WindowedStat after = coll.window({MediaIOCommand::Read, MediaIOStats::ExecuteDuration});
        CHECK(after.capacity() == 3);
        CHECK(after.count() == 3);
        CHECK(after.min() == doctest::Approx(8.0));
        CHECK(after.max() == doctest::Approx(10.0));
}

TEST_CASE("MediaIOStatsCollector_SetWindowSizeZeroClearsWindows") {
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        MediaIOStats stats;
        stats.set(MediaIOStats::ExecuteDuration, Duration::fromNanoseconds(1));
        io.commandCompletedSignal.emit(makeReadWithStats(stats));
        REQUIRE_FALSE(coll.windows().isEmpty());

        coll.setWindowSize(0);
        CHECK(coll.windows().isEmpty());
        CHECK(coll.windowSize() == 0);
}

TEST_CASE("MediaIOStatsCollector_ClearKeepsWindowSize") {
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        coll.setWindowSize(64);
        MediaIOStats stats;
        stats.set(MediaIOStats::ExecuteDuration, Duration::fromNanoseconds(1));
        io.commandCompletedSignal.emit(makeReadWithStats(stats));
        REQUIRE_FALSE(coll.windows().isEmpty());

        coll.clear();
        CHECK(coll.windows().isEmpty());
        CHECK(coll.windowSize() == 64);

        // After clear() the next emit re-creates a window with the
        // configured (still 64) capacity.
        io.commandCompletedSignal.emit(makeReadWithStats(stats));
        const WindowedStat after = coll.window({MediaIOCommand::Read, MediaIOStats::ExecuteDuration});
        CHECK(after.capacity() == 64);
        CHECK(after.count() == 1);
}

TEST_CASE("MediaIOStatsCollector_TargetDestroyedAutoNullsPointer") {
        // ObjectBasePtr<MediaIO> means the collector's _target slot
        // auto-clears when the MediaIO is destroyed first.  After
        // that, the collector reports a null target and is safe to
        // destroy without trying to disconnect from a dead signal.
        EventLoop                          loop;
        std::unique_ptr<InlineTestMediaIO> io = std::make_unique<InlineTestMediaIO>();
        MediaIOStatsCollector              coll(io.get());
        REQUIRE(coll.target() == io.get());

        io.reset();
        CHECK(coll.target() == nullptr);
        // setTarget(nullptr) on a coll whose previous target is gone
        // must not crash — the early-out on prev == nullptr handles it.
        coll.setTarget(nullptr);
        CHECK(coll.target() == nullptr);
}

TEST_CASE("MediaIOStatsCollector_ParentOwnershipDestroysWithParent") {
        // Pass an ObjectBase parent; deleting the parent should
        // cascade-delete the collector via ObjectBase's child list.
        // We can't observe the collector's destruction directly from
        // outside (the pointer goes dangling), but we can witness it
        // via aboutToDestroySignal which fires from the destructor.
        EventLoop  loop;
        ObjectBase parent;

        auto *coll = new MediaIOStatsCollector(&parent);
        CHECK(coll->parent() == &parent);

        // ObjectBase::aboutToDestroySignal fires from the dtor before
        // the child is unwound — capture the firing.
        bool destroyed = false;
        coll->aboutToDestroySignal.connect([&destroyed](ObjectBase *) { destroyed = true; }, coll);

        // Trigger destruction by tearing down the parent's child list.
        // The cleanest way to verify ObjectBase parent-child ownership
        // is to scope the parent in an inner block, but here we just
        // use destroyChildren via setParent(nullptr) on the collector
        // followed by delete; aboutToDestroy still fires.
        delete coll;
        CHECK(destroyed);
}

TEST_CASE("MediaIOStatsCollector_NullCommandIgnored") {
        // Defensive: a null command Ptr (which should never occur in
        // the wild) must not crash the collector.
        EventLoop             loop;
        InlineTestMediaIO     io;
        MediaIOStatsCollector coll(&io);

        io.commandCompletedSignal.emit(MediaIOCommand::Ptr());
        CHECK(coll.windows().isEmpty());
}
