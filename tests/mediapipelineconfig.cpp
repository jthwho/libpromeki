/**
 * @file      mediapipelineconfig.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mediaconfig.h>
#include <promeki/metadata.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/dir.h>
#include <promeki/size2d.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

// Builds a small, well-formed config: TPG -> Converter -> file sink.
MediaPipelineConfig makeSample() {
        MediaPipelineConfig cfg;

        Metadata pm;
        pm.set(Metadata::Title, String("MediaPipelineConfig sample"));
        cfg.setPipelineMetadata(pm);

        MediaPipelineConfig::Stage src;
        src.name = "src";
        src.type = "TPG";
        src.mode = MediaIO::Output;
        src.config.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
        src.config.set(MediaConfig::VideoEnabled, true);
        cfg.addStage(src);

        MediaPipelineConfig::Stage csc;
        csc.name = "csc";
        csc.type = "Converter";
        csc.mode = MediaIO::InputAndOutput;
        cfg.addStage(csc);

        MediaPipelineConfig::Stage sink;
        sink.name = "sink";
        sink.path = "/tmp/mediapipelineconfig_sample.dpx";
        sink.mode = MediaIO::Input;
        cfg.addStage(sink);

        cfg.addRoute("src", "csc");
        cfg.addRoute("csc", "sink");
        return cfg;
}

} // namespace

TEST_CASE("MediaPipelineConfig_ModeNameRoundTrip") {
        CHECK(MediaPipelineConfig::modeName(MediaIO::Output) == "Output");
        CHECK(MediaPipelineConfig::modeName(MediaIO::Input) == "Input");
        CHECK(MediaPipelineConfig::modeName(MediaIO::InputAndOutput) == "InputAndOutput");
        CHECK(MediaPipelineConfig::modeName(MediaIO::NotOpen) == "NotOpen");

        Error err;
        CHECK(MediaPipelineConfig::modeFromName("Output", &err) == MediaIO::Output);
        CHECK(err.isOk());
        CHECK(MediaPipelineConfig::modeFromName("Input", &err) == MediaIO::Input);
        CHECK(err.isOk());
        CHECK(MediaPipelineConfig::modeFromName("InputAndOutput", &err) == MediaIO::InputAndOutput);
        CHECK(err.isOk());
        CHECK(MediaPipelineConfig::modeFromName("NotOpen", &err) == MediaIO::NotOpen);
        CHECK(err.isOk());
        CHECK(MediaPipelineConfig::modeFromName("", &err) == MediaIO::NotOpen);
        CHECK(err.isOk());
        CHECK(MediaPipelineConfig::modeFromName("Bogus", &err) == MediaIO::NotOpen);
        CHECK(err.isError());
}

TEST_CASE("MediaPipelineConfig_Accessors") {
        MediaPipelineConfig cfg = makeSample();

        CHECK(cfg.stages().size() == 3);
        CHECK(cfg.routes().size() == 2);
        CHECK(cfg.hasStage("src"));
        CHECK(cfg.hasStage("csc"));
        CHECK(cfg.hasStage("sink"));
        CHECK_FALSE(cfg.hasStage("nope"));

        const MediaPipelineConfig::Stage *s = cfg.findStage("csc");
        REQUIRE(s != nullptr);
        CHECK(s->type == "Converter");
        CHECK(s->mode == MediaIO::InputAndOutput);

        StringList names = cfg.stageNames();
        REQUIRE(names.size() == 3);
        CHECK(names[0] == "src");
        CHECK(names[1] == "csc");
        CHECK(names[2] == "sink");
}

TEST_CASE("MediaPipelineConfig_Validate_Success") {
        MediaPipelineConfig cfg = makeSample();
        CHECK(cfg.validate() == Error::Ok);
}

TEST_CASE("MediaPipelineConfig_Validate_EmptyStages") {
        MediaPipelineConfig cfg;
        CHECK(cfg.validate().isError());
}

TEST_CASE("MediaPipelineConfig_Validate_DuplicateName") {
        MediaPipelineConfig cfg = makeSample();
        MediaPipelineConfig::Stage dup;
        dup.name = "src"; // duplicate
        dup.type = "TPG";
        dup.mode = MediaIO::Output;
        cfg.addStage(dup);
        CHECK(cfg.validate() == Error::InvalidArgument);
}

TEST_CASE("MediaPipelineConfig_Validate_MissingRouteEndpoint") {
        MediaPipelineConfig cfg = makeSample();
        cfg.addRoute("csc", "ghost");
        CHECK(cfg.validate() == Error::InvalidArgument);
}

TEST_CASE("MediaPipelineConfig_Validate_SelfLoop") {
        MediaPipelineConfig cfg = makeSample();
        cfg.addRoute("csc", "csc");
        CHECK(cfg.validate() == Error::InvalidArgument);
}

TEST_CASE("MediaPipelineConfig_Validate_Cycle") {
        MediaPipelineConfig cfg = makeSample();
        // csc → sink and sink → src closes a cycle through src → csc → sink.
        cfg.addRoute("sink", "src");
        CHECK(cfg.validate() == Error::Invalid);
}

TEST_CASE("MediaPipelineConfig_Validate_OrphanStage") {
        MediaPipelineConfig cfg = makeSample();
        MediaPipelineConfig::Stage orphan;
        orphan.name = "orphan";
        orphan.type = "TPG";
        orphan.mode = MediaIO::Output;
        cfg.addStage(orphan);
        CHECK(cfg.validate() == Error::InvalidArgument);
}

TEST_CASE("MediaPipelineConfig_Validate_MissingTypeAndPath") {
        MediaPipelineConfig cfg;
        MediaPipelineConfig::Stage bad;
        bad.name = "only";
        bad.mode = MediaIO::Output;
        cfg.addStage(bad);
        CHECK(cfg.validate() == Error::InvalidArgument);
}

TEST_CASE("MediaPipelineConfig_Validate_InvalidMode") {
        MediaPipelineConfig cfg;
        MediaPipelineConfig::Stage bad;
        bad.name = "only";
        bad.type = "TPG";
        bad.mode = MediaIO::NotOpen; // not a valid opening mode
        cfg.addStage(bad);
        CHECK(cfg.validate() == Error::InvalidArgument);
}

TEST_CASE("MediaPipelineConfig_JsonRoundTrip") {
        MediaPipelineConfig orig = makeSample();
        JsonObject j = orig.toJson();

        // Basic shape sanity.
        CHECK(j.valueIsObject("metadata"));
        CHECK(j.valueIsArray("stages"));
        CHECK(j.valueIsArray("routes"));
        CHECK(j.getArray("stages").size() == 3);
        CHECK(j.getArray("routes").size() == 2);

        Error err;
        MediaPipelineConfig round = MediaPipelineConfig::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round == orig);
}

TEST_CASE("MediaPipelineConfig_JsonRoundTrip_FromString") {
        MediaPipelineConfig orig = makeSample();
        const String text = orig.toJson().toString(2);
        Error perr;
        JsonObject reparsed = JsonObject::parse(text, &perr);
        REQUIRE(perr.isOk());
        Error ferr;
        MediaPipelineConfig round = MediaPipelineConfig::fromJson(reparsed, &ferr);
        CHECK(ferr.isOk());
        CHECK(round == orig);
}

TEST_CASE("MediaPipelineConfig_DataStreamRoundTrip") {
        MediaPipelineConfig orig = makeSample();
        Buffer buf(16384);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream writer = DataStream::createWriter(&dev);
                writer << orig;
                CHECK(writer.status() == DataStream::Ok);
        }

        dev.seek(0);

        MediaPipelineConfig round;
        {
                DataStream reader = DataStream::createReader(&dev);
                reader >> round;
                CHECK(reader.status() == DataStream::Ok);
        }

        CHECK(round == orig);
}

TEST_CASE("MediaPipelineConfig_Describe_NotEmpty") {
        MediaPipelineConfig cfg = makeSample();
        StringList lines = cfg.describe();
        CHECK(!lines.isEmpty());
        // Stage and route headers should always show up.
        bool hasStages = false;
        bool hasRoutes = false;
        bool mentionsSrc = false;
        bool mentionsRouteArrow = false;
        for(size_t i = 0; i < lines.size(); ++i) {
                const String &l = lines[i];
                if(l.contains("Stages"))  hasStages = true;
                if(l.contains("Routes"))  hasRoutes = true;
                if(l.contains("src"))     mentionsSrc = true;
                if(l.contains("->"))      mentionsRouteArrow = true;
        }
        CHECK(hasStages);
        CHECK(hasRoutes);
        CHECK(mentionsSrc);
        CHECK(mentionsRouteArrow);
}

TEST_CASE("MediaPipelineConfig_StageRouteEquality") {
        MediaPipelineConfig::Stage a;
        a.name = "n"; a.type = "TPG"; a.mode = MediaIO::Output;
        MediaPipelineConfig::Stage b = a;
        CHECK(a == b);
        b.mode = MediaIO::Input;
        CHECK(a != b);

        MediaPipelineConfig::Route r1{"x", "y", "", ""};
        MediaPipelineConfig::Route r2{"x", "y", "", ""};
        CHECK(r1 == r2);
        r2.toTrack = "video";
        CHECK(r1 != r2);
}

TEST_CASE("MediaPipelineConfig_SaveLoadFile") {
        MediaPipelineConfig orig = makeSample();

        // Use Dir::temp() so the file lands wherever the host has
        // configured its temp area (LibraryOptions::TempDir override,
        // TMPDIR env var, or the platform default).
        FilePath path = Dir::temp().path()
                / "mediapipelineconfig_save_load_test.json";
        CHECK(orig.saveToFile(path).isOk());

        Error err;
        MediaPipelineConfig round = MediaPipelineConfig::loadFromFile(path, &err);
        CHECK(err.isOk());
        CHECK(round == orig);
}
