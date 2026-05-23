/**
 * @file      st2110video.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/enums.h>
#include <promeki/framerate.h>
#include <promeki/map.h>
#include <promeki/pixelaspect.h>
#include <promeki/pixelformat.h>
#include <promeki/st2110video.h>
#include <cstring>
#include <vector>

using namespace promeki;

// Every (sampling, depth) pair that ST 2110-20:2022 §6.2 Tables 1-4
// lists, in (octets, pixels, samples, bitsPerSample, depthIsFloat)
// form.  The expected values come straight from the tables.
namespace {

struct PgroupExpect {
                St2110Sampling sampling;
                St2110Depth    depth;
                size_t         octets;
                size_t         pixels;
                size_t         samples;
                int            bits;
                bool           isFloat;
};

const std::vector<PgroupExpect> kSupportedCombos = {
        // 4:4:4 triplet group — applies identically to YCbCr / CLYCbCr / ICtCp / RGB.
        {St2110Sampling::YCbCr444,   St2110Depth::Bits8,   3, 1,  3,  8, false},
        {St2110Sampling::YCbCr444,   St2110Depth::Bits10, 15, 4, 12, 10, false},
        {St2110Sampling::YCbCr444,   St2110Depth::Bits12,  9, 2,  6, 12, false},
        {St2110Sampling::YCbCr444,   St2110Depth::Bits16,  6, 1,  3, 16, false},
        {St2110Sampling::YCbCr444,   St2110Depth::Bits16f, 6, 1,  3, 16, true},

        {St2110Sampling::CLYCbCr444, St2110Depth::Bits8,   3, 1,  3,  8, false},
        {St2110Sampling::CLYCbCr444, St2110Depth::Bits10, 15, 4, 12, 10, false},
        {St2110Sampling::CLYCbCr444, St2110Depth::Bits12,  9, 2,  6, 12, false},
        {St2110Sampling::CLYCbCr444, St2110Depth::Bits16,  6, 1,  3, 16, false},
        {St2110Sampling::CLYCbCr444, St2110Depth::Bits16f, 6, 1,  3, 16, true},

        {St2110Sampling::ICtCp444,   St2110Depth::Bits8,   3, 1,  3,  8, false},
        {St2110Sampling::ICtCp444,   St2110Depth::Bits10, 15, 4, 12, 10, false},
        {St2110Sampling::ICtCp444,   St2110Depth::Bits12,  9, 2,  6, 12, false},
        {St2110Sampling::ICtCp444,   St2110Depth::Bits16,  6, 1,  3, 16, false},
        {St2110Sampling::ICtCp444,   St2110Depth::Bits16f, 6, 1,  3, 16, true},

        {St2110Sampling::Rgb,        St2110Depth::Bits8,   3, 1,  3,  8, false},
        {St2110Sampling::Rgb,        St2110Depth::Bits10, 15, 4, 12, 10, false},
        {St2110Sampling::Rgb,        St2110Depth::Bits12,  9, 2,  6, 12, false},
        {St2110Sampling::Rgb,        St2110Depth::Bits16,  6, 1,  3, 16, false},
        {St2110Sampling::Rgb,        St2110Depth::Bits16f, 6, 1,  3, 16, true},

        // 4:2:2 pair group.
        {St2110Sampling::YCbCr422,   St2110Depth::Bits8,   4, 2, 4,  8, false},
        {St2110Sampling::YCbCr422,   St2110Depth::Bits10,  5, 2, 4, 10, false},
        {St2110Sampling::YCbCr422,   St2110Depth::Bits12,  6, 2, 4, 12, false},
        {St2110Sampling::YCbCr422,   St2110Depth::Bits16,  8, 2, 4, 16, false},
        {St2110Sampling::YCbCr422,   St2110Depth::Bits16f, 8, 2, 4, 16, true},

        {St2110Sampling::CLYCbCr422, St2110Depth::Bits8,   4, 2, 4,  8, false},
        {St2110Sampling::CLYCbCr422, St2110Depth::Bits10,  5, 2, 4, 10, false},
        {St2110Sampling::CLYCbCr422, St2110Depth::Bits12,  6, 2, 4, 12, false},
        {St2110Sampling::CLYCbCr422, St2110Depth::Bits16,  8, 2, 4, 16, false},
        {St2110Sampling::CLYCbCr422, St2110Depth::Bits16f, 8, 2, 4, 16, true},

        {St2110Sampling::ICtCp422,   St2110Depth::Bits8,   4, 2, 4,  8, false},
        {St2110Sampling::ICtCp422,   St2110Depth::Bits10,  5, 2, 4, 10, false},
        {St2110Sampling::ICtCp422,   St2110Depth::Bits12,  6, 2, 4, 12, false},
        {St2110Sampling::ICtCp422,   St2110Depth::Bits16,  8, 2, 4, 16, false},
        {St2110Sampling::ICtCp422,   St2110Depth::Bits16f, 8, 2, 4, 16, true},

        // 4:2:0 group — progressive only, depths 8/10/12 (no 16 / 16f).
        {St2110Sampling::YCbCr420,   St2110Depth::Bits8,   6, 4,  6,  8, false},
        {St2110Sampling::YCbCr420,   St2110Depth::Bits10, 15, 8, 12, 10, false},
        {St2110Sampling::YCbCr420,   St2110Depth::Bits12,  9, 4,  6, 12, false},

        {St2110Sampling::CLYCbCr420, St2110Depth::Bits8,   6, 4,  6,  8, false},
        {St2110Sampling::CLYCbCr420, St2110Depth::Bits10, 15, 8, 12, 10, false},
        {St2110Sampling::CLYCbCr420, St2110Depth::Bits12,  9, 4,  6, 12, false},

        {St2110Sampling::ICtCp420,   St2110Depth::Bits8,   6, 4,  6,  8, false},
        {St2110Sampling::ICtCp420,   St2110Depth::Bits10, 15, 8, 12, 10, false},
        {St2110Sampling::ICtCp420,   St2110Depth::Bits12,  9, 4,  6, 12, false},

        // XYZ — only depths 12 / 16 / 16f per §6.2.3.
        {St2110Sampling::Xyz,        St2110Depth::Bits12,  9, 2, 6, 12, false},
        {St2110Sampling::Xyz,        St2110Depth::Bits16,  6, 1, 3, 16, false},
        {St2110Sampling::Xyz,        St2110Depth::Bits16f, 6, 1, 3, 16, true},

        // Key — all depths.
        {St2110Sampling::Key,        St2110Depth::Bits8,   1, 1, 1,  8, false},
        {St2110Sampling::Key,        St2110Depth::Bits10,  5, 4, 4, 10, false},
        {St2110Sampling::Key,        St2110Depth::Bits12,  3, 2, 2, 12, false},
        {St2110Sampling::Key,        St2110Depth::Bits16,  2, 1, 1, 16, false},
        {St2110Sampling::Key,        St2110Depth::Bits16f, 2, 1, 1, 16, true},
};

// (sampling, depth) combos the standard does not define and which
// @ref St2110Video::pgroup must therefore reject.
const std::vector<std::pair<St2110Sampling, St2110Depth>> kUnsupportedCombos = {
        // 4:2:0 has no 16 or 16f entries (§6.2.5).
        {St2110Sampling::YCbCr420,   St2110Depth::Bits16},
        {St2110Sampling::YCbCr420,   St2110Depth::Bits16f},
        {St2110Sampling::CLYCbCr420, St2110Depth::Bits16},
        {St2110Sampling::CLYCbCr420, St2110Depth::Bits16f},
        {St2110Sampling::ICtCp420,   St2110Depth::Bits16},
        {St2110Sampling::ICtCp420,   St2110Depth::Bits16f},
        // XYZ skips depths 8 and 10 (§6.2.3).
        {St2110Sampling::Xyz, St2110Depth::Bits8},
        {St2110Sampling::Xyz, St2110Depth::Bits10},
        // Invalid sampling never resolves regardless of depth.
        {St2110Sampling::Invalid, St2110Depth::Bits10},
        // Invalid depth never resolves regardless of sampling.
        {St2110Sampling::YCbCr422, St2110Depth::Invalid},
};

} // namespace

TEST_CASE("St2110Video: pgroup table matches §6.2 tables 1-4") {
        for (const auto &exp : kSupportedCombos) {
                CAPTURE(exp.sampling.valueName());
                CAPTURE(exp.depth.valueName());
                const auto pg = St2110Video::pgroup(exp.sampling, exp.depth);
                CHECK(pg.octets == exp.octets);
                CHECK(pg.pixels == exp.pixels);
                CHECK(pg.samples == exp.samples);
                // Self-consistency: samples == octets * 8 / bits.
                CHECK(pg.samples == pg.octets * 8u / static_cast<size_t>(exp.bits));
                CHECK(St2110Video::isSupported(exp.sampling, exp.depth));
                CHECK(St2110Video::bitsPerSample(exp.depth) == exp.bits);
                CHECK(St2110Video::isFloatDepth(exp.depth) == exp.isFloat);
        }
}

TEST_CASE("St2110Video: pgroup rejects undefined combos") {
        for (const auto &p : kUnsupportedCombos) {
                CAPTURE(p.first.valueName());
                CAPTURE(p.second.valueName());
                const auto pg = St2110Video::pgroup(p.first, p.second);
                CHECK(pg.octets == 0u);
                CHECK(pg.pixels == 0u);
                CHECK(pg.samples == 0u);
                CHECK_FALSE(St2110Video::isSupported(p.first, p.second));
        }
}

TEST_CASE("St2110Video: 4:2:2 10-bit pgroup matches Figure 3") {
        // §6.2.4 Figure 3 spells out the bit layout of a 5-octet
        // 4:2:2 10-bit pgroup:
        //
        //   bits 0-9   = C'B
        //   bits 10-19 = Y0'
        //   bits 20-29 = C'R
        //   bits 30-39 = Y1'
        //
        // Pack a known sample quad and verify byte-by-byte that the
        // wire bytes carry the bits in MSB-first order.
        const uint16_t samples[4] = {
                0x3FFu, // C'B = 1111111111 (all ones)
                0x000u, // Y0' = all zeros
                0x2A0u, // C'R = 1010100000
                0x155u, // Y1' = 0101010101
        };
        uint8_t buf[5] = {};
        const size_t n = St2110Video::packSamplesBE(buf, sizeof(buf), samples, 4, 10);
        REQUIRE(n == 5u);

        // Reconstruct the 40-bit stream and split into samples.
        const uint64_t stream =
                (static_cast<uint64_t>(buf[0]) << 32) | (static_cast<uint64_t>(buf[1]) << 24)
                | (static_cast<uint64_t>(buf[2]) << 16) | (static_cast<uint64_t>(buf[3]) << 8)
                | static_cast<uint64_t>(buf[4]);
        CHECK(((stream >> 30) & 0x3FFu) == 0x3FFu);
        CHECK(((stream >> 20) & 0x3FFu) == 0x000u);
        CHECK(((stream >> 10) & 0x3FFu) == 0x2A0u);
        CHECK(((stream >>  0) & 0x3FFu) == 0x155u);

        // Round-trip back through unpack.
        uint16_t back[4] = {};
        const size_t consumed = St2110Video::unpackSamplesBE(buf, sizeof(buf), back, 4, 10);
        CHECK(consumed == 5u);
        CHECK(back[0] == 0x3FFu);
        CHECK(back[1] == 0x000u);
        CHECK(back[2] == 0x2A0u);
        CHECK(back[3] == 0x155u);
}

TEST_CASE("St2110Video: 4:2:0 10-bit 15-octet pgroup matches Figure 5") {
        // §6.2.5 Figure 5 enumerates the sample order:
        //   Y'00, Y'01, Y'10, Y'11, C'B00, C'R00,
        //   Y'02, Y'03, Y'12, Y'13, C'B01, C'R01
        // 12 samples × 10 bits = 120 bits = 15 octets.
        std::vector<uint16_t> samples = {
                0x010u, 0x020u, 0x030u, 0x040u, 0x150u, 0x260u,
                0x070u, 0x080u, 0x090u, 0x0A0u, 0x1B0u, 0x2C0u,
        };
        uint8_t      buf[15] = {};
        const size_t n = St2110Video::packSamplesBE(buf, sizeof(buf), samples.data(), 12, 10);
        REQUIRE(n == 15u);

        uint16_t back[12] = {};
        const size_t consumed = St2110Video::unpackSamplesBE(buf, sizeof(buf), back, 12, 10);
        REQUIRE(consumed == 15u);
        for (size_t i = 0; i < samples.size(); i++) CHECK(back[i] == samples[i]);
}

TEST_CASE("St2110Video: bit-packing round-trip across every depth") {
        // Random-ish but deterministic sample stream, masked to the
        // appropriate bit width for each depth.  Round-trip
        // pack → unpack for every depth defined by §7.4.2 and check
        // every sample comes back bit-exact.
        const int depths[] = {8, 10, 12, 16};
        for (int bits : depths) {
                CAPTURE(bits);
                const uint16_t      mask = static_cast<uint16_t>((1u << bits) - 1u);
                const size_t        nSamples = 47; // odd so we exercise the trailing-byte path
                std::vector<uint16_t> samples(nSamples);
                for (size_t i = 0; i < nSamples; i++) {
                        samples[i] = static_cast<uint16_t>((i * 0x1234u + 0xCAFEu) & mask);
                }
                const size_t needBytes = (nSamples * static_cast<size_t>(bits) + 7) / 8;
                std::vector<uint8_t> buf(needBytes);
                const size_t written = St2110Video::packSamplesBE(buf.data(), buf.size(),
                                                                   samples.data(), nSamples, bits);
                CHECK(written == needBytes);

                std::vector<uint16_t> back(nSamples);
                const size_t read = St2110Video::unpackSamplesBE(buf.data(), buf.size(),
                                                                  back.data(), nSamples, bits);
                CHECK(read == needBytes);
                for (size_t i = 0; i < nSamples; i++) CHECK(back[i] == samples[i]);
        }
}

TEST_CASE("St2110Video: 16f carries binary16 bit patterns verbatim") {
        // 16f is just 16 bits of binary16 — the codec does not
        // interpret the float value, it copies the bits big-endian.
        // Pack a few binary16 patterns and verify byte-by-byte.
        const uint16_t samples[3] = {
                0x3C00u, // 1.0 in binary16
                0xBC00u, // -1.0
                0x7BFFu, // +max finite
        };
        uint8_t buf[6] = {};
        const size_t n = St2110Video::packSamplesBE(buf, sizeof(buf), samples, 3, 16);
        REQUIRE(n == 6u);
        CHECK(buf[0] == 0x3Cu); CHECK(buf[1] == 0x00u);
        CHECK(buf[2] == 0xBCu); CHECK(buf[3] == 0x00u);
        CHECK(buf[4] == 0x7Bu); CHECK(buf[5] == 0xFFu);

        uint16_t back[3] = {};
        const size_t consumed = St2110Video::unpackSamplesBE(buf, sizeof(buf), back, 3, 16);
        CHECK(consumed == 6u);
        for (size_t i = 0; i < 3; i++) CHECK(back[i] == samples[i]);
}

TEST_CASE("St2110Video: packSamplesBE rejects invalid input") {
        uint16_t s = 0;
        uint8_t  out[4] = {};
        SUBCASE("null dst") {
                CHECK(St2110Video::packSamplesBE(nullptr, 4, &s, 1, 8) == 0u);
        }
        SUBCASE("null samples") {
                CHECK(St2110Video::packSamplesBE(out, 4, nullptr, 1, 8) == 0u);
        }
        SUBCASE("zero samples") {
                CHECK(St2110Video::packSamplesBE(out, 4, &s, 0, 8) == 0u);
        }
        SUBCASE("depthBits 0") {
                CHECK(St2110Video::packSamplesBE(out, 4, &s, 1, 0) == 0u);
        }
        SUBCASE("depthBits 17") {
                CHECK(St2110Video::packSamplesBE(out, 4, &s, 1, 17) == 0u);
        }
        SUBCASE("dst too small") {
                uint16_t big[4] = {0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu};
                uint8_t  tiny[1] = {};
                CHECK(St2110Video::packSamplesBE(tiny, 1, big, 4, 16) == 0u);
        }
}

TEST_CASE("St2110Video: row helpers cover every supported (sampling, depth)") {
        // For every supported (sampling, depth), build a row of
        // exactly 8 × pgroup.pixels = 8 pgroup widths.  That gives
        // enough variation to surface off-by-one errors in the row
        // helpers while staying compact enough to run quickly across
        // ~50 combos.
        for (const auto &exp : kSupportedCombos) {
                CAPTURE(exp.sampling.valueName());
                CAPTURE(exp.depth.valueName());
                const auto pg = St2110Video::pgroup(exp.sampling, exp.depth);
                REQUIRE(pg.octets > 0u);

                const size_t nGroups = 8u;
                const size_t nPixels = pg.pixels * nGroups;
                const size_t nSamples = pg.samples * nGroups;
                const size_t expectOctets = pg.octets * nGroups;

                // Build a deterministic sample stream masked to bit
                // width so the comparison after unpack is exact.
                const uint16_t        mask = static_cast<uint16_t>((1u << exp.bits) - 1u);
                std::vector<uint16_t> samples(nSamples);
                for (size_t i = 0; i < nSamples; i++) {
                        samples[i] = static_cast<uint16_t>((i * 0x9E37u + 0xBEEFu) & mask);
                }

                std::vector<uint8_t> wire(expectOctets);
                const size_t         written = St2110Video::packRow(exp.sampling, exp.depth,
                                                                    nPixels, samples.data(),
                                                                    wire.data(), wire.size());
                CHECK(written == expectOctets);
                CHECK(St2110Video::rowOctets(exp.sampling, exp.depth, nPixels) == expectOctets);
                CHECK(St2110Video::rowSamples(exp.sampling, exp.depth, nPixels) == nSamples);

                std::vector<uint16_t> back(nSamples);
                const size_t          consumed = St2110Video::unpackRow(exp.sampling, exp.depth,
                                                                        nPixels, wire.data(),
                                                                        wire.size(), back.data());
                CHECK(consumed == expectOctets);
                for (size_t i = 0; i < nSamples; i++) CHECK(back[i] == samples[i]);
        }
}

TEST_CASE("St2110Video: row helpers reject misaligned pixel counts") {
        // §6.2.2: pgroups shall not be fragmented across packets and
        // shall not represent samples from more than one image source
        // array line.  The row helpers enforce the former by refusing
        // any @c nPixels that isn't a multiple of @c pgroup.pixels.
        SUBCASE("4:2:2 demands even pixel count") {
                uint16_t s[4] = {0, 0, 0, 0};
                uint8_t  out[5] = {};
                CHECK(St2110Video::packRow(St2110Sampling::YCbCr422, St2110Depth::Bits10,
                                           /*nPixels=*/3, s, out, sizeof(out)) == 0u);
                CHECK(St2110Video::rowOctets(St2110Sampling::YCbCr422, St2110Depth::Bits10, 3) == 0u);
        }
        SUBCASE("4:2:0 10-bit demands multiple of 8 pixels") {
                uint16_t s[12] = {};
                uint8_t  out[15] = {};
                CHECK(St2110Video::packRow(St2110Sampling::YCbCr420, St2110Depth::Bits10,
                                           /*nPixels=*/6, s, out, sizeof(out)) == 0u);
                CHECK(St2110Video::rowOctets(St2110Sampling::YCbCr420, St2110Depth::Bits10, 4) == 0u);
        }
        SUBCASE("unsupported combo always rejected") {
                uint16_t s[6] = {};
                uint8_t  out[15] = {};
                CHECK(St2110Video::packRow(St2110Sampling::YCbCr420, St2110Depth::Bits16,
                                           /*nPixels=*/4, s, out, sizeof(out)) == 0u);
                CHECK(St2110Video::rowOctets(St2110Sampling::YCbCr420, St2110Depth::Bits16, 4) == 0u);
        }
}

TEST_CASE("St2110Video: 4:2:0 10-bit Annex A packet size matches table") {
        // ST 2110-20 Annex A Table A.1: 4:2:0 10-bit BPM packet
        // carries 672 pixels at 1260 octets.  Verify the row helpers
        // produce that byte count for that pixel count.
        const size_t nPixels = 672u;
        const auto   pg = St2110Video::pgroup(St2110Sampling::YCbCr420, St2110Depth::Bits10);
        REQUIRE(pg.octets == 15u);
        REQUIRE(pg.pixels == 8u);
        // 672 pixels / 8 pixels-per-pgroup = 84 pgroups × 15 octets = 1260 octets.
        CHECK(St2110Video::rowOctets(St2110Sampling::YCbCr420, St2110Depth::Bits10, nPixels)
              == 1260u);
}

TEST_CASE("St2110Video: every §6.2 pgroup divides the §6.3.3 BPM block size cleanly (except pg=8)") {
        // ST 2110-20 §6.3.3 requires every BPM packet to be a multiple
        // of 180 octets and §6.2.1 requires each SRD's data to be
        // pgroup-aligned.  For BPM to be cleanly representable the
        // pgroup size must divide 180 evenly.  Verify the property
        // holds for every (sampling, depth) pair in the §6.2 tables,
        // calling out the 4:2:2/16 and 4:2:2/16f outliers (pg=8) as
        // the documented BPM-not-supported case (RtpPayloadRawVideo
        // falls back to GPM with a one-shot warning for those).
        struct Row {
                St2110Sampling sampling;
                St2110Depth    depth;
                bool           bpmExpected; // false → pgroup is 8 octets, BPM N/A.
        };
        const Row rows[] = {
                {St2110Sampling::YCbCr444, St2110Depth::Bits8,  true},
                {St2110Sampling::YCbCr444, St2110Depth::Bits10, true},
                {St2110Sampling::YCbCr444, St2110Depth::Bits12, true},
                {St2110Sampling::YCbCr444, St2110Depth::Bits16, true},
                {St2110Sampling::YCbCr444, St2110Depth::Bits16f, true},
                {St2110Sampling::YCbCr422, St2110Depth::Bits8,  true},
                {St2110Sampling::YCbCr422, St2110Depth::Bits10, true},
                {St2110Sampling::YCbCr422, St2110Depth::Bits12, true},
                {St2110Sampling::YCbCr422, St2110Depth::Bits16, false}, // pg = 8 → 180/8 = 22.5
                {St2110Sampling::YCbCr422, St2110Depth::Bits16f, false}, // pg = 8 → 180/8 = 22.5
                {St2110Sampling::YCbCr420, St2110Depth::Bits8,  true},
                {St2110Sampling::YCbCr420, St2110Depth::Bits10, true},
                {St2110Sampling::YCbCr420, St2110Depth::Bits12, true},
                {St2110Sampling::Rgb,      St2110Depth::Bits8,  true},
                {St2110Sampling::Rgb,      St2110Depth::Bits10, true},
                {St2110Sampling::Rgb,      St2110Depth::Bits12, true},
                {St2110Sampling::Xyz,      St2110Depth::Bits12, true},
                {St2110Sampling::Xyz,      St2110Depth::Bits16f, true},
                {St2110Sampling::Key,      St2110Depth::Bits8,  true},
                {St2110Sampling::Key,      St2110Depth::Bits10, true},
                {St2110Sampling::Key,      St2110Depth::Bits12, true},
                {St2110Sampling::Key,      St2110Depth::Bits16, true},
        };
        for (const auto &r : rows) {
                CAPTURE(St2110Video::samplingWire(r.sampling).cstr());
                CAPTURE(St2110Video::depthWire(r.depth).cstr());
                const auto pg = St2110Video::pgroup(r.sampling, r.depth);
                REQUIRE(pg.octets > 0u);
                const bool divides = (180u % pg.octets) == 0u;
                CHECK(divides == r.bpmExpected);
        }
}

// ============================================================================
// SDP wire-form mapping and Fmtp round-trip
// ============================================================================

TEST_CASE("St2110Video: enum ↔ wire form round-trips") {
        SUBCASE("Sampling") {
                const St2110Sampling vs[] = {
                        St2110Sampling::YCbCr444, St2110Sampling::YCbCr422, St2110Sampling::YCbCr420,
                        St2110Sampling::CLYCbCr444, St2110Sampling::CLYCbCr422, St2110Sampling::CLYCbCr420,
                        St2110Sampling::ICtCp444, St2110Sampling::ICtCp422, St2110Sampling::ICtCp420,
                        St2110Sampling::Rgb, St2110Sampling::Xyz, St2110Sampling::Key,
                };
                for (auto v : vs) {
                        const String wire = St2110Video::samplingWire(v);
                        CAPTURE(wire);
                        CHECK(!wire.isEmpty());
                        CHECK(St2110Video::samplingFromWire(wire).value() == v.value());
                }
                CHECK(St2110Video::samplingWire(St2110Sampling::Invalid).isEmpty());
                CHECK(St2110Video::samplingFromWire("nonsense") == St2110Sampling::Invalid);
        }

        SUBCASE("Depth") {
                const St2110Depth vs[] = {
                        St2110Depth::Bits8, St2110Depth::Bits10, St2110Depth::Bits12,
                        St2110Depth::Bits16, St2110Depth::Bits16f,
                };
                for (auto v : vs) {
                        const String wire = St2110Video::depthWire(v);
                        CAPTURE(wire);
                        CHECK(!wire.isEmpty());
                        CHECK(St2110Video::depthFromWire(wire).value() == v.value());
                }
                // The standard's wire form is bare decimals; the 16f form
                // is the only suffixed entry.
                CHECK(St2110Video::depthWire(St2110Depth::Bits8) == "8");
                CHECK(St2110Video::depthWire(St2110Depth::Bits16f) == "16f");
        }

        SUBCASE("Colorimetry") {
                const St2110Colorimetry vs[] = {
                        St2110Colorimetry::Bt601, St2110Colorimetry::Bt709, St2110Colorimetry::Bt2020,
                        St2110Colorimetry::Bt2100, St2110Colorimetry::St2065_1, St2110Colorimetry::St2065_3,
                        St2110Colorimetry::Unspecified, St2110Colorimetry::Xyz, St2110Colorimetry::Alpha,
                };
                for (auto v : vs) {
                        const String wire = St2110Video::colorimetryWire(v);
                        CHECK(!wire.isEmpty());
                        CHECK(St2110Video::colorimetryFromWire(wire).value() == v.value());
                }
                CHECK(St2110Video::colorimetryWire(St2110Colorimetry::St2065_1) == "ST2065-1");
        }

        SUBCASE("TCS") {
                const St2110Tcs vs[] = {
                        St2110Tcs::Sdr, St2110Tcs::Pq, St2110Tcs::Hlg, St2110Tcs::Linear,
                        St2110Tcs::Bt2100LinPq, St2110Tcs::Bt2100LinHlg, St2110Tcs::St2065_1,
                        St2110Tcs::St428_1, St2110Tcs::Density, St2110Tcs::St2115LogS3,
                        St2110Tcs::Unspecified,
                };
                for (auto v : vs) {
                        const String wire = St2110Video::tcsWire(v);
                        CHECK(!wire.isEmpty());
                        CHECK(St2110Video::tcsFromWire(wire).value() == v.value());
                }
                CHECK(St2110Video::tcsWire(St2110Tcs::St2115LogS3) == "ST2115LOGS3");
                CHECK(St2110Video::tcsWire(St2110Tcs::Bt2100LinPq) == "BT2100LINPQ");
        }

        SUBCASE("Range") {
                CHECK(St2110Video::rangeWire(St2110Range::Narrow) == "NARROW");
                CHECK(St2110Video::rangeWire(St2110Range::Full) == "FULL");
                CHECK(St2110Video::rangeWire(St2110Range::FullProtect) == "FULLPROTECT");
                CHECK(St2110Video::rangeFromWire("FULL") == St2110Range::Full);
                CHECK(St2110Video::rangeFromWire("xxx") == St2110Range::Invalid);
        }

        SUBCASE("PackingMode") {
                CHECK(St2110Video::packingModeWire(St2110PackingMode::Gpm) == "2110GPM");
                CHECK(St2110Video::packingModeWire(St2110PackingMode::Bpm) == "2110BPM");
                CHECK(St2110Video::packingModeFromWire("2110GPM") == St2110PackingMode::Gpm);
                CHECK(St2110Video::packingModeFromWire("2110BPM") == St2110PackingMode::Bpm);
        }
}

TEST_CASE("St2110Video: SSN follows §7.2 rule") {
        // §7.2: SSN is ST2110-20:2017 unless colorimetry=ALPHA or
        // TCS=ST2115LOGS3, in which case it's ST2110-20:2022.
        CHECK(St2110Video::ssnFor(St2110Colorimetry::Bt709, St2110Tcs::Sdr) == "ST2110-20:2017");
        CHECK(St2110Video::ssnFor(St2110Colorimetry::Bt2020, St2110Tcs::Pq) == "ST2110-20:2017");
        CHECK(St2110Video::ssnFor(St2110Colorimetry::Alpha, St2110Tcs::Sdr) == "ST2110-20:2022");
        CHECK(St2110Video::ssnFor(St2110Colorimetry::Bt709, St2110Tcs::St2115LogS3) == "ST2110-20:2022");
}

TEST_CASE("St2110Video: frame-rate wire form follows §7.2") {
        // §7.2: integer rates as a single decimal, non-integer as num/den.
        CHECK(St2110Video::frameRateToWire(FrameRate(FrameRate::FPS_25)) == "25");
        CHECK(St2110Video::frameRateToWire(FrameRate(FrameRate::FPS_60)) == "60");
        CHECK(St2110Video::frameRateToWire(FrameRate(FrameRate::FPS_29_97)) == "30000/1001");
        CHECK(St2110Video::frameRateToWire(FrameRate(FrameRate::FPS_59_94)) == "60000/1001");
        CHECK(St2110Video::frameRateToWire(FrameRate()).isEmpty());
}

TEST_CASE("St2110Video: fmtpScanMode <-> setFmtpScanMode round-trip (§7.3)") {
        // Round-trip every VideoScanMode through the Fmtp flag pair
        // (interlace, segmented) and back.  The interlaced sub-variants
        // (EvenFirst / OddFirst) project down to plain Interlaced on
        // the wire — SDP §7.3 has no field-order signal, so the
        // round-trip is lossy in that one direction (which is
        // expected; field order lives in MediaConfig::VideoScanMode).
        struct Case {
                VideoScanMode in;
                VideoScanMode wireRoundTrip;
                bool          expectInterlace;
                bool          expectSegmented;
        };
        const Case cs[] = {
                {VideoScanMode::Progressive,         VideoScanMode::Progressive, false, false},
                {VideoScanMode::Unknown,             VideoScanMode::Progressive, false, false},
                {VideoScanMode::Interlaced,          VideoScanMode::Interlaced,  true,  false},
                {VideoScanMode::InterlacedEvenFirst, VideoScanMode::Interlaced,  true,  false},
                {VideoScanMode::InterlacedOddFirst,  VideoScanMode::Interlaced,  true,  false},
                {VideoScanMode::PsF,                 VideoScanMode::PsF,         true,  true},
        };
        for (const auto &c : cs) {
                CAPTURE(c.in.toString().cstr());
                St2110Video::Fmtp f;
                St2110Video::setFmtpScanMode(f, c.in);
                CHECK(f.interlace == c.expectInterlace);
                CHECK(f.segmented == c.expectSegmented);
                CHECK(St2110Video::fmtpScanMode(f).value() == c.wireRoundTrip.value());
        }
}

TEST_CASE("St2110Video::fromFmtp rejects `segmented` without `interlace` (§7.3)") {
        Map<String, String> params;
        params.insert(String("sampling"), String("YCbCr-4:2:2"));
        params.insert(String("depth"),    String("10"));
        params.insert(String("width"),    String("1920"));
        params.insert(String("height"),   String("1080"));
        params.insert(String("exactframerate"), String("60000/1001"));
        params.insert(String("colorimetry"), String("BT709"));
        params.insert(String("PM"),       String("2110GPM"));
        params.insert(String("segmented"), String());

        auto f = St2110Video::fromFmtp(params);
        CHECK(f.interlace == false);
        // segmented must have been cleared by the parser since it
        // appeared without an `interlace` companion.
        CHECK(f.segmented == false);
}

TEST_CASE("St2110Video::fromFmtp rejects 4:2:0 + interlace/segmented (§6.2.5)") {
        SUBCASE("4:2:0 + interlace clears both flags") {
                Map<String, String> params;
                params.insert(String("sampling"), String("YCbCr-4:2:0"));
                params.insert(String("depth"),    String("10"));
                params.insert(String("width"),    String("1920"));
                params.insert(String("height"),   String("1080"));
                params.insert(String("exactframerate"), String("60000/1001"));
                params.insert(String("colorimetry"), String("BT709"));
                params.insert(String("PM"),       String("2110GPM"));
                params.insert(String("interlace"), String());

                auto f = St2110Video::fromFmtp(params);
                CHECK(f.sampling == St2110Sampling::YCbCr420);
                CHECK(f.interlace == false);
                CHECK(f.segmented == false);
        }
        SUBCASE("4:2:0 + interlace + segmented (PsF) clears both flags") {
                Map<String, String> params;
                params.insert(String("sampling"), String("YCbCr-4:2:0"));
                params.insert(String("depth"),    String("12"));
                params.insert(String("width"),    String("1920"));
                params.insert(String("height"),   String("1080"));
                params.insert(String("exactframerate"), String("60000/1001"));
                params.insert(String("colorimetry"), String("BT709"));
                params.insert(String("PM"),       String("2110GPM"));
                params.insert(String("interlace"), String());
                params.insert(String("segmented"), String());

                auto f = St2110Video::fromFmtp(params);
                CHECK(f.interlace == false);
                CHECK(f.segmented == false);
        }
}

TEST_CASE("St2110Video: interlace / segmented round-trip through toFmtp / fromFmtp") {
        // Build a populated Fmtp with PsF scan mode, serialise, parse
        // back, and verify both flags survive the round-trip.
        St2110Video::Fmtp f;
        f.sampling = St2110Sampling::YCbCr422;
        f.depth = St2110Depth::Bits10;
        f.width = 1920;
        f.height = 1080;
        f.exactFrameRate = FrameRate(FrameRate::FPS_30);
        f.colorimetry = St2110Colorimetry::Bt709;
        f.pm = St2110PackingMode::Gpm;
        St2110Video::setFmtpScanMode(f, VideoScanMode::PsF);
        REQUIRE(f.interlace);
        REQUIRE(f.segmented);

        const String body = St2110Video::toFmtp(f);
        CAPTURE(body);
        CHECK(body.contains("interlace"));
        CHECK(body.contains("segmented"));

        // Reverse: pre-split into a name → value map mimicking
        // SdpMediaDescription::fmtpParameters().
        Map<String, String> params;
        const StringList    fields = body.split("; ");
        for (const auto &entry : fields) {
                const ssize_t eq = entry.find('=');
                if (eq < 0) {
                        params.insert(entry, String());
                } else {
                        params.insert(entry.mid(0, eq), entry.mid(eq + 1));
                }
        }
        auto back = St2110Video::fromFmtp(params);
        CHECK(back.interlace == true);
        CHECK(back.segmented == true);
        CHECK(St2110Video::fmtpScanMode(back) == VideoScanMode::PsF);
}

TEST_CASE("St2110Video::toFmtp emits §7 example verbatim") {
        // From §7.7: m=video 30000 RTP/AVP 112 with fmtp:
        //   sampling=YCbCr-4:2:2; width=1280; height=720;
        //   exactframerate=60000/1001; depth=10; TCS=SDR;
        //   colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017
        //
        // Our canonical-order emission puts the required parameters
        // in §7.2 order (sampling, depth, width, height,
        // exactframerate, colorimetry, PM, SSN) — different from
        // the spec's example ordering but equally valid per §7.1
        // (order is unconstrained).  TCS=SDR is the default and we
        // omit it.
        St2110Video::Fmtp f;
        f.sampling = St2110Sampling::YCbCr422;
        f.depth = St2110Depth::Bits10;
        f.width = 1280;
        f.height = 720;
        f.exactFrameRate = FrameRate(FrameRate::FPS_59_94);
        f.colorimetry = St2110Colorimetry::Bt709;
        f.pm = St2110PackingMode::Gpm;
        // tcs defaults to Sdr — omitted on emission.

        const String body = St2110Video::toFmtp(f);
        CAPTURE(body);
        CHECK(body == "sampling=YCbCr-4:2:2; depth=10; width=1280; height=720; "
                      "exactframerate=60000/1001; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017");
}

TEST_CASE("St2110Video::toFmtp ↔ fromFmtp round-trip") {
        struct Case {
                        const char        *name;
                        St2110Video::Fmtp  fmtp;
        };

        std::vector<Case> cases;
        {
                St2110Video::Fmtp f;
                f.sampling = St2110Sampling::YCbCr422;
                f.depth = St2110Depth::Bits10;
                f.width = 1920;
                f.height = 1080;
                f.exactFrameRate = FrameRate(FrameRate::FPS_59_94);
                f.colorimetry = St2110Colorimetry::Bt709;
                f.pm = St2110PackingMode::Gpm;
                cases.push_back({"baseline 1080p59.94 4:2:2 10-bit", f});
        }
        {
                St2110Video::Fmtp f;
                f.sampling = St2110Sampling::Rgb;
                f.depth = St2110Depth::Bits8;
                f.width = 1280;
                f.height = 720;
                f.exactFrameRate = FrameRate(FrameRate::FPS_50);
                f.colorimetry = St2110Colorimetry::Bt709;
                f.pm = St2110PackingMode::Gpm;
                f.range = St2110Range::Full; // override
                cases.push_back({"720p50 RGB8 FULL range", f});
        }
        {
                St2110Video::Fmtp f;
                f.sampling = St2110Sampling::YCbCr420;
                f.depth = St2110Depth::Bits10;
                f.width = 3840;
                f.height = 2160;
                f.exactFrameRate = FrameRate(FrameRate::FPS_60);
                f.colorimetry = St2110Colorimetry::Bt2100;
                f.tcs = St2110Tcs::Pq;
                f.pm = St2110PackingMode::Bpm;
                f.maxUdp = 8960;
                f.par = PixelAspect(16, 15);
                cases.push_back({"UHDp60 4:2:0 10-bit PQ BPM with PAR + MAXUDP", f});
        }
        {
                St2110Video::Fmtp f;
                f.sampling = St2110Sampling::YCbCr422;
                f.depth = St2110Depth::Bits10;
                f.width = 1920;
                f.height = 1080;
                f.exactFrameRate = FrameRate(FrameRate::FPS_29_97);
                f.colorimetry = St2110Colorimetry::Bt709;
                f.pm = St2110PackingMode::Gpm;
                f.interlace = true; // 1080i
                cases.push_back({"1080i59.94 (interlaced)", f});
        }
        {
                St2110Video::Fmtp f;
                f.sampling = St2110Sampling::Key;
                f.depth = St2110Depth::Bits10;
                f.width = 1920;
                f.height = 1080;
                f.exactFrameRate = FrameRate(FrameRate::FPS_59_94);
                f.colorimetry = St2110Colorimetry::Alpha; // forces 2022 SSN
                f.pm = St2110PackingMode::Gpm;
                cases.push_back({"Key signal forces ST2110-20:2022 SSN", f});
        }

        for (const auto &c : cases) {
                CAPTURE(c.name);
                const String body = St2110Video::toFmtp(c.fmtp);
                CHECK(!body.isEmpty());

                // Split body into a name=value map the way SdpMediaDescription
                // does, then parse via fromFmtp.
                Map<String, String> params;
                size_t              cursor = 0;
                while (cursor < body.size()) {
                        const size_t sep = body.find(';', cursor);
                        const String chunk = (sep == String::npos) ? body.mid(cursor) : body.mid(cursor, sep - cursor);
                        // Trim leading whitespace
                        const char *cs = chunk.cstr();
                        size_t      start = 0;
                        while (start < chunk.size() && (cs[start] == ' ' || cs[start] == '\t')) start++;
                        const String trimmed = chunk.mid(start);
                        const size_t eq = trimmed.find('=');
                        if (eq == String::npos) {
                                if (!trimmed.isEmpty()) params[trimmed] = String();
                        } else {
                                params[trimmed.left(eq)] = trimmed.mid(eq + 1);
                        }
                        cursor = (sep == String::npos) ? body.size() : sep + 1;
                }

                const auto parsed = St2110Video::fromFmtp(params);
                CHECK(parsed.sampling.value() == c.fmtp.sampling.value());
                CHECK(parsed.depth.value() == c.fmtp.depth.value());
                CHECK(parsed.width == c.fmtp.width);
                CHECK(parsed.height == c.fmtp.height);
                CHECK(parsed.exactFrameRate.numerator() == c.fmtp.exactFrameRate.numerator());
                CHECK(parsed.exactFrameRate.denominator() == c.fmtp.exactFrameRate.denominator());
                CHECK(parsed.colorimetry.value() == c.fmtp.colorimetry.value());
                CHECK(parsed.pm.value() == c.fmtp.pm.value());
                CHECK(parsed.interlace == c.fmtp.interlace);
                CHECK(parsed.segmented == c.fmtp.segmented);
                CHECK(parsed.tcs.value() == c.fmtp.tcs.value());
                CHECK(parsed.range.value() == c.fmtp.range.value());
                CHECK(parsed.maxUdp == c.fmtp.maxUdp);
                CHECK(parsed.par == c.fmtp.par);
        }
}

TEST_CASE("St2110Video::toFmtp rejects incomplete required parameters") {
        // Missing sampling, depth, colorimetry, width/height, or
        // exactframerate yields an empty body so the SDP layer
        // doesn't emit a malformed fmtp line.
        St2110Video::Fmtp f;
        CHECK(St2110Video::toFmtp(f).isEmpty()); // nothing set

        f.sampling = St2110Sampling::YCbCr422;
        CHECK(St2110Video::toFmtp(f).isEmpty()); // depth still Invalid

        f.depth = St2110Depth::Bits10;
        CHECK(St2110Video::toFmtp(f).isEmpty()); // colorimetry still Invalid

        f.colorimetry = St2110Colorimetry::Bt709;
        CHECK(St2110Video::toFmtp(f).isEmpty()); // dimensions still 0

        f.width = 1920;
        f.height = 1080;
        CHECK(St2110Video::toFmtp(f).isEmpty()); // exactFrameRate still invalid

        f.exactFrameRate = FrameRate(FrameRate::FPS_59_94);
        CHECK(!St2110Video::toFmtp(f).isEmpty());
}

#if PROMEKI_ENABLE_PROAV
TEST_CASE("St2110Video::bridgeForPixelFormat") {
        SUBCASE("YUV8 422 UYVY Rec.709 → 4:2:2 8-bit BT709 NARROW SDR") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat::YUV8_422_UYVY_Rec709);
                CHECK(br.sampling == St2110Sampling::YCbCr422);
                CHECK(br.depth == St2110Depth::Bits8);
                CHECK(br.colorimetry == St2110Colorimetry::Bt709);
                CHECK(br.tcs == St2110Tcs::Sdr);
                CHECK(br.range == St2110Range::Narrow);
        }
        SUBCASE("RGB8 sRGB → RGB 8-bit BT709 FULL") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat::RGB8_sRGB);
                CHECK(br.sampling == St2110Sampling::Rgb);
                CHECK(br.depth == St2110Depth::Bits8);
                CHECK(br.colorimetry == St2110Colorimetry::Bt709);
                CHECK(br.range == St2110Range::Full);
        }
        SUBCASE("YUV10 422 UYVY BE Rec.709 → 4:2:2 10-bit BT709 NARROW") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat::YUV10_422_UYVY_BE_Rec709);
                CHECK(br.sampling == St2110Sampling::YCbCr422);
                CHECK(br.depth == St2110Depth::Bits10);
                CHECK(br.colorimetry == St2110Colorimetry::Bt709);
                CHECK(br.range == St2110Range::Narrow);
        }
        SUBCASE("YUV10 422 UYVY BE Rec.2020 → 4:2:2 10-bit BT2020 NARROW") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat::YUV10_422_UYVY_BE_Rec2020);
                CHECK(br.sampling == St2110Sampling::YCbCr422);
                CHECK(br.depth == St2110Depth::Bits10);
                CHECK(br.colorimetry == St2110Colorimetry::Bt2020);
        }
        SUBCASE("Invalid PixelFormat → sampling Invalid") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat());
                CHECK(br.sampling == St2110Sampling::Invalid);
        }
        SUBCASE("Mono10 sRGB → KEY 10-bit ALPHA") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat::Mono10_BE_sRGB);
                CHECK(br.sampling == St2110Sampling::Key);
                CHECK(br.depth == St2110Depth::Bits10);
                CHECK(br.colorimetry == St2110Colorimetry::Alpha);
        }
        SUBCASE("Wire-format 4:2:2 10-bit Rec.709 round-trips through itself") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat::YUV10_422_2110_Rec709);
                CHECK(br.sampling == St2110Sampling::YCbCr422);
                CHECK(br.depth == St2110Depth::Bits10);
                CHECK(br.colorimetry == St2110Colorimetry::Bt709);
                CHECK(br.range == St2110Range::Narrow);
        }
        SUBCASE("Wire-format 4:2:0 12-bit Rec.709 → YCbCr420 + Bits12") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat::YUV12_420_2110_Rec709);
                CHECK(br.sampling == St2110Sampling::YCbCr420);
                CHECK(br.depth == St2110Depth::Bits12);
                CHECK(br.colorimetry == St2110Colorimetry::Bt709);
        }
        SUBCASE("Wire-format XYZ 12-bit → XYZ sampling + XYZ colorimetry") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat::XYZ12_BE_2110);
                CHECK(br.sampling == St2110Sampling::Xyz);
                CHECK(br.depth == St2110Depth::Bits12);
                CHECK(br.colorimetry == St2110Colorimetry::Xyz);
        }
        SUBCASE("Wire-format Mono 10-bit → KEY + Alpha colorimetry") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat::Mono10_BE_2110_sRGB);
                CHECK(br.sampling == St2110Sampling::Key);
                CHECK(br.depth == St2110Depth::Bits10);
                CHECK(br.colorimetry == St2110Colorimetry::Alpha);
        }
        SUBCASE("Wire-format RGB 10-bit → RGB FULL") {
                const auto br = St2110Video::bridgeForPixelFormat(PixelFormat::RGB10_BE_2110_sRGB);
                CHECK(br.sampling == St2110Sampling::Rgb);
                CHECK(br.depth == St2110Depth::Bits10);
                CHECK(br.range == St2110Range::Full);
        }
}

TEST_CASE("ST 2110-20 wire-format PixelMemLayout geometry matches §6.2 pgroup table") {
        struct LayoutCheck {
                        PixelMemLayout::ID id;
                        size_t             pixelsPerBlock;
                        size_t             bytesPerBlock;
                        int                bitsPerComp;
                        size_t             compCount;
        };
        // Every new ST 2110-20 wire-format layout, paired with the
        // pgroup geometry §6.2 Tables 1-4 say it must satisfy.
        const std::vector<LayoutCheck> kChecks = {
                {PixelMemLayout::I_3x10_BE_2110, 4, 15, 10, 3},
                {PixelMemLayout::I_3x12_BE_2110, 2, 9, 12, 3},
                {PixelMemLayout::I_422_UYVY_3x10_BE_2110, 2, 5, 10, 3},
                {PixelMemLayout::I_422_UYVY_3x12_BE_2110, 2, 6, 12, 3},
                {PixelMemLayout::I_422_UYVY_3xF16_BE, 2, 8, 16, 3},
                {PixelMemLayout::I_1x10_BE_2110, 4, 5, 10, 1},
                {PixelMemLayout::I_1x12_BE_2110, 2, 3, 12, 1},
        };
        for (const auto &c : kChecks) {
                PixelMemLayout pf(c.id);
                CAPTURE(pf.name().cstr());
                CHECK(pf.isValid());
                CHECK(pf.pixelsPerBlock() == c.pixelsPerBlock);
                CHECK(pf.bytesPerBlock() == c.bytesPerBlock);
                REQUIRE(pf.compCount() == c.compCount);
                CHECK(static_cast<int>(pf.compDesc(0).bits) == c.bitsPerComp);
        }
}

TEST_CASE("ST 2110-20 4:2:0 wire-format single-plane layout geometry") {
        // 4:2:0 wire is modelled single-plane per §6.2.5: each wire
        // byte row carries pgroups that span 2 image rows (one pair).
        // plane 0 has vSubsampling=2, so the plane has height/2 rows.
        struct Check {
                        PixelMemLayout::ID id;
                        size_t             pixelsPerBlock;
                        size_t             bytesPerBlock;
                        int                depthBits;
        };
        const std::vector<Check> kChecks = {
                {PixelMemLayout::I_420_BE_2110_8, 2, 6, 8},
                {PixelMemLayout::I_420_BE_2110_10, 4, 15, 10},
                {PixelMemLayout::I_420_BE_2110_12, 2, 9, 12},
        };
        for (const auto &c : kChecks) {
                PixelMemLayout pf(c.id);
                CAPTURE(pf.name().cstr());
                CHECK(pf.isValid());
                CHECK(pf.sampling() == PixelMemLayout::Sampling420);
                REQUIRE(pf.planeCount() == 1);
                REQUIRE(pf.compCount() == 3);
                CHECK(pf.pixelsPerBlock() == c.pixelsPerBlock);
                CHECK(pf.bytesPerBlock() == c.bytesPerBlock);
                CHECK(static_cast<int>(pf.compDesc(0).bits) == c.depthBits);
                CHECK(pf.planeDesc(0).vSubsampling == 2);
                // One wire row at 1920 pixels = (1920 / pixelsPerBlock) * bytesPerBlock.
                const size_t expected = (1920u / c.pixelsPerBlock) * c.bytesPerBlock;
                CHECK(pf.lineStride(0, 1920) == expected);
                // 1920×1080 image: 540 wire rows × stride bytes.
                CHECK(pf.planeSize(0, 1920, 1080) == expected * 540u);
        }
}

TEST_CASE("ST 2110-20 wire-format PixelFormats reference matching layouts") {
        struct Check {
                        PixelFormat::ID    pfId;
                        PixelMemLayout::ID layoutId;
        };
        const std::vector<Check> kChecks = {
                {PixelFormat::RGB10_BE_2110_sRGB, PixelMemLayout::I_3x10_BE_2110},
                {PixelFormat::RGB12_BE_2110_sRGB, PixelMemLayout::I_3x12_BE_2110},
                {PixelFormat::YUV10_2110_Rec709, PixelMemLayout::I_3x10_BE_2110},
                {PixelFormat::YUV12_2110_Rec709, PixelMemLayout::I_3x12_BE_2110},
                {PixelFormat::YUV10_422_2110_Rec709, PixelMemLayout::I_422_UYVY_3x10_BE_2110},
                {PixelFormat::YUV12_422_2110_Rec709, PixelMemLayout::I_422_UYVY_3x12_BE_2110},
                {PixelFormat::YUV8_420_2110_Rec709, PixelMemLayout::I_420_BE_2110_8},
                {PixelFormat::YUV10_420_2110_Rec709, PixelMemLayout::I_420_BE_2110_10},
                {PixelFormat::YUV12_420_2110_Rec709, PixelMemLayout::I_420_BE_2110_12},
                {PixelFormat::Mono10_BE_2110_sRGB, PixelMemLayout::I_1x10_BE_2110},
                {PixelFormat::Mono12_BE_2110_sRGB, PixelMemLayout::I_1x12_BE_2110},
                {PixelFormat::XYZ12_BE_2110, PixelMemLayout::I_3x12_BE_2110},
                {PixelFormat::XYZ16_BE_2110, PixelMemLayout::I_3x16_BE},
        };
        for (const auto &c : kChecks) {
                PixelFormat pd(c.pfId);
                CAPTURE(pd.name().cstr());
                CHECK(pd.isValid());
                CHECK(pd.memLayout().id() == c.layoutId);
        }
}

TEST_CASE("St2110Video::wirePixelFormatFor returns matching PixelFormat") {
        struct Probe {
                        St2110Sampling    sampling;
                        St2110Depth       depth;
                        St2110Colorimetry colorimetry;
                        PixelFormat::ID   expected;
        };
        const std::vector<Probe> kProbes = {
                // 4:2:2 across depths (Rec.709 baseline).
                {St2110Sampling::YCbCr422, St2110Depth::Bits8, St2110Colorimetry::Bt709,
                 PixelFormat::YUV8_422_UYVY_Rec709},
                {St2110Sampling::YCbCr422, St2110Depth::Bits10, St2110Colorimetry::Bt709,
                 PixelFormat::YUV10_422_2110_Rec709},
                {St2110Sampling::YCbCr422, St2110Depth::Bits12, St2110Colorimetry::Bt709,
                 PixelFormat::YUV12_422_2110_Rec709},
                {St2110Sampling::YCbCr422, St2110Depth::Bits16, St2110Colorimetry::Bt709,
                 PixelFormat::YUV16_422_UYVY_BE_Rec709},
                // 4:4:4 across depths.
                {St2110Sampling::YCbCr444, St2110Depth::Bits10, St2110Colorimetry::Bt709,
                 PixelFormat::YUV10_2110_Rec709},
                {St2110Sampling::YCbCr444, St2110Depth::Bits12, St2110Colorimetry::Bt709,
                 PixelFormat::YUV12_2110_Rec709},
                // 4:2:0 across depths.
                {St2110Sampling::YCbCr420, St2110Depth::Bits8, St2110Colorimetry::Bt709,
                 PixelFormat::YUV8_420_2110_Rec709},
                {St2110Sampling::YCbCr420, St2110Depth::Bits10, St2110Colorimetry::Bt709,
                 PixelFormat::YUV10_420_2110_Rec709},
                {St2110Sampling::YCbCr420, St2110Depth::Bits12, St2110Colorimetry::Bt709,
                 PixelFormat::YUV12_420_2110_Rec709},
                // RGB across depths.
                {St2110Sampling::Rgb, St2110Depth::Bits8, St2110Colorimetry::Bt709, PixelFormat::RGB8_sRGB},
                {St2110Sampling::Rgb, St2110Depth::Bits10, St2110Colorimetry::Bt709, PixelFormat::RGB10_BE_2110_sRGB},
                {St2110Sampling::Rgb, St2110Depth::Bits12, St2110Colorimetry::Bt709, PixelFormat::RGB12_BE_2110_sRGB},
                {St2110Sampling::Rgb, St2110Depth::Bits16, St2110Colorimetry::Bt709, PixelFormat::RGB16_BE_sRGB},
                // KEY across depths (colorimetry = Alpha per §7.5).
                {St2110Sampling::Key, St2110Depth::Bits8, St2110Colorimetry::Alpha, PixelFormat::Mono8_sRGB},
                {St2110Sampling::Key, St2110Depth::Bits10, St2110Colorimetry::Alpha, PixelFormat::Mono10_BE_2110_sRGB},
                {St2110Sampling::Key, St2110Depth::Bits12, St2110Colorimetry::Alpha, PixelFormat::Mono12_BE_2110_sRGB},
                {St2110Sampling::Key, St2110Depth::Bits16, St2110Colorimetry::Alpha, PixelFormat::Mono16_BE_sRGB},
                // XYZ (cinema): depths 12 / 16 / 16f only.
                {St2110Sampling::Xyz, St2110Depth::Bits12, St2110Colorimetry::Xyz, PixelFormat::XYZ12_BE_2110},
                {St2110Sampling::Xyz, St2110Depth::Bits16, St2110Colorimetry::Xyz, PixelFormat::XYZ16_BE_2110},
                {St2110Sampling::Xyz, St2110Depth::Bits16f, St2110Colorimetry::Xyz, PixelFormat::XYZ16_BE_2110},
        };
        for (const auto &p : kProbes) {
                const PixelFormat pd =
                        St2110Video::wirePixelFormatFor(p.sampling, p.depth, p.colorimetry, St2110Range::Invalid);
                CAPTURE(p.sampling.toString().cstr());
                CAPTURE(p.depth.toString().cstr());
                CHECK(pd.id() == p.expected);
        }
}

TEST_CASE("St2110Video::wirePixelFormatFor rejects unsupported combos") {
        // 4:2:0 + Bits16 is not in §6.2.5 — invalid.
        CHECK(St2110Video::wirePixelFormatFor(St2110Sampling::YCbCr420, St2110Depth::Bits16, St2110Colorimetry::Bt709,
                                              St2110Range::Invalid)
                      .id() == PixelFormat::Invalid);
        // XYZ + Bits8 is not in §6.2.3 — invalid.
        CHECK(St2110Video::wirePixelFormatFor(St2110Sampling::Xyz, St2110Depth::Bits8, St2110Colorimetry::Xyz,
                                              St2110Range::Invalid)
                      .id() == PixelFormat::Invalid);
        // Invalid sampling.
        CHECK(St2110Video::wirePixelFormatFor(St2110Sampling::Invalid, St2110Depth::Bits10, St2110Colorimetry::Bt709,
                                              St2110Range::Invalid)
                      .id() == PixelFormat::Invalid);
}

TEST_CASE("ST 2110-20 wire-format layout pixelsPerBlock × pgroup match St2110Video::pgroup()") {
        // Every wire-format PixelFormat's layout must agree with the
        // (sampling, depth) pgroup geometry St2110Video::pgroup
        // declares.  This binds the registry to the §6.2 reference.
        struct Probe {
                        PixelFormat::ID pfId;
                        St2110Sampling  sampling;
                        St2110Depth     depth;
        };
        const std::vector<Probe> kProbes = {
                {PixelFormat::YUV10_422_2110_Rec709, St2110Sampling::YCbCr422, St2110Depth::Bits10},
                {PixelFormat::YUV12_422_2110_Rec709, St2110Sampling::YCbCr422, St2110Depth::Bits12},
                {PixelFormat::YUV10_2110_Rec709, St2110Sampling::YCbCr444, St2110Depth::Bits10},
                {PixelFormat::YUV12_2110_Rec709, St2110Sampling::YCbCr444, St2110Depth::Bits12},
                {PixelFormat::RGB10_BE_2110_sRGB, St2110Sampling::Rgb, St2110Depth::Bits10},
                {PixelFormat::RGB12_BE_2110_sRGB, St2110Sampling::Rgb, St2110Depth::Bits12},
                {PixelFormat::Mono10_BE_2110_sRGB, St2110Sampling::Key, St2110Depth::Bits10},
                {PixelFormat::Mono12_BE_2110_sRGB, St2110Sampling::Key, St2110Depth::Bits12},
                {PixelFormat::XYZ12_BE_2110, St2110Sampling::Xyz, St2110Depth::Bits12},
        };
        for (const auto &p : kProbes) {
                const PixelFormat       pd(p.pfId);
                const PixelMemLayout    pl = pd.memLayout();
                const St2110Video::Pgroup pg = St2110Video::pgroup(p.sampling, p.depth);
                CAPTURE(pd.name().cstr());
                CHECK(pl.pixelsPerBlock() == pg.pixels);
                CHECK(pl.bytesPerBlock() == pg.octets);
        }
}
#endif
