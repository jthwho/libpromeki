/**
 * @file      windowedstatsbundle.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/mediaiostats.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/windowedstat.h>
#include <promeki/windowedstatsbundle.h>

using namespace promeki;

namespace {

        // Build a small WindowedStat with a known capacity and the
        // supplied samples — used as the per-key payload across the
        // bundle tests so the round-trip checks have something
        // distinctive to compare.
        WindowedStat makeWs(size_t capacity, std::initializer_list<double> samples) {
                WindowedStat ws(capacity);
                for (double v : samples) ws.push(v);
                return ws;
        }

} // anonymous namespace

TEST_CASE("WindowedStatsBundle_DefaultIsEmpty") {
        WindowedStatsBundle b;
        CHECK(b.size() == 0);
        CHECK(b.isEmpty());
        CHECK_FALSE(b.contains(MediaIOStats::ExecuteDuration));
}

TEST_CASE("WindowedStatsBundle_SetGetContains") {
        WindowedStatsBundle b;
        b.set(MediaIOStats::ExecuteDuration, makeWs(4, {1.0, 2.0, 3.0}));
        b.set(MediaIOStats::BytesProcessed, makeWs(2, {100.0, 200.0}));

        CHECK(b.size() == 2);
        CHECK_FALSE(b.isEmpty());
        CHECK(b.contains(MediaIOStats::ExecuteDuration));
        CHECK(b.contains(MediaIOStats::BytesProcessed));
        CHECK_FALSE(b.contains(MediaIOStats::QueueWaitDuration));

        const WindowedStat exec = b.get(MediaIOStats::ExecuteDuration);
        CHECK(exec.capacity() == 4);
        CHECK(exec.count() == 3);
        CHECK(exec.average() == doctest::Approx(2.0));

        // get() on an absent key returns a default-constructed (empty)
        // WindowedStat — never throws and never inserts.
        const WindowedStat missing = b.get(MediaIOStats::QueueWaitDuration);
        CHECK(missing.capacity() == 0);
        CHECK(missing.isEmpty());
        CHECK_FALSE(b.contains(MediaIOStats::QueueWaitDuration));
}

TEST_CASE("WindowedStatsBundle_SetReplacesExistingEntry") {
        WindowedStatsBundle b;
        b.set(MediaIOStats::ExecuteDuration, makeWs(4, {1.0, 2.0}));
        b.set(MediaIOStats::ExecuteDuration, makeWs(8, {7.0, 8.0, 9.0}));
        CHECK(b.size() == 1);
        const WindowedStat ws = b.get(MediaIOStats::ExecuteDuration);
        CHECK(ws.capacity() == 8);
        CHECK(ws.count() == 3);
        CHECK(ws.max() == 9.0);
}

TEST_CASE("WindowedStatsBundle_RemoveAndClear") {
        WindowedStatsBundle b;
        b.set(MediaIOStats::ExecuteDuration, makeWs(4, {1.0}));
        b.set(MediaIOStats::BytesProcessed, makeWs(4, {2.0}));
        b.remove(MediaIOStats::ExecuteDuration);
        CHECK(b.size() == 1);
        CHECK_FALSE(b.contains(MediaIOStats::ExecuteDuration));
        CHECK(b.contains(MediaIOStats::BytesProcessed));

        // Removing an absent ID is a no-op.
        b.remove(MediaIOStats::ExecuteDuration);
        CHECK(b.size() == 1);

        b.clear();
        CHECK(b.size() == 0);
        CHECK(b.isEmpty());
}

TEST_CASE("WindowedStatsBundle_ForEachWalksEveryEntry") {
        WindowedStatsBundle b;
        b.set(MediaIOStats::ExecuteDuration, makeWs(2, {10.0}));
        b.set(MediaIOStats::BytesProcessed, makeWs(2, {20.0}));
        b.set(MediaIOStats::QueueWaitDuration, makeWs(2, {30.0}));

        size_t visited = 0;
        double sum = 0.0;
        b.forEach([&](WindowedStatsBundle::ID, const WindowedStat &ws) {
                ++visited;
                sum += ws.sum();
        });
        CHECK(visited == 3);
        CHECK(sum == doctest::Approx(60.0));
}

TEST_CASE("WindowedStatsBundle_Equality") {
        WindowedStatsBundle a;
        WindowedStatsBundle b;
        CHECK(a == b);

        a.set(MediaIOStats::ExecuteDuration, makeWs(4, {1.0, 2.0}));
        CHECK(a != b);

        b.set(MediaIOStats::ExecuteDuration, makeWs(4, {1.0, 2.0}));
        CHECK(a == b);

        // Differing capacity counts as a difference.
        b.set(MediaIOStats::ExecuteDuration, makeWs(8, {1.0, 2.0}));
        CHECK(a != b);
}

TEST_CASE("WindowedStatsBundle_Describe") {
        WindowedStatsBundle b;
        b.set(MediaIOStats::ExecuteDuration, makeWs(4, {1.0, 2.0, 3.0}));
        b.set(MediaIOStats::BytesProcessed, makeWs(4, {100.0}));

        const StringList lines = b.describe();
        CHECK(lines.size() == 2);
        // Each line begins with "<id-name>: " — confirm both IDs were
        // rendered.  Order follows the underlying ordered map's
        // iteration order; we check membership rather than position.
        bool sawExecute = false;
        bool sawBytes = false;
        for (const String &line : lines) {
                if (line.startsWith("ExecuteDuration: ")) sawExecute = true;
                if (line.startsWith("BytesProcessed: ")) sawBytes = true;
                CHECK(line.contains("WinSz: "));
        }
        CHECK(sawExecute);
        CHECK(sawBytes);

        // Custom formatter is dispatched per-ID; we use it to prove
        // the bundle threads the ID through to the picker so callers
        // can humanise units differently for different stat IDs.
        const StringList custom =
                b.describe([](WindowedStatsBundle::ID) { return [](double v) { return String("v=") + String::number(v); }; });
        bool sawFormatted = false;
        for (const String &line : custom) {
                if (line.contains("v=")) sawFormatted = true;
        }
        CHECK(sawFormatted);
}

TEST_CASE("WindowedStatsBundle_JsonRoundTrip") {
        WindowedStatsBundle b;
        b.set(MediaIOStats::ExecuteDuration, makeWs(4, {1.5, 2.5, 3.5}));
        b.set(MediaIOStats::BytesProcessed, makeWs(2, {100.0, 200.0}));

        const JsonObject j = b.toJson();
        Error            err;
        const WindowedStatsBundle round = WindowedStatsBundle::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round == b);
}

TEST_CASE("WindowedStatsBundle_FromJsonRejectsMalformedEntry") {
        // Build a JSON object with one valid entry plus one entry whose
        // value is not a canonical "cap=N:[...]" string.  fromJson
        // should drop the bad entry, surface the failure via @p err,
        // and still return the valid entry.
        JsonObject j;
        j.set("ExecuteDuration", String("cap=2:[1,2]"));
        j.set("BytesProcessed", String("not a windowedstat"));

        Error                     err;
        const WindowedStatsBundle round = WindowedStatsBundle::fromJson(j, &err);
        CHECK(err == Error::Invalid);
        CHECK(round.size() == 1);
        CHECK(round.contains(MediaIOStats::ExecuteDuration));
        CHECK_FALSE(round.contains(MediaIOStats::BytesProcessed));
}

TEST_CASE("WindowedStatsBundle_FromJsonEmptyInputProducesEmptyBundle") {
        JsonObject                j;
        Error                     err;
        const WindowedStatsBundle round = WindowedStatsBundle::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round.isEmpty());
}

TEST_CASE("WindowedStatsBundle_DataStreamRoundTrip") {
        WindowedStatsBundle b;
        b.set(MediaIOStats::ExecuteDuration, makeWs(4, {0.5, 1.5, 2.5}));
        b.set(MediaIOStats::QueueWaitDuration, makeWs(8, {10.0, 20.0}));
        b.set(MediaIOStats::BytesProcessed, makeWs(2, {99.0}));

        Buffer         buf(1024);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << b;
                CHECK(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        WindowedStatsBundle round;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                CHECK(r.status() == DataStream::Ok);
        }
        CHECK(round == b);
}

TEST_CASE("WindowedStatsBundle_DataStreamReadOverwritesExistingEntries") {
        // Reading into an already-populated bundle must clear the
        // prior contents — otherwise a deserialize-into-shared-target
        // pattern would silently merge stale entries into the new
        // payload.
        WindowedStatsBundle target;
        target.set(MediaIOStats::FramesDropped, makeWs(2, {42.0}));

        WindowedStatsBundle src;
        src.set(MediaIOStats::ExecuteDuration, makeWs(4, {1.0, 2.0}));

        Buffer         buf(512);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << src;
                CHECK(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream r = DataStream::createReader(&dev);
                r >> target;
                CHECK(r.status() == DataStream::Ok);
        }
        CHECK(target == src);
        CHECK_FALSE(target.contains(MediaIOStats::FramesDropped));
}
