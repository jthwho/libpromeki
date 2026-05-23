/**
 * @file      jxsmarker.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <promeki/jxsmarker.h>
#include <promeki/list.h>

using namespace promeki;

namespace {

// Build a synthetic JPEG XS codestream for parser testing.  Layout:
//   SOC (0xFF10)
//   CAP (0xFF50) — capabilities marker with @p capPayload extra bytes
//                  in its segment (the length field accounts for both
//                  itself and the payload).
//   PIH (0xFF12) — picture header with @p pihPayload extra bytes.
//   For each slice:
//     SLH (0xFF20) with @p slhPayload extra bytes.
//     @p sliceCoeffBytes bytes of coefficient data (no 0xFF in the
//     synthetic stream so the marker scan never trips).
//   EOC (0xFF11)
List<uint8_t> buildJxsCodestream(int capPayload, int pihPayload, int slhPayload, int sliceCoeffBytes,
                                 int numSlices) {
        List<uint8_t> bytes;
        auto         pushMarker = [&](uint16_t code) {
                bytes.pushToBack(static_cast<uint8_t>(code >> 8));
                bytes.pushToBack(static_cast<uint8_t>(code & 0xFF));
        };
        auto pushBe16 = [&](uint16_t v) {
                bytes.pushToBack(static_cast<uint8_t>(v >> 8));
                bytes.pushToBack(static_cast<uint8_t>(v & 0xFF));
        };

        pushMarker(0xFF10); // SOC

        pushMarker(0xFF50); // CAP
        pushBe16(static_cast<uint16_t>(2 + capPayload));
        for (int i = 0; i < capPayload; i++) bytes.pushToBack(static_cast<uint8_t>(0x20 + (i & 0x0F)));

        pushMarker(0xFF12); // PIH
        pushBe16(static_cast<uint16_t>(2 + pihPayload));
        for (int i = 0; i < pihPayload; i++) bytes.pushToBack(static_cast<uint8_t>(0x40 + (i & 0x0F)));

        for (int s = 0; s < numSlices; s++) {
                pushMarker(0xFF20); // SLH
                pushBe16(static_cast<uint16_t>(2 + slhPayload));
                for (int i = 0; i < slhPayload; i++) bytes.pushToBack(static_cast<uint8_t>(s));
                for (int i = 0; i < sliceCoeffBytes; i++) {
                        // Coefficient bytes — keep them ≤ 0x7F so no
                        // 0xFF prefix appears to confuse the marker
                        // scanner.
                        bytes.pushToBack(static_cast<uint8_t>((s * 7 + i) & 0x7F));
                }
        }

        pushMarker(0xFF11); // EOC
        return bytes;
}

} // namespace

TEST_CASE("JxsMarker::parse: empty / null input is rejected") {
        auto r1 = JxsMarker::parse(nullptr, 0);
        CHECK_FALSE(r1.valid);
        CHECK(r1.slices.isEmpty());

        const uint8_t small = 0xFF;
        auto          r2 = JxsMarker::parse(&small, 1);
        CHECK_FALSE(r2.valid);
}

TEST_CASE("JxsMarker::parse: codestream not starting with SOC fails") {
        const uint8_t bad[] = {0xFF, 0x20, 0x00, 0x04, 0xFF, 0x11};
        auto          r = JxsMarker::parse(bad, sizeof(bad));
        CHECK_FALSE(r.valid);
}

TEST_CASE("JxsMarker::parse: well-formed 4-slice codestream") {
        const auto bytes = buildJxsCodestream(8, 16, 4, 64, 4);
        auto       r = JxsMarker::parse(bytes.data(), bytes.size());
        REQUIRE(r.valid);
        REQUIRE(r.slices.size() == 4u);
        // Main header = SOC (2) + CAP segment (2 + 2 + 8 = 12) +
        //               PIH segment (2 + 2 + 16 = 20) = 34 bytes.
        CHECK(r.mainHeaderSize == 34u);
        // Each slice = SLH (2) + SLH length (2) + SLH payload (4) +
        //              coefficient bytes (64) = 72 bytes.
        for (size_t i = 0; i < r.slices.size(); i++) {
                CAPTURE(i);
                CHECK(r.slices[i].size == 72u);
        }
        // EOC sits right after the last slice's coefficient bytes.
        CHECK(r.eocOffset == r.mainHeaderSize + 4u * 72u);
        // EOC bytes are present at that offset.
        CHECK(bytes[r.eocOffset] == 0xFF);
        CHECK(bytes[r.eocOffset + 1] == 0x11);
}

TEST_CASE("JxsMarker::parse: codestream with no slices ends cleanly at EOC") {
        const auto bytes = buildJxsCodestream(8, 16, 4, 64, 0);
        auto       r = JxsMarker::parse(bytes.data(), bytes.size());
        CHECK(r.valid);
        CHECK(r.slices.isEmpty());
        CHECK(r.mainHeaderSize == 34u);
        CHECK(r.eocOffset == 34u);
}

TEST_CASE("JxsMarker::parse: slices contiguous — offsets line up") {
        const auto bytes = buildJxsCodestream(8, 16, 4, 64, 3);
        auto       r = JxsMarker::parse(bytes.data(), bytes.size());
        REQUIRE(r.valid);
        REQUIRE(r.slices.size() == 3u);
        // Slice 0 starts at mainHeaderSize.
        CHECK(r.slices[0].offset == r.mainHeaderSize);
        // Each subsequent slice starts where the previous ended.
        for (size_t i = 1; i < r.slices.size(); i++) {
                CAPTURE(i);
                CHECK(r.slices[i].offset == r.slices[i - 1].offset + r.slices[i - 1].size);
        }
}

TEST_CASE("JxsMarker::parse: truncated main-header marker length") {
        const uint8_t bad[] = {0xFF, 0x10, 0xFF, 0x50, 0x00, 0x10, 0x00, 0x00};
        // CAP says it has length 0x0010 but only 4 payload bytes are
        // present — should fail.
        auto r = JxsMarker::parse(bad, sizeof(bad));
        CHECK_FALSE(r.valid);
}

TEST_CASE("JxsMarker::parse: bogus segment length (less than 2)") {
        // SOC then CAP claiming length=1 (impossible — length
        // includes itself which is already 2 bytes).
        const uint8_t bad[] = {0xFF, 0x10, 0xFF, 0x50, 0x00, 0x01};
        auto          r = JxsMarker::parse(bad, sizeof(bad));
        CHECK_FALSE(r.valid);
}

TEST_CASE("JxsMarker::parse: walker keeps slice sizes in lockstep with input") {
        // Stress: 16 slices, each 200 coefficient bytes.
        const auto bytes = buildJxsCodestream(8, 32, 4, 200, 16);
        auto       r = JxsMarker::parse(bytes.data(), bytes.size());
        REQUIRE(r.valid);
        REQUIRE(r.slices.size() == 16u);
        size_t expected = r.mainHeaderSize;
        const  size_t sliceSize = 200 + 2 + 2 + 4; // coeff + SLH + len + payload
        for (size_t i = 0; i < r.slices.size(); i++) {
                CAPTURE(i);
                CHECK(r.slices[i].offset == expected);
                CHECK(r.slices[i].size == sliceSize);
                expected += sliceSize;
        }
        CHECK(r.eocOffset == expected);
}
