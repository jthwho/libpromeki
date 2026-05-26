/**
 * @file      tests/cscmediaio.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Verifies that @ref CscMediaIO passes ANC payloads, frame-level
 * @c captureTime, the per-frame @c configUpdate, and frame metadata
 * through to the output Frame across a pixel-format hop.  Regression
 * test for the SEI-captions-vs-SubtitleBurn drop: when the planner
 * splices a CSC in after a paint-engine-requiring transform (e.g.
 * SubtitleBurn) on the way to an NVENC encoder, the CEA-708 ANC
 * packets the encoder turns into HLS SEI captions must survive the
 * colour-space conversion.
 */

#include <doctest/doctest.h>
#include <promeki/ancdesc.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/clockdomain.h>
#include <promeki/enums_video.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/st291packet.h>
#include <promeki/timestamp.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        UncompressedVideoPayload::Ptr makeVideoPayload(const PixelFormat &pf, uint32_t w = 64, uint32_t h = 32) {
                return UncompressedVideoPayload::allocate(ImageDesc(Size2Du32(w, h), pf));
        }

        AncPayload::Ptr makeCea708AncPayload(uint32_t w = 64, uint32_t h = 32) {
                AncDesc desc(Size2Du32(w, h), VideoScanMode::Progressive, FrameRate::FPS_30);
                AncPayload::Ptr ap = AncPayload::Ptr::create(desc);
                List<uint16_t>  udw;
                udw.pushToBack(uint16_t(0x10));
                udw.pushToBack(uint16_t(0x20));
                ap.modify()->addPacket(St291Packet::build(AncFormat(AncFormat::Cea708), udw, 11));
                return ap;
        }

        MediaDesc makeVideoDesc(const PixelFormat &pf, uint32_t w = 64, uint32_t h = 32) {
                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::FPS_30));
                md.imageList().pushToBack(ImageDesc(Size2Du32(w, h), pf));
                return md;
        }

} // namespace

TEST_CASE("CscMediaIO: factory is registered") {
        const MediaIOFactory *f = MediaIOFactory::findByName("CSC");
        REQUIRE(f != nullptr);
        CHECK(f->canBeTransform());
}

TEST_CASE("CscMediaIO: ANC payload, captureTime, configUpdate, and metadata survive a pixel-format hop") {
        // sRGB -> Rec709 4:2:0 SemiPlanar is the exact path the
        // planner takes when SubtitleBurn (paint-engine input) feeds
        // an NVENC H.264 encoder (NV12 input) — the production case
        // that triggered the SEI-caption regression.
        const PixelFormat srcPd(PixelFormat::RGBA8_sRGB);
        const PixelFormat dstPd(PixelFormat::YUV8_420_SemiPlanar_Rec709);

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("CSC");
        cfg.set(MediaConfig::OutputPixelFormat, dstPd);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc upstream = makeVideoDesc(srcPd);
        REQUIRE(io->setPendingMediaDesc(upstream).isOk());
        REQUIRE(io->open().wait().isOk());
        REQUIRE(io->sink(0) != nullptr);
        REQUIRE(io->source(0) != nullptr);

        Frame in;
        in.addPayload(makeVideoPayload(srcPd));
        in.addPayload(makeCea708AncPayload());
        in.metadata().set(Metadata::FrameRate, Variant(FrameRate(FrameRate::FPS_30)));

        const TimeStamp        tsRaw = TimeStamp::now();
        const MediaTimeStamp   captureTs(tsRaw, ClockDomain::SystemMonotonic);
        in.setCaptureTime(captureTs);

        MediaConfig cfgDelta;
        cfgDelta.set(MediaConfig::VideoBurnText, String("delta-test"));
        in.setConfigUpdate(cfgDelta);

        REQUIRE(io->sink(0)->writeFrame(in).wait().isOk());

        MediaIORequest readReq = io->source(0)->readFrame();
        REQUIRE(readReq.wait().isOk());
        const auto *cr = readReq.commandAs<MediaIOCommandRead>();
        REQUIRE(cr != nullptr);
        REQUIRE(cr->frame.isValid());
        const Frame &out = cr->frame;

        // --- ANC payloads pass through unchanged.  This is the
        //     regression: dropping ANC at the CSC stage was breaking
        //     YouTube CC when SubtitleBurn was added to the pipeline.
        AncPayload::PtrList ancOut = out.ancPayloads();
        REQUIRE(ancOut.size() == 1);
        REQUIRE(ancOut[0].isValid());
        REQUIRE(ancOut[0]->packets().size() == 1);
        CHECK(ancOut[0]->packets()[0].format().id() == AncFormat::Cea708);

        // --- captureTime is preserved verbatim ---
        CHECK(out.captureTime() == captureTs);

        // --- configUpdate is preserved verbatim ---
        CHECK(out.configUpdate().getAs<String>(MediaConfig::VideoBurnText) == String("delta-test"));

        // --- Metadata is preserved (FrameRate survives the hop —
        //     the IO write path may stamp other keys, but caller-
        //     supplied ones must not be lost).
        REQUIRE(out.metadata().contains(Metadata::FrameRate));
        CHECK(out.metadata().getAs<FrameRate>(Metadata::FrameRate) == FrameRate(FrameRate::FPS_30));

        // --- Video payload now carries the destination pixel format
        //     (i.e. the CSC actually ran).
        VideoPayload::PtrList vids = out.videoPayloads();
        REQUIRE(vids.size() == 1);
        REQUIRE(vids[0].isValid());
        const auto *uvp = vids[0]->as<UncompressedVideoPayload>();
        REQUIRE(uvp != nullptr);
        CHECK(uvp->desc().pixelFormat().id() == dstPd.id());

        REQUIRE(io->close().wait().isOk());
        delete io;
}

TEST_CASE("CscMediaIO: ANC + captureTime + configUpdate survive a no-op (matched pixel format) hop") {
        // The no-op path (input and output formats match) goes
        // through the same convertFrame code; verify it still
        // preserves everything.
        const PixelFormat pd(PixelFormat::RGBA8_sRGB);

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("CSC");
        cfg.set(MediaConfig::OutputPixelFormat, pd);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        REQUIRE(io->setPendingMediaDesc(makeVideoDesc(pd)).isOk());
        REQUIRE(io->open().wait().isOk());

        Frame in;
        in.addPayload(makeVideoPayload(pd));
        in.addPayload(makeCea708AncPayload());
        in.metadata().set(Metadata::FrameRate, Variant(FrameRate(FrameRate::FPS_30)));

        const TimeStamp      tsRaw = TimeStamp::now();
        const MediaTimeStamp captureTs(tsRaw, ClockDomain::SystemMonotonic);
        in.setCaptureTime(captureTs);

        MediaConfig cfgDelta;
        cfgDelta.set(MediaConfig::VideoBurnText, String("noop-delta"));
        in.setConfigUpdate(cfgDelta);

        REQUIRE(io->sink(0)->writeFrame(in).wait().isOk());

        MediaIORequest readReq = io->source(0)->readFrame();
        REQUIRE(readReq.wait().isOk());
        const auto *cr = readReq.commandAs<MediaIOCommandRead>();
        REQUIRE(cr != nullptr);
        REQUIRE(cr->frame.isValid());
        const Frame &out = cr->frame;

        CHECK(out.ancPayloads().size() == 1);
        CHECK(out.captureTime() == captureTs);
        CHECK(out.configUpdate().getAs<String>(MediaConfig::VideoBurnText) == String("noop-delta"));
        CHECK(out.videoPayloads().size() == 1);

        REQUIRE(io->close().wait().isOk());
        delete io;
}
