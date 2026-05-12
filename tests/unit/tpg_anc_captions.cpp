/**
 * @file      tpg_anc_captions.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
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
#include <promeki/dir.h>
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
        Result<Variant>  parsed = t.parse(pkt);
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
        // At 30 fps that's frame 30..60.  The encoder pre-rolls 5
        // frames (RCL,RCL,PAC,PAC,chars) so first RCL lands at
        // frame 25, EOC at frame 30 / 31, EDM at frame 60 / 61.
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
        bool sawRclAt25 = false;
        bool sawEocAt30 = false;
        bool sawCharsAt29 = false;
        bool sawEdmAt60 = false;
        for (int64_t f = 0; f < 70; ++f) {
                Frame              frame = readOneFrame(io);
                AncPayload::PtrList ancs = frame.ancPayloads();
                REQUIRE(ancs.size() == 1);
                Result<Variant> parsed = t.parse(ancs[0]->packets()[0]);
                REQUIRE(parsed.second().isOk());
                Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();
                if (f == 25 && tripleMatches(cdp.ccData, Cea608::RclB1, Cea608::RclB2)) sawRclAt25 = true;
                if (f == 29 && tripleMatches(cdp.ccData, 0x41, 0x42)) sawCharsAt29 = true;
                if (f == 30 && tripleMatches(cdp.ccData, Cea608::EocB1, Cea608::EocB2)) sawEocAt30 = true;
                if (f == 60 && tripleMatches(cdp.ccData, Cea608::EdmB1, Cea608::EdmB2)) sawEdmAt60 = true;
        }
        CHECK(sawRclAt25);
        CHECK(sawCharsAt29);
        CHECK(sawEocAt30);
        CHECK(sawEdmAt60);

        io->close().wait();
        delete io;
}

TEST_CASE("TPG: offset shifts SubRip cues forward in time") {
        // SubRip cue at t=0.5s.  Offset = +1s.  Expected effective
        // cue start = 1.5s → frame 45 at 30 fps.  First RCL at 40.
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
        bool          sawRclAt40 = false;
        bool          sawEocAt45 = false;
        for (int64_t f = 0; f < 70; ++f) {
                Frame              frame = readOneFrame(io);
                AncPayload::PtrList ancs = frame.ancPayloads();
                REQUIRE(ancs.size() == 1);
                Result<Variant> parsed = t.parse(ancs[0]->packets()[0]);
                REQUIRE(parsed.second().isOk());
                Cea708Cdp cdp = parsed.first().get<Cea708Cdp>();
                if (f == 40 && tripleMatches(cdp.ccData, Cea608::RclB1, Cea608::RclB2)) sawRclAt40 = true;
                if (f == 45 && tripleMatches(cdp.ccData, Cea608::EocB1, Cea608::EocB2)) sawEocAt45 = true;
        }
        CHECK(sawRclAt40);
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
                if (s.anchor().value() == SubtitleAnchor::TopCenter.value()) sawAnchoredCue = true;
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
                Result<Variant> parsed = t.parse(ancs[0]->packets()[0]);
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
