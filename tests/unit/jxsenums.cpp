/**
 * @file      jxsenums.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <promeki/enums.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("JxsPacketMode: value identity + default") {
        CHECK(JxsPacketMode::Codestream.value() == 0);
        CHECK(JxsPacketMode::Slice.value() == 1);
        JxsPacketMode def;
        CHECK(def == JxsPacketMode::Codestream);
}

TEST_CASE("JxsTransMode: default is SequentialOnly (RFC 9134 absent => T=1)") {
        CHECK(JxsTransMode::OutOfOrderAllowed.value() == 0);
        CHECK(JxsTransMode::SequentialOnly.value() == 1);
        JxsTransMode def;
        CHECK(def == JxsTransMode::SequentialOnly);
}

TEST_CASE("JxsProfile: all canonical values are distinct") {
        CHECK(JxsProfile::Unspecified.value() == 0);
        CHECK(JxsProfile::Light422_10.value() == 1);
        CHECK(JxsProfile::Light444_12.value() == 2);
        CHECK(JxsProfile::LightSubline422_10.value() == 3);
        CHECK(JxsProfile::Main422_10.value() == 4);
        CHECK(JxsProfile::Main444_12.value() == 5);
        CHECK(JxsProfile::Main4444_12.value() == 6);
        CHECK(JxsProfile::High444_12.value() == 7);
        CHECK(JxsProfile::High4444_12.value() == 8);
        CHECK(JxsProfile::Tdc422_10.value() == 9);
}

TEST_CASE("JxsLevel: all canonical values are distinct") {
        CHECK(JxsLevel::Unspecified.value() == 0);
        CHECK(JxsLevel::Lvl1k_1.value() == 1);
        CHECK(JxsLevel::Lvl2k_1.value() == 2);
        CHECK(JxsLevel::Lvl4k_1.value() == 3);
        CHECK(JxsLevel::Lvl4k_2.value() == 4);
        CHECK(JxsLevel::Lvl4k_3.value() == 5);
        CHECK(JxsLevel::Lvl8k_1.value() == 6);
        CHECK(JxsLevel::Lvl8k_2.value() == 7);
        CHECK(JxsLevel::Lvl8k_3.value() == 8);
        CHECK(JxsLevel::Lvl10k_1.value() == 9);
}

TEST_CASE("JxsSublevel: all canonical values are distinct") {
        CHECK(JxsSublevel::Unspecified.value() == 0);
        CHECK(JxsSublevel::Full.value() == 1);
        CHECK(JxsSublevel::Sublev3bpp.value() == 2);
        CHECK(JxsSublevel::Sublev6bpp.value() == 3);
        CHECK(JxsSublevel::Sublev9bpp.value() == 4);
        CHECK(JxsSublevel::Sublev12bpp.value() == 5);
}

TEST_CASE("JxsProfile: round-trips via TypedEnum string constructor") {
        const char *names[] = {"Unspecified",  "Light422_10",        "Light444_12",
                               "LightSubline422_10", "Main422_10",   "Main444_12",
                               "Main4444_12",   "High444_12",        "High4444_12",
                               "Tdc422_10"};
        for (const char *n : names) {
                CAPTURE(n);
                JxsProfile v{String(n)};
                CHECK(v.hasListedValue());
                CHECK(v.valueName() == String(n));
        }
}

TEST_CASE("JxsLevel: round-trips via TypedEnum string constructor") {
        const char *names[] = {"Unspecified", "Lvl1k_1", "Lvl2k_1", "Lvl4k_1", "Lvl4k_2",
                               "Lvl4k_3",     "Lvl8k_1", "Lvl8k_2", "Lvl8k_3", "Lvl10k_1"};
        for (const char *n : names) {
                CAPTURE(n);
                JxsLevel v{String(n)};
                CHECK(v.hasListedValue());
                CHECK(v.valueName() == String(n));
        }
}
