/**
 * @file      qtclosedcaption.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/qtclosedcaption.h>
#include <promeki/cea708cdp.h>
#include <promeki/buffer.h>

using namespace promeki;

TEST_CASE("QtClosedCaption: c608 cdat atom matches the ffmpeg layout") {
        Cea708Cdp::CcDataList cc;
        cc.pushToBack({true, 0, 0x94, 0x2C}); // field 1 (cdat)
        cc.pushToBack({true, 0, 0x41, 0x42}); // field 1
        Buffer s = QtClosedCaption::encode608(cc);

        // One cdat atom: [size=8+4][cdat][94 2C 41 42]
        REQUIRE(s.size() == 12);
        const uint8_t *p = static_cast<const uint8_t *>(s.data());
        CHECK(p[0] == 0);
        CHECK(p[1] == 0);
        CHECK(p[2] == 0);
        CHECK(p[3] == 12);
        CHECK(p[4] == 'c');
        CHECK(p[5] == 'd');
        CHECK(p[6] == 'a');
        CHECK(p[7] == 't');
        CHECK(p[8] == 0x94);
        CHECK(p[9] == 0x2C);
        CHECK(p[10] == 0x41);
        CHECK(p[11] == 0x42);
}

TEST_CASE("QtClosedCaption: field 1 + field 2 produce cdat + cdt2") {
        Cea708Cdp::CcDataList cc;
        cc.pushToBack({true, 0, 0x11, 0x22}); // field 1
        cc.pushToBack({true, 1, 0x33, 0x44}); // field 2
        cc.pushToBack({true, 2, 0x55, 0x66}); // 708 DTVCC — ignored by c608
        Buffer s = QtClosedCaption::encode608(cc);

        // cdat(8+2) + cdt2(8+2) = 20 bytes.
        REQUIRE(s.size() == 20);
        const uint8_t *p = static_cast<const uint8_t *>(s.data());
        CHECK(std::string(reinterpret_cast<const char *>(p + 4), 4) == "cdat");
        CHECK(p[8] == 0x11);
        CHECK(p[9] == 0x22);
        CHECK(std::string(reinterpret_cast<const char *>(p + 14), 4) == "cdt2");
        CHECK(p[18] == 0x33);
        CHECK(p[19] == 0x44);
}

TEST_CASE("QtClosedCaption: encode/decode round-trips field 1 + field 2") {
        Cea708Cdp::CcDataList cc;
        cc.pushToBack({true, 0, 0x94, 0x20});
        cc.pushToBack({true, 0, 0x48, 0x69});
        cc.pushToBack({true, 1, 0x15, 0x26});
        cc.pushToBack({true, 3, 0x01, 0x02}); // 708 start — dropped

        Buffer                s = QtClosedCaption::encode608(cc);
        Cea708Cdp::CcDataList out = QtClosedCaption::decode608(s);

        // Only the three 608 (cc_type 0/1) entries survive, field 1 first.
        REQUIRE(out.size() == 3);
        CHECK(out[0].type == 0);
        CHECK(out[0].b1 == 0x94);
        CHECK(out[0].b2 == 0x20);
        CHECK(out[1].type == 0);
        CHECK(out[1].b1 == 0x48);
        CHECK(out[1].b2 == 0x69);
        CHECK(out[2].type == 1);
        CHECK(out[2].b1 == 0x15);
        CHECK(out[2].b2 == 0x26);
}

TEST_CASE("QtClosedCaption: empty cc_data yields a single empty cdat atom") {
        Cea708Cdp::CcDataList cc;
        Buffer                s = QtClosedCaption::encode608(cc);
        REQUIRE(s.size() == 8); // empty cdat header only
        const uint8_t *p = static_cast<const uint8_t *>(s.data());
        CHECK(p[3] == 8);
        CHECK(std::string(reinterpret_cast<const char *>(p + 4), 4) == "cdat");
        CHECK(QtClosedCaption::decode608(s).isEmpty());
}
