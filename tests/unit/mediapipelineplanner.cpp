/**
 * @file      mediapipelineplanner.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for MediaPipelinePlanner — the offline pass that splices
 * bridging stages (CSC, FrameSync, SRC, VideoDecoder, VideoEncoder)
 * into a partial MediaPipelineConfig until every route is directly
 * format-compatible.
 *
 * Most tests use a synthetic source (PlannerSyntheticSrc) and sink
 * (PlannerSyntheticSink) registered at file scope so the test
 * coverage doesn't depend on the side effects of opening real
 * backends like TPG.  A separate group exercises the planner against
 * the real TPG → CSC path end-to-end.
 */

#include <doctest/doctest.h>

#include <promeki/audiodesc.h>
#include <promeki/enums.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/inlinemediaio.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mediapipelineplanner.h>
#include <promeki/variantspec.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/string.h>
#include <promeki/videocodec.h>
#include <promeki/videoformat.h>

using namespace promeki;

// ============================================================================
// Synthetic source / sink for deterministic planner tests
// ============================================================================
//
// Both backends parse a single config key (PlannerProducedDesc /
// PlannerAcceptedDesc) carrying a stable PixelFormat::ID.  describe()
// projects that into a MediaDesc whose raster is fixed (1920x1080 @
// 30 fps) so test fixtures only need to vary the pixel format.

namespace {

        // Helper IDs in the MediaConfig registry — picked from the user-
        // defined band so they don't collide with shipped keys.
        const MediaConfig::ID PlannerProducedDesc("PlannerProducedDesc");
        const MediaConfig::ID PlannerAcceptedDesc("PlannerAcceptedDesc");
        const MediaConfig::ID PlannerProducedRate("PlannerProducedRate");
        const MediaConfig::ID PlannerAcceptedRate("PlannerAcceptedRate");

        MediaDesc syntheticDesc(PixelFormat::ID id, FrameRate rate = FrameRate(FrameRate::FPS_30)) {
                MediaDesc md;
                md.setFrameRate(rate);
                md.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080), PixelFormat(id)));
                return md;
        }

        PixelFormat::ID readDescId(const MediaIO::Config &cfg, MediaConfig::ID key, PixelFormat::ID fallback) {
                if (!cfg.contains(key)) return fallback;
                const PixelFormat pd = cfg.getAs<PixelFormat>(key);
                return pd.isValid() ? pd.id() : fallback;
        }

        FrameRate readRate(const MediaIO::Config &cfg, MediaConfig::ID key, FrameRate fallback) {
                if (!cfg.contains(key)) return fallback;
                const FrameRate fr = cfg.getAs<FrameRate>(key);
                return fr.isValid() ? fr : fallback;
        }

        class PlannerSyntheticSrcMediaIO : public InlineMediaIO {
                                PROMEKI_OBJECT(PlannerSyntheticSrcMediaIO, InlineMediaIO)
                        public:
                                PlannerSyntheticSrcMediaIO(ObjectBase *parent = nullptr) : InlineMediaIO(parent) {}
                                ~PlannerSyntheticSrcMediaIO() override {
                                        if (isOpen()) (void)close().wait();
                                }

                                Error describe(MediaIODescription *out) const override {
                                        Error baseErr = MediaIO::describe(out);
                                        if (baseErr.isError()) return baseErr;
                                        const PixelFormat::ID id =
                                                readDescId(config(), PlannerProducedDesc, PixelFormat::RGBA8_sRGB);
                                        const FrameRate rate =
                                                readRate(config(), PlannerProducedRate, FrameRate(FrameRate::FPS_30));
                                        out->setPreferredFormat(syntheticDesc(id, rate));
                                        out->producibleFormats().pushToBack(syntheticDesc(id, rate));
                                        return Error::Ok;
                                }

                        protected:
                                Error executeCmd(MediaIOCommandOpen &cmd) override {
                                        MediaIOPortGroup *group = addPortGroup("synth-src");
                                        if (group == nullptr) return Error::Invalid;
                                        const PixelFormat::ID id =
                                                readDescId(cmd.config, PlannerProducedDesc, PixelFormat::RGBA8_sRGB);
                                        const FrameRate rate = readRate(cmd.config, PlannerProducedRate,
                                                                        FrameRate(FrameRate::FPS_30));
                                        const MediaDesc desc = syntheticDesc(id, rate);
                                        // Mirror the desc on the cmd so MediaIO::mediaDesc()
                                        // (and the planner's discoverSourceDesc strategy 4)
                                        // can see the produced shape post-open.
                                        cmd.mediaDesc = desc;
                                        cmd.frameRate = rate;
                                        if (addSource(group, desc) == nullptr) return Error::Invalid;
                                        return Error::Ok;
                                }
                                Error executeCmd(MediaIOCommandClose &cmd) override {
                                        (void)cmd;
                                        return Error::Ok;
                                }
        };

        class PlannerSyntheticSinkMediaIO : public InlineMediaIO {
                                PROMEKI_OBJECT(PlannerSyntheticSinkMediaIO, InlineMediaIO)
                        public:
                                PlannerSyntheticSinkMediaIO(ObjectBase *parent = nullptr) : InlineMediaIO(parent) {}
                                ~PlannerSyntheticSinkMediaIO() override {
                                        if (isOpen()) (void)close().wait();
                                }

                                Error describe(MediaIODescription *out) const override {
                                        Error baseErr = MediaIO::describe(out);
                                        if (baseErr.isError()) return baseErr;
                                        const PixelFormat::ID id =
                                                readDescId(config(), PlannerAcceptedDesc, PixelFormat::RGBA8_sRGB);
                                        const FrameRate rate =
                                                readRate(config(), PlannerAcceptedRate, FrameRate(FrameRate::FPS_30));
                                        out->acceptableFormats().pushToBack(syntheticDesc(id, rate));
                                        out->setPreferredFormat(syntheticDesc(id, rate));
                                        return Error::Ok;
                                }

                                Error proposeInput(const MediaDesc &offered,
                                                   MediaDesc       *preferred) const override {
                                        if (preferred == nullptr) return Error::Invalid;
                                        if (offered.imageList().isEmpty()) return Error::NotSupported;

                                        const PixelFormat::ID id =
                                                readDescId(config(), PlannerAcceptedDesc, PixelFormat::RGBA8_sRGB);
                                        const FrameRate rate =
                                                readRate(config(), PlannerAcceptedRate, FrameRate(FrameRate::FPS_30));
                                        const bool pixelMatches = offered.imageList()[0].pixelFormat().id() == id;
                                        const bool rateMatches = offered.frameRate() == rate;
                                        if (pixelMatches && rateMatches) {
                                                *preferred = offered;
                                                return Error::Ok;
                                        }
                                        MediaDesc want = offered;
                                        if (!pixelMatches) want.imageList()[0].setPixelFormat(PixelFormat(id));
                                        if (!rateMatches) want.setFrameRate(rate);
                                        *preferred = want;
                                        return Error::Ok;
                                }

                        protected:
                                Error executeCmd(MediaIOCommandOpen &cmd) override {
                                        MediaIOPortGroup *group = addPortGroup("synth-sink");
                                        if (group == nullptr) return Error::Invalid;
                                        const PixelFormat::ID id =
                                                readDescId(cmd.config, PlannerAcceptedDesc, PixelFormat::RGBA8_sRGB);
                                        const FrameRate rate = readRate(cmd.config, PlannerAcceptedRate,
                                                                        FrameRate(FrameRate::FPS_30));
                                        const MediaDesc desc = syntheticDesc(id, rate);
                                        cmd.mediaDesc = desc;
                                        cmd.frameRate = rate;
                                        if (addSink(group, desc) == nullptr) return Error::Invalid;
                                        return Error::Ok;
                                }
                                Error executeCmd(MediaIOCommandClose &cmd) override {
                                        (void)cmd;
                                        return Error::Ok;
                                }
        };

        class PlannerSyntheticSrcFactory : public MediaIOFactory {
                public:
                        PlannerSyntheticSrcFactory() = default;

                        String name() const override { return String("PlannerSyntheticSrc"); }
                        String description() const override {
                                return String("Synthetic source emitting a configurable PixelFormat.");
                        }
                        bool canBeSource() const override { return true; }

                        Config::SpecMap configSpecs() const override {
                                Config::SpecMap m;
                                m.insert(PlannerProducedDesc,
                                         VariantSpec().setType(Variant::TypePixelFormat).setDefault(
                                                 PixelFormat(PixelFormat::RGBA8_sRGB)));
                                m.insert(PlannerProducedRate, VariantSpec()
                                                                      .setType(Variant::TypeFrameRate)
                                                                      .setDefault(FrameRate(FrameRate::FPS_30)));
                                return m;
                        }

                        MediaIO *create(const Config &config, ObjectBase *parent) const override {
                                auto *io = new PlannerSyntheticSrcMediaIO(parent);
                                io->setConfig(config);
                                return io;
                        }
        };

        class PlannerSyntheticSinkFactory : public MediaIOFactory {
                public:
                        PlannerSyntheticSinkFactory() = default;

                        String name() const override { return String("PlannerSyntheticSink"); }
                        String description() const override {
                                return String("Synthetic sink demanding a configurable PixelFormat.");
                        }
                        bool canBeSink() const override { return true; }

                        Config::SpecMap configSpecs() const override {
                                Config::SpecMap m;
                                m.insert(PlannerAcceptedDesc,
                                         VariantSpec().setType(Variant::TypePixelFormat).setDefault(
                                                 PixelFormat(PixelFormat::RGBA8_sRGB)));
                                m.insert(PlannerAcceptedRate, VariantSpec()
                                                                      .setType(Variant::TypeFrameRate)
                                                                      .setDefault(FrameRate(FrameRate::FPS_30)));
                                return m;
                        }

                        MediaIO *create(const Config &config, ObjectBase *parent) const override {
                                auto *io = new PlannerSyntheticSinkMediaIO(parent);
                                io->setConfig(config);
                                return io;
                        }
        };

        PROMEKI_REGISTER_MEDIAIO_FACTORY(PlannerSyntheticSrcFactory)
        PROMEKI_REGISTER_MEDIAIO_FACTORY(PlannerSyntheticSinkFactory)

        // ----- helpers -----

        MediaPipelineConfig makeSrcSinkConfig(PixelFormat::ID srcId, PixelFormat::ID sinkId) {
                MediaPipelineConfig cfg;

                MediaPipelineConfig::Stage src;
                src.name = "src";
                src.type = "PlannerSyntheticSrc";
                src.role = MediaPipelineConfig::StageRole::Source;
                src.config.set(PlannerProducedDesc, PixelFormat(srcId));
                cfg.addStage(src);

                MediaPipelineConfig::Stage sink;
                sink.name = "sink";
                sink.type = "PlannerSyntheticSink";
                sink.role = MediaPipelineConfig::StageRole::Sink;
                sink.config.set(PlannerAcceptedDesc, PixelFormat(sinkId));
                cfg.addStage(sink);

                cfg.addRoute("src", "sink");
                return cfg;
        }

        bool stageTypeAt(const MediaPipelineConfig &cfg, size_t i, const char *typeName) {
                return i < cfg.stages().size() && cfg.stages()[i].type == typeName;
        }

} // namespace

// ============================================================================
// Identity / pass-through
// ============================================================================

TEST_CASE("MediaPipelinePlanner_Identity_NoBridgesInserted") {
        // Source produces RGBA8, sink also accepts RGBA8 — there's no
        // gap, so the resolved config must be route-equivalent to the
        // input.
        const MediaPipelineConfig in = makeSrcSinkConfig(PixelFormat::RGBA8_sRGB, PixelFormat::RGBA8_sRGB);

        MediaPipelineConfig out;
        String              diag;
        REQUIRE(MediaPipelinePlanner::plan(in, &out, {}, &diag) == Error::Ok);
        CHECK(diag.isEmpty());
        CHECK(out.stages().size() == 2);
        CHECK(out.routes().size() == 1);
        CHECK(out.routes()[0].from == "src");
        CHECK(out.routes()[0].to == "sink");
}

TEST_CASE("MediaPipelineConfig_isResolved_ReturnsTrueWhenDirect") {
        const MediaPipelineConfig cfg = makeSrcSinkConfig(PixelFormat::RGBA8_sRGB, PixelFormat::RGBA8_sRGB);
        String                    diag;
        CHECK(cfg.isResolved(&diag));
        CHECK(diag.isEmpty());
}

// ============================================================================
// Single-hop CSC insertion
// ============================================================================

TEST_CASE("MediaPipelinePlanner_PixelGap_InsertsCSC") {
        // Source produces RGBA8 but sink demands NV12 (4:2:0 YUV) —
        // CSC must be spliced into the route.
        const MediaPipelineConfig in =
                makeSrcSinkConfig(PixelFormat::RGBA8_sRGB, PixelFormat::YUV8_420_SemiPlanar_Rec709);

        MediaPipelineConfig out;
        String              diag;
        REQUIRE(MediaPipelinePlanner::plan(in, &out, {}, &diag) == Error::Ok);
        CHECK(diag.isEmpty());

        REQUIRE(out.stages().size() == 3);
        REQUIRE(out.routes().size() == 2);

        // The original stages are preserved verbatim, with the
        // CSC bridge inserted between them.
        CHECK(out.stages()[0].name == "src");
        CHECK(out.stages()[1].name == "sink");
        CHECK(stageTypeAt(out, 2, "CSC"));

        // The bridge stage has a deterministic, scopable name.
        CHECK(out.stages()[2].name == "br0_src_sink");

        // Routes flow through the bridge.
        CHECK(out.routes()[0].from == "src");
        CHECK(out.routes()[0].to == "br0_src_sink");
        CHECK(out.routes()[1].from == "br0_src_sink");
        CHECK(out.routes()[1].to == "sink");

        // The bridge stage carries the right OutputPixelFormat so the
        // runtime CSC actually converts to the sink's preferred form.
        const auto &bridgeCfg = out.stages()[2].config;
        CHECK(bridgeCfg.getAs<PixelFormat>(MediaConfig::OutputPixelFormat).id() ==
              PixelFormat::YUV8_420_SemiPlanar_Rec709);
}

TEST_CASE("MediaPipelineConfig_isResolved_ReturnsFalseOnGap") {
        const MediaPipelineConfig cfg =
                makeSrcSinkConfig(PixelFormat::RGBA8_sRGB, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        String diag;
        CHECK_FALSE(cfg.isResolved(&diag));
        CHECK(diag.contains("src"));
        CHECK(diag.contains("sink"));
}

// ============================================================================
// Excluded bridges
// ============================================================================

TEST_CASE("MediaPipelinePlanner_ExcludedBridges_BlockInsertion") {
        // Same setup as the CSC-insertion test, but block CSC via
        // policy.  No other bridge can satisfy the gap, so the
        // planner must fail with NotSupported.
        const MediaPipelineConfig in =
                makeSrcSinkConfig(PixelFormat::RGBA8_sRGB, PixelFormat::YUV8_420_SemiPlanar_Rec709);

        MediaPipelinePlanner::Policy policy;
        policy.excludedBridges.pushToBack("CSC");

        MediaPipelineConfig out;
        String              diag;
        Error               err = MediaPipelinePlanner::plan(in, &out, policy, &diag);
        CHECK(err == Error::NotSupported);
        CHECK(out.stages().isEmpty());
        CHECK(diag.contains("src"));
        CHECK(diag.contains("sink"));
        // Verbose diagnostic enumerates the upstream / preferred
        // shapes and the per-bridge trace so the user can read the
        // failure log and figure out the gap without guessing.
        CHECK(diag.contains("upstream produced"));
        CHECK(diag.contains("sink preferred"));
        CHECK(diag.contains("bridges considered"));
        CHECK(diag.contains("CSC"));
        CHECK(diag.contains("excluded by policy"));
}

TEST_CASE("MediaPipelinePlanner_FailureDiagnostic_HasShapeAndTrace") {
        // ZeroCopyOnly forces every CSC bridge to be rejected by
        // policy on cost grounds.  The diagnostic must spell out
        // both endpoints and each bridge's verdict — the explicit
        // contract that mediaplay relies on for end-user-visible
        // failure logs.
        const MediaPipelineConfig in =
                makeSrcSinkConfig(PixelFormat::RGBA8_sRGB, PixelFormat::YUV8_420_SemiPlanar_Rec709);

        MediaPipelinePlanner::Policy policy;
        policy.quality = MediaPipelinePlanner::Quality::ZeroCopyOnly;

        MediaPipelineConfig out;
        String              diag;
        REQUIRE(MediaPipelinePlanner::plan(in, &out, policy, &diag) == Error::NotSupported);

        // Diagnostic is multi-line — split on '\n' so we can assert
        // on individual rows rather than a brittle substring test.
        const StringList lines = diag.split(std::string("\n"));
        REQUIRE(lines.size() >= 4);
        CHECK(diag.contains("RGBA8_sRGB"));
        CHECK(diag.contains("YUV8_420_SemiPlanar_Rec709"));
        CHECK(diag.contains("rejected by ZeroCopyOnly"));
}

// ============================================================================
// Idempotence — re-planning a resolved config returns an equivalent config
// ============================================================================

TEST_CASE("MediaPipelinePlanner_ReplanIsIdempotent") {
        const MediaPipelineConfig in =
                makeSrcSinkConfig(PixelFormat::RGBA8_sRGB, PixelFormat::YUV8_420_SemiPlanar_Rec709);

        MediaPipelineConfig once;
        REQUIRE(MediaPipelinePlanner::plan(in, &once) == Error::Ok);

        MediaPipelineConfig twice;
        REQUIRE(MediaPipelinePlanner::plan(once, &twice) == Error::Ok);

        CHECK(twice.stages().size() == once.stages().size());
        CHECK(twice.routes().size() == once.routes().size());
}

// ============================================================================
// Quality bias
// ============================================================================

TEST_CASE("MediaPipelinePlanner_QualityBias_Highest") {
        // Highest never modifies the raw cost.
        CHECK(MediaPipelinePlanner::adjustCostForQuality(50, MediaPipelinePlanner::Quality::Highest) == 50);
        CHECK(MediaPipelinePlanner::adjustCostForQuality(5000, MediaPipelinePlanner::Quality::Highest) == 5000);
}

TEST_CASE("MediaPipelinePlanner_QualityBias_Balanced") {
        // Balanced penalises heavy bridges (cost > 1000) by 25 %.
        CHECK(MediaPipelinePlanner::adjustCostForQuality(50, MediaPipelinePlanner::Quality::Balanced) == 50);
        CHECK(MediaPipelinePlanner::adjustCostForQuality(5000, MediaPipelinePlanner::Quality::Balanced) == 6250);
}

TEST_CASE("MediaPipelinePlanner_QualityBias_Fastest") {
        // Fastest penalises bounded-error band 50 % and heavy band 200 %.
        CHECK(MediaPipelinePlanner::adjustCostForQuality(50, MediaPipelinePlanner::Quality::Fastest) == 50);
        CHECK(MediaPipelinePlanner::adjustCostForQuality(500, MediaPipelinePlanner::Quality::Fastest) == 750);
        CHECK(MediaPipelinePlanner::adjustCostForQuality(5000, MediaPipelinePlanner::Quality::Fastest) == 15000);
}

TEST_CASE("MediaPipelinePlanner_QualityBias_ZeroCopyOnly_RejectsHeavyBridges") {
        // ZeroCopyOnly returns the raw cost (the rejection happens
        // inside findSingleBridge), but the test confirms the
        // pass-through arithmetic.
        CHECK(MediaPipelinePlanner::adjustCostForQuality(50, MediaPipelinePlanner::Quality::ZeroCopyOnly) == 50);
        CHECK(MediaPipelinePlanner::adjustCostForQuality(5000, MediaPipelinePlanner::Quality::ZeroCopyOnly) == 5000);
}

TEST_CASE("MediaPipelinePlanner_ZeroCopyOnly_BlocksLossyBridges") {
        // RGBA8 → NV12 has a CSC cost > 100 (chroma subsampling).
        // ZeroCopyOnly should reject the CSC and the planner has no
        // alternative — so it fails with NotSupported.
        const MediaPipelineConfig in =
                makeSrcSinkConfig(PixelFormat::RGBA8_sRGB, PixelFormat::YUV8_420_SemiPlanar_Rec709);

        MediaPipelinePlanner::Policy policy;
        policy.quality = MediaPipelinePlanner::Quality::ZeroCopyOnly;

        MediaPipelineConfig out;
        String              diag;
        Error               err = MediaPipelinePlanner::plan(in, &out, policy, &diag);
        CHECK(err == Error::NotSupported);
        CHECK(out.stages().isEmpty());
}

// ============================================================================
// Invalid input
// ============================================================================

TEST_CASE("MediaPipelinePlanner_InvalidInput_FailsValidation") {
        MediaPipelineConfig empty;
        MediaPipelineConfig out;
        String              diag;
        Error               err = MediaPipelinePlanner::plan(empty, &out, {}, &diag);
        CHECK(err.isError());
        CHECK(out.stages().isEmpty());
        CHECK(!diag.isEmpty());
}

TEST_CASE("MediaPipelinePlanner_NullOutPointer_ReturnsInvalid") {
        const MediaPipelineConfig in = makeSrcSinkConfig(PixelFormat::RGBA8_sRGB, PixelFormat::RGBA8_sRGB);
        Error                     err = MediaPipelinePlanner::plan(in, nullptr);
        CHECK(err == Error::Invalid);
}

// ============================================================================
// MediaPipelineConfig::resolved() helper
// ============================================================================

TEST_CASE("MediaPipelineConfig_resolved_DelegatesToPlanner") {
        const MediaPipelineConfig in =
                makeSrcSinkConfig(PixelFormat::RGBA8_sRGB, PixelFormat::YUV8_420_SemiPlanar_Rec709);

        Error               err;
        MediaPipelineConfig resolvedCfg = in.resolved(&err);
        CHECK(err.isOk());
        CHECK(resolvedCfg.stages().size() == 3);
        CHECK(stageTypeAt(resolvedCfg, 2, "CSC"));
}

TEST_CASE("MediaPipelineConfig_resolved_PropagatesPlannerError") {
        // Empty config — planner returns Invalid and resolved()
        // surfaces it through the err out-param.
        MediaPipelineConfig empty;
        Error               err;
        MediaPipelineConfig resolvedCfg = empty.resolved(&err);
        CHECK(err.isError());
        CHECK(resolvedCfg.stages().isEmpty());
}

// ============================================================================
// Simultaneous pixel + frame-rate gap — documented limitation
// ============================================================================

TEST_CASE("MediaPipelinePlanner_SimultaneousPixelAndRateGap_FailsWithHint") {
        // Source emits 30 fps RGBA8, sink demands 24 fps NV12.  CSC
        // rejects rate mismatches, FrameSync rejects pixel mismatches,
        // and the codec-transitive path doesn't apply to uncompressed
        // endpoints.  The planner should fail with NotSupported and
        // surface the "insert an explicit intermediate" hint so the
        // caller knows the gap is not auto-solvable in v1.
        MediaPipelineConfig cfg;

        MediaPipelineConfig::Stage src;
        src.name = "src";
        src.type = "PlannerSyntheticSrc";
        src.role = MediaPipelineConfig::StageRole::Source;
        src.config.set(PlannerProducedDesc, PixelFormat(PixelFormat::RGBA8_sRGB));
        src.config.set(PlannerProducedRate, FrameRate(FrameRate::FPS_30));
        cfg.addStage(src);

        MediaPipelineConfig::Stage sink;
        sink.name = "sink";
        sink.type = "PlannerSyntheticSink";
        sink.role = MediaPipelineConfig::StageRole::Sink;
        sink.config.set(PlannerAcceptedDesc, PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
        sink.config.set(PlannerAcceptedRate, FrameRate(FrameRate::FPS_24));
        cfg.addStage(sink);

        cfg.addRoute("src", "sink");

        MediaPipelineConfig out;
        String              diag;
        Error               err = MediaPipelinePlanner::plan(cfg, &out, {}, &diag);
        CHECK(err == Error::NotSupported);
        CHECK(out.stages().isEmpty());

        // Diagnostic must spell out the gap and surface the hint so
        // the caller can act without reading the planner source.
        CHECK(diag.contains("RGBA8_sRGB"));
        CHECK(diag.contains("YUV8_420_SemiPlanar_Rec709"));
        CHECK(diag.contains("frame rate and pixel format"));
        CHECK(diag.contains("CSC"));
        CHECK(diag.contains("FrameSync"));
}

// ============================================================================
// Codec-transitive two-hop — full chain through the planner
// ============================================================================

TEST_CASE("MediaPipelinePlanner_CodecTransitive_InsertsDecoderEncoderPair") {
        // Source produces H264, sink accepts HEVC.  The single-hop
        // search finds no bridge (neither VideoDecoder nor
        // VideoEncoder satisfies compressed→compressed on its own),
        // so the planner must invoke the codec-transitive two-hop
        // path and splice in (VideoDecoder, VideoEncoder).
        //
        // The test is gated on both codecs having a registered
        // decoder / encoder respectively; if either is missing in
        // this build, the codec-transitive path cannot solve the
        // gap and the test is skipped.
        const VideoCodec h264 = value(VideoCodec::lookup("H264"));
        const VideoCodec hevc = value(VideoCodec::lookup("HEVC"));
        if (!h264.canDecode() || !hevc.canEncode()) {
                INFO("H264 decoder or HEVC encoder not registered in "
                     "this build; skipping.");
                return;
        }

        const MediaPipelineConfig in = makeSrcSinkConfig(PixelFormat::H264, PixelFormat::HEVC);

        MediaPipelineConfig out;
        String              diag;
        Error               err = MediaPipelinePlanner::plan(in, &out, {}, &diag);
        REQUIRE(err.isOk());
        CHECK(diag.isEmpty());

        // Two bridge stages spliced in between src and sink →
        // original 2 stages + 2 bridges = 4 stages, 3 routes.
        REQUIRE(out.stages().size() == 4);
        REQUIRE(out.routes().size() == 3);

        // The two bridges should be a VideoDecoder followed by a
        // VideoEncoder (in that order).  Stages are appended to the
        // output config as bridges are spliced, so indices 2 and 3
        // hold the decoder and encoder respectively.
        CHECK(stageTypeAt(out, 2, "VideoDecoder"));
        CHECK(stageTypeAt(out, 3, "VideoEncoder"));

        // Routes flow src → decoder → encoder → sink.
        CHECK(out.routes()[0].from == "src");
        CHECK(out.routes()[0].to == out.stages()[2].name);
        CHECK(out.routes()[1].from == out.stages()[2].name);
        CHECK(out.routes()[1].to == out.stages()[3].name);
        CHECK(out.routes()[2].from == out.stages()[3].name);
        CHECK(out.routes()[2].to == "sink");
}
