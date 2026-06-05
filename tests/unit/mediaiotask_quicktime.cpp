/**
 * @file      mediaiotask_quicktime.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * MediaIO-level coverage for QuickTimeMediaIO — in particular the
 * QuickTimeCaptionReadPolicy that governs how a CEA-608 (c608) caption
 * track is surfaced into the ancillary-data model on read.
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/ancdesc.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/anctranslator.h>
#include <promeki/buffer.h>
#include <promeki/cea608packet.h>
#include <promeki/cea708cdp.h>
#include <promeki/dir.h>
#include <promeki/st291packet.h>
#include <promeki/st436m.h>
#include <promeki/enums_mediaio.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/pixelformat.h>
#include <promeki/qtclosedcaption.h>
#include <promeki/quicktime.h>
#include <promeki/size2d.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        Frame readOneFrame(MediaIO *io) {
                MediaIORequest req = io->source(0)->readFrame();
                REQUIRE(req.wait().isOk());
                const auto *cr = req.commandAs<MediaIOCommandRead>();
                REQUIRE(cr != nullptr);
                return cr->frame;
        }

        // Writes a tiny .mov with a UYVY video track and a c608 caption
        // track (no ST 436M vanc track), so the read path must rely on the
        // caption track for any ANC.
        void writeCaptionOnlyMov(const String &path) {
                std::remove(path.cstr());
                const FrameRate fr(FrameRate::RationalType(30000, 1001));

                Cea708Cdp::CcDataList cc;
                cc.pushToBack({true, 0, 0x94, 0x2C});
                cc.pushToBack({true, 0, 0x48, 0x69});

                QuickTime qt = QuickTime::createWriter(path);
                REQUIRE(qt.open() == Error::Ok);
                uint32_t vid = 0, cid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), Size2Du32(16, 16), fr,
                                         &vid) == Error::Ok);
                REQUIRE(qt.addCaptionTrack(fr, &cid) == Error::Ok);
                for (int f = 0; f < 4; ++f) {
                        QuickTime::Sample vs;
                        Buffer            vb(512);
                        std::memset(vb.data(), 0x80, 512);
                        vb.setSize(512);
                        vs.data = vb;
                        vs.duration = 1;
                        vs.keyframe = true;
                        REQUIRE(qt.writeSample(vid, vs) == Error::Ok);

                        QuickTime::Sample cs;
                        cs.data = QtClosedCaption::encode608(cc);
                        cs.duration = 1;
                        cs.keyframe = true;
                        REQUIRE(qt.writeSample(cid, cs) == Error::Ok);
                }
                REQUIRE(qt.finalize() == Error::Ok);
        }

        MediaIO *openReader(const String &path, const QuickTimeCaptionReadPolicy &policy) {
                MediaIO::Config cfg = MediaIOFactory::defaultConfig("QuickTime");
                cfg.set(MediaConfig::Filename, path);
                cfg.set(MediaConfig::QuickTimeCaptionReadPolicy, policy);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open().wait().isOk());
                return io;
        }

        bool frameHasCea708(const Frame &f) {
                for (const AncPayload::Ptr &ap : f.ancPayloads()) {
                        if (!ap.isValid()) continue;
                        for (const AncPacket &pkt : ap->packets())
                                if (pkt.format().id() == AncFormat::Cea708) return true;
                }
                return false;
        }

        // True when the frame's ANC carries CEA-608 content — a 608 packet or
        // a 708 CDP with a cc_type 0/1 triple.
        bool frameHas608(const Frame &f) {
                for (const AncPayload::Ptr &ap : f.ancPayloads()) {
                        if (!ap.isValid()) continue;
                        for (const AncPacket &pkt : ap->packets()) {
                                if (pkt.format().id() == AncFormat::Cea608) return true;
                                if (pkt.format().id() != AncFormat::Cea708) continue;
                                Result<St291Packet> sp = St291Packet::from(pkt);
                                if (sp.second().isError()) continue;
                                List<uint16_t> udw = sp.first().udw();
                                Buffer         buf(udw.size());
                                uint8_t       *b = static_cast<uint8_t *>(buf.data());
                                for (size_t i = 0; i < udw.size(); ++i) b[i] = static_cast<uint8_t>(udw[i] & 0xFF);
                                buf.setSize(udw.size());
                                Result<Cea708Cdp> cdp = Cea708Cdp::fromBuffer(buf);
                                if (cdp.second().isError()) continue;
                                for (size_t j = 0; j < cdp.first().ccData.size(); ++j)
                                        if (cdp.first().ccData[j].valid &&
                                            (cdp.first().ccData[j].type == 0 || cdp.first().ccData[j].type == 1))
                                                return true;
                        }
                }
                return false;
        }

        // Writes a .mov with video + a vanc track carrying the CDP built from
        // @p vancCdpCc + a c608 caption track carrying field-1 608 pairs.
        void writeVancPlusCaptionMov(const String &path, const Cea708Cdp::CcDataList &vancCdpCc) {
                std::remove(path.cstr());
                const FrameRate fr(FrameRate::RationalType(30000, 1001));

                Cea708Cdp      cdp(Cea708Cdp::frameRateCodeFor(fr), vancCdpCc);
                Buffer         cdpBytes = cdp.toBuffer();
                List<uint16_t> udw;
                const uint8_t *p = static_cast<const uint8_t *>(cdpBytes.data());
                for (size_t i = 0; i < cdpBytes.size(); ++i) udw.pushToBack(p[i]);
                AncPacket cdpPkt =
                        St291Packet::build(AncFormat(AncFormat::Cea708), udw, St291Packet::UnspecifiedLine).packet();
                AncPacket::List vancPkts;
                vancPkts.pushToBack(cdpPkt);

                Cea708Cdp::CcDataList c608cc;
                c608cc.pushToBack({true, 0, 0x94, 0x2C});
                c608cc.pushToBack({true, 0, 0x48, 0x69});

                QuickTime qt = QuickTime::createWriter(path);
                REQUIRE(qt.open() == Error::Ok);
                uint32_t vid = 0, anid = 0, cid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), Size2Du32(16, 16), fr,
                                         &vid) == Error::Ok);
                REQUIRE(qt.addAncTrack(AncDesc(), fr, &anid) == Error::Ok);
                REQUIRE(qt.addCaptionTrack(fr, &cid) == Error::Ok);
                for (int f = 0; f < 3; ++f) {
                        QuickTime::Sample vs;
                        Buffer            vb(512);
                        std::memset(vb.data(), 0x80, 512);
                        vb.setSize(512);
                        vs.data = vb;
                        vs.duration = 1;
                        vs.keyframe = true;
                        REQUIRE(qt.writeSample(vid, vs) == Error::Ok);

                        QuickTime::Sample as;
                        as.data = St436m::encodeFrame(vancPkts, AncDesc());
                        as.duration = 1;
                        as.keyframe = true;
                        REQUIRE(qt.writeSample(anid, as) == Error::Ok);

                        QuickTime::Sample cs;
                        cs.data = QtClosedCaption::encode608(c608cc);
                        cs.duration = 1;
                        cs.keyframe = true;
                        REQUIRE(qt.writeSample(cid, cs) == Error::Ok);
                }
                REQUIRE(qt.finalize() == Error::Ok);
        }

        // Collects the CEA-608 (cc_type 0/1) cc_data pairs surfaced on a
        // read-back frame's reconstructed 708 CDP.  Used to byte-check the
        // captions that round-tripped through the c608 caption track.
        Cea708Cdp::CcDataList collect608Pairs(const Frame &f) {
                Cea708Cdp::CcDataList out;
                for (const AncPayload::Ptr &ap : f.ancPayloads()) {
                        if (!ap.isValid()) continue;
                        for (const AncPacket &pkt : ap->packets()) {
                                if (pkt.format().id() != AncFormat::Cea708) continue;
                                Result<St291Packet> sp = St291Packet::from(pkt);
                                if (sp.second().isError()) continue;
                                List<uint16_t> udw = sp.first().udw();
                                Buffer         buf(udw.size());
                                uint8_t       *b = static_cast<uint8_t *>(buf.data());
                                for (size_t i = 0; i < udw.size(); ++i) b[i] = static_cast<uint8_t>(udw[i] & 0xFF);
                                buf.setSize(udw.size());
                                Result<Cea708Cdp> cdp = Cea708Cdp::fromBuffer(buf);
                                if (cdp.second().isError()) continue;
                                for (size_t j = 0; j < cdp.first().ccData.size(); ++j) {
                                        const Cea708Cdp::CcData &cc = cdp.first().ccData[j];
                                        if (cc.valid && (cc.type == 0 || cc.type == 1)) out.pushToBack(cc);
                                }
                        }
                }
                return out;
        }

        // Drives the QuickTimeMediaIO *writer* (sink) with frames carrying a
        // raw SMPTE 334-1 line-21 CEA-608 ancillary packet (AncFormat::Cea608)
        // built through the registered ANC codec — no hand-built caption
        // track.  This exercises the write-side harvest of raw 608 into the
        // c608 caption track (frameHasCaptions / collectCaption608 in
        // quicktimemediaio.cpp), which is only possible now that AncFormat
        // ::Cea608 has a registered parser.
        void writeRaw608ViaMediaIO(const String &path, uint8_t b1, uint8_t b2) {
                std::remove(path.cstr());

                // Build the raw 608 ANC packet via the codec we want to exercise.
                Cea708Cdp::CcData cc;
                cc.valid = true;
                cc.type  = 0; // field 1
                cc.b1    = b1;
                cc.b2    = b2;
                Cea708Cdp::CcDataList ccList;
                ccList.pushToBack(cc);
                AncTranslator                t;
                AncTranslator::PacketsResult built =
                        t.build(Variant(Cea608Packet(Cea608Packet::Channel::CC1, ccList)),
                                AncFormat(AncFormat::Cea608), AncTransport::St291);
                REQUIRE(built.second().isOk());
                REQUIRE(built.first().size() == 1);
                const AncPacket cea608Pkt = built.first().front();
                REQUIRE(cea608Pkt.format().id() == AncFormat::Cea608);

                MediaIO *w = MediaIO::createForFileWrite(path);
                REQUIRE(w != nullptr);
                REQUIRE(w->open().wait().isOk());
                REQUIRE(w->sink(0) != nullptr);
                for (int f = 0; f < 3; ++f) {
                        Frame                         frame;
                        UncompressedVideoPayload::Ptr vp = UncompressedVideoPayload::allocate(
                                ImageDesc(16, 16, PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709)));
                        frame.addPayload(vp);
                        AncPayload::Ptr ap = AncPayload::Ptr::create();
                        ap.modify()->addPacket(cea608Pkt);
                        frame.addPayload(ap);
                        REQUIRE(w->sink(0)->writeFrame(frame).wait().isOk());
                }
                REQUIRE(w->close().wait().isOk());
                delete w;
        }

} // namespace

TEST_CASE("QuickTimeMediaIO: Auto policy surfaces a c608-only track as ANC") {
        const String path = "/tmp/qt_mediaio_cc_auto.mov";
        writeCaptionOnlyMov(path);

        MediaIO *io = openReader(path, QuickTimeCaptionReadPolicy::Auto);
        Frame    f = readOneFrame(io);
        // No vanc track → ANC has no captions → Auto injects the c608 track.
        CHECK(frameHasCea708(f));
        io->close().wait();
        delete io;

        std::remove(path.cstr());
}

TEST_CASE("QuickTimeMediaIO: VancOnly policy ignores the c608 track") {
        const String path = "/tmp/qt_mediaio_cc_vanc.mov";
        writeCaptionOnlyMov(path);

        MediaIO *io = openReader(path, QuickTimeCaptionReadPolicy::VancOnly);
        Frame    f = readOneFrame(io);
        // No vanc track and c608 ignored → no ANC at all.
        CHECK(f.ancPayloads().size() == 0);
        io->close().wait();
        delete io;

        std::remove(path.cstr());
}

TEST_CASE("QuickTimeMediaIO: CaptionTrackOnly policy injects the c608 track") {
        const String path = "/tmp/qt_mediaio_cc_only.mov";
        writeCaptionOnlyMov(path);

        MediaIO *io = openReader(path, QuickTimeCaptionReadPolicy::CaptionTrackOnly);
        Frame    f = readOneFrame(io);
        CHECK(frameHasCea708(f));
        io->close().wait();
        delete io;

        std::remove(path.cstr());
}

TEST_CASE("QuickTimeMediaIO: Auto merges c608 when the vanc CDP is 708-only") {
        // vanc CDP carries only 708 DTVCC (cc_type 2/3) — no 608 — so Auto
        // must still surface the c608 track's 608 (608-in-708 awareness).
        const String          path = "/tmp/qt_mediaio_708only.mov";
        Cea708Cdp::CcDataList cc708;
        cc708.pushToBack({true, 3, 0x01, 0x02}); // DTVCC packet start
        cc708.pushToBack({true, 2, 0x03, 0x04}); // DTVCC continuation
        writeVancPlusCaptionMov(path, cc708);

        MediaIO *io = openReader(path, QuickTimeCaptionReadPolicy::Auto);
        Frame    f = readOneFrame(io);
        CHECK(frameHas608(f)); // injected from the c608 track
        io->close().wait();
        delete io;

        std::remove(path.cstr());
}

TEST_CASE("QuickTimeMediaIO: Auto does not duplicate 608 already in the vanc CDP") {
        // vanc CDP already carries 608 (cc_type 0) — Auto should leave the
        // c608 track out (no second 608 source).  The single ANC payload then
        // holds exactly one caption packet (the vanc CDP).
        const String          path = "/tmp/qt_mediaio_608in708.mov";
        Cea708Cdp::CcDataList cc608;
        cc608.pushToBack({true, 0, 0x94, 0x2C}); // 608 field 1
        writeVancPlusCaptionMov(path, cc608);

        MediaIO *io = openReader(path, QuickTimeCaptionReadPolicy::Auto);
        Frame    f = readOneFrame(io);
        CHECK(frameHas608(f));
        // Only the vanc CDP packet — the c608 track was not merged in.
        size_t captionPkts = 0;
        for (const AncPayload::Ptr &ap : f.ancPayloads()) {
                if (!ap.isValid()) continue;
                for (const AncPacket &pkt : ap->packets())
                        if (pkt.format().id() == AncFormat::Cea708 || pkt.format().id() == AncFormat::Cea608)
                                ++captionPkts;
        }
        CHECK(captionPkts == 1);
        io->close().wait();
        delete io;

        std::remove(path.cstr());
}

TEST_CASE("QuickTimeMediaIO: writer harvests raw CEA-608 ANC into the c608 track") {
        // Round-trips a frame whose only caption content is a raw line-21
        // CEA-608 ANC packet (no hand-built caption track) through the
        // QuickTimeMediaIO writer, then reads it back.  CaptionTrackOnly
        // makes the c608 track authoritative: it strips any vanc caption
        // packet and injects the c608 track as a reconstructed 708 CDP.  If
        // the writer's collectCaption608 had ignored the raw 608 packet the
        // c608 samples would be empty and no 608 would surface here.
        const String path = (Dir::temp().path() / "qt_mediaio_raw608_writer.mov").toString();
        writeRaw608ViaMediaIO(path, 0x94, 0x2C);

        MediaIO *io = openReader(path, QuickTimeCaptionReadPolicy::CaptionTrackOnly);
        Frame    f  = readOneFrame(io);
        CHECK(frameHas608(f));

        // The exact caption bytes survive the raw-608 → c608 → read round trip.
        Cea708Cdp::CcDataList pairs = collect608Pairs(f);
        REQUIRE_FALSE(pairs.isEmpty());
        CHECK(pairs[0].type == 0);
        CHECK(pairs[0].b1 == 0x94);
        CHECK(pairs[0].b2 == 0x2C);

        io->close().wait();
        delete io;

        std::remove(path.cstr());
}
