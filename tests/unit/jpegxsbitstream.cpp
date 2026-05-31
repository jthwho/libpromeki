/**
 * @file      tests/jpegxsbitstream.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for @ref JpegXsBitstream — hand-crafted PIH + CDT markers
 * exercise the codestream walker and the @ref pixelFormatFor mapping.
 */

#include <doctest/doctest.h>

#include <cstring>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/jpegxsbitstream.h>
#include <promeki/pixelformat.h>
#include <vector>

using namespace promeki;

namespace {

        // Append a 16-bit value, big-endian.
        void appendBe16(std::vector<uint8_t> &v, uint16_t x) {
                v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
                v.push_back(static_cast<uint8_t>(x & 0xFF));
        }

        // Build a minimal codestream: [SOC] [PIH(profile,level,w,h,nc)] [CDT(per-comp bpc/sub)].
        // PIH segment is 25 bytes total (Lpih=25), payload after the
        // length is 23 bytes.  Most fields are filled with zero
        // (parser only inspects width / height / numComponents).
        std::vector<uint8_t> buildCodestream(uint16_t width, uint16_t height, uint8_t nc,
                                              const std::vector<uint8_t> &perCompBcMinus1,
                                              const std::vector<uint8_t> &perCompSxSy) {
                std::vector<uint8_t> out;
                // SOC
                appendBe16(out, JpegXsBitstream::MarkerSoc);
                // PIH
                appendBe16(out, JpegXsBitstream::MarkerPih);
                appendBe16(out, 25); // Lpih (segment length excluding marker)
                // Lcod (4)
                for (int i = 0; i < 4; ++i) out.push_back(0);
                // Ppih (2) — profile
                appendBe16(out, 0x2400); // Main 422.10 marker for illustration
                // Plev (2)
                appendBe16(out, 0x0440);
                // Wf, Hf
                appendBe16(out, width);
                appendBe16(out, height);
                // Cw, Hsl (4)
                appendBe16(out, 0);
                appendBe16(out, 0);
                // Nc, Ng, Ss, Bw
                out.push_back(nc);
                out.push_back(0);
                out.push_back(0);
                out.push_back(0); // Bw default — overridden by CDT bit depth below
                // Fq+Br, Fslc+Ppoc+Cpih, Nlx+Nly
                out.push_back(0);
                out.push_back(0);
                out.push_back(0);

                // CDT
                appendBe16(out, JpegXsBitstream::MarkerCdt);
                const uint16_t lcdt = static_cast<uint16_t>(2 + 2 * nc);
                appendBe16(out, lcdt);
                for (unsigned i = 0; i < nc; ++i) {
                        out.push_back(perCompBcMinus1[i]);
                        out.push_back(perCompSxSy[i]);
                }
                return out;
        }

} // namespace

TEST_CASE("JpegXsBitstream::parsePictureHeader — 1080p 10-bit 4:2:2 Rec.709") {
        // 3 components, all 10-bit (Bc = 9 = bit-depth-minus-1),
        // luma full-rate (Sx=Sy=1), chroma sub 2:1 horizontal
        // (Sx=2, Sy=1).
        const std::vector<uint8_t> bc{9, 9, 9};
        const std::vector<uint8_t> sub{0x11, 0x21, 0x21};
        const auto                 cs = buildCodestream(1920, 1080, 3, bc, sub);
        Buffer                     b(cs.size());
        b.setSize(cs.size());
        std::memcpy(b.data(), cs.data(), cs.size());
        BufferView                  v(b, 0, b.size());
        JpegXsBitstream::PictureInfo info;
        REQUIRE(JpegXsBitstream::parsePictureHeader(v, info).isOk());
        CHECK(info.width == 1920);
        CHECK(info.height == 1080);
        CHECK(info.numComponents == 3);
        CHECK(info.bitDepth == 10);
        CHECK(info.hasCdt);
        CHECK(info.hSubsampling[0] == 1);
        CHECK(info.hSubsampling[1] == 2);
        CHECK(info.hSubsampling[2] == 2);
        CHECK(info.vSubsampling[0] == 1);
        CHECK(info.vSubsampling[1] == 1);
        CHECK(info.vSubsampling[2] == 1);
        CHECK(JpegXsBitstream::pixelFormatFor(info) == PixelFormat::JPEG_XS_YUV10_422_Rec709);
}

TEST_CASE("JpegXsBitstream::parsePictureHeader — 720p 8-bit 4:2:0 Rec.709") {
        const std::vector<uint8_t> bc{7, 7, 7};   // 8-bit
        const std::vector<uint8_t> sub{0x11, 0x22, 0x22}; // luma 4:4:4 ratio; chroma 2x2
        const auto                 cs = buildCodestream(1280, 720, 3, bc, sub);
        Buffer                     b(cs.size());
        b.setSize(cs.size());
        std::memcpy(b.data(), cs.data(), cs.size());
        BufferView                  v(b, 0, b.size());
        JpegXsBitstream::PictureInfo info;
        REQUIRE(JpegXsBitstream::parsePictureHeader(v, info).isOk());
        CHECK(info.bitDepth == 8);
        CHECK(JpegXsBitstream::pixelFormatFor(info) == PixelFormat::JPEG_XS_YUV8_420_Rec709);
}

TEST_CASE("JpegXsBitstream::parsePictureHeader — 4K 12-bit 4:2:2") {
        const std::vector<uint8_t> bc{11, 11, 11}; // 12-bit
        const std::vector<uint8_t> sub{0x11, 0x21, 0x21};
        const auto                 cs = buildCodestream(3840, 2160, 3, bc, sub);
        Buffer                     b(cs.size());
        b.setSize(cs.size());
        std::memcpy(b.data(), cs.data(), cs.size());
        BufferView                  v(b, 0, b.size());
        JpegXsBitstream::PictureInfo info;
        REQUIRE(JpegXsBitstream::parsePictureHeader(v, info).isOk());
        CHECK(info.bitDepth == 12);
        CHECK(JpegXsBitstream::pixelFormatFor(info) == PixelFormat::JPEG_XS_YUV12_422_Rec709);
}

TEST_CASE("JpegXsBitstream::parsePictureHeader — RGB 8-bit sRGB") {
        const std::vector<uint8_t> bc{7, 7, 7};   // 8-bit
        const std::vector<uint8_t> sub{0x11, 0x11, 0x11}; // 4:4:4 → RGB inferred
        const auto                 cs = buildCodestream(1920, 1080, 3, bc, sub);
        Buffer                     b(cs.size());
        b.setSize(cs.size());
        std::memcpy(b.data(), cs.data(), cs.size());
        BufferView                  v(b, 0, b.size());
        JpegXsBitstream::PictureInfo info;
        REQUIRE(JpegXsBitstream::parsePictureHeader(v, info).isOk());
        CHECK(JpegXsBitstream::pixelFormatFor(info) == PixelFormat::JPEG_XS_RGB8_sRGB);
}

TEST_CASE("JpegXsBitstream::parsePictureHeader — empty input rejected") {
        BufferView                  empty;
        JpegXsBitstream::PictureInfo info;
        CHECK(JpegXsBitstream::parsePictureHeader(empty, info) == Error::InvalidArgument);
}

TEST_CASE("JpegXsBitstream::parsePictureHeader — no PIH marker returns NotFound") {
        std::vector<uint8_t> cs;
        appendBe16(cs, JpegXsBitstream::MarkerSoc);
        appendBe16(cs, JpegXsBitstream::MarkerEoc);
        Buffer b(cs.size());
        b.setSize(cs.size());
        std::memcpy(b.data(), cs.data(), cs.size());
        BufferView                  v(b, 0, b.size());
        JpegXsBitstream::PictureInfo info;
        CHECK(JpegXsBitstream::parsePictureHeader(v, info) == Error::NotFound);
}

TEST_CASE("JpegXsBitstream::parsePictureHeader — truncated segment returns CorruptData") {
        // SOC + PIH marker but segment length claims 25 bytes
        // when only 2 are present.
        std::vector<uint8_t> cs;
        appendBe16(cs, JpegXsBitstream::MarkerSoc);
        appendBe16(cs, JpegXsBitstream::MarkerPih);
        appendBe16(cs, 25);
        cs.push_back(0x01);
        cs.push_back(0x02);
        Buffer b(cs.size());
        b.setSize(cs.size());
        std::memcpy(b.data(), cs.data(), cs.size());
        BufferView                  v(b, 0, b.size());
        JpegXsBitstream::PictureInfo info;
        CHECK(JpegXsBitstream::parsePictureHeader(v, info) == Error::CorruptData);
}
