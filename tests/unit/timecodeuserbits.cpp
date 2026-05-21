/**
 * @file      timecodeuserbits.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/timecodeuserbits.h>
#include <promeki/variant.h>

using namespace promeki;

TEST_CASE("TimecodeUserbits: default constructor is zero + Unspecified") {
        TimecodeUserbits ub;
        CHECK(ub.mode() == TimecodeUserbits::Unspecified);
        CHECK(ub.toUint32() == 0u);
        for (size_t i = 0; i < TimecodeUserbits::NibbleCount; ++i) {
                CHECK(ub.nibbles()[i] == 0u);
        }
}

TEST_CASE("TimecodeUserbits: fromRawBits / toUint32 round-trip preserves bits + mode") {
        const uint32_t bits = 0xDEADBEEFu;
        auto ub = TimecodeUserbits::fromRawBits(bits, TimecodeUserbits::ClockTime);
        CHECK(ub.mode() == TimecodeUserbits::ClockTime);
        CHECK(ub.toUint32() == bits);
        // nibble[0] holds bits 0..3 of the input
        CHECK(ub.nibbles()[0] == (bits & 0x0Fu));
        CHECK(ub.nibbles()[7] == ((bits >> 28) & 0x0Fu));
}

TEST_CASE("TimecodeUserbits: fromNibbles masks each value to its low 4 bits") {
        TimecodeUserbits::Nibbles n{};
        for (size_t i = 0; i < TimecodeUserbits::NibbleCount; ++i) n[i] = uint8_t(0xF0u | i);
        auto ub = TimecodeUserbits::fromNibbles(n, TimecodeUserbits::DateTimeZone);
        CHECK(ub.mode() == TimecodeUserbits::DateTimeZone);
        for (size_t i = 0; i < TimecodeUserbits::NibbleCount; ++i) {
                CHECK(ub.nibbles()[i] == uint8_t(i));
        }
}

TEST_CASE("TimecodeUserbits: fromAsciiChars packs 4 chars across the 8 nibbles") {
        auto ub = TimecodeUserbits::fromAsciiChars("TAKE");
        CHECK(ub.mode() == TimecodeUserbits::EightBitChars);
        // 'T' = 0x54 -> low nibble 0x4, high nibble 0x5
        CHECK(ub.nibbles()[0] == 0x4u);
        CHECK(ub.nibbles()[1] == 0x5u);
        CHECK(ub.nibbles()[2] == ('A' & 0x0Fu));
        CHECK(ub.nibbles()[3] == (('A' >> 4) & 0x0Fu));
        auto back = ub.asAsciiChars();
        REQUIRE(back.second().isOk());
        CHECK(back.first() == String("TAKE"));
}

TEST_CASE("TimecodeUserbits: short strings right-pad with zeros") {
        auto ub = TimecodeUserbits::fromAsciiChars("HI");
        CHECK(ub.mode() == TimecodeUserbits::EightBitChars);
        // 'I' is the second character (nibbles 2-3), the last two char
        // slots (nibbles 4-5 and 6-7) must be zero.
        CHECK(ub.nibbles()[4] == 0u);
        CHECK(ub.nibbles()[5] == 0u);
        CHECK(ub.nibbles()[6] == 0u);
        CHECK(ub.nibbles()[7] == 0u);
}

TEST_CASE("TimecodeUserbits: asAsciiChars rejects non-Eight-bit modes") {
        auto raw = TimecodeUserbits::fromRawBits(0x12345678u, TimecodeUserbits::Unspecified);
        auto r = raw.asAsciiChars();
        CHECK(r.second().isError());
}

TEST_CASE("TimecodeUserbits: reinterpret preserves nibbles + swaps mode") {
        auto raw = TimecodeUserbits::fromRawBits(0xCAFEBABEu, TimecodeUserbits::Unspecified);
        auto re  = raw.reinterpret(TimecodeUserbits::DateTimeZone);
        CHECK(re.mode() == TimecodeUserbits::DateTimeZone);
        CHECK(re.toUint32() == raw.toUint32());
}

TEST_CASE("TimecodeUserbits: hasClockTimeReference matches the ST 12-1 Table 1 modes") {
        CHECK_FALSE(TimecodeUserbits::fromRawBits(0, TimecodeUserbits::Unspecified).hasClockTimeReference());
        CHECK_FALSE(TimecodeUserbits::fromRawBits(0, TimecodeUserbits::EightBitChars).hasClockTimeReference());
        CHECK(TimecodeUserbits::fromRawBits(0, TimecodeUserbits::ClockTime).hasClockTimeReference());
        CHECK(TimecodeUserbits::fromRawBits(0, TimecodeUserbits::DateTimeZoneClock).hasClockTimeReference());
        CHECK(TimecodeUserbits::fromRawBits(0, TimecodeUserbits::PageLineClock).hasClockTimeReference());
        CHECK_FALSE(TimecodeUserbits::fromRawBits(0, TimecodeUserbits::DateTimeZone).hasClockTimeReference());
        CHECK_FALSE(TimecodeUserbits::fromRawBits(0, TimecodeUserbits::PageLine).hasClockTimeReference());
}

TEST_CASE("TimecodeUserbits: equality + inequality") {
        auto a = TimecodeUserbits::fromRawBits(0x11223344u, TimecodeUserbits::ClockTime);
        auto b = TimecodeUserbits::fromRawBits(0x11223344u, TimecodeUserbits::ClockTime);
        auto c = TimecodeUserbits::fromRawBits(0x11223344u, TimecodeUserbits::Unspecified);
        auto d = TimecodeUserbits::fromRawBits(0x22223344u, TimecodeUserbits::ClockTime);
        CHECK(a == b);
        CHECK(a != c);
        CHECK(a != d);
}

TEST_CASE("TimecodeUserbits: fromDateTimeZone is NotSupported stub") {
        DateTime dt;
        auto r = TimecodeUserbits::fromDateTimeZone(dt);
        CHECK(r.second().isError());
        CHECK(r.second().code() == Error::NotSupported);
}

TEST_CASE("TimecodeUserbits: toString includes mode code + hex bits") {
        auto ub = TimecodeUserbits::fromRawBits(0x12345678u, TimecodeUserbits::ClockTime);
        String s = ub.toString();
        // Format: "ub:T:0x12345678"
        CHECK(s.find("ub:") == 0);
        CHECK(s.find(":T:") != String::npos);
        CHECK(s.find("0x12345678") != String::npos);
}

TEST_CASE("TimecodeUserbits: JsonObject carries mode + raw + nibbles") {
        auto ub = TimecodeUserbits::fromRawBits(0xAABBCCDDu, TimecodeUserbits::DateTimeZone);
        JsonObject obj = ub.toJson();
        CHECK(obj.getInt("mode") == static_cast<int64_t>(TimecodeUserbits::DateTimeZone));
        CHECK(obj.getInt("raw") == static_cast<int64_t>(0xAABBCCDDu));
}

TEST_CASE("TimecodeUserbits: DataStream round-trip preserves bits + mode") {
        auto src = TimecodeUserbits::fromRawBits(0xFEEDFACEu, TimecodeUserbits::PageLineClock);
        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        DataStream sout = DataStream::createWriter(&dev);
        sout << src;
        REQUIRE(sout.status() == DataStream::Ok);

        dev.seek(0);
        DataStream sin = DataStream::createReader(&dev);
        TimecodeUserbits dst;
        sin >> dst;
        REQUIRE(sin.status() == DataStream::Ok);
        CHECK(dst == src);
}

TEST_CASE("TimecodeUserbits: Variant round-trip") {
        auto src = TimecodeUserbits::fromRawBits(0xCAFE1234u, TimecodeUserbits::ClockTime);
        Variant v(src);
        TimecodeUserbits dst = v.get<TimecodeUserbits>();
        CHECK(dst == src);
}

TEST_CASE("TimecodeUserbits: std::formatter prints the toString form") {
        auto ub = TimecodeUserbits::fromRawBits(0x00000010u, TimecodeUserbits::EightBitChars);
        std::string s = std::format("{}", ub);
        CHECK(s.find("ub:C:") == 0);
}
