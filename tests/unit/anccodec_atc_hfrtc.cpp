/**
 * @file      anccodec_atc_hfrtc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Phase 6 — ST 12-3:2016 ATC_HFRTC codec round-trip tests.
 */

#include <doctest/doctest.h>
#include <promeki/ancatc.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/result.h>
#include <promeki/st291packet.h>
#include <promeki/timecode.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        AncTranslator::PacketsResult buildHfrtc(const AncAtc &atc,
                                                const AncTranslateConfig &cfg = {}) {
                AncTranslator t(cfg);
                return t.build(Variant(atc), AncFormat(AncFormat::AtcHfrtc), AncTransport::St291);
        }

        AncTranslator::PacketsResult buildHfrtc(const Timecode &tc,
                                                const AncTranslateConfig &cfg = {}) {
                AncTranslator t(cfg);
                return t.build(Variant(tc), AncFormat(AncFormat::AtcHfrtc), AncTransport::St291);
        }

        AncTranslator::ParseResult parseHfrtc(const AncPacket &pkt,
                                              const AncTranslateConfig &cfg = {}) {
                AncTranslator t(cfg);
                return t.parse(pkt);
        }

} // namespace

TEST_CASE("ATC_HFRTC: AncFormat::AtcHfrtc registered with DID=0x60 SDID=0x61") {
        AncFormat f(AncFormat::AtcHfrtc);
        CHECK(f.isValid());
        CHECK(f.st291Did() == 0x60);
        CHECK(f.st291Sdid() == 0x61);
        CHECK(f.canonicalTransport() == AncTransport::St291);
        CHECK(f.name() == String("AtcHfrtc"));
}

TEST_CASE("ATC_HFRTC: parser+builder+sync registration") {
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::AtcHfrtc), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(AncFormat::AtcHfrtc), AncTransport::St291));
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(AncFormat::AtcHfrtc)));
}

TEST_CASE("ATC_HFRTC: AncFormat::fromSt291DidSdid resolves (0x60, 0x61) to AtcHfrtc") {
        AncFormat f = AncFormat::fromSt291DidSdid(0x60, 0x61);
        REQUIRE(f.isValid());
        CHECK(f.id() == AncFormat::AtcHfrtc);
        // The pre-Phase-6 ATC trio at (0x60, 0x60) still resolves to AtcLtc.
        AncFormat g = AncFormat::fromSt291DidSdid(0x60, 0x60);
        REQUIRE(g.isValid());
        CHECK(g.id() == AncFormat::AtcLtc);
}

TEST_CASE("ATC_HFRTC: round-trip 120(30x4) NDF across every physical sub-frame") {
        // At NDF120 the super-frame index walks 0..29 and each carries
        // N=4 physical sub-frames; verify every physical frame 0..119
        // in one second round-trips.
        for (uint32_t physFrame = 0; physFrame < 120; ++physFrame) {
                CAPTURE(physFrame);
                Timecode src(Timecode::Mode(Timecode::NDF120), 1, 2, 3,
                             static_cast<Timecode::DigitType>(physFrame));
                REQUIRE(src.frame() == physFrame);
                REQUIRE(src.superFrameIndex() == physFrame / 4u);
                REQUIRE(src.subFrameIndex() == physFrame % 4u);

                AncTranslator::PacketsResult built = buildHfrtc(src);
                REQUIRE(built.second().isOk());
                Result<St291Packet> rp = St291Packet::from(built.first().front());
                REQUIRE(rp.second().isOk());
                CHECK(rp.first().did() == 0x60);
                CHECK(rp.first().sdid() == 0x61);
                CHECK(rp.first().dataCount() == 0x10);
                CHECK(rp.first().checksumValid());

                AncTranslator::ParseResult parsed = parseHfrtc(built.first().front());
                REQUIRE(parsed.second().isOk());
                AncAtc out = parsed.first().get<AncAtc>();
                CHECK(out.timecode().hour() == 1);
                CHECK(out.timecode().min() == 2);
                CHECK(out.timecode().sec() == 3);
                CHECK(out.timecode().frame() == physFrame);
                CHECK(out.timecode().fps() == 120u);
                CHECK_FALSE(out.timecode().isDropFrame());
                CHECK(out.isHfrtcPayload());
                CHECK(out.hfrtcBitstream() == 0);
        }
}

TEST_CASE("ATC_HFRTC: round-trip 100(25x4) across every physical sub-frame") {
        for (uint32_t physFrame = 0; physFrame < 100; ++physFrame) {
                CAPTURE(physFrame);
                Timecode src(Timecode::Mode(Timecode::NDF100), 0, 0, 0,
                             static_cast<Timecode::DigitType>(physFrame));
                AncTranslator::PacketsResult built = buildHfrtc(src);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseHfrtc(built.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = parsed.first().get<AncAtc>().timecode();
                CHECK(out.frame() == physFrame);
                CHECK(out.fps() == 100u);
        }
}

TEST_CASE("ATC_HFRTC: round-trip 72(24x3) across every physical sub-frame") {
        for (uint32_t physFrame = 0; physFrame < 72; ++physFrame) {
                CAPTURE(physFrame);
                Timecode src(Timecode::Mode(Timecode::NDF72), 0, 0, 0,
                             static_cast<Timecode::DigitType>(physFrame));
                AncTranslator::PacketsResult built = buildHfrtc(src);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseHfrtc(built.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = parsed.first().get<AncAtc>().timecode();
                CHECK(out.frame() == physFrame);
                CHECK(out.fps() == 72u);
        }
}

TEST_CASE("ATC_HFRTC: round-trip 120(24x5) across every physical sub-frame") {
        for (uint32_t physFrame = 0; physFrame < 120; ++physFrame) {
                CAPTURE(physFrame);
                Timecode src(Timecode::Mode(Timecode::NDF120_24x5), 0, 0, 0,
                             static_cast<Timecode::DigitType>(physFrame));
                AncTranslator::PacketsResult built = buildHfrtc(src);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseHfrtc(built.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = parsed.first().get<AncAtc>().timecode();
                CHECK(out.frame() == physFrame);
                CHECK(out.fps() == 120u);
                // 120(24x5) has framesPerSuperFrame=5 vs the 120(30x4) variant.
                CHECK(out.mode().framesPerSuperFrame() == 5u);
        }
}

TEST_CASE("ATC_HFRTC: round-trip 119.88 DF preserves drop-frame bit") {
        for (uint32_t physFrame : {0u, 1u, 7u, 8u, 60u, 119u}) {
                CAPTURE(physFrame);
                Timecode src(Timecode::Mode(Timecode::DF120), 1, 0, 0,
                             static_cast<Timecode::DigitType>(physFrame));
                AncTranslator::PacketsResult built = buildHfrtc(src);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseHfrtc(built.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = parsed.first().get<AncAtc>().timecode();
                CHECK(out.frame() == physFrame);
                CHECK(out.fps() == 120u);
                CHECK(out.isDropFrame());
        }
}

TEST_CASE("ATC_HFRTC: DBB1 bitstream number cfg lands at 0x80 + n") {
        // Default bitstream = 0 → DBB1 = 0x80.
        {
                Timecode src(Timecode::Mode(Timecode::NDF120), 0, 0, 0, 5);
                AncTranslator::PacketsResult built = buildHfrtc(src);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseHfrtc(built.first().front());
                REQUIRE(parsed.second().isOk());
                AncAtc out = parsed.first().get<AncAtc>();
                CHECK(out.payloadType() == 0x80u);
                CHECK(out.hfrtcBitstream() == 0);
        }
        // Cfg override → DBB1 = 0x80 | n.
        for (uint8_t n = 0; n < 16; ++n) {
                CAPTURE(static_cast<int>(n));
                Timecode src(Timecode::Mode(Timecode::NDF120), 0, 0, 0, 5);
                AncTranslateConfig cfg;
                cfg.set(AncTranslateConfig::AtcHfrtcBitstreamNumber, n);
                AncTranslator::PacketsResult built = buildHfrtc(src, cfg);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseHfrtc(built.first().front());
                REQUIRE(parsed.second().isOk());
                AncAtc out = parsed.first().get<AncAtc>();
                CHECK(out.payloadType() == static_cast<uint8_t>(0x80u | n));
                CHECK(out.hfrtcBitstream() == n);
        }
}

TEST_CASE("ATC_HFRTC: DBB2 round-trips with libvtc-computed (sf-count, N)") {
        // Every standard HFR format must round-trip through DBB2 since
        // the (sf-count, N) tuple uniquely identifies the format on the
        // wire.  Spot-check each entry.
        struct Case {
                        Timecode::TimecodeType type;
                        uint8_t                expectedSfCount; // 0=24, 1=25, 2=30
                        uint8_t                expectedN;       // ST 12-3 N value
        };
        const Case cases[] = {
                {Timecode::NDF72, 0, 3},
                {Timecode::NDF96, 0, 4},
                {Timecode::NDF100, 1, 4},
                {Timecode::NDF120, 2, 4},
                {Timecode::DF120, 2, 4},
                {Timecode::NDF120_24x5, 0, 5},
        };
        for (const Case &c : cases) {
                CAPTURE(static_cast<int>(c.type));
                Timecode src(Timecode::Mode(c.type), 0, 0, 0, 0);
                AncTranslator::PacketsResult built = buildHfrtc(src);
                REQUIRE(built.second().isOk());
                AncTranslator::ParseResult parsed = parseHfrtc(built.first().front());
                REQUIRE(parsed.second().isOk());
                AncAtc                  out = parsed.first().get<AncAtc>();
                AncAtc::HfrtcDbb2 d   = AncAtc::dbb2DecodeHfrtc(out.dbb2());
                CHECK(d.superFrameCount == c.expectedSfCount);
                CHECK(d.n == c.expectedN);
        }
}

TEST_CASE("ATC_HFRTC: rejects non-HFR timecode") {
        // Building HFRTC from a 30 fps timecode is malformed input.
        Timecode src(Timecode::Mode(Timecode::NDF30), 1, 0, 0, 0);
        AncTranslator::PacketsResult built = buildHfrtc(src);
        REQUIRE(built.second().isError());
        CHECK(built.second().code() == Error::FormatMismatch);
}

TEST_CASE("ATC_HFRTC: rejects bitstream number > 15") {
        Timecode src(Timecode::Mode(Timecode::NDF120), 0, 0, 0, 0);
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcHfrtcBitstreamNumber, uint8_t(16));
        AncTranslator::PacketsResult built = buildHfrtc(src, cfg);
        REQUIRE(built.second().isError());
        CHECK(built.second().code() == Error::OutOfRange);
}

TEST_CASE("ATC_HFRTC: sync policy Drop returns empty list") {
        Timecode src(Timecode::Mode(Timecode::NDF120), 0, 0, 0, 0);
        AncTranslator::PacketsResult built = buildHfrtc(src);
        REQUIRE(built.second().isOk());
        AncTranslator t;
        auto res = t.applySyncPolicy(built.first().front(),
                                     FrameSyncDisposition::drop(), 0);
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

TEST_CASE("ATC_HFRTC: sync policy Play returns packet unchanged") {
        Timecode src(Timecode::Mode(Timecode::NDF120), 0, 0, 0, 0);
        AncTranslator::PacketsResult built = buildHfrtc(src);
        REQUIRE(built.second().isOk());
        AncTranslator t;
        auto res = t.applySyncPolicy(built.first().front(),
                                     FrameSyncDisposition::play(), 0);
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        CHECK(res.first().front() == built.first().front());
}

TEST_CASE("ATC_HFRTC: sync policy Repeat advances physical frames") {
        // At 120p ++tc walks 0..119; the sync-policy increment by
        // repeatIndex must reach the matching physical-frame value.
        Timecode src(Timecode::Mode(Timecode::NDF120), 0, 0, 0, 5);
        AncTranslator::PacketsResult built = buildHfrtc(src);
        REQUIRE(built.second().isOk());
        AncTranslator t;
        for (uint8_t i = 0; i < 4; ++i) {
                CAPTURE(static_cast<int>(i));
                auto res = t.applySyncPolicy(built.first().front(),
                                             FrameSyncDisposition::repeat(4), i);
                REQUIRE(res.second().isOk());
                REQUIRE(res.first().size() == 1);
                AncTranslator::ParseResult parsed = parseHfrtc(res.first().front());
                REQUIRE(parsed.second().isOk());
                Timecode out = parsed.first().get<AncAtc>().timecode();
                CHECK(out.frame() == 5u + i);
        }
}

TEST_CASE("ATC_HFRTC: AncAtc payloadType override wins over cfg bitstream") {
        // Caller-supplied DBB1 in the HFRTC range overrides the cfg.
        Timecode tc(Timecode::Mode(Timecode::NDF120), 0, 0, 0, 0);
        AncAtc src(tc);
        src.setPayloadType(static_cast<uint8_t>(0x80u | 7u));  // bitstream 7
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AtcHfrtcBitstreamNumber, uint8_t(3));
        AncTranslator t(cfg);
        AncTranslator::PacketsResult built =
                t.build(Variant(src), AncFormat(AncFormat::AtcHfrtc), AncTransport::St291);
        REQUIRE(built.second().isOk());
        AncTranslator::ParseResult parsed = parseHfrtc(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncAtc out = parsed.first().get<AncAtc>();
        CHECK(out.hfrtcBitstream() == 7);
}

TEST_CASE("ATC_HFRTC: userbit nibbles round-trip across the codeword") {
        // Per ST 12-3 §6.2 + Table 3, the bit positions ST 12-1 used for
        // the color-frame flag (bit 11) and the BGF triple (bits 43, 58, 59)
        // are reassigned to the sub-frame identifier bits at HFR rates.
        // So at HFR colorFrame and BGF mode are NOT carried — only the
        // eight user-bit nibbles (bits 4-7, 12-15, 20-23, 28-31, 36-39,
        // 44-47, 52-55, 60-63) survive a parse → build round-trip.
        Timecode tc(Timecode::Mode(Timecode::NDF120), 1, 2, 3, 47);
        const TimecodeUserbits::Nibbles src = {0x1, 0x2, 0x3, 0x4,
                                                0x5, 0x6, 0x7, 0x8};
        // Set a nominal BGF mode + colorFrame on the source so we can
        // confirm they get stripped on the wire.
        tc.setColorFrame(true);
        tc.setUserbits(TimecodeUserbits::fromNibbles(src, TimecodeUserbits::ClockTime));
        AncAtc src_atc(tc);
        AncTranslator::PacketsResult built = buildHfrtc(src_atc);
        REQUIRE(built.second().isOk());
        AncTranslator::ParseResult parsed = parseHfrtc(built.first().front());
        REQUIRE(parsed.second().isOk());
        AncAtc out = parsed.first().get<AncAtc>();
        CHECK(out.timecode().frame() == 47u);
        for (size_t i = 0; i < TimecodeUserbits::NibbleCount; ++i) {
                CAPTURE(i);
                CHECK(out.timecode().userbits().nibbles()[i] == src[i]);
        }
        // Color frame and BGF mode are dropped by the HFR codeword layout
        // — the bit slots ST 12-1 used for them are repurposed for sub-frame
        // identifier bits.  Callers needing per-physical-frame BGF
        // semantics must use a parallel out-of-band channel.
        CHECK_FALSE(out.timecode().colorFrame());
        CHECK(out.timecode().userbits().mode() == TimecodeUserbits::Unspecified);
}

// ============================================================================
// Phase 7 — explicit ST 12-3 Table 3 sub-frame bit-position assertions
// ============================================================================
//
// libvtc's pack/unpack is exercised round-trip elsewhere; this group of
// tests pins down the *wire* bit positions so a future libvtc refactor
// that swaps positions silently fails here instead of much further
// downstream.
//
// Codeword bit → UDW data byte bit mapping (each UDW carries one nibble
// in bits 4-7):
//   bit 11 of codeword = UDW 2 (index 2) bit 7
//   bit 27 of codeword = UDW 6 (index 6) bit 7
//   bit 43 of codeword = UDW 10 (index 10) bit 7
//   bit 58 of codeword = UDW 14 (index 14) bit 6
//   bit 59 of codeword = UDW 14 (index 14) bit 7
//
// Per ST 12-3 Table 3:
//   24×N / 30×N families: bit 11=sf_2, bit 27=sf_1
//   25×N family:          bit 11=sf_2, bit 59=sf_1
//   24×5 (NDF120_24x5):   bit 11=sf_2, bit 27=sf_1, bit 43=sf_3

namespace {

        // Pull the parity-stripped data byte out of a 10-bit UDW.
        uint8_t hfrtcUdwByte(uint16_t w) { return static_cast<uint8_t>(w & 0xFFu); }

        // Build a HFRTC packet at @p type with physical frame @p physFrame,
        // return the parsed wire UDWs.
        List<uint16_t> hfrtcWireUdws(Timecode::TimecodeType type, uint32_t physFrame) {
                Timecode src(Timecode::Mode(type), 0, 0, 0,
                             static_cast<Timecode::DigitType>(physFrame));
                AncTranslator t;
                AncTranslator::PacketsResult built =
                        t.build(Variant(src), AncFormat(AncFormat::AtcHfrtc), AncTransport::St291);
                REQUIRE(built.second().isOk());
                Result<St291Packet> rp = St291Packet::from(built.first().front());
                REQUIRE(rp.second().isOk());
                List<uint16_t> w = rp.first().udw();
                REQUIRE(w.size() == 16);
                return w;
        }

} // namespace

TEST_CASE("ATC_HFRTC Phase 7 — 30×N family: sf_1 at bit 27, sf_2 at bit 11 (NDF120)") {
        // N=4 → sub_frame 0..3 = (sf_1 MSB, sf_2 LSB).
        struct Expect {
                        uint32_t physFrame;
                        uint8_t  sf_1; // bit 27 (UDW 6 b7)
                        uint8_t  sf_2; // bit 11 (UDW 2 b7)
        };
        const Expect cases[] = {
                {0, 0, 0}, // sub_frame=0 → 0b00
                {1, 0, 1}, // sub_frame=1 → 0b01
                {2, 1, 0}, // sub_frame=2 → 0b10
                {3, 1, 1}, // sub_frame=3 → 0b11
        };
        for (const Expect &e : cases) {
                CAPTURE(e.physFrame);
                List<uint16_t> w = hfrtcWireUdws(Timecode::NDF120, e.physFrame);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[2]) >> 7) & 1u) == e.sf_2);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[6]) >> 7) & 1u) == e.sf_1);
                // Bits 43, 58, 59 are zero at N=4.
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[10]) >> 7) & 1u) == 0);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[14]) >> 6) & 1u) == 0);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[14]) >> 7) & 1u) == 0);
        }
}

TEST_CASE("ATC_HFRTC Phase 7 — 24×N family bit layout (NDF72, N=3 / NDF96, N=4)") {
        // Same bit layout as 30×N: sf_1 at bit 27, sf_2 at bit 11.
        for (Timecode::TimecodeType type : {Timecode::NDF72, Timecode::NDF96}) {
                CAPTURE(static_cast<int>(type));
                const uint32_t n = (type == Timecode::NDF72) ? 3u : 4u;
                for (uint32_t sub = 0; sub < n; ++sub) {
                        CAPTURE(sub);
                        List<uint16_t> w = hfrtcWireUdws(type, sub);
                        const uint8_t sf_1 = static_cast<uint8_t>((sub >> 1) & 1u);
                        const uint8_t sf_2 = static_cast<uint8_t>(sub & 1u);
                        CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[2]) >> 7) & 1u) == sf_2);
                        CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[6]) >> 7) & 1u) == sf_1);
                        CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[14]) >> 7) & 1u) == 0);
                }
        }
}

TEST_CASE("ATC_HFRTC Phase 7 — 25×N family swap: sf_1 at bit 59, sf_2 at bit 11 (NDF100)") {
        // 25fps base reassigns sf_1 to bit 59 (the polarity slot for
        // 25fps base ST 12-1 layout) instead of bit 27.  This is the
        // load-bearing wire-position difference between the 25×N and
        // 24×N/30×N families.
        struct Expect {
                        uint32_t physFrame;
                        uint8_t  sf_1; // bit 59 (UDW 14 b7) — the swap
                        uint8_t  sf_2; // bit 11 (UDW 2 b7)
        };
        const Expect cases[] = {
                {0, 0, 0},
                {1, 0, 1},
                {2, 1, 0},
                {3, 1, 1},
        };
        for (const Expect &e : cases) {
                CAPTURE(e.physFrame);
                List<uint16_t> w = hfrtcWireUdws(Timecode::NDF100, e.physFrame);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[2]) >> 7) & 1u) == e.sf_2);
                // Bit 27 (24×N/30×N sf_1 slot) must NOT carry sf_1 here.
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[6]) >> 7) & 1u) == 0);
                // Bit 59 carries sf_1 for 25×N.
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[14]) >> 7) & 1u) == e.sf_1);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[14]) >> 6) & 1u) == 0);
        }
}

TEST_CASE("ATC_HFRTC Phase 7 — 24×5 family: sf_1 at bit 27, sf_2 at bit 11, sf_3 at bit 43 (NDF120_24x5)") {
        // N=5 → sub_frame 0..4 = (sf_1 << 2 | sf_2 << 1 | sf_3).  All
        // three bits land on the 24×5 layout positions per Table 3.
        struct Expect {
                        uint32_t physFrame;
                        uint8_t  sf_1; // bit 27
                        uint8_t  sf_2; // bit 11
                        uint8_t  sf_3; // bit 43
        };
        const Expect cases[] = {
                {0, 0, 0, 0}, // sub=0 = 0b000
                {1, 0, 0, 1}, // sub=1 = 0b001
                {2, 0, 1, 0}, // sub=2 = 0b010
                {3, 0, 1, 1}, // sub=3 = 0b011
                {4, 1, 0, 0}, // sub=4 = 0b100
        };
        for (const Expect &e : cases) {
                CAPTURE(e.physFrame);
                List<uint16_t> w = hfrtcWireUdws(Timecode::NDF120_24x5, e.physFrame);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[2]) >> 7) & 1u) == e.sf_2);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[6]) >> 7) & 1u) == e.sf_1);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[10]) >> 7) & 1u) == e.sf_3);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[14]) >> 7) & 1u) == 0);
                CHECK(static_cast<uint8_t>((hfrtcUdwByte(w[14]) >> 6) & 1u) == 0);
        }
}
