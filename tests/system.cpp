/**
 * @file      system.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/system.h>

using namespace promeki;

TEST_CASE("System: hostname returns non-empty string") {
        String host = System::hostname();
        CHECK_FALSE(host.isEmpty());
}

TEST_CASE("System: endianness is consistent") {
        // One of these must be true (or neither for mixed, which is unlikely)
        bool le = System::isLittleEndian();
        bool be = System::isBigEndian();
        CHECK(le != be);
}

TEST_CASE("System: isLittleEndian is constexpr") {
        constexpr bool le = System::isLittleEndian();
        (void)le;
        CHECK(true);
}

TEST_CASE("System: swapEndian 16-bit") {
        uint16_t val = 0x0102;
        System::swapEndian(val);
        CHECK(val == 0x0201);
}

TEST_CASE("System: swapEndian 32-bit") {
        uint32_t val = 0x01020304;
        System::swapEndian(val);
        CHECK(val == 0x04030201);
}

TEST_CASE("System: swapEndian 8-bit is no-op") {
        uint8_t val = 0x42;
        System::swapEndian(val);
        CHECK(val == 0x42);
}

TEST_CASE("System: demangleSymbol") {
        // Test with a known mangled C++ symbol
        String result = System::demangleSymbol("_ZN7promeki6SystemE");
        // Should return something non-empty
        CHECK_FALSE(result.isEmpty());
}
