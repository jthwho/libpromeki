/**
 * @file      tests/subtitleburnmediaio.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Verifies that @ref SubtitleBurnMediaIO passes the source subtitle
 * data (ANC payloads + Metadata::Subtitle), frame-level capture
 * timestamp, and config-update delta through to the output Frame so
 * downstream consumers can compare the source cue against the burned-
 * in pixels.
 */

#include <doctest/doctest.h>
#include <promeki/ancdesc.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/anctranslator.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708encoder.h>
#include <promeki/clockdomain.h>
#include <promeki/duration.h>
#include <promeki/enumlist.h>
#include <promeki/enums_anc.h>
#include <promeki/enums_subtitle.h>
#include <promeki/enums_video.h>
#include <promeki/frame.h>
#include <promeki/framenumber.h>
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
#include <promeki/mediaiostats.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/st291packet.h>
#include <promeki/subtitle.h>
#include <promeki/subtitleburnmediaio.h>
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

        Subtitle makeCue() {
                using ClockDur = TimeStamp::Value::duration;
                TimeStamp start(TimeStamp::Value(std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(0))));
                TimeStamp end(TimeStamp::Value(std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(1000))));
                return Subtitle(start, end, "Hello");
        }

        /// @brief Builds an AncPayload carrying a real CEA-708 CDP whose
        ///        @c cc_data list is @p triples (typically a frame's
        ///        worth of DTVCC pulled from @ref Cea708Encoder).  Goes
        ///        through @ref AncTranslator::build so the bytes match
        ///        what the TPG would emit on the wire — exercising the
        ///        SubtitleBurn ANC path against the real packet shape
        ///        instead of a hand-rolled fake.
        AncPayload::Ptr makeCea708CdpAncPayload(const Cea708Cdp::CcDataList &triples, uint32_t w = 64,
                                                 uint32_t h = 32) {
                AncDesc         desc(Size2Du32(w, h), VideoScanMode::Progressive, FrameRate::FPS_30);
                AncPayload::Ptr ap = AncPayload::Ptr::create(desc);

                Cea708Cdp cdp(0, triples, 0);
                AncTranslator           t;
                AncTranslator::PacketsResult r = t.build(Variant(cdp), AncFormat(AncFormat::Cea708),
                                                    AncTransport(AncTransport::St291));
                REQUIRE(r.second().isOk());
                for (const AncPacket &pkt : r.first()) ap.modify()->addPacket(pkt);
                return ap;
        }

} // namespace

TEST_CASE("SubtitleBurnMediaIO: factory is registered") {
        const MediaIOFactory *f = MediaIOFactory::findByName("SubtitleBurn");
        REQUIRE(f != nullptr);
        CHECK(f->canBeTransform());
}

TEST_CASE("SubtitleBurnMediaIO: passes ANC payload, metadata, captureTime, and configUpdate through") {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("SubtitleBurn");
        // Keep painting enabled — the test wants to verify that
        // burning a cue does not strip the source subtitle data
        // from the output Frame.
        cfg.set(MediaConfig::VideoSubtitleBurnEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc upstream = makeVideoDesc();
        REQUIRE(io->setPendingMediaDesc(upstream).isOk());
        REQUIRE(io->open().wait().isOk());
        REQUIRE(io->sink(0) != nullptr);
        REQUIRE(io->source(0) != nullptr);

        // Build an input Frame carrying everything the user wants
        // preserved end-to-end.
        Frame in;
        in.addPayload(makeVideoPayload());
        in.addPayload(makeCea708AncPayload());
        in.metadata().set(Metadata::Subtitle, Variant(makeCue()));
        in.metadata().set(Metadata::FrameRate, Variant(FrameRate(FrameRate::FPS_30)));

        const TimeStamp        tsRaw = TimeStamp::now();
        const MediaTimeStamp   captureTs(tsRaw, ClockDomain::SystemMonotonic);
        in.setCaptureTime(captureTs);

        // A non-empty config-update delta should also survive the
        // transform — these ride per-frame for dynamic parameter
        // changes and must not be silently dropped.
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

        // --- Metadata::Subtitle is still on the output ---
        REQUIRE(out.metadata().contains(Metadata::Subtitle));
        Variant subVar = out.metadata().get(Metadata::Subtitle);
        REQUIRE(subVar.type() == DataTypeSubtitle);
        Subtitle outCue = subVar.get<Subtitle>();
        CHECK_FALSE(outCue.isEmpty());

        // --- captureTime is preserved ---
        CHECK(out.captureTime() == captureTs);

        // --- configUpdate is preserved ---
        CHECK(out.configUpdate().getAs<String>(MediaConfig::VideoBurnText) == String("delta-test"));

        // --- Video payload still present (and is the one painted into) ---
        VideoPayload::PtrList vids = out.videoPayloads();
        REQUIRE(vids.size() == 1);
        REQUIRE(vids[0].isValid());

        REQUIRE(io->close().wait().isOk());
        delete io;
}

TEST_CASE("SubtitleBurnMediaIO: passthrough is preserved with painting disabled") {
        // The disabled-burn fast path must also preserve all source
        // data — otherwise a disabled burn-in would corrupt downstream
        // consumers that rely on the source ANC / metadata / timing.
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("SubtitleBurn");
        cfg.set(MediaConfig::VideoSubtitleBurnEnabled, false);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc upstream = makeVideoDesc();
        REQUIRE(io->setPendingMediaDesc(upstream).isOk());
        REQUIRE(io->open().wait().isOk());

        Frame in;
        in.addPayload(makeVideoPayload());
        in.addPayload(makeCea708AncPayload());
        in.metadata().set(Metadata::Subtitle, Variant(makeCue()));
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
        CHECK(out.metadata().contains(Metadata::Subtitle));
        CHECK(out.captureTime() == captureTs);
        CHECK(out.videoPayloads().size() == 1);

        REQUIRE(io->close().wait().isOk());
        delete io;
}

// ============================================================================
// CEA-708 ANC source
// ============================================================================
//
// Drives the SubtitleBurn through its Cea708Anc source by encoding a
// real cue into a CDP via Cea708Encoder, wrapping that into an
// AncPayload, and writing the frame through the transform.  The cue
// is painted onto the output video and the FramesPainted stat
// reflects it.

TEST_CASE("SubtitleBurnMediaIO[708]: Cea708Anc source paints a cue from a real CDP") {
        using ClockDur = TimeStamp::Value::duration;

        // Build a cue at 1.0s..2.0s @ 30 fps and pull the encoder's
        // "show" packet (frame 30).  That single frame's cc_data
        // already contains DefineWindow + chars + DisplayWindow, so
        // the decoder reaches displayedCue()==non-empty after one
        // pushFrame.
        Cea708Encoder::Config encCfg;
        encCfg.frameRate = FrameRate(FrameRate::FPS_30);
        Cea708Encoder enc(encCfg);
        SubtitleList  cues;
        TimeStamp     cueStart(TimeStamp::Value(
                std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(1000))));
        TimeStamp     cueEnd(TimeStamp::Value(
                std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(2000))));
        cues.append(Subtitle(cueStart, cueEnd, "HELLO"));
        REQUIRE(enc.setSubtitles(cues).isOk());
        Cea708Cdp::CcDataList showTriples = enc.nextFrame(FrameNumber(30));
        REQUIRE(showTriples.size() > 0);

        // Configure SubtitleBurn with Cea708Anc as the sole source.
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("SubtitleBurn");
        cfg.set(MediaConfig::VideoSubtitleBurnEnabled, true);
        EnumList sources = EnumList::forType<SubtitleSource>();
        sources.append(SubtitleSource::Cea708Anc);
        cfg.set(MediaConfig::VideoSubtitleBurnSources, sources);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->setPendingMediaDesc(makeVideoDesc()).isOk());
        REQUIRE(io->open().wait().isOk());

        // Frame carrying the synthesised CDP.  The video payload is
        // present so the renderer has something to paint onto.
        Frame in;
        in.addPayload(makeVideoPayload());
        in.addPayload(makeCea708CdpAncPayload(showTriples));
        in.metadata().set(Metadata::FrameRate, Variant(FrameRate(FrameRate::FPS_30)));

        REQUIRE(io->sink(0)->writeFrame(in).wait().isOk());
        MediaIORequest readReq = io->source(0)->readFrame();
        REQUIRE(readReq.wait().isOk());
        const auto *cr = readReq.commandAs<MediaIOCommandRead>();
        REQUIRE(cr != nullptr);
        REQUIRE(cr->frame.isValid());

        // ANC payload still on the output (passthrough), and the burn
        // stat advanced.
        AncPayload::PtrList ancOut = cr->frame.ancPayloads();
        REQUIRE(ancOut.size() == 1);
        CHECK(ancOut[0]->packets()[0].format().id() == AncFormat::Cea708);

        MediaIORequest statsReq = io->stats();
        REQUIRE(statsReq.wait().isOk());
        MediaIOStats stats = statsReq.stats();
        const int64_t painted = stats.getAs<FrameCount>(SubtitleBurnMediaIO::StatsFramesPainted).value();
        CHECK(painted >= 1);

        REQUIRE(io->close().wait().isOk());
        delete io;
}

TEST_CASE("SubtitleBurnMediaIO[708]: empty cc_data leaves FramesPainted at 0") {
        // SubtitleBurn with Cea708Anc as the only source, but the frame
        // carries an empty CDP (no DTVCC bytes).  The decoder finds no
        // cue, the renderer paints nothing.
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("SubtitleBurn");
        cfg.set(MediaConfig::VideoSubtitleBurnEnabled, true);
        EnumList sources = EnumList::forType<SubtitleSource>();
        sources.append(SubtitleSource::Cea708Anc);
        cfg.set(MediaConfig::VideoSubtitleBurnSources, sources);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->setPendingMediaDesc(makeVideoDesc()).isOk());
        REQUIRE(io->open().wait().isOk());

        Frame in;
        in.addPayload(makeVideoPayload());
        in.addPayload(makeCea708CdpAncPayload(Cea708Cdp::CcDataList{}));

        REQUIRE(io->sink(0)->writeFrame(in).wait().isOk());
        MediaIORequest readReq = io->source(0)->readFrame();
        REQUIRE(readReq.wait().isOk());

        MediaIORequest statsReq = io->stats();
        REQUIRE(statsReq.wait().isOk());
        const int64_t painted = statsReq.stats()
                                        .getAs<FrameCount>(SubtitleBurnMediaIO::StatsFramesPainted)
                                        .value();
        CHECK(painted == 0);

        REQUIRE(io->close().wait().isOk());
        delete io;
}
