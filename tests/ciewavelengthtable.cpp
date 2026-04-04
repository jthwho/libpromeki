/**
 * @file      ciewavelengthtable.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ciewavelengthtable.h>

using namespace promeki;

TEST_CASE("CIEWavelengthTable: first entry is 360nm") {
        CHECK(cieWavelengthTable[0].wavelength == doctest::Approx(360.0));
}

TEST_CASE("CIEWavelengthTable: entries have valid XYZ colors") {
        // Check a few representative entries
        CHECK(cieWavelengthTable[0].xyz.isValid());
}

TEST_CASE("CIEWavelengthTable: entries have valid CIE points") {
        CHECK(cieWavelengthTable[0].xy.isValid());
}

TEST_CASE("CIEWavelengthTable: 550nm green peak region") {
        // 550nm is at index 190 (550 - 360)
        auto entry = cieWavelengthTable[190];
        CHECK(entry.wavelength == doctest::Approx(550.0));
        // Green region: Y should be significant
        CHECK(entry.xyz.y() > 0.5);
}

TEST_CASE("CIEWavelengthTable: wavelengths are monotonically increasing") {
        for(int i = 1; i < 471; ++i) {
                CHECK(cieWavelengthTable[i].wavelength > cieWavelengthTable[i - 1].wavelength);
        }
}

TEST_CASE("CIEWavelengthTable: last entry is 830nm") {
        CHECK(cieWavelengthTable[470].wavelength == doctest::Approx(830.0));
}
