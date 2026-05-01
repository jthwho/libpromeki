/**
 * @file      mediaio_negotiation.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for the MediaIO introspection (describe) and negotiation
 * (proposeInput / proposeOutput) APIs introduced in Phase 1 of the
 * pipeline planner work.  Covers:
 *
 *   - Default behaviour of MediaIO's three negotiation virtuals against
 *     real registered backends (TPG, Inspector, etc.).
 *   - A synthetic backend (registered once at file scope) that
 *     overrides every virtual to verify the override pathway works
 *     end-to-end through MediaIO::describe / proposeInput /
 *     proposeOutput.
 */

#include <cstdio>
#include <doctest/doctest.h>

#include <promeki/audiodesc.h>
#include <promeki/dir.h>
#include <promeki/enums.h>
#include <promeki/filepath.h>
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
#include <promeki/pixelformat.h>
#include <promeki/set.h>
#include <promeki/size2d.h>

using namespace promeki;

namespace {

        // Returns a scratch path with the requested extension under
        // @ref Dir::temp.  Multi-port refactor pushed the negotiation
        // surface to MediaIOSource / MediaIOSink, both of which only
        // exist after the backend's open() runs (the task creates
        // ports inside executeCmd(Open)).  File-based backends will
        // create files on open, so we do open / probe / close /
        // remove inside each test to stay clean.
        String scratchPathWithExt(const char *ext) {
                FilePath p = Dir::temp().path() / (String("mediaio_negotiation_test.") + ext);
                return p.toString();
        }

        // Backwards-compat shims for the test bodies that were updated
        // during the multi-port refactor.  proposeInput / proposeOutput
        // are now task-level forwarders on MediaIO again, so these
        // wrappers just delegate; the explicit open/close dance is
        // unnecessary because the task answers pre-open.
        Error proposeInputOpen(MediaIO *io, const MediaDesc &offered, MediaDesc *preferred) {
                return io->proposeInput(offered, preferred);
        }

        Error proposeOutputOpen(MediaIO *io, const MediaDesc &requested, MediaDesc *achievable) {
                return io->proposeOutput(requested, achievable);
        }

} // namespace

// ============================================================================
// Synthetic test backend
// ============================================================================
//
// Registered at static-init time as "NegotiationProbe".  Overrides
// every Phase-1 virtual so the override pathway can be exercised end
// to end through MediaIO::describe / proposeInput / proposeOutput.

namespace {

        class NegotiationProbeMediaIO : public InlineMediaIO {
                                PROMEKI_OBJECT(NegotiationProbeMediaIO, InlineMediaIO)
                        public:
                                NegotiationProbeMediaIO(ObjectBase *parent = nullptr) : InlineMediaIO(parent) {}
                                ~NegotiationProbeMediaIO() override {
                                        if (isOpen()) (void)close().wait();
                                }

                                Error describe(MediaIODescription *out) const override {
                                        // Chain to the base so identity fields
                                        // (backendName, role flags, instance id)
                                        // come from the registered factory; this
                                        // override only adds format-specific fields
                                        // on top.
                                        Error baseErr = MediaIO::describe(out);
                                        if (baseErr.isError()) return baseErr;

                                        MediaDesc preferred;
                                        preferred.setFrameRate(FrameRate(FrameRate::FPS_30));
                                        preferred.imageList().pushToBack(
                                                ImageDesc(Size2Du32(1280, 720), PixelFormat(PixelFormat::RGBA8_sRGB)));
                                        out->setPreferredFormat(preferred);
                                        out->producibleFormats().pushToBack(preferred);

                                        MediaDesc alt;
                                        alt.setFrameRate(FrameRate(FrameRate::FPS_25));
                                        alt.imageList().pushToBack(
                                                ImageDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::RGBA8_sRGB)));
                                        out->producibleFormats().pushToBack(alt);

                                        out->acceptableFormats().pushToBack(preferred);
                                        out->setCanSeek(false);
                                        out->setFrameCount(MediaIODescription::FrameCountInfinite);
                                        return Error::Ok;
                                }

                                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override {
                                        // Accept only RGBA8_sRGB images; for anything
                                        // else, narrow to RGBA8_sRGB at the offered
                                        // raster.
                                        if (preferred == nullptr) return Error::Invalid;
                                        if (offered.imageList().isEmpty()) {
                                                return Error::NotSupported;
                                        }
                                        const PixelFormat &pd = offered.imageList()[0].pixelFormat();
                                        if (pd.id() == PixelFormat::RGBA8_sRGB) {
                                                *preferred = offered;
                                                return Error::Ok;
                                        }
                                        MediaDesc want = offered;
                                        ImageDesc img(offered.imageList()[0].size(),
                                                      PixelFormat(PixelFormat::RGBA8_sRGB));
                                        img.setVideoScanMode(offered.imageList()[0].videoScanMode());
                                        want.imageList().clear();
                                        want.imageList().pushToBack(img);
                                        *preferred = want;
                                        return Error::Ok;
                                }

                                Error proposeOutput(const MediaDesc &requested,
                                                    MediaDesc       *achievable) const override {
                                        if (achievable == nullptr) return Error::Invalid;
                                        // Synthetic source can switch raster freely as
                                        // long as the requested pixel format is RGBA8_sRGB.
                                        if (requested.imageList().isEmpty()) return Error::NotSupported;
                                        const PixelFormat &pd = requested.imageList()[0].pixelFormat();
                                        if (pd.id() != PixelFormat::RGBA8_sRGB) return Error::NotSupported;
                                        *achievable = requested;
                                        return Error::Ok;
                                }

                        protected:
                                // Both directions live on the same backend so each
                                // test can exercise either by setting OpenMode before
                                // calling open().
                                Error executeCmd(MediaIOCommandOpen &cmd) override {
                                        MediaIOPortGroup *group = addPortGroup("probe");
                                        if (group == nullptr) return Error::Invalid;
                                        Enum modeEnum =
                                                cmd.config.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
                                        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();
                                        if (isWrite) {
                                                if (addSink(group, MediaDesc()) == nullptr) return Error::Invalid;
                                        } else {
                                                if (addSource(group, MediaDesc()) == nullptr) return Error::Invalid;
                                        }
                                        return Error::Ok;
                                }
                                Error executeCmd(MediaIOCommandClose &cmd) override {
                                        (void)cmd;
                                        return Error::Ok;
                                }
        };

        class NegotiationProbeFactory : public MediaIOFactory {
                public:
                        NegotiationProbeFactory() = default;

                        String name() const override { return String("NegotiationProbe"); }
                        String description() const override {
                                return String("Synthetic backend for proposeInput / "
                                              "proposeOutput / describe coverage.");
                        }
                        bool canBeSource() const override { return true; }
                        bool canBeSink() const override { return true; }
                        bool canBeTransform() const override { return true; }

                        MediaIO *create(const Config &config, ObjectBase *parent) const override {
                                auto *io = new NegotiationProbeMediaIO(parent);
                                io->setConfig(config);
                                return io;
                        }
        };

        PROMEKI_REGISTER_MEDIAIO_FACTORY(NegotiationProbeFactory)

        // ----- helpers used by both default-behaviour and override tests -----

        MediaIO *makeProbe() {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "NegotiationProbe");
                return MediaIO::create(cfg);
        }

        MediaDesc makeRgbaDesc(uint32_t w, uint32_t h) {
                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::FPS_30));
                md.imageList().pushToBack(ImageDesc(Size2Du32(w, h), PixelFormat(PixelFormat::RGBA8_sRGB)));
                return md;
        }

        MediaDesc makeNv12Desc(uint32_t w, uint32_t h) {
                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::FPS_30));
                md.imageList().pushToBack(
                        ImageDesc(Size2Du32(w, h), PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709)));
                return md;
        }

} // namespace

// ============================================================================
// Default behaviour (MediaIO base impls)
// ============================================================================

TEST_CASE("MediaIO_describe_PopulatesIdentityAndRoles") {
        // Inspector is a passthrough sink that doesn't override
        // describe().  The MediaIO wrapper must still fill in
        // identity and role flags from the registered MediaIOFactory
        // plus the live instance identity (name).
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "Inspector");
        cfg.set(MediaConfig::Name, String("inspector-probe"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaIODescription d;
        CHECK(io->describe(&d) == Error::Ok);

        CHECK(d.backendName() == "Inspector");
        CHECK_FALSE(d.backendDescription().isEmpty());
        CHECK_FALSE(d.canBeSource());
        CHECK(d.canBeSink());
        CHECK_FALSE(d.canBeTransform());
        CHECK(io->name() == d.name());

        // Inspector has no describe() override and isn't open, so
        // it carries no probe data.
        CHECK(d.producibleFormats().isEmpty());
        CHECK(d.acceptableFormats().isEmpty());
        CHECK(d.frameCount() == MediaIODescription::FrameCountUnknown);
        CHECK(d.probeStatus() == Error::Ok);

        delete io;
}

TEST_CASE("MediaIO_describe_TPGAdvertisesProducedShape") {
        // TPG implements describe() — it advertises the MediaDesc
        // it would emit given its configured VideoFormat /
        // VideoPixelFormat without needing to be opened.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaIODescription d;
        REQUIRE(io->describe(&d) == Error::Ok);
        CHECK(d.canBeSource());
        CHECK(d.preferredFormat().isValid());
        CHECK(d.frameCount() == MediaIODescription::FrameCountInfinite);
        REQUIRE(!d.producibleFormats().isEmpty());

        delete io;
}

TEST_CASE("MediaIO_proposeInput_DefaultAcceptsAnything") {
        // Inspector is a registered passthrough sink with the default
        // proposeInput impl — it should accept whatever is offered
        // verbatim.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "Inspector");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeNv12Desc(1920, 1080);
        MediaDesc       preferred;
        CHECK(proposeInputOpen(io, offered, &preferred) == Error::Ok);
        CHECK(preferred == offered);

        delete io;
}

TEST_CASE("MediaIO_proposeOutput_DefaultReturnsNotSupported") {
        // The base MediaIO::proposeOutput default reports that
        // sources have no flexibility; the planner uses this to
        // decide it must insert a bridge rather than re-configuring
        // the source.  Inspector / Burn / FrameBridge — pure
        // passthroughs that don't override proposeOutput — exhibit
        // the default behaviour.  (TPG overrides it; see the
        // dedicated TPG test below.)
        //
        // Inspector is sink-only: when opened as a sink, source(0)
        // is null and the wrapper returns NotSupported — exactly the
        // pre-port semantics this test was checking.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "Inspector");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc requested = makeRgbaDesc(640, 480);
        MediaDesc       achievable;
        CHECK(proposeOutputOpen(io, requested, &achievable) == Error::NotSupported);
        // The default also clears the achievable result so callers
        // don't accidentally consume stale data.
        CHECK_FALSE(achievable.isValid());

        delete io;
}

TEST_CASE("MediaIO_proposeOutput_TPGAcceptsRequest") {
        // TPG's override accepts any uncompressed shape and echoes
        // it back as achievable — the planner can therefore re-
        // configure TPG to produce whatever a sink prefers without
        // inserting a CSC bridge.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc requested = makeRgbaDesc(640, 480);
        MediaDesc       achievable;
        CHECK(proposeOutputOpen(io, requested, &achievable) == Error::Ok);
        CHECK(achievable == requested);

        delete io;
}

TEST_CASE("MediaIO_proposeOutput_TPGRejectsCompressed") {
        // TPG synthesises uncompressed only — a compressed request
        // must be refused so the planner inserts a VideoEncoder
        // bridge instead of asking TPG to magically emit H.264.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc requested;
        requested.setFrameRate(FrameRate(FrameRate::FPS_30));
        requested.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::H264)));
        MediaDesc achievable;
        CHECK(proposeOutputOpen(io, requested, &achievable) == Error::NotSupported);

        delete io;
}

TEST_CASE("MediaIO_describe_NullOutReturnsInvalid") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        CHECK(io->describe(nullptr) == Error::Invalid);

        delete io;
}

// ============================================================================
// Synthetic backend overrides
// ============================================================================

TEST_CASE("MediaIO_describe_OverrideAddsFormatLandscape") {
        MediaIO *io = makeProbe();
        REQUIRE(io != nullptr);

        MediaIODescription d;
        REQUIRE(io->describe(&d) == Error::Ok);

        // Identity from MediaIOFactory.
        CHECK(d.backendName() == "NegotiationProbe");
        CHECK(d.canBeSource());
        CHECK(d.canBeSink());
        CHECK(d.canBeTransform());

        // Format landscape from the override.
        REQUIRE(d.producibleFormats().size() == 2);
        CHECK(d.preferredFormat().isValid());
        CHECK(d.preferredFormat().imageList()[0].size() == Size2Du32(1280, 720));
        CHECK(d.acceptableFormats().size() == 1);
        CHECK(d.frameCount() == MediaIODescription::FrameCountInfinite);

        delete io;
}

TEST_CASE("MediaIO_proposeInput_OverrideAcceptsExpected") {
        MediaIO *io = makeProbe();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeRgbaDesc(1280, 720);
        MediaDesc       preferred;
        CHECK(proposeInputOpen(io, offered, &preferred) == Error::Ok);
        CHECK(preferred == offered);

        delete io;
}

TEST_CASE("MediaIO_proposeInput_OverrideRequestsConversion") {
        // Probe accepts only RGBA8_sRGB.  An NV12 input must come
        // back with the preferred desc rewritten to RGBA8_sRGB but
        // the same raster + frame rate.
        MediaIO *io = makeProbe();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeNv12Desc(1920, 1080);
        MediaDesc       preferred;
        REQUIRE(proposeInputOpen(io, offered, &preferred) == Error::Ok);

        REQUIRE(!preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat().id() == PixelFormat::RGBA8_sRGB);
        CHECK(preferred.imageList()[0].size() == offered.imageList()[0].size());
        CHECK(preferred.frameRate() == offered.frameRate());

        delete io;
}

TEST_CASE("MediaIO_proposeInput_OverrideRejects") {
        // Empty image list -> NotSupported.
        MediaIO *io = makeProbe();
        REQUIRE(io != nullptr);

        MediaDesc empty;
        empty.setFrameRate(FrameRate(FrameRate::FPS_30));
        MediaDesc preferred;
        CHECK(proposeInputOpen(io, empty, &preferred) == Error::NotSupported);

        delete io;
}

TEST_CASE("MediaIO_proposeOutput_OverrideMatchesRequest") {
        MediaIO *io = makeProbe();
        REQUIRE(io != nullptr);

        const MediaDesc requested = makeRgbaDesc(800, 600);
        MediaDesc       achievable;
        CHECK(proposeOutputOpen(io, requested, &achievable) == Error::Ok);
        CHECK(achievable == requested);

        delete io;
}

TEST_CASE("MediaIO_proposeOutput_OverrideRejectsUnsupported") {
        MediaIO *io = makeProbe();
        REQUIRE(io != nullptr);

        // NV12 is outside the probe's producible set; it should
        // refuse rather than silently lying about what it can do.
        const MediaDesc requested = makeNv12Desc(1280, 720);
        MediaDesc       achievable;
        CHECK(proposeOutputOpen(io, requested, &achievable) == Error::NotSupported);

        delete io;
}

// ============================================================================
// MediaIOFactory::bridge field
// ============================================================================

TEST_CASE("MediaIO_proposeInput_ImageFile_DPX_PreservesBitDepth") {
        // ImageFile sink with .dpx extension: the writer currently
        // emits 8-bit RGBA, 10-bit DPX-packed RGB, and 16-bit BE RGB.
        // 12-bit has no writer support yet, so it collapses to the
        // nearest supported precision (10-bit).  The proposeInput
        // override must pick the highest-precision *supported* target
        // so the planner doesn't silently drop precision when a real
        // writer path exists.
        struct Case {
                        PixelFormat::ID source;
                        PixelFormat::ID expectedTarget;
                        const char     *label;
        };
        const Case cases[] = {
                {PixelFormat::RGB8_sRGB, PixelFormat::RGBA8_sRGB, "8-bit  source -> 8-bit  DPX target"},
                {PixelFormat::RGB10_LE_sRGB, PixelFormat::RGB10_DPX_sRGB, "10-bit source -> 10-bit DPX target"},
                {PixelFormat::RGB12_LE_sRGB, PixelFormat::RGB10_DPX_sRGB,
                 "12-bit source -> 10-bit DPX (no 12-bit writer)"},
                {PixelFormat::RGB16_LE_sRGB, PixelFormat::RGB16_BE_sRGB, "16-bit source -> 16-bit BE DPX target"},
        };
        for (const auto &c : cases) {
                INFO(c.label);
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "ImageFile");
                cfg.set(MediaConfig::Filename, scratchPathWithExt("dpx"));
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);

                MediaDesc offered;
                offered.setFrameRate(FrameRate(FrameRate::FPS_30));
                offered.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080), PixelFormat(c.source)));

                MediaDesc preferred;
                CHECK(proposeInputOpen(io, offered, &preferred) == Error::Ok);
                REQUIRE(!preferred.imageList().isEmpty());
                CHECK(preferred.imageList()[0].pixelFormat().id() == c.expectedTarget);
                delete io;
        }
}

TEST_CASE("MediaIO_proposeInput_ImageFile_JPEG_DropsToEightBit") {
        // JPEG is 8-bit only; 10/12/16-bit sources have to be
        // narrowed.  The override picks YUV when the source is YUV
        // and RGB otherwise so the inserted CSC stays inside the
        // matching colour family.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "ImageFile");
        cfg.set(MediaConfig::Filename, scratchPathWithExt("jpg"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc rgbOffered;
        rgbOffered.setFrameRate(FrameRate(FrameRate::FPS_30));
        rgbOffered.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::RGB10_LE_sRGB)));
        MediaDesc rgbPreferred;
        REQUIRE(proposeInputOpen(io, rgbOffered, &rgbPreferred) == Error::Ok);
        CHECK(rgbPreferred.imageList()[0].pixelFormat().id() == PixelFormat::RGBA8_sRGB);

        MediaDesc yuvOffered;
        yuvOffered.setFrameRate(FrameRate(FrameRate::FPS_30));
        yuvOffered.imageList().pushToBack(
                ImageDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::YUV10_422_Planar_LE_Rec709)));
        MediaDesc yuvPreferred;
        REQUIRE(proposeInputOpen(io, yuvOffered, &yuvPreferred) == Error::Ok);
        CHECK(yuvPreferred.imageList()[0].pixelFormat().id() == PixelFormat::YUV8_422_Planar_Rec709);

        delete io;
}

TEST_CASE("MediaIO_proposeInput_AudioFile_BWF_PassesThroughIntegerPCM") {
        // For PCM-friendly containers (.wav, .bwf, .aiff, .flac)
        // the writer passes integer / float source data types
        // through verbatim so bit depth is preserved.  Unsupported
        // formats fall back to PCMI_S24LE — the production-typical
        // sample shape.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioFile");
        cfg.set(MediaConfig::Filename, scratchPathWithExt("bwf"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        struct Case {
                        AudioFormat::ID source;
                        AudioFormat::ID expected;
                        const char     *label;
        };
        const Case cases[] = {
                {AudioFormat::PCMI_S16LE, AudioFormat::PCMI_S16LE, "S16 source stays S16"},
                {AudioFormat::PCMI_S24LE, AudioFormat::PCMI_S24LE, "S24 source stays S24"},
                {AudioFormat::PCMI_Float32LE, AudioFormat::PCMI_Float32LE, "Float source stays Float"},
                // Non-PCM-friendly source (e.g. PCMI_U8) should be
                // upgraded to S24LE since BWF can't store U8 directly.
                {AudioFormat::PCMI_U8, AudioFormat::PCMI_S24LE, "U8 source upgraded to S24"},
        };
        for (const auto &c : cases) {
                INFO(c.label);
                MediaDesc offered;
                offered.setFrameRate(FrameRate(FrameRate::FPS_30));
                AudioDesc ad;
                ad.setSampleRate(48000.0f);
                ad.setChannels(2);
                ad.setFormat(c.source);
                offered.audioList().pushToBack(ad);

                MediaDesc preferred;
                CHECK(proposeInputOpen(io, offered, &preferred) == Error::Ok);
                REQUIRE(!preferred.audioList().isEmpty());
                CHECK(preferred.audioList()[0].format().id() == c.expected);
        }
        delete io;
}

TEST_CASE("MediaIO_proposeInput_AudioFile_OGG_AlwaysFloat") {
        // OGG / Opus / Vorbis are float-pipeline codecs — even an
        // S16 source is upgraded to Float32 to skip libsndfile's
        // internal int↔float round trip.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioFile");
        cfg.set(MediaConfig::Filename, scratchPathWithExt("ogg"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc offered;
        offered.setFrameRate(FrameRate(FrameRate::FPS_30));
        AudioDesc ad;
        ad.setSampleRate(48000.0f);
        ad.setChannels(2);
        ad.setFormat(AudioFormat::PCMI_S16LE);
        offered.audioList().pushToBack(ad);

        MediaDesc preferred;
        REQUIRE(proposeInputOpen(io, offered, &preferred) == Error::Ok);
        CHECK(preferred.audioList()[0].format().id() == AudioFormat::PCMI_Float32LE);
        delete io;
}

TEST_CASE("MediaIO_proposeInput_QuickTime_PreservesBitDepthAndChroma") {
        // QuickTime's writer accepts a curated set of FourCCs.  The
        // proposeInput override must (a) reject unsupported
        // PixelFormats and rewrite them to the closest supported one,
        // and (b) preserve bit depth and chroma subsampling where
        // possible — a 10-bit YUV 4:2:2 source should round-trip
        // through v210, not be downconverted to NV12.
        struct Case {
                        PixelFormat::ID source;
                        PixelFormat::ID expectedTarget;
                        const char     *label;
        };
        const Case cases[] = {
                // 10-bit 4:2:2 YUV stays 10-bit 4:2:2 via v210.
                {PixelFormat::YUV10_422_v210_Rec709, PixelFormat::YUV10_422_v210_Rec709,
                 "v210 already supported, pass-through"},
                // 8-bit 4:2:2 YUV stays 4:2:2 via YUYV.
                {PixelFormat::YUV8_422_Rec709, PixelFormat::YUV8_422_Rec709, "YUYV already supported, pass-through"},
                // 8-bit 4:2:0 NV12 stays 4:2:0 NV12.
                {PixelFormat::YUV8_420_SemiPlanar_Rec709, PixelFormat::YUV8_420_SemiPlanar_Rec709,
                 "NV12 already supported, pass-through"},
                // 10-bit YUV 4:4:4 (unsupported by QT) drops to v210
                // (4:2:2) — chroma loss is unavoidable, but bit depth
                // is preserved.
                {PixelFormat::YUV10_444_Planar_LE_Rec709, PixelFormat::YUV10_422_v210_Rec709,
                 "10-bit 4:4:4 -> 10-bit 4:2:2 (preserve bits, drop chroma)"},
                // 8-bit RGB stays 8-bit RGB.
                {PixelFormat::RGBA8_sRGB, PixelFormat::RGBA8_sRGB, "RGBA8 already supported, pass-through"},
                // 16-bit RGB (no QT support) drops to 8-bit RGBA.
                {PixelFormat::RGBA16_LE_sRGB, PixelFormat::RGBA8_sRGB,
                 "16-bit RGBA -> 8-bit RGBA (no QT 16-bit RGBA support)"},
        };
        for (const auto &c : cases) {
                INFO(c.label);
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "QuickTime");
                cfg.set(MediaConfig::Filename, scratchPathWithExt("mov"));
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);

                MediaDesc offered;
                offered.setFrameRate(FrameRate(FrameRate::FPS_30));
                offered.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080), PixelFormat(c.source)));

                MediaDesc preferred;
                CHECK(proposeInputOpen(io, offered, &preferred) == Error::Ok);
                REQUIRE(!preferred.imageList().isEmpty());
                CHECK(preferred.imageList()[0].pixelFormat().id() == c.expectedTarget);
                delete io;
        }
}

TEST_CASE("MediaIOFactory_BridgeOverrideOnlyOnTransforms") {
        // The MediaIOFactory::bridge() default returns false; the five
        // transform backends the planner can insert (CSC, FrameSync,
        // SRC, VideoDecoder, VideoEncoder) override it.  Every other
        // backend — sources, sinks, pure passthroughs, the synthetic
        // NegotiationProbe defined in this file — leaves the default
        // in place.  Probe each factory by calling bridge() with empty
        // inputs and observing whether the override took control of
        // the answer (which it can do by returning true on these
        // inputs, populating outConfig, or just by being a transform).
        const Set<String> bridgeCapable{"CSC", "FrameSync", "SRC", "VideoDecoder", "VideoEncoder"};

        const auto &factories = MediaIOFactory::registeredFactories();
        REQUIRE(!factories.isEmpty());
        for (const MediaIOFactory *f : factories) {
                if (f == nullptr) continue;
                INFO("Backend: ", f->name().cstr());
                if (bridgeCapable.contains(f->name())) {
                        // Transform backends declare canBeTransform.
                        CHECK(f->canBeTransform());
                } else {
                        // Non-transform backends leave the default
                        // bridge() in place — calling it with empty
                        // inputs returns false.
                        MediaIO::Config cfg;
                        int             cost = 0;
                        CHECK_FALSE(f->bridge(MediaDesc(), MediaDesc(), &cfg, &cost));
                }
        }
}
