/**
 * @file      tpg_anc_captions.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * End-to-end coverage for TPG CEA-708 caption injection via SubRip
 * file input.  Verifies that @c TpgAncCaptionsEnabled +
 * @c TpgAncCaptionsFile cause every emitted Frame to carry a
 * @ref AncPayload with a @ref Cea708Cdp, that the per-frame
 * @c CcDataList matches what @ref Cea608Encoder schedules, that the
 * VANC line / sequence counter are stamped per the configured policy,
 * and that the @ref Metadata::Subtitle stamp lands on each cue's
 * start frame.
 */

#include <chrono>
#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/anctranslator.h>
#include <promeki/buffer.h>
#include <promeki/cea608.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708decoder.h>
#include <promeki/dir.h>
#include <promeki/enums_subtitle.h>
#include <promeki/framenumber.h>
#include <promeki/duration.h>
#include <promeki/file.h>
#include <promeki/frame.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosource.h>
#include <promeki/st291packet.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>
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

        /// @brief Writes a tiny SubRip fixture to a unique path under
        ///        @ref Dir::temp.  Returns the path so the test can pass
        ///        it via @c TpgAncCaptionsFile.
        String writeSrtFixture(const String &body) {
                const int64_t ns = TimeStamp::now().nanoseconds();
                FilePath      p = Dir::temp().path()
                                / String::sprintf("promeki_tpg_anc_captions_%lld.srt",
                                                   static_cast<long long>(ns));
                File          f(p.toString());
                REQUIRE(f.open(IODevice::WriteOnly, File::Create | File::Truncate).isOk());
                f.write(body.cstr(), static_cast<int64_t>(body.byteCount()));
                f.close();
                return p.toString();
        }

        /// @brief Returns @c true if the CcDataList carries exactly one
        ///        triple whose parity-stripped bytes equal the given pair.
        bool tripleMatches(const Cea708Cdp::CcDataList &list, uint8_t b1, uint8_t b2) {
                if (list.size() != 1) return false;
                return Cea608::stripParity(list[0].b1) == b1 && Cea608::stripParity(list[0].b2) == b2;
        }

} // namespace

// ============================================================================
// Disabled / no file
// ============================================================================

TEST_CASE("TPG: ANC captions disabled emits no AncPayload") {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, false);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        Frame f = readOneFrame(io);
        CHECK(f.ancPayloads().size() == 0);

        io->close().wait();
        delete io;
}

TEST_CASE("TPG: ANC captions enabled with no file emits null-pair CDPs") {
        // Enabled but no file configured: the per-frame CDP still
        // carries one cc_data triple (the encoder's null pair) so the
        // receiver sees a steady caption-service-active stream.
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, String());
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        Frame              f = readOneFrame(io);
        AncPayload::PtrList ancs = f.ancPayloads();
        REQUIRE(ancs.size() == 1);

        AncTranslator    t;
        const AncPacket &pkt = ancs[0]->packets()[0];
        AncTranslator::ParseResult  parsed = t.parse(pkt);
        REQUIRE(parsed.second().isOk());
        Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();
        REQUIRE(cdp.ccData.size() == 1);
        // Null pair: 0x80 0x80 on the wire.
        CHECK(cdp.ccData[0].b1 == 0x80);
        CHECK(cdp.ccData[0].b2 == 0x80);

        io->close().wait();
        delete io;
}

// ============================================================================
// SubRip-driven injection
// ============================================================================

TEST_CASE("TPG: SubRip-driven captions schedule per-frame CcData from the encoder") {
        // SubRip cue: "AB" at 00:00:01,000 --> 00:00:02,000.
        // At 30 fps that's frame 30..60.  The SubRip parser defaults
        // un-prefixed cues to BottomCenter (broadcast convention), so
        // the encoder centers "AB" (2 chars) at col 15 → PAC indent
        // 12 + doubled Tab Offset T3.  Pre-roll is now 9 frames:
        // RCL,RCL,ENM,ENM,PAC,PAC,Tab,Tab,chars.  First RCL lands at
        // frame 21, EOC at frame 30 / 31, EDM at frame 60 / 61.
        const String srtPath = writeSrtFixture(
                "1\r\n00:00:01,000 --> 00:00:02,000\r\nAB\r\n\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        AncTranslator t;
        // Read 70 frames so we span the full pre-roll + cue body + EDM.
        // Pull the cc_data pair for each frame and check key offsets.
        bool sawRclAt21 = false;
        bool sawEocAt30 = false;
        bool sawCharsAt29 = false;
        bool sawEdmAt60 = false;
        for (int64_t f = 0; f < 70; ++f) {
                Frame              frame = readOneFrame(io);
                AncPayload::PtrList ancs = frame.ancPayloads();
                REQUIRE(ancs.size() == 1);
                AncTranslator::ParseResult parsed = t.parse(ancs[0]->packets()[0]);
                REQUIRE(parsed.second().isOk());
                Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();
                if (f == 21 && tripleMatches(cdp.ccData, Cea608::RclB1, Cea608::RclB2)) sawRclAt21 = true;
                if (f == 29 && tripleMatches(cdp.ccData, 0x41, 0x42)) sawCharsAt29 = true;
                if (f == 30 && tripleMatches(cdp.ccData, Cea608::EocB1, Cea608::EocB2)) sawEocAt30 = true;
                if (f == 60 && tripleMatches(cdp.ccData, Cea608::EdmB1, Cea608::EdmB2)) sawEdmAt60 = true;
        }
        CHECK(sawRclAt21);
        CHECK(sawCharsAt29);
        CHECK(sawEocAt30);
        CHECK(sawEdmAt60);

        io->close().wait();
        delete io;
}

TEST_CASE("TPG: offset shifts SubRip cues forward in time") {
        // SubRip cue at t=0.5s.  Offset = +1s.  Expected effective
        // cue start = 1.5s → frame 45 at 30 fps.  Pre-roll is 9
        // frames (RCL,RCL,ENM,ENM,PAC,PAC,Tab,Tab,chars — the SubRip
        // parser defaults un-prefixed cues to BottomCenter so the
        // encoder centers "AB" via doubled PAC indent + Tab Offset
        // T3), so first RCL lands at 36.
        const String srtPath = writeSrtFixture(
                "1\r\n00:00:00,500 --> 00:00:01,500\r\nAB\r\n\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        cfg.set(MediaConfig::TpgAncCaptionsOffset, Duration::fromMilliseconds(1000));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        AncTranslator t;
        bool          sawRclAt36 = false;
        bool          sawEocAt45 = false;
        for (int64_t f = 0; f < 70; ++f) {
                Frame              frame = readOneFrame(io);
                AncPayload::PtrList ancs = frame.ancPayloads();
                REQUIRE(ancs.size() == 1);
                AncTranslator::ParseResult parsed = t.parse(ancs[0]->packets()[0]);
                REQUIRE(parsed.second().isOk());
                Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();
                if (f == 36 && tripleMatches(cdp.ccData, Cea608::RclB1, Cea608::RclB2)) sawRclAt36 = true;
                if (f == 45 && tripleMatches(cdp.ccData, Cea608::EocB1, Cea608::EocB2)) sawEocAt45 = true;
        }
        CHECK(sawRclAt36);
        CHECK(sawEocAt45);

        io->close().wait();
        delete io;
}

TEST_CASE("TPG: malformed SubRip file makes open() fail with ParseFailed") {
        const String srtPath = writeSrtFixture(
                "1\r\nBADTC --> 00:00:02,000\r\nx\r\n\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        Error openErr = io->open().wait();
        CHECK(openErr.code() == Error::ParseFailed);

        delete io;
}

TEST_CASE("TPG: missing SubRip file makes open() fail") {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, String("/tmp/this-file-should-not-exist.srt"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        Error openErr = io->open().wait();
        CHECK(openErr.isError());

        delete io;
}

// ============================================================================
// Frame metadata stamping
// ============================================================================

TEST_CASE("TPG: Metadata::Subtitle from etc/substest.srt preserves per-cue anchors") {
        // Drive the TPG from the canonical fixture so any regression
        // between SubRip parsing, the encodableSubset filter, the
        // per-frame stamp, and the Variant round-trip surfaces here.
        const String srtPath = String(PROMEKI_SOURCE_DIR) + "/etc/substest.srt";

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        // Cues we care about in the fixture:
        //   3: {\an8} → TopCenter at 10.0s
        //   4: {\an5} → MiddleCenter at 15.0s
        //   5: {\an1} → BottomLeft at 20.0s
        // Run for ~22 seconds at default rate.
        bool sawTopCenter = false;
        bool sawMiddleCenter = false;
        bool sawBottomLeft = false;
        for (int64_t f = 0; f < 22 * 60; ++f) { // generous upper bound
                Frame frame = readOneFrame(io);
                if (!frame.metadata().contains(Metadata::Subtitle)) continue;
                Subtitle s = frame.metadata().get(Metadata::Subtitle).get<Subtitle>();
                const int v = s.anchor().value();
                if (v == SubtitleAnchor::TopCenter.value()) sawTopCenter = true;
                else if (v == SubtitleAnchor::MiddleCenter.value()) sawMiddleCenter = true;
                else if (v == SubtitleAnchor::BottomLeft.value()) sawBottomLeft = true;
        }
        CHECK(sawTopCenter);
        CHECK(sawMiddleCenter);
        CHECK(sawBottomLeft);

        io->close().wait();
        delete io;
}

TEST_CASE("TPG: Metadata::Subtitle preserves SRT anchor + styled spans") {
        // SRT fixture with an ASS anchor prefix and inline markup.
        const String srtPath = writeSrtFixture(
                "1\r\n00:00:01,000 --> 00:00:02,000\r\n"
                "{\\an8}<i>top italic</i>\r\n\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        bool sawAnchoredCue = false;
        bool sawItalicSpan = false;
        for (int64_t f = 0; f < 70; ++f) {
                Frame frame = readOneFrame(io);
                if (!frame.metadata().contains(Metadata::Subtitle)) continue;
                Subtitle s = frame.metadata().get(Metadata::Subtitle).get<Subtitle>();
                if (s.text() != "top italic") continue;
                if (s.anchor() == SubtitleAnchor::TopCenter) sawAnchoredCue = true;
                for (size_t i = 0; i < s.spans().size(); ++i) {
                        if (s.spans()[i].italic()) sawItalicSpan = true;
                }
        }
        // Both attributes must survive the SubRip -> TPG -> Metadata
        // chain so SubtitleBurn renders them faithfully.
        CHECK(sawAnchoredCue);
        CHECK(sawItalicSpan);

        io->close().wait();
        delete io;
}

TEST_CASE("TPG: Metadata::Subtitle stamped on every frame within the cue window") {
        // Cue at 1.0s..2.0s → frames [30, 60) at 30 fps.  The
        // metadata stamp is re-applied every frame the cue is
        // visible (so renderers can paint without tracking state
        // across frames); pre-window and post-window frames carry
        // no Subtitle key.
        const String srtPath = writeSrtFixture(
                "1\r\n00:00:01,000 --> 00:00:02,000\r\nHi\r\n\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        bool sawSubtitleAt29 = false;
        bool sawSubtitleAt30 = false;
        bool sawSubtitleAt45 = false;
        bool sawSubtitleAt59 = false;
        bool sawSubtitleAt60 = false;
        for (int64_t f = 0; f < 70; ++f) {
                Frame    frame = readOneFrame(io);
                bool     present = frame.metadata().contains(Metadata::Subtitle);
                if (present) {
                        Subtitle s = frame.metadata().get(Metadata::Subtitle).get<Subtitle>();
                        if (s.text() != "Hi") continue;
                        if (f == 29) sawSubtitleAt29 = true;
                        if (f == 30) sawSubtitleAt30 = true;
                        if (f == 45) sawSubtitleAt45 = true;
                        if (f == 59) sawSubtitleAt59 = true;
                        if (f == 60) sawSubtitleAt60 = true;
                }
        }
        // Pre-window: nothing.
        CHECK_FALSE(sawSubtitleAt29);
        // Cue window [30, 60): every frame carries the stamp.
        CHECK(sawSubtitleAt30);
        CHECK(sawSubtitleAt45);
        CHECK(sawSubtitleAt59);
        // Post-window: nothing.
        CHECK_FALSE(sawSubtitleAt60);

        io->close().wait();
        delete io;
}

// ============================================================================
// Sequence counter + MediaDesc
// ============================================================================

TEST_CASE("TPG: ANC sequence counter advances each frame") {
        // Empty-file path is enough to exercise the per-frame CDP
        // emission — sequence advances regardless of cue content.
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, String());
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        AncTranslator t;
        for (uint16_t expected = 0; expected < 4; ++expected) {
                Frame                frame = readOneFrame(io);
                AncPayload::PtrList  ancs = frame.ancPayloads();
                REQUIRE(ancs.size() == 1);
                AncTranslator::ParseResult parsed = t.parse(ancs[0]->packets()[0]);
                REQUIRE(parsed.second().isOk());
                Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();
                CHECK(cdp.sequenceCounter == expected);
        }

        io->close().wait();
        delete io;
}

TEST_CASE("TPG: MediaDesc.ancList() advertises CEA-708 when captions are enabled") {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, String());
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        const MediaDesc &md = io->source(0)->mediaDesc();
        const AncDesc::List &ancList = md.ancList();
        REQUIRE(ancList.size() == 1);
        const AncFormat::IDList &allowed = ancList[0].allowedFormats();
        bool                     hasCea708 = false;
        for (size_t i = 0; i < allowed.size(); ++i) {
                if (allowed[i] == AncFormat::Cea708) hasCea708 = true;
        }
        CHECK(hasCea708);

        io->close().wait();
        delete io;
}

TEST_CASE("TPG: VANC line config carries through to the emitted ST 291 packet") {
        const String srtPath = writeSrtFixture(
                "1\r\n00:00:01,000 --> 00:00:02,000\r\nX\r\n\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        cfg.set(MediaConfig::TpgAncCaptionsLine, int32_t(13));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        Frame              f = readOneFrame(io);
        AncPayload::PtrList ancs = f.ancPayloads();
        REQUIRE(ancs.size() == 1);
        Result<St291Packet> rp = St291Packet::from(ancs[0]->packets()[0]);
        REQUIRE(rp.second().isOk());
        CHECK(rp.first().line() == 13);

        io->close().wait();
        delete io;
}

// ============================================================================
// CEA-708 codec selector
// ============================================================================
//
// The TPG's TpgAncCaptionsCodec key flips the per-frame encoder
// between Cea608Encoder (cc_type=0 line-21 byte pairs), Cea708Encoder
// (cc_type=2/3 DTVCC triples), or Both.  These tests verify each
// shape lands in the CDP's @c cc_data list and that the 708 path
// round-trips a SubRip cue through @ref Cea708Decoder.

TEST_CASE("TPG[708]: codec=Cea708 emits DTVCC triples; Cea708Decoder round-trips the cue") {
        // Cue "HELLO" at 1.0s..2.0s.  At default 29.97 fps this rounds
        // to frame 30..60.  The 708 encoder emits the show packet
        // (DF0 + chars + DSW) at frame 30 and the hide packet (HDW)
        // at frame 60; all other frames carry no DTVCC payload.
        const String srtPath = writeSrtFixture(
                "1\r\n00:00:01,000 --> 00:00:02,000\r\nHELLO\r\n\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        cfg.set(MediaConfig::TpgAncCaptionsCodec, CaptionCodec::Cea708);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        AncTranslator t;
        Cea708Decoder dec;
        bool          sawDtvccStartAt30 = false;
        bool          sawDtvccStartAt60 = false;
        bool          saw608TripleAnywhere = false;
        for (int64_t f = 0; f < 80; ++f) {
                Frame               frame = readOneFrame(io);
                AncPayload::PtrList ancs = frame.ancPayloads();
                REQUIRE(ancs.size() == 1);
                AncTranslator::ParseResult parsed = t.parse(ancs[0]->packets()[0]);
                REQUIRE(parsed.second().isOk());
                Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();
                for (size_t i = 0; i < cdp.ccData.size(); ++i) {
                        const auto &cc = cdp.ccData[i];
                        if (cc.type == 0 || cc.type == 1) saw608TripleAnywhere = true;
                        if (f == 30 && cc.type == 2) sawDtvccStartAt30 = true;
                        if (f == 60 && cc.type == 2) sawDtvccStartAt60 = true;
                }
                // Use the frame's media timestamp (rational tick conversion
                // mirrors the TPG's own clock).  TimeStamp here is just
                // the cue boundary; the decoder doesn't care about absolute
                // value, only ordering.
                dec.pushFrame(FrameNumber(f),
                              TimeStamp(TimeStamp::Value(
                                      std::chrono::duration_cast<TimeStamp::Value::duration>(
                                              std::chrono::nanoseconds(f * 1001 * 1000000 / 30)))),
                              cdp.ccData);
        }
        CHECK(sawDtvccStartAt30);
        CHECK(sawDtvccStartAt60);
        CHECK_FALSE(saw608TripleAnywhere);
        SubtitleList out = dec.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "HELLO");

        io->close().wait();
        delete io;
}

TEST_CASE("TPG[708]: codec=Both packs 608 byte pair and 708 triples into the same CDP") {
        // Same cue, codec=Both.  The CDP at frame 30 (the cue's show
        // boundary) must carry the 608 byte pair AND at least one
        // cc_type=2 triple from the 708 encoder.
        const String srtPath = writeSrtFixture(
                "1\r\n00:00:01,000 --> 00:00:02,000\r\nHELLO\r\n\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        cfg.set(MediaConfig::TpgAncCaptionsCodec, CaptionCodec::Both);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        AncTranslator t;
        bool          sawBothShapesAt30 = false;
        // Every frame should have the 608 byte pair (the 608 encoder
        // emits the null pair between cues and real bytes during).
        bool          every608Present = true;
        for (int64_t f = 0; f < 70; ++f) {
                Frame               frame = readOneFrame(io);
                AncPayload::PtrList ancs = frame.ancPayloads();
                REQUIRE(ancs.size() == 1);
                Cea708Cdp cdp = t.parse(ancs[0]->packets()[0]).first().get<Cea708Cdp>();
                bool has608 = false;
                bool has708Start = false;
                for (size_t i = 0; i < cdp.ccData.size(); ++i) {
                        if (cdp.ccData[i].type == 0 || cdp.ccData[i].type == 1) has608 = true;
                        if (cdp.ccData[i].type == 2) has708Start = true;
                }
                if (!has608) every608Present = false;
                if (f == 30 && has608 && has708Start) sawBothShapesAt30 = true;
        }
        CHECK(sawBothShapesAt30);
        CHECK(every608Present);

        io->close().wait();
        delete io;
}

TEST_CASE("TPG[708]: codec=Cea708 with no file emits no DTVCC payload (no per-frame filler)") {
        // DTVCC has no equivalent of the line-21 null pair — between
        // cue transactions there is simply no service-block traffic.
        // So a 708-only TPG with no SubRip file must produce an empty
        // cc_data list on every frame.
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, String());
        cfg.set(MediaConfig::TpgAncCaptionsCodec, CaptionCodec::Cea708);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        AncTranslator t;
        for (int64_t f = 0; f < 5; ++f) {
                Frame               frame = readOneFrame(io);
                AncPayload::PtrList ancs = frame.ancPayloads();
                REQUIRE(ancs.size() == 1);
                Cea708Cdp cdp = t.parse(ancs[0]->packets()[0]).first().get<Cea708Cdp>();
                CHECK(cdp.ccData.size() == 0);
        }

        io->close().wait();
        delete io;
}

TEST_CASE("TPG[708]: TpgAncCaptions708Service routes via the configured DTVCC service") {
        // Encoder targets service 2; a decoder configured for service 1
        // sees no cues, but a decoder configured for service 2 recovers
        // the text.
        const String srtPath = writeSrtFixture(
                "1\r\n00:00:01,000 --> 00:00:02,000\r\nQ\r\n\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        cfg.set(MediaConfig::TpgAncCaptionsCodec, CaptionCodec::Cea708);
        cfg.set(MediaConfig::TpgAncCaptions708Service, int32_t(2));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        AncTranslator         t;
        Cea708Decoder         decService1; // default service 1
        Cea708Decoder::Config decCfg2;
        decCfg2.serviceNumber = 2;
        Cea708Decoder         decService2(decCfg2);
        for (int64_t f = 0; f < 80; ++f) {
                Frame               frame = readOneFrame(io);
                AncPayload::PtrList ancs = frame.ancPayloads();
                REQUIRE(ancs.size() == 1);
                Cea708Cdp cdp = t.parse(ancs[0]->packets()[0]).first().get<Cea708Cdp>();
                const TimeStamp ts(TimeStamp::Value(
                        std::chrono::duration_cast<TimeStamp::Value::duration>(
                                std::chrono::nanoseconds(f * 1001 * 1000000 / 30))));
                decService1.pushFrame(FrameNumber(f), ts, cdp.ccData);
                decService2.pushFrame(FrameNumber(f), ts, cdp.ccData);
        }
        CHECK(decService1.finalize().isEmpty());
        SubtitleList out = decService2.finalize();
        REQUIRE(out.size() == 1);
        CHECK(out[0].text() == "Q");

        io->close().wait();
        delete io;
}

// ============================================================================
// SCC bypass (Phase 3.5f)
// ============================================================================

namespace {

        /// @brief Writes a tiny SCC fixture to a unique path.
        String writeSccFixture(const String &body) {
                const int64_t ns = TimeStamp::now().nanoseconds();
                FilePath      p = Dir::temp().path()
                                / String::sprintf("promeki_tpg_anc_scc_%lld.scc",
                                                   static_cast<long long>(ns));
                File          f(p.toString());
                REQUIRE(f.open(IODevice::WriteOnly, File::Create | File::Truncate).isOk());
                f.write(body.cstr(), static_cast<int64_t>(body.byteCount()));
                f.close();
                return p.toString();
        }

} // namespace

TEST_CASE("TPG[SCC]: byte pairs from SCC file ride into cc_data at the matching frames") {
        // Single SCC row with three byte pairs.  The first row anchors
        // to TPG frame 0, so pair 0 lands on frame 0, pair 1 on frame 1,
        // pair 2 on frame 2.  Frames 3+ get the null pair.
        const String sccPath = writeSccFixture(
                "Scenarist_SCC V1.0\r\n\r\n01:00:00:00\t9420 c441 cd4f\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsScc, sccPath);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        AncTranslator t;
        // Frame 0: 0x9420 (RCL b1 + b2 already stamped).
        Frame                   f0 = readOneFrame(io);
        AncPayload::PtrList     ancs0 = f0.ancPayloads();
        REQUIRE(ancs0.size() == 1);
        AncTranslator::ParseResult parsed0 = t.parse(ancs0[0]->packets()[0]);
        REQUIRE(parsed0.second().isOk());
        Cea708Cdp cdp0 = parsed0.first().get<Cea708Cdp>();
        REQUIRE(cdp0.ccData.size() == 1);
        CHECK(cdp0.ccData[0].b1 == 0x94);
        CHECK(cdp0.ccData[0].b2 == 0x20);

        // Frame 1: 0xC441 (parity-stamped "D" - 'A' in 6-bit, with parity).
        Frame                   f1 = readOneFrame(io);
        AncPayload::PtrList     ancs1 = f1.ancPayloads();
        REQUIRE(ancs1.size() == 1);
        Cea708Cdp cdp1 = t.parse(ancs1[0]->packets()[0]).first().get<Cea708Cdp>();
        REQUIRE(cdp1.ccData.size() == 1);
        CHECK(cdp1.ccData[0].b1 == 0xC4);
        CHECK(cdp1.ccData[0].b2 == 0x41);

        // Frame 2: 0xCD4F.
        Frame                   f2 = readOneFrame(io);
        Cea708Cdp               cdp2 = t.parse(f2.ancPayloads()[0]->packets()[0]).first().get<Cea708Cdp>();
        REQUIRE(cdp2.ccData.size() == 1);
        CHECK(cdp2.ccData[0].b1 == 0xCD);
        CHECK(cdp2.ccData[0].b2 == 0x4F);

        // Frame 3: null pair.
        Frame     f3 = readOneFrame(io);
        Cea708Cdp cdp3 = t.parse(f3.ancPayloads()[0]->packets()[0]).first().get<Cea708Cdp>();
        REQUIRE(cdp3.ccData.size() == 1);
        CHECK(cdp3.ccData[0].b1 == 0x80);
        CHECK(cdp3.ccData[0].b2 == 0x80);

        io->close().wait();
        delete io;
}

TEST_CASE("TPG[SCC]: TpgAncCaptionsFile + TpgAncCaptionsScc are mutually exclusive") {
        const String srtPath = writeSrtFixture(
                "1\r\n00:00:01,000 --> 00:00:02,000\r\nAB\r\n\r\n");
        const String sccPath = writeSccFixture(
                "Scenarist_SCC V1.0\r\n\r\n01:00:00:00\t9420\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsFile, srtPath);
        cfg.set(MediaConfig::TpgAncCaptionsScc, sccPath);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open().wait().code() == Error::InvalidArgument);
        delete io;
}

TEST_CASE("TPG[SCC]: malformed SCC file -> ParseFailed on open") {
        const String sccPath = writeSccFixture("not-an-scc-file\r\n");

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::TpgAncCaptionsEnabled, true);
        cfg.set(MediaConfig::TpgAncCaptionsScc, sccPath);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open().wait().code() == Error::ParseFailed);
        delete io;
}
