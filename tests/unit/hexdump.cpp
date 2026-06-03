/**
 * @file      hexdump.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/hexdump.h>
#include <promeki/buffer.h>
#include <cstring>

using namespace promeki;

TEST_CASE("HexDump - canonical line with address and ASCII") {
        const uint8_t bytes[8] = {0x01, 0x02, 0x03, 0x04, 0xAA, 0xBB, 0xCC, 0xDD};
        const String  out = HexDump().setBaseAddress(0x0123ABCD).setBytesPerLine(8).build(bytes, sizeof(bytes));
        // Address (8 upper-case digits) + ": " + space-separated hex (each
        // byte trailed by a space, so one space falls before the gutter) +
        // ASCII (all non-printable here -> dots).
        CHECK(out == String("0123ABCD: 01 02 03 04 AA BB CC DD ........\n"));
}

TEST_CASE("HexDump - ASCII gutter renders printable characters") {
        const char  *text = "Hello!";
        const String out = HexDump().setBytesPerLine(8).build(text, std::strlen(text));
        // Six present bytes ("XX " each) then two missing bytes padded with
        // three spaces apiece, so seven spaces precede the gutter.
        CHECK(out == String("00000000: 48 65 6C 6C 6F 21       Hello!\n"));
}

TEST_CASE("HexDump - short final line pads the hex column for alignment") {
        const uint8_t bytes[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        const String  out = HexDump().setShowAddress(false).setBytesPerLine(8).build(bytes, sizeof(bytes));
        // Full first row, then a two-byte row whose six missing bytes pad
        // with three spaces each so the gutter aligns under the first row.
        const String expect = "00 01 02 03 04 05 06 07 ........\n"
                              "08 09                   ..\n";
        CHECK(out == expect);
}

TEST_CASE("HexDump - address advances by the line width") {
        const uint8_t bytes[20] = {0};
        const String  out = HexDump().setBaseAddress(0x1000).setShowAscii(false).setBytesPerLine(8).build(bytes,
                                                                                                          sizeof(bytes));
        // Three rows at 8 bytes each: 0x1000, 0x1008, 0x1010.
        const String expect = "00001000: 00 00 00 00 00 00 00 00 \n"
                              "00001008: 00 00 00 00 00 00 00 00 \n"
                              "00001010: 00 00 00 00 \n";
        CHECK(out == expect);
}

TEST_CASE("HexDump - lower-case digits and custom address width") {
        const uint8_t bytes[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        const String  out =
            HexDump().setUppercase(false).setAddressDigits(4).setShowAscii(false).setBytesPerLine(4).build(bytes,
                                                                                                           sizeof(bytes));
        CHECK(out == String("0000: de ad be ef \n"));
}

TEST_CASE("HexDump - indent prefixes every line") {
        const uint8_t bytes[3] = {0xAA, 0xBB, 0xCC};
        const String  out =
            HexDump().setIndent("    ").setShowAddress(false).setShowAscii(false).setBytesPerLine(2).build(bytes,
                                                                                                           sizeof(bytes));
        CHECK(out == String("    AA BB \n"
                            "    CC \n"));
}

TEST_CASE("HexDump - Buffer overload matches the raw-pointer path") {
        const uint8_t bytes[5] = {0x10, 0x20, 0x30, 0x40, 0x50};
        Buffer        buf(sizeof(bytes));
        REQUIRE(buf.copyFrom(bytes, sizeof(bytes)).isOk());
        buf.setSize(sizeof(bytes));
        const HexDump d = HexDump().setBytesPerLine(8);
        CHECK(d.build(buf) == d.build(bytes, sizeof(bytes)));
}

TEST_CASE("HexDump - empty and null ranges yield an empty string") {
        CHECK(HexDump().build(nullptr, 0) == String());
        const uint8_t b = 0;
        CHECK(HexDump().build(&b, 0) == String());
}
