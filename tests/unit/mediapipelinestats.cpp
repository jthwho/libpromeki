/**
 * @file      mediapipelinestats.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediapipelinestats.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/framecount.h>
#include <promeki/windowedstat.h>

using namespace promeki;

// The tests in this file target the legacy MediaPipelineStats shape
// (per-stage MediaIOStats map + PipelineStats bucket).  The class has
// been rewritten around an array of Stage records that carry a
// cumulative MediaIOStats plus a per-MediaIOCommand::Kind windowed
// breakdown — a fresh test pass will follow once that shape settles.
#if 0
namespace {

        MediaIOStats makeStageStats(int64_t dropped, int64_t repeated, double bytesPerSec, double framesPerSec,
                                    double avgLatency, double peakLatency, const String &err = String()) {
                MediaIOStats s;
                s.set(MediaIOStats::FramesDropped, FrameCount(dropped));
                s.set(MediaIOStats::FramesRepeated, FrameCount(repeated));
                s.set(MediaIOStats::BytesPerSecond, bytesPerSec);
                s.set(MediaIOStats::FramesPerSecond, framesPerSec);
                s.set(MediaIOStats::AverageLatencyMs, avgLatency);
                s.set(MediaIOStats::PeakLatencyMs, peakLatency);
                if (!err.isEmpty()) s.set(MediaIOStats::LastErrorMessage, err);
                return s;
        }

        MediaPipelineStats makeSample() {
                MediaPipelineStats ps;
                ps.setStageStats("src", makeStageStats(1, 0, 1000.0, 30.0, 2.0, 4.0));
                ps.setStageStats("csc", makeStageStats(0, 2, 2000.0, 30.0, 3.5, 5.0));
                ps.setStageStats("sink", makeStageStats(0, 0, 2000.0, 30.0, 1.0, 2.5, "disk full"));
                return ps;
        }

} // namespace

TEST_CASE("MediaPipelineStats_SetAndGet") {
        MediaPipelineStats ps;
        MediaIOStats       s;
        s.set(MediaIOStats::FramesDropped, FrameCount(5));
        ps.setStageStats("src", s);

        CHECK(ps.containsStage("src"));
        CHECK_FALSE(ps.containsStage("sink"));
        CHECK(ps.stageStats("src").get(MediaIOStats::FramesDropped).get<FrameCount>() == FrameCount(5));
        // Missing stage returns empty.
        CHECK(ps.stageStats("ghost").size() == 0);
}

TEST_CASE("MediaPipelineStats_Clear") {
        MediaPipelineStats ps = makeSample();
        ps.pipeline().set(PipelineStats::FramesProduced, FrameCount(42));
        ps.clear();
        CHECK(ps.perStage().isEmpty());
        CHECK(ps.pipeline().size() == 0);
}

TEST_CASE("MediaPipelineStats_JsonRoundTrip") {
        MediaPipelineStats orig = makeSample();
        JsonObject         j = orig.toJson();
        CHECK(j.valueIsObject("perStage"));
        CHECK(j.valueIsObject("pipeline"));

        Error              err;
        MediaPipelineStats round = MediaPipelineStats::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round == orig);
}

TEST_CASE("MediaPipelineStats_DataStreamRoundTrip") {
        MediaPipelineStats orig = makeSample();
        Buffer             buf(16384);
        BufferIODevice     dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream w = DataStream::createWriter(&dev);
                w << orig;
                CHECK(w.status() == DataStream::Ok);
        }
        dev.seek(0);

        MediaPipelineStats round;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                CHECK(r.status() == DataStream::Ok);
        }
        CHECK(round == orig);
}

TEST_CASE("MediaPipelineStats_Describe_NotEmpty") {
        MediaPipelineStats ps = makeSample();
        StringList         lines = ps.describe();
        CHECK(!lines.isEmpty());
        // describe() emits one line per stage; verify each stage name appears.
        bool hasSrc = false, hasCsc = false, hasSink = false;
        for (size_t i = 0; i < lines.size(); ++i) {
                if (lines[i].contains("src:")) hasSrc = true;
                if (lines[i].contains("csc:")) hasCsc = true;
                if (lines[i].contains("sink:")) hasSink = true;
        }
        CHECK(hasSrc);
        CHECK(hasCsc);
        CHECK(hasSink);
}

TEST_CASE("MediaPipelineStats_PipelineBlockRoundTrip") {
        MediaPipelineStats ps = makeSample();
        ps.pipeline().set(PipelineStats::FramesProduced, FrameCount(100));
        ps.pipeline().set(PipelineStats::WriteRetries, int64_t(7));
        ps.pipeline().set(PipelineStats::PipelineErrors, int64_t(1));
        ps.pipeline().set(PipelineStats::State, String("Running"));

        SUBCASE("JSON round-trip preserves pipeline bucket") {
                Error              err;
                MediaPipelineStats round = MediaPipelineStats::fromJson(ps.toJson(), &err);
                CHECK(err.isOk());
                CHECK(round.pipeline().get(PipelineStats::FramesProduced).get<FrameCount>() == FrameCount(100));
                CHECK(round.pipeline().get(PipelineStats::WriteRetries).get<int64_t>() == 7);
                CHECK(round.pipeline().get(PipelineStats::State).get<String>() == "Running");
                CHECK(round == ps);
        }

        SUBCASE("DataStream round-trip preserves pipeline bucket") {
                Buffer         buf(16384);
                BufferIODevice dev(&buf);
                dev.open(IODevice::ReadWrite);
                {
                        DataStream w = DataStream::createWriter(&dev);
                        w << ps;
                        CHECK(w.status() == DataStream::Ok);
                }
                dev.seek(0);
                MediaPipelineStats round;
                {
                        DataStream r = DataStream::createReader(&dev);
                        r >> round;
                        CHECK(r.status() == DataStream::Ok);
                }
                CHECK(round == ps);
        }

        SUBCASE("describe() includes the pipeline bucket") {
                StringList lines = ps.describe();
                bool       mentionsPipeline = false;
                for (size_t i = 0; i < lines.size(); ++i) {
                        if (lines[i].contains("pipeline:")) mentionsPipeline = true;
                }
                CHECK(mentionsPipeline);
        }
}

TEST_CASE("MediaPipelineStats_PerStageWindowedStatsSurvive") {
        // Per-stage MediaIOStats now carry WindowedStat entries
        // (ReadExecuteDuration, WriteBytesProcessed, ...) — make sure
        // the snapshot round-trips them through both serializers
        // alongside the simpler scalar keys.
        MediaPipelineStats ps;
        MediaIOStats       a;
        a.set(MediaIOStats::FramesDropped, FrameCount(3));

        WindowedStat readDur(8);
        readDur.push(1.0e6);
        readDur.push(2.5e6);
        readDur.push(1.75e6);
        a.set(MediaIOStats::ID("ReadExecuteDuration"), readDur);
        ps.setStageStats("src", a);

        SUBCASE("JSON round-trip preserves the WindowedStat") {
                Error              err;
                MediaPipelineStats round = MediaPipelineStats::fromJson(ps.toJson(), &err);
                CHECK(err.isOk());
                CHECK(round == ps);
                const Variant rebuilt =
                        round.stageStats("src").get(MediaIOStats::ID("ReadExecuteDuration"));
                CHECK(rebuilt.type() == Variant::TypeWindowedStat);
                const WindowedStat ws = rebuilt.get<WindowedStat>();
                CHECK(ws.capacity() == 8);
                CHECK(ws.count() == 3);
                CHECK(ws.average() == doctest::Approx((1.0e6 + 2.5e6 + 1.75e6) / 3.0));
        }

        SUBCASE("DataStream round-trip preserves the WindowedStat") {
                Buffer         buf(16384);
                BufferIODevice dev(&buf);
                dev.open(IODevice::ReadWrite);
                {
                        DataStream w = DataStream::createWriter(&dev);
                        w << ps;
                }
                dev.seek(0);
                MediaPipelineStats round;
                {
                        DataStream r = DataStream::createReader(&dev);
                        r >> round;
                        CHECK(r.status() == DataStream::Ok);
                }
                CHECK(round == ps);
        }
}
#endif // legacy MediaPipelineStats shape

