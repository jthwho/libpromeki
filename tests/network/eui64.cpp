/**
 * @file      eui64.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/eui64.h>
#include <promeki/macaddress.h>
#include <promeki/variant.h>
#include <promeki/datastream.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>

using namespace promeki;

TEST_CASE("EUI64: default is null") {
        EUI64 eui;
        CHECK(eui.isNull());
}

TEST_CASE("EUI64: construction from octets") {
        EUI64 eui(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11);
        CHECK_FALSE(eui.isNull());
        CHECK(eui.octet(0) == 0xAA);
        CHECK(eui.octet(7) == 0x11);
}

TEST_CASE("EUI64: toString OctetHyphen (default)") {
        EUI64 eui(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11);
        CHECK(eui.toString() == "aa-bb-cc-dd-ee-ff-00-11");
        CHECK(eui.toString(EUI64Format::OctetHyphen) == "aa-bb-cc-dd-ee-ff-00-11");
}

TEST_CASE("EUI64: toString OctetColon") {
        EUI64 eui(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11);
        CHECK(eui.toString(EUI64Format::OctetColon) == "aa:bb:cc:dd:ee:ff:00:11");
}

TEST_CASE("EUI64: toString IPv6") {
        EUI64 eui(0x02, 0x1A, 0x2B, 0xFF, 0xFE, 0x3C, 0x4D, 0x5E);
        CHECK(eui.toString(EUI64Format::IPv6) == "021a:2bff:fe3c:4d5e");
}

TEST_CASE("EUI64: fromString hyphen-separated") {
        auto [eui, err] = EUI64::fromString("aa-bb-cc-dd-ee-ff-00-11");
        CHECK(err.isOk());
        CHECK_FALSE(eui.isNull());
        CHECK(eui.octet(0) == 0xAA);
        CHECK(eui.octet(3) == 0xDD);
        CHECK(eui.octet(7) == 0x11);
}

TEST_CASE("EUI64: fromString colon-separated") {
        auto [eui, err] = EUI64::fromString("aa:bb:cc:dd:ee:ff:00:11");
        CHECK(err.isOk());
        CHECK(eui.octet(0) == 0xAA);
}

TEST_CASE("EUI64: fromString IPv6 format") {
        auto [eui, err] = EUI64::fromString("021a:2bff:fe3c:4d5e");
        CHECK(err.isOk());
        CHECK(eui.octet(0) == 0x02);
        CHECK(eui.octet(1) == 0x1A);
        CHECK(eui.octet(2) == 0x2B);
        CHECK(eui.octet(3) == 0xFF);
        CHECK(eui.octet(4) == 0xFE);
        CHECK(eui.octet(5) == 0x3C);
        CHECK(eui.octet(6) == 0x4D);
        CHECK(eui.octet(7) == 0x5E);
}

TEST_CASE("EUI64: fromString case-insensitive") {
        auto [eui, err] = EUI64::fromString("AA-BB-CC-DD-EE-FF-00-11");
        CHECK(err.isOk());
        CHECK(eui.octet(0) == 0xAA);
}

TEST_CASE("EUI64: fromString octet round-trip") {
        EUI64 original(0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF);
        String s = original.toString();
        auto [parsed, err] = EUI64::fromString(s);
        CHECK(err.isOk());
        CHECK(parsed == original);
}

TEST_CASE("EUI64: fromString IPv6 round-trip") {
        EUI64 original(0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF);
        String s = original.toString(EUI64Format::IPv6);
        auto [parsed, err] = EUI64::fromString(s);
        CHECK(err.isOk());
        CHECK(parsed == original);
}

TEST_CASE("EUI64: fromString invalid") {
        auto [eui, err] = EUI64::fromString("invalid");
        CHECK(err.isError());
        CHECK(eui.isNull());
}

TEST_CASE("EUI64: fromMacAddress flips U/L bit and inserts FF:FE") {
        // MAC 00:1A:2B:3C:4D:5E (universal, U/L bit clear)
        MacAddress mac(0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E);
        EUI64 eui = EUI64::fromMacAddress(mac);
        // Byte 0: 0x00 XOR 0x02 = 0x02 (U/L bit flipped)
        CHECK(eui.octet(0) == 0x02);
        CHECK(eui.octet(1) == 0x1A);
        CHECK(eui.octet(2) == 0x2B);
        CHECK(eui.octet(3) == 0xFF);
        CHECK(eui.octet(4) == 0xFE);
        CHECK(eui.octet(5) == 0x3C);
        CHECK(eui.octet(6) == 0x4D);
        CHECK(eui.octet(7) == 0x5E);
}

TEST_CASE("EUI64: hasMacAddress true for FF:FE") {
        EUI64 eui(0x02, 0x1A, 0x2B, 0xFF, 0xFE, 0x3C, 0x4D, 0x5E);
        CHECK(eui.hasMacAddress());
}

TEST_CASE("EUI64: hasMacAddress false otherwise") {
        EUI64 eui(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11);
        CHECK_FALSE(eui.hasMacAddress());
}

TEST_CASE("EUI64: toMacAddress flips U/L bit back") {
        // EUI-64 02:1A:2B:FF:FE:3C:4D:5E (U/L bit set by modified EUI-64)
        EUI64 eui(0x02, 0x1A, 0x2B, 0xFF, 0xFE, 0x3C, 0x4D, 0x5E);
        MacAddress mac = eui.toMacAddress();
        // Byte 0: 0x02 XOR 0x02 = 0x00 (U/L bit flipped back)
        CHECK(mac.octet(0) == 0x00);
        CHECK(mac.octet(1) == 0x1A);
        CHECK(mac.octet(2) == 0x2B);
        CHECK(mac.octet(3) == 0x3C);
        CHECK(mac.octet(4) == 0x4D);
        CHECK(mac.octet(5) == 0x5E);
}

TEST_CASE("EUI64: MAC round-trip preserves address") {
        MacAddress original(0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E);
        EUI64 eui = EUI64::fromMacAddress(original);
        CHECK(eui.hasMacAddress());
        MacAddress back = eui.toMacAddress();
        CHECK(back == original);
}

TEST_CASE("EUI64: MAC round-trip with locally-administered") {
        // Locally-administered MAC (U/L bit already set)
        MacAddress original(0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE);
        EUI64 eui = EUI64::fromMacAddress(original);
        // 0x02 XOR 0x02 = 0x00 in EUI-64
        CHECK(eui.octet(0) == 0x00);
        MacAddress back = eui.toMacAddress();
        CHECK(back == original);
}

TEST_CASE("EUI64: toMacAddress returns null when no FF:FE") {
        EUI64 eui(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11);
        MacAddress mac = eui.toMacAddress();
        CHECK(mac.isNull());
}

TEST_CASE("EUI64: comparison") {
        EUI64 a(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08);
        EUI64 b(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08);
        EUI64 c(0xFF, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08);
        CHECK(a == b);
        CHECK(a != c);
        CHECK(a < c);
}

TEST_CASE("EUI64: Variant round-trip") {
        EUI64 original(0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11);
        Variant v = original;
        CHECK(v.type() == Variant::TypeEUI64);

        EUI64 retrieved = v.get<EUI64>();
        CHECK(retrieved == original);

        String s = v.get<String>();
        CHECK(s == "aa-bb-cc-dd-ee-ff-00-11");
}

TEST_CASE("EUI64: DataStream round-trip") {
        EUI64 original(0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF);

        Buffer buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << original;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                EUI64 loaded;
                rs >> loaded;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(loaded == original);
        }
}

TEST_CASE("EUI64Format: enum values") {
        CHECK(EUI64Format::OctetHyphen.valueName() == "OctetHyphen");
        CHECK(EUI64Format::OctetColon.valueName() == "OctetColon");
        CHECK(EUI64Format::IPv6.valueName() == "IPv6");
}

TEST_CASE("EUI64: std::format default") {
        EUI64 eui(0x02, 0x1A, 0x2B, 0xFF, 0xFE, 0x3C, 0x4D, 0x5E);
        CHECK(String::format("{}", eui) == "02-1a-2b-ff-fe-3c-4d-5e");
}

TEST_CASE("EUI64: std::format hyphen specifier") {
        EUI64 eui(0x02, 0x1A, 0x2B, 0xFF, 0xFE, 0x3C, 0x4D, 0x5E);
        CHECK(String::format("{:h}", eui) == "02-1a-2b-ff-fe-3c-4d-5e");
}

TEST_CASE("EUI64: std::format colon specifier") {
        EUI64 eui(0x02, 0x1A, 0x2B, 0xFF, 0xFE, 0x3C, 0x4D, 0x5E);
        CHECK(String::format("{:o}", eui) == "02:1a:2b:ff:fe:3c:4d:5e");
}

TEST_CASE("EUI64: std::format IPv6 specifier") {
        EUI64 eui(0x02, 0x1A, 0x2B, 0xFF, 0xFE, 0x3C, 0x4D, 0x5E);
        CHECK(String::format("{:v}", eui) == "021a:2bff:fe3c:4d5e");
}
