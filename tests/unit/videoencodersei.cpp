/**
 * @file      tests/videoencodersei.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/videoencodersei.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/ciepoint.h>
#include <promeki/frame.h>
#include <promeki/anctranslator.h>

using namespace promeki;

namespace {

        uint16_t readU16(const Buffer &b, size_t off) {
                const uint8_t *p = static_cast<const uint8_t *>(b.data());
                return static_cast<uint16_t>((p[off] << 8) | p[off + 1]);
        }
        uint32_t readU32(const Buffer &b, size_t off) {
                const uint8_t *p = static_cast<const uint8_t *>(b.data());
                return (uint32_t(p[off]) << 24) | (uint32_t(p[off + 1]) << 16) | (uint32_t(p[off + 2]) << 8) |
                       uint32_t(p[off + 3]);
        }

} // namespace

TEST_CASE("VideoEncoderSei::contentLightLevel emits 4 big-endian bytes") {
        ContentLightLevel cll(1000, 400);
        auto              sp = VideoEncoderSei::contentLightLevel(cll);
        CHECK(sp.type == VideoEncoderSei::TypeContentLightLevel);
        REQUIRE(sp.bytes.size() == 4);
        CHECK(readU16(sp.bytes, 0) == 1000);
        CHECK(readU16(sp.bytes, 2) == 400);
}

TEST_CASE("VideoEncoderSei::contentLightLevel on invalid input is empty") {
        auto sp = VideoEncoderSei::contentLightLevel(ContentLightLevel());
        CHECK(sp.type == VideoEncoderSei::TypeContentLightLevel);
        CHECK(sp.bytes.size() == 0);
}

TEST_CASE("VideoEncoderSei::masteringDisplay emits green/blue/red ordered 24 bytes") {
        MasteringDisplay md(CIEPoint(0.708, 0.292),   // red
                            CIEPoint(0.170, 0.797),   // green
                            CIEPoint(0.131, 0.046),   // blue
                            CIEPoint(0.3127, 0.3290), // white point
                            0.005, 1000.0);
        REQUIRE(md.isValid());
        auto sp = VideoEncoderSei::masteringDisplay(md);
        CHECK(sp.type == VideoEncoderSei::TypeMasteringDisplay);
        REQUIRE(sp.bytes.size() == 24);

        // Primaries are written in green, blue, red order, each × 50000.
        CHECK(readU16(sp.bytes, 0) == 8500);   // green.x
        CHECK(readU16(sp.bytes, 2) == 39850);  // green.y
        CHECK(readU16(sp.bytes, 4) == 6550);   // blue.x
        CHECK(readU16(sp.bytes, 6) == 2300);   // blue.y
        CHECK(readU16(sp.bytes, 8) == 35400);  // red.x
        CHECK(readU16(sp.bytes, 10) == 14600); // red.y
        CHECK(readU16(sp.bytes, 12) == 15635); // white.x
        CHECK(readU16(sp.bytes, 14) == 16450); // white.y
        CHECK(readU32(sp.bytes, 16) == 10000000u); // max luminance × 10000
        CHECK(readU32(sp.bytes, 20) == 50u);       // min luminance × 10000
}

TEST_CASE("VideoEncoderSei::masteringDisplay on invalid input is empty") {
        auto sp = VideoEncoderSei::masteringDisplay(MasteringDisplay());
        CHECK(sp.bytes.size() == 0);
}

TEST_CASE("VideoEncoderSei::captions on a frame with no ANC is empty") {
        AncTranslator translator;
        Frame         empty;
        auto          list = VideoEncoderSei::captions(empty, /*videoStreamIndex*/ 0, translator);
        CHECK(list.size() == 0);
}
