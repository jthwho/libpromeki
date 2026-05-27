/**
 * @file      csc_st2110.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Scalar CSC fast-path correctness tests for the ST 2110-20 wire
 * formats.  Each test covers one (source PixelFormat, wire
 * PixelFormat) pair registered in @ref src/proav/csc/st2110.cpp;
 * verifies byte-exact pack output for known bit patterns and full
 * pack→unpack round-trip through the registered reverse kernel.
 */

#include <doctest/doctest.h>
#include <promeki/csccontext.h>
#include <promeki/cscregistry.h>
#include <promeki/pixelformat.h>
#include <promeki/st2110video.h>
#include <cstdint>
#include <vector>

using namespace promeki;

namespace {

// Build a UYVY BE 10-bit source row.  Each pair is 8 octets: Cb_BE Y0_BE Cr_BE Y1_BE,
// each component a 16-bit BE word with the 10-bit sample in the low 10 bits.
std::vector<uint8_t> uyvyRow10BE(const std::vector<uint16_t> &samples) {
        std::vector<uint8_t> row(samples.size() * 2);
        for (size_t i = 0; i < samples.size(); i++) {
                row[i * 2 + 0] = static_cast<uint8_t>((samples[i] >> 8) & 0x3u);
                row[i * 2 + 1] = static_cast<uint8_t>(samples[i] & 0xFFu);
        }
        return row;
}

std::vector<uint8_t> uyvyRow10LE(const std::vector<uint16_t> &samples) {
        std::vector<uint8_t> row(samples.size() * 2);
        for (size_t i = 0; i < samples.size(); i++) {
                row[i * 2 + 0] = static_cast<uint8_t>(samples[i] & 0xFFu);
                row[i * 2 + 1] = static_cast<uint8_t>((samples[i] >> 8) & 0x3u);
        }
        return row;
}

std::vector<uint8_t> uyvyRow12BE(const std::vector<uint16_t> &samples) {
        std::vector<uint8_t> row(samples.size() * 2);
        for (size_t i = 0; i < samples.size(); i++) {
                row[i * 2 + 0] = static_cast<uint8_t>((samples[i] >> 8) & 0xFu);
                row[i * 2 + 1] = static_cast<uint8_t>(samples[i] & 0xFFu);
        }
        return row;
}

std::vector<uint8_t> uyvyRow12LE(const std::vector<uint16_t> &samples) {
        std::vector<uint8_t> row(samples.size() * 2);
        for (size_t i = 0; i < samples.size(); i++) {
                row[i * 2 + 0] = static_cast<uint8_t>(samples[i] & 0xFFu);
                row[i * 2 + 1] = static_cast<uint8_t>((samples[i] >> 8) & 0xFu);
        }
        return row;
}

void invokeFastPath(const PixelFormat &src, const PixelFormat &dst, const void *srcBuf, void *dstBuf, size_t width) {
        CSCRegistry::LineFuncPtr fn = CSCRegistry::lookupFastPath(src, dst);
        REQUIRE(fn != nullptr);
        const void  *srcPlanes[1] = {srcBuf};
        void        *dstPlanes[1] = {dstBuf};
        const size_t srcStrides[1] = {0};
        const size_t dstStrides[1] = {0};
        CSCContext   ctx(width);
        fn(srcPlanes, srcStrides, dstPlanes, dstStrides, width, ctx);
}

} // namespace

TEST_CASE("CSC ST 2110: UYVY 10-bit BE → ST 2110 4:2:2 10-bit wire") {
        // §6.2 Figure 3 bit layout for 4:2:2 10-bit pgroup:
        //   Octet 0:  Cb[9..2]
        //   Octet 1:  Cb[1..0] Y0[9..4]
        //   Octet 2:  Y0[3..0] Cr[9..6]
        //   Octet 3:  Cr[5..0] Y1[9..8]
        //   Octet 4:  Y1[7..0]
        // Use a distinct value per sample so the bit field check is unambiguous.
        const std::vector<uint16_t> samples = {0x3AF, 0x125, 0x276, 0x199,  // pgroup 0
                                               0x000, 0x3FF, 0x040, 0x2AA}; // pgroup 1
        const auto src = uyvyRow10BE(samples);
        std::vector<uint8_t> wire(2 * 5, 0xFFu);
        invokeFastPath(PixelFormat::YUV10_422_UYVY_BE_Rec709, PixelFormat::YUV10_422_2110_Rec709, src.data(),
                       wire.data(), 4);

        // Re-pack the reference using the verified packSamplesBE helper.
        std::vector<uint8_t> expected(2 * 5);
        St2110Video::packSamplesBE(expected.data(), expected.size(), samples.data(), samples.size(), 10);
        for (size_t i = 0; i < wire.size(); i++) {
                CAPTURE(i);
                CHECK(static_cast<int>(wire[i]) == static_cast<int>(expected[i]));
        }
}

TEST_CASE("CSC ST 2110: UYVY 10-bit LE → ST 2110 4:2:2 10-bit wire") {
        const std::vector<uint16_t> samples = {0x3AF, 0x125, 0x276, 0x199, 0x000, 0x3FF, 0x040, 0x2AA};
        const auto                  src = uyvyRow10LE(samples);
        std::vector<uint8_t>        wire(2 * 5, 0u);
        invokeFastPath(PixelFormat::YUV10_422_UYVY_LE_Rec709, PixelFormat::YUV10_422_2110_Rec709, src.data(),
                       wire.data(), 4);
        std::vector<uint8_t> expected(2 * 5);
        St2110Video::packSamplesBE(expected.data(), expected.size(), samples.data(), samples.size(), 10);
        CHECK(wire == expected);
}

TEST_CASE("CSC ST 2110: UYVY 12-bit BE → ST 2110 4:2:2 12-bit wire") {
        const std::vector<uint16_t> samples = {0xABC, 0x123, 0xDEF, 0x456,  // pgroup 0
                                               0x000, 0xFFF, 0x080, 0xAAA}; // pgroup 1
        const auto                  src = uyvyRow12BE(samples);
        std::vector<uint8_t>        wire(2 * 6, 0u);
        invokeFastPath(PixelFormat::YUV12_422_UYVY_BE_Rec709, PixelFormat::YUV12_422_2110_Rec709, src.data(),
                       wire.data(), 4);
        std::vector<uint8_t> expected(2 * 6);
        St2110Video::packSamplesBE(expected.data(), expected.size(), samples.data(), samples.size(), 12);
        CHECK(wire == expected);
}

TEST_CASE("CSC ST 2110: UYVY 12-bit LE → ST 2110 4:2:2 12-bit wire") {
        const std::vector<uint16_t> samples = {0xABC, 0x123, 0xDEF, 0x456, 0x000, 0xFFF, 0x080, 0xAAA};
        const auto                  src = uyvyRow12LE(samples);
        std::vector<uint8_t>        wire(2 * 6, 0u);
        invokeFastPath(PixelFormat::YUV12_422_UYVY_LE_Rec709, PixelFormat::YUV12_422_2110_Rec709, src.data(),
                       wire.data(), 4);
        std::vector<uint8_t> expected(2 * 6);
        St2110Video::packSamplesBE(expected.data(), expected.size(), samples.data(), samples.size(), 12);
        CHECK(wire == expected);
}

TEST_CASE("CSC ST 2110: UYVY ↔ wire pack→unpack round-trip preserves samples") {
        // For every (BE/LE × 10/12) source, pack to wire then unpack
        // through the reverse kernel and confirm sample-exact match.
        struct RT {
                        PixelFormat::ID source;
                        PixelFormat::ID wire;
                        int             depth;
                        size_t          wireBytesPerPair;
                        size_t          srcBytesPerPair;
        };
        const std::vector<RT> kCases = {
                {PixelFormat::YUV10_422_UYVY_BE_Rec709, PixelFormat::YUV10_422_2110_Rec709, 10, 5, 8},
                {PixelFormat::YUV10_422_UYVY_LE_Rec709, PixelFormat::YUV10_422_2110_Rec709, 10, 5, 8},
                {PixelFormat::YUV12_422_UYVY_BE_Rec709, PixelFormat::YUV12_422_2110_Rec709, 12, 6, 8},
                {PixelFormat::YUV12_422_UYVY_LE_Rec709, PixelFormat::YUV12_422_2110_Rec709, 12, 6, 8},
        };

        // 64-pixel row with a deterministic pseudo-random sample
        // pattern that exercises every bit position.  Mask each
        // sample to the case's depth to keep the round-trip well-
        // defined.
        const size_t width = 64;
        const size_t pairs = width / 2;
        std::vector<uint16_t> samples(pairs * 4);
        for (size_t i = 0; i < samples.size(); i++) {
                // LCG-style mixing — independent of <random> overhead.
                const uint32_t mixed = static_cast<uint32_t>(i * 2654435761u);
                samples[i] = static_cast<uint16_t>(mixed >> 16);
        }

        for (const auto &rt : kCases) {
                CAPTURE(static_cast<int>(rt.source));
                CAPTURE(static_cast<int>(rt.wire));
                const uint16_t mask = static_cast<uint16_t>((1u << rt.depth) - 1u);
                std::vector<uint16_t> masked(samples.size());
                for (size_t i = 0; i < samples.size(); i++) masked[i] = samples[i] & mask;

                // Build the source row in the (LE/BE × 10/12)-specific layout.
                std::vector<uint8_t> srcRow(pairs * rt.srcBytesPerPair);
                for (size_t i = 0; i < pairs; i++) {
                        uint8_t *p = srcRow.data() + i * 8;
                        for (int s = 0; s < 4; s++) {
                                const uint16_t v = masked[i * 4 + s];
                                if (rt.source == PixelFormat::YUV10_422_UYVY_BE_Rec709 ||
                                    rt.source == PixelFormat::YUV12_422_UYVY_BE_Rec709) {
                                        p[s * 2 + 0] = static_cast<uint8_t>((v >> 8) & 0xFFu);
                                        p[s * 2 + 1] = static_cast<uint8_t>(v & 0xFFu);
                                } else {
                                        p[s * 2 + 0] = static_cast<uint8_t>(v & 0xFFu);
                                        p[s * 2 + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
                                }
                        }
                }

                // Pack to wire.
                std::vector<uint8_t> wire(pairs * rt.wireBytesPerPair);
                invokeFastPath(PixelFormat(rt.source), PixelFormat(rt.wire), srcRow.data(), wire.data(), width);

                // Unpack the wire back through the reverse kernel.
                std::vector<uint8_t> roundtrip(pairs * rt.srcBytesPerPair);
                invokeFastPath(PixelFormat(rt.wire), PixelFormat(rt.source), wire.data(), roundtrip.data(), width);

                // Source bytes must match exactly.
                CHECK(roundtrip == srcRow);
        }
}

TEST_CASE("CSC ST 2110: reverse kernels emit the expected UYVY layout") {
        // Spot-check that the reverse kernel writes the right bytes
        // for a known wire-format pgroup.  Sample values chosen so
        // the high and low bytes are both non-zero.
        const std::vector<uint16_t> samples = {0x3AF, 0x125, 0x276, 0x199};
        std::vector<uint8_t>        wire(5);
        St2110Video::packSamplesBE(wire.data(), wire.size(), samples.data(), samples.size(), 10);

        std::vector<uint8_t> uyvyBE(8);
        invokeFastPath(PixelFormat::YUV10_422_2110_Rec709, PixelFormat::YUV10_422_UYVY_BE_Rec709, wire.data(),
                       uyvyBE.data(), 2);
        // BE layout: high byte first.
        for (int s = 0; s < 4; s++) {
                const uint16_t v = samples[s];
                CHECK(static_cast<int>(uyvyBE[s * 2 + 0]) == static_cast<int>((v >> 8) & 0xFFu));
                CHECK(static_cast<int>(uyvyBE[s * 2 + 1]) == static_cast<int>(v & 0xFFu));
        }

        std::vector<uint8_t> uyvyLE(8);
        invokeFastPath(PixelFormat::YUV10_422_2110_Rec709, PixelFormat::YUV10_422_UYVY_LE_Rec709, wire.data(),
                       uyvyLE.data(), 2);
        // LE layout: low byte first.
        for (int s = 0; s < 4; s++) {
                const uint16_t v = samples[s];
                CHECK(static_cast<int>(uyvyLE[s * 2 + 0]) == static_cast<int>(v & 0xFFu));
                CHECK(static_cast<int>(uyvyLE[s * 2 + 1]) == static_cast<int>((v >> 8) & 0xFFu));
        }
}

TEST_CASE("CSC ST 2110: DPX-B 10-bit ↔ ST 2110 4:4:4 10-bit wire round-trip") {
        // 4 pixels (one wire pgroup) × 3 components per pixel; pick
        // distinct values per slot so the bit positions are unambiguous.
        const uint16_t pix[4][3] = {
                {0x2AB, 0x123, 0x3FF},
                {0x000, 0x1FF, 0x040},
                {0x355, 0x244, 0x199},
                {0x3FF, 0x000, 0x2AA},
        };
        // Build a DPX-B source row: 4 pixels × 4 octets each.
        std::vector<uint8_t> src(4 * 4, 0u);
        for (int i = 0; i < 4; i++) {
                uint32_t w = (uint32_t(pix[i][2] & 0x3FFu) << 20) | (uint32_t(pix[i][1] & 0x3FFu) << 10) |
                             uint32_t(pix[i][0] & 0x3FFu);
                src[i * 4 + 0] = uint8_t((w >> 24) & 0xFFu);
                src[i * 4 + 1] = uint8_t((w >> 16) & 0xFFu);
                src[i * 4 + 2] = uint8_t((w >> 8) & 0xFFu);
                src[i * 4 + 3] = uint8_t(w & 0xFFu);
        }
        // Pack to wire.
        std::vector<uint8_t> wire(15, 0u);
        invokeFastPath(PixelFormat::YUV10_DPX_B_Rec709, PixelFormat::YUV10_2110_Rec709, src.data(), wire.data(), 4);

        // Verify against St2110Video::packSamplesBE (12 samples in
        // c0/c1/c2 order, 4 pixels sequential).
        std::vector<uint16_t> samples(12);
        for (int i = 0; i < 4; i++) {
                samples[i * 3 + 0] = pix[i][0];
                samples[i * 3 + 1] = pix[i][1];
                samples[i * 3 + 2] = pix[i][2];
        }
        std::vector<uint8_t> expected(15);
        St2110Video::packSamplesBE(expected.data(), expected.size(), samples.data(), samples.size(), 10);
        CHECK(wire == expected);

        // Round-trip back to DPX-B.
        std::vector<uint8_t> back(4 * 4, 0u);
        invokeFastPath(PixelFormat::YUV10_2110_Rec709, PixelFormat::YUV10_DPX_B_Rec709, wire.data(), back.data(), 4);
        CHECK(back == src);
}

TEST_CASE("CSC ST 2110: Mono10 BE ↔ ST 2110 Key 10-bit wire") {
        // 4 samples (one pgroup) in 10-bit.
        const std::vector<uint16_t> samples = {0x3AF, 0x125, 0x040, 0x2AA};
        std::vector<uint8_t>        src(samples.size() * 2);
        for (size_t i = 0; i < samples.size(); i++) {
                src[i * 2 + 0] = uint8_t((samples[i] >> 8) & 0x3u);
                src[i * 2 + 1] = uint8_t(samples[i] & 0xFFu);
        }
        std::vector<uint8_t> wire(5, 0u);
        invokeFastPath(PixelFormat::Mono10_BE_sRGB, PixelFormat::Mono10_BE_2110_sRGB, src.data(), wire.data(), 4);
        std::vector<uint8_t> expected(5);
        St2110Video::packSamplesBE(expected.data(), expected.size(), samples.data(), samples.size(), 10);
        CHECK(wire == expected);

        // Round-trip back.
        std::vector<uint8_t> back(samples.size() * 2, 0u);
        invokeFastPath(PixelFormat::Mono10_BE_2110_sRGB, PixelFormat::Mono10_BE_sRGB, wire.data(), back.data(), 4);
        CHECK(back == src);
}

TEST_CASE("CSC ST 2110: Mono12 BE ↔ ST 2110 Key 12-bit wire") {
        const std::vector<uint16_t> samples = {0xABC, 0xDEF};
        std::vector<uint8_t>        src(samples.size() * 2);
        for (size_t i = 0; i < samples.size(); i++) {
                src[i * 2 + 0] = uint8_t((samples[i] >> 8) & 0xFu);
                src[i * 2 + 1] = uint8_t(samples[i] & 0xFFu);
        }
        std::vector<uint8_t> wire(3, 0u);
        invokeFastPath(PixelFormat::Mono12_BE_sRGB, PixelFormat::Mono12_BE_2110_sRGB, src.data(), wire.data(), 2);
        std::vector<uint8_t> expected(3);
        St2110Video::packSamplesBE(expected.data(), expected.size(), samples.data(), samples.size(), 12);
        CHECK(wire == expected);

        // Round-trip back.
        std::vector<uint8_t> back(samples.size() * 2, 0u);
        invokeFastPath(PixelFormat::Mono12_BE_2110_sRGB, PixelFormat::Mono12_BE_sRGB, wire.data(), back.data(), 2);
        CHECK(back == src);
}

TEST_CASE("CSC ST 2110: v210 → ST 2110 4:2:2 10-bit wire") {
        // One v210 block = 6 pixels = 3 wire pgroups.  Cover every
        // sample position with distinct 10-bit values.
        const uint16_t Cb0 = 0x123, Y0 = 0x040, Cr0 = 0x355, Y1 = 0x199;
        const uint16_t Cb2 = 0x2AB, Y2 = 0x1FF, Cr2 = 0x000, Y3 = 0x3FF;
        const uint16_t Cb4 = 0x244, Y4 = 0x080, Cr4 = 0x2AA, Y5 = 0x111;

        const auto packWord = [](uint32_t c0, uint32_t c1, uint32_t c2) -> uint32_t {
                return (c0 & 0x3FFu) | ((c1 & 0x3FFu) << 10) | ((c2 & 0x3FFu) << 20);
        };
        const uint32_t w0 = packWord(Cb0, Y0, Cr0);
        const uint32_t w1 = packWord(Y1, Cb2, Y2);
        const uint32_t w2 = packWord(Cr2, Y3, Cb4);
        const uint32_t w3 = packWord(Y4, Cr4, Y5);

        std::vector<uint8_t> src(16);
        auto                 storeLE = [&](size_t off, uint32_t w) {
                src[off + 0] = uint8_t(w & 0xFFu);
                src[off + 1] = uint8_t((w >> 8) & 0xFFu);
                src[off + 2] = uint8_t((w >> 16) & 0xFFu);
                src[off + 3] = uint8_t((w >> 24) & 0xFFu);
        };
        storeLE(0, w0);
        storeLE(4, w1);
        storeLE(8, w2);
        storeLE(12, w3);

        std::vector<uint8_t> wire(15, 0u);
        invokeFastPath(PixelFormat::YUV10_422_v210_Rec709, PixelFormat::YUV10_422_2110_Rec709, src.data(), wire.data(),
                       6);

        // Reference: build the 12-sample stream and pack to BE.  Wire
        // pgroup order is Cb Y0 Cr Y1 per 2 pixels.
        const std::vector<uint16_t> samples = {Cb0, Y0, Cr0, Y1,   // pgroup 0
                                               Cb2, Y2, Cr2, Y3,   // pgroup 1
                                               Cb4, Y4, Cr4, Y5};  // pgroup 2
        std::vector<uint8_t> expected(15);
        St2110Video::packSamplesBE(expected.data(), expected.size(), samples.data(), samples.size(), 10);
        CHECK(wire == expected);
}

TEST_CASE("CSC ST 2110: NV12 ↔ ST 2110 4:2:0 8-bit wire round-trip via row-pair iterator") {
        // The CSCPipeline::execute extension iterates by destination
        // plane 0 vSubsampling; we exercise the kernel directly with
        // hand-built source row pointers to keep the test focused on
        // the kernel's behavior.  4:2:0 8-bit wire pgroup: 6 octets /
        // 2×2 block = 4 samples (Y top0, Y top1, Y bot0, Y bot1) + Cb +
        // Cr.
        const size_t width = 8;
        // Build deterministic source rows.
        std::vector<uint8_t> yTop = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
        std::vector<uint8_t> yBot = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27};
        // NV12 chroma row is width samples (W/2 Cb + W/2 Cr interleaved at each chroma position).
        std::vector<uint8_t> chroma = {0xC0, 0xD0, 0xC1, 0xD1, 0xC2, 0xD2, 0xC3, 0xD3};

        // CSCRegistry kernel signature reads the second source row via
        // srcStrides[0]; concatenate the two Y rows so yTop + stride
        // points at yBot.
        std::vector<uint8_t> contiguousY(yTop.size() + yBot.size());
        std::memcpy(contiguousY.data(), yTop.data(), yTop.size());
        std::memcpy(contiguousY.data() + yTop.size(), yBot.data(), yBot.size());
        const void  *srcPlanes[2] = {contiguousY.data(), chroma.data()};
        const size_t srcStrides2[2] = {yTop.size(), chroma.size()};

        // Wire output: 4 pgroups (width/2) × 6 octets = 24 octets.
        std::vector<uint8_t> wire(width / 2 * 6, 0u);
        void                *dstPlanes[1] = {wire.data()};
        const size_t         dstStrides[1] = {wire.size()};
        CSCContext           ctx(width);
        auto fn = CSCRegistry::lookupFastPath(PixelFormat::YUV8_420_SemiPlanar_Rec709, PixelFormat::YUV8_420_2110_Rec709);
        REQUIRE(fn != nullptr);
        fn(srcPlanes, srcStrides2, dstPlanes, dstStrides, width, ctx);

        // Verify pgroup byte order Y_top0 Y_top1 Y_bot0 Y_bot1 Cb Cr.
        for (size_t i = 0; i < width / 2; i++) {
                CAPTURE(i);
                CHECK(wire[i * 6 + 0] == yTop[i * 2 + 0]);
                CHECK(wire[i * 6 + 1] == yTop[i * 2 + 1]);
                CHECK(wire[i * 6 + 2] == yBot[i * 2 + 0]);
                CHECK(wire[i * 6 + 3] == yBot[i * 2 + 1]);
                CHECK(wire[i * 6 + 4] == chroma[i * 2 + 0]);
                CHECK(wire[i * 6 + 5] == chroma[i * 2 + 1]);
        }

        // Round-trip: invoke reverse kernel and check we recover the source.
        std::vector<uint8_t> backY(contiguousY.size(), 0u);
        std::vector<uint8_t> backChroma(chroma.size(), 0u);
        void                *backDstPlanes[2] = {backY.data(), backChroma.data()};
        const size_t         backDstStrides[2] = {yTop.size(), chroma.size()};
        const void          *backSrcPlanes[1] = {wire.data()};
        const size_t         backSrcStrides[1] = {wire.size()};
        auto fnRev = CSCRegistry::lookupFastPath(PixelFormat::YUV8_420_2110_Rec709, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        REQUIRE(fnRev != nullptr);
        fnRev(backSrcPlanes, backSrcStrides, backDstPlanes, backDstStrides, width, ctx);
        CHECK(backY == contiguousY);
        CHECK(backChroma == chroma);
}

TEST_CASE("CSC ST 2110: P010 BE ↔ ST 2110 4:2:0 10-bit wire round-trip") {
        const size_t width = 8;
        // 10-bit samples in 16-bit BE words.  8 Y per row, 8 chroma
        // samples per row (W/2 Cb + W/2 Cr interleaved at chroma positions).
        const std::vector<uint16_t> yTopVals = {0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107};
        const std::vector<uint16_t> yBotVals = {0x200, 0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x207};
        const std::vector<uint16_t> chromaVals = {0x300, 0x301, 0x302, 0x303, 0x304, 0x305, 0x306, 0x307};

        auto encodeBE10 = [](const std::vector<uint16_t> &samples) {
                std::vector<uint8_t> row(samples.size() * 2);
                for (size_t i = 0; i < samples.size(); i++) {
                        row[i * 2 + 0] = static_cast<uint8_t>((samples[i] >> 8) & 0x3u);
                        row[i * 2 + 1] = static_cast<uint8_t>(samples[i] & 0xFFu);
                }
                return row;
        };
        const auto           yTopBytes = encodeBE10(yTopVals);
        const auto           yBotBytes = encodeBE10(yBotVals);
        const auto           chromaBytes = encodeBE10(chromaVals);
        std::vector<uint8_t> contiguousY(yTopBytes.size() + yBotBytes.size());
        std::memcpy(contiguousY.data(), yTopBytes.data(), yTopBytes.size());
        std::memcpy(contiguousY.data() + yTopBytes.size(), yBotBytes.data(), yBotBytes.size());

        const void  *srcPlanes[2] = {contiguousY.data(), chromaBytes.data()};
        const size_t srcStrides[2] = {yTopBytes.size(), chromaBytes.size()};
        std::vector<uint8_t> wire(width / 4 * 15, 0u); // 2 pgroups × 15 octets = 30 octets.
        void                *dstPlanes[1] = {wire.data()};
        const size_t         dstStrides[1] = {wire.size()};
        CSCContext           ctx(width);
        auto fn = CSCRegistry::lookupFastPath(PixelFormat::YUV10_420_SemiPlanar_BE_Rec709,
                                              PixelFormat::YUV10_420_2110_Rec709);
        REQUIRE(fn != nullptr);
        fn(srcPlanes, srcStrides, dstPlanes, dstStrides, width, ctx);

        // Build reference: 12 samples per pgroup in (Y_top×4, Y_bot×4,
        // Cb0, Cr0, Cb1, Cr1) order, packed BE.
        std::vector<uint8_t> expected(wire.size());
        for (size_t i = 0; i < width / 4; i++) {
                std::vector<uint16_t> pg(12);
                for (int s = 0; s < 4; s++) {
                        pg[s] = yTopVals[i * 4 + s];
                        pg[4 + s] = yBotVals[i * 4 + s];
                }
                pg[8] = chromaVals[i * 4 + 0];
                pg[9] = chromaVals[i * 4 + 1];
                pg[10] = chromaVals[i * 4 + 2];
                pg[11] = chromaVals[i * 4 + 3];
                St2110Video::packSamplesBE(expected.data() + i * 15, 15, pg.data(), pg.size(), 10);
        }
        CHECK(wire == expected);

        // Reverse round-trip.
        std::vector<uint8_t> backY(contiguousY.size(), 0u);
        std::vector<uint8_t> backChroma(chromaBytes.size(), 0u);
        const void          *backSrcPlanes[1] = {wire.data()};
        const size_t         backSrcStrides[1] = {wire.size()};
        void                *backDstPlanes[2] = {backY.data(), backChroma.data()};
        const size_t         backDstStrides[2] = {yTopBytes.size(), chromaBytes.size()};
        auto fnRev = CSCRegistry::lookupFastPath(PixelFormat::YUV10_420_2110_Rec709,
                                                 PixelFormat::YUV10_420_SemiPlanar_BE_Rec709);
        REQUIRE(fnRev != nullptr);
        fnRev(backSrcPlanes, backSrcStrides, backDstPlanes, backDstStrides, width, ctx);
        CHECK(backY == contiguousY);
        CHECK(backChroma == chromaBytes);
}

TEST_CASE("CSC ST 2110: v210 round-trip — wire → v210 reverses the writer kernel") {
        // 6 pixels = 3 wire pgroups → 1 v210 block (16 octets).  Build the
        // wire byte stream from a known 12-sample sequence and verify the
        // reverse kernel produces the matching v210 layout.
        const uint16_t Cb0 = 0x123, Y0 = 0x040, Cr0 = 0x355, Y1 = 0x199;
        const uint16_t Cb2 = 0x2AB, Y2 = 0x1FF, Cr2 = 0x000, Y3 = 0x3FF;
        const uint16_t Cb4 = 0x244, Y4 = 0x080, Cr4 = 0x2AA, Y5 = 0x111;

        const std::vector<uint16_t> samples = {Cb0, Y0, Cr0, Y1, Cb2, Y2, Cr2, Y3, Cb4, Y4, Cr4, Y5};
        std::vector<uint8_t>        wire(15, 0u);
        St2110Video::packSamplesBE(wire.data(), wire.size(), samples.data(), samples.size(), 10);

        std::vector<uint8_t> v210(16, 0u);
        invokeFastPath(PixelFormat::YUV10_422_2110_Rec709, PixelFormat::YUV10_422_v210_Rec709, wire.data(), v210.data(),
                       6);

        auto loadLE = [&](size_t off) -> uint32_t {
                return static_cast<uint32_t>(v210[off + 0]) | (static_cast<uint32_t>(v210[off + 1]) << 8) |
                       (static_cast<uint32_t>(v210[off + 2]) << 16) | (static_cast<uint32_t>(v210[off + 3]) << 24);
        };
        auto packWord = [](uint32_t c0, uint32_t c1, uint32_t c2) -> uint32_t {
                return (c0 & 0x3FFu) | ((c1 & 0x3FFu) << 10) | ((c2 & 0x3FFu) << 20);
        };
        CHECK(loadLE(0) == packWord(Cb0, Y0, Cr0));
        CHECK(loadLE(4) == packWord(Y1, Cb2, Y2));
        CHECK(loadLE(8) == packWord(Cr2, Y3, Cb4));
        CHECK(loadLE(12) == packWord(Y4, Cr4, Y5));

        // Bonus: wire → v210 → wire round-trip should be lossless.
        std::vector<uint8_t> wire2(15, 0u);
        invokeFastPath(PixelFormat::YUV10_422_v210_Rec709, PixelFormat::YUV10_422_2110_Rec709, v210.data(), wire2.data(),
                       6);
        CHECK(wire2 == wire);
}

TEST_CASE("CSC ST 2110: P010 LE ↔ ST 2110 4:2:0 10-bit wire round-trip") {
        const size_t width = 8;
        const std::vector<uint16_t> yTopVals = {0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107};
        const std::vector<uint16_t> yBotVals = {0x200, 0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x207};
        const std::vector<uint16_t> chromaVals = {0x300, 0x301, 0x302, 0x303, 0x304, 0x305, 0x306, 0x307};

        auto encodeLE10 = [](const std::vector<uint16_t> &samples) {
                std::vector<uint8_t> row(samples.size() * 2);
                for (size_t i = 0; i < samples.size(); i++) {
                        row[i * 2 + 0] = static_cast<uint8_t>(samples[i] & 0xFFu);
                        row[i * 2 + 1] = static_cast<uint8_t>((samples[i] >> 8) & 0x3u);
                }
                return row;
        };
        const auto           yTopBytes = encodeLE10(yTopVals);
        const auto           yBotBytes = encodeLE10(yBotVals);
        const auto           chromaBytes = encodeLE10(chromaVals);
        std::vector<uint8_t> contiguousY(yTopBytes.size() + yBotBytes.size());
        std::memcpy(contiguousY.data(), yTopBytes.data(), yTopBytes.size());
        std::memcpy(contiguousY.data() + yTopBytes.size(), yBotBytes.data(), yBotBytes.size());

        const void  *srcPlanes[2] = {contiguousY.data(), chromaBytes.data()};
        const size_t srcStrides[2] = {yTopBytes.size(), chromaBytes.size()};
        std::vector<uint8_t> wire(width / 4 * 15, 0u);
        void                *dstPlanes[1] = {wire.data()};
        const size_t         dstStrides[1] = {wire.size()};
        CSCContext           ctx(width);
        auto                 fn = CSCRegistry::lookupFastPath(PixelFormat::YUV10_420_SemiPlanar_LE_Rec709,
                                                              PixelFormat::YUV10_420_2110_Rec709);
        REQUIRE(fn != nullptr);
        fn(srcPlanes, srcStrides, dstPlanes, dstStrides, width, ctx);

        // The wire byte stream must match the BE variant's output for the
        // same sample values — the LE distinction is on the source side.
        std::vector<uint8_t> expected(wire.size());
        for (size_t i = 0; i < width / 4; i++) {
                std::vector<uint16_t> pg(12);
                for (int s = 0; s < 4; s++) {
                        pg[s] = yTopVals[i * 4 + s];
                        pg[4 + s] = yBotVals[i * 4 + s];
                }
                pg[8] = chromaVals[i * 4 + 0];
                pg[9] = chromaVals[i * 4 + 1];
                pg[10] = chromaVals[i * 4 + 2];
                pg[11] = chromaVals[i * 4 + 3];
                St2110Video::packSamplesBE(expected.data() + i * 15, 15, pg.data(), pg.size(), 10);
        }
        CHECK(wire == expected);

        // Reverse.
        std::vector<uint8_t> backY(contiguousY.size(), 0u);
        std::vector<uint8_t> backChroma(chromaBytes.size(), 0u);
        const void          *backSrcPlanes[1] = {wire.data()};
        const size_t         backSrcStrides[1] = {wire.size()};
        void                *backDstPlanes[2] = {backY.data(), backChroma.data()};
        const size_t         backDstStrides[2] = {yTopBytes.size(), chromaBytes.size()};
        auto                 fnRev = CSCRegistry::lookupFastPath(PixelFormat::YUV10_420_2110_Rec709,
                                                                 PixelFormat::YUV10_420_SemiPlanar_LE_Rec709);
        REQUIRE(fnRev != nullptr);
        fnRev(backSrcPlanes, backSrcStrides, backDstPlanes, backDstStrides, width, ctx);
        CHECK(backY == contiguousY);
        CHECK(backChroma == chromaBytes);
}

TEST_CASE("CSC ST 2110: P012 BE ↔ ST 2110 4:2:0 12-bit wire round-trip") {
        // 12-bit 4:2:0 wire pgroup = 9 octets / 2×2 pixel block (4 Y + 1
        // Cb + 1 Cr × 12 bits MSB-first BE).  Width = 4 → 2 pgroups, 18
        // wire octets.
        const size_t width = 4;
        const std::vector<uint16_t> yTopVals = {0x100, 0x111, 0x222, 0x333};
        const std::vector<uint16_t> yBotVals = {0x444, 0x555, 0x666, 0x777};
        const std::vector<uint16_t> chromaVals = {0x800, 0x901, 0xA02, 0xB03}; // Cb0 Cr0 Cb1 Cr1

        auto encodeBE12 = [](const std::vector<uint16_t> &samples) {
                std::vector<uint8_t> row(samples.size() * 2);
                for (size_t i = 0; i < samples.size(); i++) {
                        row[i * 2 + 0] = static_cast<uint8_t>((samples[i] >> 8) & 0xFu);
                        row[i * 2 + 1] = static_cast<uint8_t>(samples[i] & 0xFFu);
                }
                return row;
        };
        const auto           yTopBytes = encodeBE12(yTopVals);
        const auto           yBotBytes = encodeBE12(yBotVals);
        const auto           chromaBytes = encodeBE12(chromaVals);
        std::vector<uint8_t> contiguousY(yTopBytes.size() + yBotBytes.size());
        std::memcpy(contiguousY.data(), yTopBytes.data(), yTopBytes.size());
        std::memcpy(contiguousY.data() + yTopBytes.size(), yBotBytes.data(), yBotBytes.size());

        const void          *srcPlanes[2] = {contiguousY.data(), chromaBytes.data()};
        const size_t         srcStrides[2] = {yTopBytes.size(), chromaBytes.size()};
        std::vector<uint8_t> wire(width / 2 * 9, 0u);
        void                *dstPlanes[1] = {wire.data()};
        const size_t         dstStrides[1] = {wire.size()};
        CSCContext           ctx(width);
        auto                 fn = CSCRegistry::lookupFastPath(PixelFormat::YUV12_420_SemiPlanar_BE_Rec709,
                                                              PixelFormat::YUV12_420_2110_Rec709);
        REQUIRE(fn != nullptr);
        fn(srcPlanes, srcStrides, dstPlanes, dstStrides, width, ctx);

        // Reference via St2110Video::packSamplesBE — sample order per
        // pgroup: Y_top0, Y_top1, Y_bot0, Y_bot1, Cb, Cr (§6.2.5).
        std::vector<uint8_t> expected(wire.size());
        for (size_t i = 0; i < width / 2; i++) {
                const std::vector<uint16_t> pg = {yTopVals[i * 2 + 0], yTopVals[i * 2 + 1], yBotVals[i * 2 + 0],
                                                  yBotVals[i * 2 + 1], chromaVals[i * 2 + 0], chromaVals[i * 2 + 1]};
                St2110Video::packSamplesBE(expected.data() + i * 9, 9, pg.data(), pg.size(), 12);
        }
        CHECK(wire == expected);

        // Reverse.
        std::vector<uint8_t> backY(contiguousY.size(), 0u);
        std::vector<uint8_t> backChroma(chromaBytes.size(), 0u);
        const void          *backSrcPlanes[1] = {wire.data()};
        const size_t         backSrcStrides[1] = {wire.size()};
        void                *backDstPlanes[2] = {backY.data(), backChroma.data()};
        const size_t         backDstStrides[2] = {yTopBytes.size(), chromaBytes.size()};
        auto                 fnRev = CSCRegistry::lookupFastPath(PixelFormat::YUV12_420_2110_Rec709,
                                                                 PixelFormat::YUV12_420_SemiPlanar_BE_Rec709);
        REQUIRE(fnRev != nullptr);
        fnRev(backSrcPlanes, backSrcStrides, backDstPlanes, backDstStrides, width, ctx);
        CHECK(backY == contiguousY);
        CHECK(backChroma == chromaBytes);
}

TEST_CASE("CSC ST 2110: P012 LE ↔ ST 2110 4:2:0 12-bit wire round-trip") {
        const size_t width = 4;
        const std::vector<uint16_t> yTopVals = {0x100, 0x111, 0x222, 0x333};
        const std::vector<uint16_t> yBotVals = {0x444, 0x555, 0x666, 0x777};
        const std::vector<uint16_t> chromaVals = {0x800, 0x901, 0xA02, 0xB03};

        auto encodeLE12 = [](const std::vector<uint16_t> &samples) {
                std::vector<uint8_t> row(samples.size() * 2);
                for (size_t i = 0; i < samples.size(); i++) {
                        row[i * 2 + 0] = static_cast<uint8_t>(samples[i] & 0xFFu);
                        row[i * 2 + 1] = static_cast<uint8_t>((samples[i] >> 8) & 0xFu);
                }
                return row;
        };
        const auto           yTopBytes = encodeLE12(yTopVals);
        const auto           yBotBytes = encodeLE12(yBotVals);
        const auto           chromaBytes = encodeLE12(chromaVals);
        std::vector<uint8_t> contiguousY(yTopBytes.size() + yBotBytes.size());
        std::memcpy(contiguousY.data(), yTopBytes.data(), yTopBytes.size());
        std::memcpy(contiguousY.data() + yTopBytes.size(), yBotBytes.data(), yBotBytes.size());

        const void          *srcPlanes[2] = {contiguousY.data(), chromaBytes.data()};
        const size_t         srcStrides[2] = {yTopBytes.size(), chromaBytes.size()};
        std::vector<uint8_t> wire(width / 2 * 9, 0u);
        void                *dstPlanes[1] = {wire.data()};
        const size_t         dstStrides[1] = {wire.size()};
        CSCContext           ctx(width);
        auto                 fn = CSCRegistry::lookupFastPath(PixelFormat::YUV12_420_SemiPlanar_LE_Rec709,
                                                              PixelFormat::YUV12_420_2110_Rec709);
        REQUIRE(fn != nullptr);
        fn(srcPlanes, srcStrides, dstPlanes, dstStrides, width, ctx);

        // Reverse.
        std::vector<uint8_t> backY(contiguousY.size(), 0u);
        std::vector<uint8_t> backChroma(chromaBytes.size(), 0u);
        const void          *backSrcPlanes[1] = {wire.data()};
        const size_t         backSrcStrides[1] = {wire.size()};
        void                *backDstPlanes[2] = {backY.data(), backChroma.data()};
        const size_t         backDstStrides[2] = {yTopBytes.size(), chromaBytes.size()};
        auto                 fnRev = CSCRegistry::lookupFastPath(PixelFormat::YUV12_420_2110_Rec709,
                                                                 PixelFormat::YUV12_420_SemiPlanar_LE_Rec709);
        REQUIRE(fnRev != nullptr);
        fnRev(backSrcPlanes, backSrcStrides, backDstPlanes, backDstStrides, width, ctx);
        CHECK(backY == contiguousY);
        CHECK(backChroma == chromaBytes);
}

TEST_CASE("CSC ST 2110: all c-3 fast paths are registered") {
        // Sanity: the static registrar runs at init and every pair
        // covered by E20c-3 (parts A + B) is present.
        struct Pair {
                        PixelFormat::ID src;
                        PixelFormat::ID dst;
        };
        const std::vector<Pair> kPairs = {
                // Part A — 4:2:2 10/12-bit UYVY ↔ wire.
                {PixelFormat::YUV10_422_UYVY_BE_Rec709, PixelFormat::YUV10_422_2110_Rec709},
                {PixelFormat::YUV10_422_UYVY_LE_Rec709, PixelFormat::YUV10_422_2110_Rec709},
                {PixelFormat::YUV12_422_UYVY_BE_Rec709, PixelFormat::YUV12_422_2110_Rec709},
                {PixelFormat::YUV12_422_UYVY_LE_Rec709, PixelFormat::YUV12_422_2110_Rec709},
                {PixelFormat::YUV10_422_2110_Rec709, PixelFormat::YUV10_422_UYVY_BE_Rec709},
                {PixelFormat::YUV10_422_2110_Rec709, PixelFormat::YUV10_422_UYVY_LE_Rec709},
                {PixelFormat::YUV12_422_2110_Rec709, PixelFormat::YUV12_422_UYVY_BE_Rec709},
                {PixelFormat::YUV12_422_2110_Rec709, PixelFormat::YUV12_422_UYVY_LE_Rec709},
                // Part B — 4:4:4 DPX-B 10-bit ↔ wire.
                {PixelFormat::YUV10_DPX_B_Rec709, PixelFormat::YUV10_2110_Rec709},
                {PixelFormat::YUV10_2110_Rec709, PixelFormat::YUV10_DPX_B_Rec709},
                // Part B — Key 10/12-bit BE ↔ wire.
                {PixelFormat::Mono10_BE_sRGB, PixelFormat::Mono10_BE_2110_sRGB},
                {PixelFormat::Mono10_BE_2110_sRGB, PixelFormat::Mono10_BE_sRGB},
                {PixelFormat::Mono12_BE_sRGB, PixelFormat::Mono12_BE_2110_sRGB},
                {PixelFormat::Mono12_BE_2110_sRGB, PixelFormat::Mono12_BE_sRGB},
                // Part B — v210 ↔ wire (both directions, reverse closes c-3 gap).
                {PixelFormat::YUV10_422_v210_Rec709, PixelFormat::YUV10_422_2110_Rec709},
                {PixelFormat::YUV10_422_2110_Rec709, PixelFormat::YUV10_422_v210_Rec709},
                // Part C — 4:2:0 (NV12 / P010 BE/LE / P012 BE/LE ↔ wire).
                {PixelFormat::YUV8_420_SemiPlanar_Rec709, PixelFormat::YUV8_420_2110_Rec709},
                {PixelFormat::YUV8_420_2110_Rec709, PixelFormat::YUV8_420_SemiPlanar_Rec709},
                {PixelFormat::YUV10_420_SemiPlanar_BE_Rec709, PixelFormat::YUV10_420_2110_Rec709},
                {PixelFormat::YUV10_420_2110_Rec709, PixelFormat::YUV10_420_SemiPlanar_BE_Rec709},
                {PixelFormat::YUV10_420_SemiPlanar_LE_Rec709, PixelFormat::YUV10_420_2110_Rec709},
                {PixelFormat::YUV10_420_2110_Rec709, PixelFormat::YUV10_420_SemiPlanar_LE_Rec709},
                {PixelFormat::YUV12_420_SemiPlanar_BE_Rec709, PixelFormat::YUV12_420_2110_Rec709},
                {PixelFormat::YUV12_420_2110_Rec709, PixelFormat::YUV12_420_SemiPlanar_BE_Rec709},
                {PixelFormat::YUV12_420_SemiPlanar_LE_Rec709, PixelFormat::YUV12_420_2110_Rec709},
                {PixelFormat::YUV12_420_2110_Rec709, PixelFormat::YUV12_420_SemiPlanar_LE_Rec709},
        };
        for (const auto &p : kPairs) {
                CAPTURE(static_cast<int>(p.src));
                CAPTURE(static_cast<int>(p.dst));
                CHECK(CSCRegistry::lookupFastPath(PixelFormat(p.src), PixelFormat(p.dst)) != nullptr);
        }
}
