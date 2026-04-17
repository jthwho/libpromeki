/**
 * @file      crc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/crc.h>
#include <cstring>

using namespace promeki;

namespace {

// Standard "check" string from the Rocksoft / reveng CRC catalogue.
constexpr const char *kCheckString = "123456789";
constexpr size_t      kCheckLen    = 9;

}  // namespace

// ============================================================================
// 8-bit
// ============================================================================

TEST_CASE("CRC8 SMBus check value") {
        // Catalogue check value for CRC-8/SMBUS over "123456789" is 0xF4.
        Crc8 crc(CrcParams::Crc8Smbus);
        crc.update(kCheckString, kCheckLen);
        CHECK(crc.value() == 0xF4);
}

TEST_CASE("CRC8 AUTOSAR check value") {
        // Catalogue check value for CRC-8/AUTOSAR over "123456789" is 0xDF.
        Crc8 crc(CrcParams::Crc8Autosar);
        crc.update(kCheckString, kCheckLen);
        CHECK(crc.value() == 0xDF);
}

TEST_CASE("CRC8 Bluetooth check value") {
        // Catalogue check value for CRC-8/BLUETOOTH over "123456789" is 0x26.
        Crc8 crc(CrcParams::Crc8Bluetooth);
        crc.update(kCheckString, kCheckLen);
        CHECK(crc.value() == 0x26);
}

TEST_CASE("CRC8 named factory") {
        // The named-factory helper produces a CRC equivalent to the
        // explicit Params constructor.
        Crc8 a = crc8_autosar();
        a.update(kCheckString, kCheckLen);
        CHECK(a.value() == 0xDF);
}

TEST_CASE("CRC8 one-shot compute") {
        const uint8_t v = Crc8::compute(CrcParams::Crc8Autosar, kCheckString, kCheckLen);
        CHECK(v == 0xDF);
}

// ============================================================================
// 16-bit
// ============================================================================

TEST_CASE("CRC16 CCITT-FALSE check value") {
        // Catalogue check value is 0x29B1.
        Crc16 crc(CrcParams::Crc16CcittFalse);
        crc.update(kCheckString, kCheckLen);
        CHECK(crc.value() == 0x29B1);
}

TEST_CASE("CRC16 Kermit check value") {
        // Catalogue check value is 0x2189.
        Crc16 crc(CrcParams::Crc16Kermit);
        crc.update(kCheckString, kCheckLen);
        CHECK(crc.value() == 0x2189);
}

// ============================================================================
// 32-bit
// ============================================================================

TEST_CASE("CRC32 ISO-HDLC check value") {
        // Catalogue check value is 0xCBF43926 (the canonical zlib CRC).
        Crc32 crc(CrcParams::Crc32IsoHdlc);
        crc.update(kCheckString, kCheckLen);
        CHECK(crc.value() == 0xCBF43926u);
}

TEST_CASE("CRC32 BZIP2 check value") {
        // Catalogue check value is 0xFC891918.
        Crc32 crc(CrcParams::Crc32Bzip2);
        crc.update(kCheckString, kCheckLen);
        CHECK(crc.value() == 0xFC891918u);
}

// ============================================================================
// State management
// ============================================================================

TEST_CASE("CRC state — incremental updates equal one-shot") {
        Crc32 a(CrcParams::Crc32IsoHdlc);
        a.update(kCheckString, kCheckLen);

        Crc32 b(CrcParams::Crc32IsoHdlc);
        b.update(kCheckString, 4);
        b.update(kCheckString + 4, kCheckLen - 4);

        CHECK(a.value() == b.value());
}

TEST_CASE("CRC reset reverts to initial state") {
        Crc32 c(CrcParams::Crc32IsoHdlc);
        c.update(kCheckString, kCheckLen);
        const uint32_t first = c.value();

        c.reset();
        c.update(kCheckString, kCheckLen);
        const uint32_t second = c.value();

        CHECK(first == second);
}

TEST_CASE("CRC value() is idempotent") {
        // Calling value() must not perturb internal state.
        Crc8 c(CrcParams::Crc8Autosar);
        c.update(kCheckString, kCheckLen);
        const uint8_t a = c.value();
        const uint8_t b = c.value();
        CHECK(a == b);
}

TEST_CASE("CRC empty update keeps initial value") {
        Crc8 c(CrcParams::Crc8Autosar);
        // No bytes processed: value should equal init ^ xorOut = 0xFF ^ 0xFF = 0.
        CHECK(c.value() == 0x00);
}

// ============================================================================
// Reflected vs unreflected basic sanity
// ============================================================================

TEST_CASE("CRC8 reflected and unreflected differ for the same poly") {
        // Construct two CRCs with the same poly + init/xor but opposite
        // reflection — they must produce different outputs over a
        // non-symmetric input.
        constexpr Crc8::Params unrefl{ 0x07, 0x00, 0x00, false, "test-unrefl" };
        constexpr Crc8::Params refl  { 0x07, 0x00, 0x00, true,  "test-refl"   };
        Crc8 a(unrefl);
        Crc8 b(refl);
        a.update(kCheckString, kCheckLen);
        b.update(kCheckString, kCheckLen);
        CHECK(a.value() != b.value());
}
