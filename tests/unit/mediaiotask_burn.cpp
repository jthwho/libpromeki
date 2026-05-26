/**
 * @file      tests/mediaiotask_burn.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Verifies that @ref BurnMediaIO passes the source frame's ANC
 * payloads, metadata, frame-level capture timestamp, and config-update
 * delta through to the output Frame even when the text burn-in
 * mutates the video payload.
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

        UncompressedVideoPayload::Ptr makeVideoPayload(uint32_t w = 64, uint32_t h = 32) {
                return UncompressedVideoPayload::allocate(
                        ImageDesc(Size2Du32(w, h), PixelFormat(PixelFormat::RGB8_sRGB)));
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

        MediaDesc makeVideoDesc(uint32_t w = 64, uint32_t h = 32) {
                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::FPS_30));
                md.imageList().pushToBack(ImageDesc(Size2Du32(w, h), PixelFormat(PixelFormat::RGB8_sRGB)));
                return md;
        }

} // namespace

TEST_CASE("BurnMediaIO: factory is registered") {
        const MediaIOFactory *f = MediaIOFactory::findByName("Burn");
        REQUIRE(f != nullptr);
        CHECK(f->canBeTransform());
}

TEST_CASE("BurnMediaIO: passes ANC payload, metadata, captureTime, and configUpdate through") {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Burn");
        cfg.set(MediaConfig::VideoBurnEnabled, true);
        cfg.set(MediaConfig::VideoBurnText, String("hello"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc upstream = makeVideoDesc();
        REQUIRE(io->setPendingMediaDesc(upstream).isOk());
        REQUIRE(io->open().wait().isOk());
        REQUIRE(io->sink(0) != nullptr);
        REQUIRE(io->source(0) != nullptr);

        Frame in;
        in.addPayload(makeVideoPayload());
        in.addPayload(makeCea708AncPayload());
        in.metadata().set(Metadata::FrameRate, Variant(FrameRate(FrameRate::FPS_30)));
        in.metadata().set(Metadata::Title, Variant(String("source-title")));

        const TimeStamp      tsRaw = TimeStamp::now();
        const MediaTimeStamp captureTs(tsRaw, ClockDomain::SystemMonotonic);
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

        // --- ANC payloads pass through unchanged ---
        AncPayload::PtrList ancOut = out.ancPayloads();
        REQUIRE(ancOut.size() == 1);
        REQUIRE(ancOut[0].isValid());
        REQUIRE(ancOut[0]->packets().size() == 1);
        CHECK(ancOut[0]->packets()[0].format().id() == AncFormat::Cea708);

        // --- Source metadata is preserved ---
        REQUIRE(out.metadata().contains(Metadata::Title));
        CHECK(out.metadata().get(Metadata::Title).get<String>() == String("source-title"));

        // --- captureTime is preserved ---
        CHECK(out.captureTime() == captureTs);

        // --- configUpdate is preserved ---
        CHECK(out.configUpdate().getAs<String>(MediaConfig::VideoBurnText) == String("delta-test"));

        // --- Video payload still present ---
        VideoPayload::PtrList vids = out.videoPayloads();
        REQUIRE(vids.size() == 1);
        REQUIRE(vids[0].isValid());

        REQUIRE(io->close().wait().isOk());
        delete io;
}

TEST_CASE("BurnMediaIO: passthrough is preserved with burn disabled") {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Burn");
        cfg.set(MediaConfig::VideoBurnEnabled, false);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc upstream = makeVideoDesc();
        REQUIRE(io->setPendingMediaDesc(upstream).isOk());
        REQUIRE(io->open().wait().isOk());

        Frame in;
        in.addPayload(makeVideoPayload());
        in.addPayload(makeCea708AncPayload());
        in.metadata().set(Metadata::FrameRate, Variant(FrameRate(FrameRate::FPS_30)));

        const TimeStamp      tsRaw = TimeStamp::now();
        const MediaTimeStamp captureTs(tsRaw, ClockDomain::SystemMonotonic);
        in.setCaptureTime(captureTs);

        REQUIRE(io->sink(0)->writeFrame(in).wait().isOk());

        MediaIORequest readReq = io->source(0)->readFrame();
        REQUIRE(readReq.wait().isOk());
        const auto *cr = readReq.commandAs<MediaIOCommandRead>();
        REQUIRE(cr != nullptr);
        REQUIRE(cr->frame.isValid());
        const Frame &out = cr->frame;

        CHECK(out.ancPayloads().size() == 1);
        CHECK(out.captureTime() == captureTs);
        CHECK(out.videoPayloads().size() == 1);

        REQUIRE(io->close().wait().isOk());
        delete io;
}
