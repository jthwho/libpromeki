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
#include <promeki/pipelinestats.h>

using namespace promeki;

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
                ps.recomputeAggregate();
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

TEST_CASE("MediaPipelineStats_RecomputeAggregate") {
        MediaPipelineStats  ps = makeSample();
        const MediaIOStats &agg = ps.aggregate();

        // Sums of counters.
        CHECK(agg.get(MediaIOStats::FramesDropped).get<FrameCount>() == FrameCount(1));
        CHECK(agg.get(MediaIOStats::FramesRepeated).get<FrameCount>() == FrameCount(2));
        // Sum of throughput.
        CHECK(agg.get(MediaIOStats::BytesPerSecond).get<double>() == doctest::Approx(5000.0));
        CHECK(agg.get(MediaIOStats::FramesPerSecond).get<double>() == doctest::Approx(90.0));
        // Average of non-zero latencies.
        CHECK(agg.get(MediaIOStats::AverageLatencyMs).get<double>() == doctest::Approx((2.0 + 3.5 + 1.0) / 3.0));
        // Max of peaks.
        CHECK(agg.get(MediaIOStats::PeakLatencyMs).get<double>() == doctest::Approx(5.0));
        // First non-empty error message wins and carries its stage prefix.
        const String msg = agg.get(MediaIOStats::LastErrorMessage).get<String>();
        CHECK(msg.contains("disk full"));
        CHECK(msg.contains("sink"));
}

TEST_CASE("MediaPipelineStats_Clear") {
        MediaPipelineStats ps = makeSample();
        ps.clear();
        CHECK(ps.perStage().isEmpty());
        CHECK(ps.aggregate().size() == 0);
}

TEST_CASE("MediaPipelineStats_JsonRoundTrip") {
        MediaPipelineStats orig = makeSample();
        JsonObject         j = orig.toJson();
        CHECK(j.valueIsObject("perStage"));
        CHECK(j.valueIsObject("aggregate"));

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
        bool mentionsAggregate = false;
        for (size_t i = 0; i < lines.size(); ++i) {
                if (lines[i].contains("aggregate")) mentionsAggregate = true;
        }
        CHECK(mentionsAggregate);
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
