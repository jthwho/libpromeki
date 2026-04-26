/**
 * @file      pipelineevent.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/error.h>
#include <promeki/framecount.h>
#include <promeki/json.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mediapipelinestats.h>
#include <promeki/metadata.h>
#include <promeki/pipelineevent.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/variant.h>

using namespace promeki;

TEST_CASE("PipelineEvent_KindRoundTrip") {
        struct Pair { PipelineEvent::Kind k; const char *name; };
        const Pair pairs[] = {
                { PipelineEvent::Kind::StateChanged, "StateChanged" },
                { PipelineEvent::Kind::StageState,   "StageState"   },
                { PipelineEvent::Kind::StageError,   "StageError"   },
                { PipelineEvent::Kind::StatsUpdated, "StatsUpdated" },
                { PipelineEvent::Kind::PlanResolved, "PlanResolved" },
                { PipelineEvent::Kind::Log,          "Log"          },
        };
        for(const Pair &p : pairs) {
                CHECK(PipelineEvent::kindToString(p.k) == String(p.name));
                bool ok = false;
                CHECK(PipelineEvent::kindFromString(String(p.name), &ok) == p.k);
                CHECK(ok);
        }

        bool ok = true;
        CHECK(PipelineEvent::kindFromString(String("BogusKind"), &ok)
              == PipelineEvent::Kind::StateChanged);
        CHECK_FALSE(ok);
}

TEST_CASE("PipelineEvent_StateChangedJsonRoundTrip") {
        PipelineEvent ev;
        ev.setKind(PipelineEvent::Kind::StateChanged);
        ev.setPayload(Variant(String("Running")));
        ev.setTimestamp(TimeStamp::now());

        JsonObject j = ev.toJson();
        CHECK(j.getString("kind") == "StateChanged");
        CHECK(j.getString("payload") == "Running");

        Error err;
        PipelineEvent round = PipelineEvent::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round.kind() == PipelineEvent::Kind::StateChanged);
        CHECK(round.payload().get<String>() == "Running");
}

TEST_CASE("PipelineEvent_StageStateJsonRoundTrip") {
        PipelineEvent ev;
        ev.setKind(PipelineEvent::Kind::StageState);
        ev.setStageName(String("tpg1"));
        ev.setPayload(Variant(String("Started")));

        JsonObject j = ev.toJson();
        CHECK(j.getString("stage") == "tpg1");
        CHECK(j.getString("payload") == "Started");

        Error err;
        PipelineEvent round = PipelineEvent::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round.kind() == PipelineEvent::Kind::StageState);
        CHECK(round.stageName() == "tpg1");
        CHECK(round.payload().get<String>() == "Started");
}

TEST_CASE("PipelineEvent_StageErrorJsonRoundTrip") {
        PipelineEvent ev;
        ev.setKind(PipelineEvent::Kind::StageError);
        ev.setStageName(String("sink1"));
        ev.setPayload(Variant(String("write failed")));
        Metadata m;
        m.set(Metadata::ID(String("code")), String("IOError"));
        ev.setMetadata(m);

        JsonObject j = ev.toJson();
        Error err;
        PipelineEvent round = PipelineEvent::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round.kind() == PipelineEvent::Kind::StageError);
        CHECK(round.stageName() == "sink1");
        CHECK(round.payload().get<String>() == "write failed");
        CHECK(round.metadata().get(Metadata::ID(String("code"))).get<String>()
              == "IOError");
}

TEST_CASE("PipelineEvent_StatsUpdatedJsonRoundTrip") {
        MediaPipelineStats stats;
        MediaIOStats stage;
        stage.set(MediaIOStats::FramesDropped, FrameCount(3));
        stats.setStageStats(String("src"), stage);
        stats.recomputeAggregate();

        PipelineEvent ev;
        ev.setKind(PipelineEvent::Kind::StatsUpdated);
        ev.setJsonPayload(stats.toJson());

        JsonObject j = ev.toJson();
        CHECK(j.valueIsObject("payload"));

        Error err;
        PipelineEvent round = PipelineEvent::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round.kind() == PipelineEvent::Kind::StatsUpdated);
        Error sErr;
        MediaPipelineStats reStats =
                MediaPipelineStats::fromJson(round.jsonPayload(), &sErr);
        CHECK(sErr.isOk());
        CHECK(reStats.containsStage(String("src")));
}

TEST_CASE("PipelineEvent_PlanResolvedJsonRoundTrip") {
        MediaPipelineConfig cfg;
        MediaPipelineConfig::Stage s;
        s.name = "src";
        s.type = "TPG";
        s.mode = MediaIO::Source;
        cfg.addStage(s);

        PipelineEvent ev;
        ev.setKind(PipelineEvent::Kind::PlanResolved);
        ev.setJsonPayload(cfg.toJson());

        JsonObject j = ev.toJson();
        CHECK(j.valueIsObject("payload"));

        Error err;
        PipelineEvent round = PipelineEvent::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round.kind() == PipelineEvent::Kind::PlanResolved);
        Error cErr;
        MediaPipelineConfig reCfg =
                MediaPipelineConfig::fromJson(round.jsonPayload(), &cErr);
        CHECK(cErr.isOk());
        CHECK(reCfg.stages().size() == 1);
}

TEST_CASE("PipelineEvent_LogJsonRoundTrip") {
        PipelineEvent ev;
        ev.setKind(PipelineEvent::Kind::Log);
        ev.setPayload(Variant(String("Hello")));
        Metadata m;
        m.set(Metadata::ID(String("level")), String("Info"));
        m.set(Metadata::ID(String("source")), String("foo.cpp"));
        m.set(Metadata::ID(String("line")), int64_t(42));
        m.set(Metadata::ID(String("threadName")), String("worker"));
        ev.setMetadata(m);

        JsonObject j = ev.toJson();
        Error err;
        PipelineEvent round = PipelineEvent::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round.kind() == PipelineEvent::Kind::Log);
        CHECK(round.payload().get<String>() == "Hello");
        CHECK(round.metadata().get(Metadata::ID(String("level"))).get<String>()
              == "Info");
        CHECK(round.metadata().get(Metadata::ID(String("source"))).get<String>()
              == "foo.cpp");
        CHECK(round.metadata().get(Metadata::ID(String("threadName"))).get<String>()
              == "worker");
}

TEST_CASE("PipelineEvent_FromJsonMissingKind") {
        JsonObject j;
        j.set("stage", "src");
        Error err;
        PipelineEvent ev = PipelineEvent::fromJson(j, &err);
        CHECK(err == Error::ParseFailed);
        // The default-constructed event is returned.
        CHECK(ev.kind() == PipelineEvent::Kind::StateChanged);
}

TEST_CASE("PipelineEvent_FromJsonUnknownKind") {
        JsonObject j;
        j.set("kind", "Unknown");
        Error err;
        PipelineEvent ev = PipelineEvent::fromJson(j, &err);
        CHECK(err == Error::ParseFailed);
        CHECK(ev.kind() == PipelineEvent::Kind::StateChanged);
}
