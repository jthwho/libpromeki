/**
 * @file      tests/codec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/codec.h>

using namespace promeki;

TEST_CASE("ImageCodec: registry starts with registered codecs") {
        auto codecs = ImageCodec::registeredCodecs();
        // jpeg codec should be registered via PROMEKI_REGISTER_IMAGE_CODEC
        CHECK(codecs.contains("jpeg"));
}

TEST_CASE("ImageCodec: createCodec returns instance for registered codec") {
        ImageCodec *codec = ImageCodec::createCodec("jpeg");
        REQUIRE(codec != nullptr);
        CHECK(codec->name() == "jpeg");
        CHECK(codec->canEncode());
        delete codec;
}

TEST_CASE("ImageCodec: createCodec returns nullptr for unknown codec") {
        ImageCodec *codec = ImageCodec::createCodec("nonexistent");
        CHECK(codec == nullptr);
}
