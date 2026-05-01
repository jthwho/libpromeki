/**
 * @file      tests/mediaio_identifiers.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for MediaIO's per-instance identifier (name).  The TPG backend
 * is used because it requires no external files and works as a
 * self-contained reader.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiorequest.h>
#include <promeki/tpgmediaio.h>
#include <promeki/mediaconfig.h>
#include <promeki/framerate.h>
#include <promeki/videoformat.h>
#include <promeki/pixelformat.h>

using namespace promeki;

namespace {

        /**
         * @brief Helper: build a configured TPG reader for name tests.
         */
        MediaIO *makeTpgReader(const String &nameOverride = String()) {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "TPG");
                cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
                cfg.set(MediaConfig::VideoEnabled, true);
                if (!nameOverride.isEmpty()) {
                        cfg.set(MediaConfig::Name, nameOverride);
                }
                return MediaIO::create(cfg);
        }

} // namespace

TEST_CASE("MediaIO::name is empty when no MediaConfig::Name is set") {
        MediaIO *io = makeTpgReader();
        REQUIRE(io != nullptr);
        CHECK(io->name().isEmpty());
        delete io;
}

TEST_CASE("MediaIO::name reflects MediaConfig::Name pre-open") {
        MediaIO *io = makeTpgReader(String("tpg-source"));
        REQUIRE(io != nullptr);
        CHECK(io->name() == "tpg-source");
        delete io;
}

TEST_CASE("MediaIO::name reflects MediaConfig::Name through open/close") {
        MediaIO *io = makeTpgReader(String("tpg-source"));
        REQUIRE(io != nullptr);
        Error err = io->open().wait();
        REQUIRE(err.isOk());
        CHECK(io->name() == "tpg-source");
        CHECK(io->config().getAs<String>(MediaConfig::Name) == "tpg-source");
        io->close().wait();
        CHECK(io->name() == "tpg-source");
        delete io;
}
