/**
 * @file      codec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/codec.h>

using namespace promeki;

TEST_CASE("Codec: default construction") {
        Codec c;
        CHECK(true); // Just verifies it constructs without crash
}

TEST_CASE("Codec: createInstance returns nullptr by default") {
        Codec c;
        CHECK(c.createInstance() == nullptr);
}

TEST_CASE("Codec::Instance: convert returns empty image") {
        Codec c;
        // Instance base class should be constructible
        Codec::Instance inst(&c);
        Image empty;
        Image result = inst.convert(empty);
        CHECK_FALSE(result.isValid());
}
