/**
 * @file      tests/typeregistry_isolation.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Cross-cutting regression test for the @ref typeregistry "TypeRegistry"
 * pattern.  Every TypeRegistry-style class in the library
 * (VideoCodec, AudioCodec, ColorModel, PixelDesc, PixelFormat,
 * MemSpace, ClockDomain) backs its lookups with its own
 * construct-on-first-use singleton.  Each singleton lives behind a
 * helper struct in the corresponding @c .cpp file.
 *
 * If two of those helper structs end up with the same unqualified
 * name in the same namespace (e.g. both called @c DataRegistry), the
 * linker happily merges them under the One Definition Rule and
 * lookups silently start returning the wrong registry's data — every
 * @ref AudioCodec ID will resolve to a @ref ColorModel name, for
 * instance.  We hit exactly that landmine while adding the @ref
 * VideoCodec / @ref AudioCodec registries; this test exists to make
 * sure the next contributor who reuses the pattern doesn't trip the
 * same wire without a noisy red CI build.
 *
 * The test picks an ID from each registry that *also* maps to a
 * different value in every other registry, then asserts that the
 * resolved name matches the one registered against that registry's
 * specific class.  Any future ODR collision between two registries
 * would fail one of these CHECK lines.
 */

#include <doctest/doctest.h>
#include <promeki/audiocodec.h>
#include <promeki/clockdomain.h>
#include <promeki/colormodel.h>
#include <promeki/memspace.h>
#include <promeki/pixeldesc.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>

using namespace promeki;

TEST_CASE("TypeRegistry: each registry resolves to its own Data records") {
        // Pick well-known IDs whose integer values overlap across
        // registries (all of these have an entry at id == 1, for
        // example) — that's the worst case for an ODR-merged
        // singleton: any two registries trampling each other would
        // surface as one of the lookups returning the other's name.
        CHECK(VideoCodec(VideoCodec::H264).name()         == "H264");
        CHECK(VideoCodec(VideoCodec::HEVC).name()         == "HEVC");
        CHECK(VideoCodec(VideoCodec::JPEG).name()         == "JPEG");

        CHECK(AudioCodec(AudioCodec::PCMI_S16LE).name()   == "PCMI_S16LE");
        CHECK(AudioCodec(AudioCodec::AAC).name()          == "AAC");
        CHECK(AudioCodec(AudioCodec::Opus).name()         == "Opus");

        CHECK(ColorModel(ColorModel::sRGB).name()         == "sRGB");
        CHECK(ColorModel(ColorModel::Rec709).name()       == "Rec709");
        CHECK(ColorModel(ColorModel::YCbCr_Rec709).name() == "YCbCr_Rec709");

        CHECK(PixelDesc(PixelDesc::RGBA8_sRGB).name()     == "RGBA8_sRGB");
        CHECK(PixelDesc(PixelDesc::H264).name()           == "H264");

        CHECK(PixelFormat(PixelFormat::I_4x8).name()      == "4x8");
        CHECK(PixelFormat(PixelFormat::I_3x8).name()      == "3x8");

        CHECK(MemSpace(MemSpace::System).name()           == "System");
        CHECK(MemSpace(MemSpace::SystemSecure).name()     == "SystemSecure");

        CHECK(ClockDomain(ClockDomain::SystemMonotonic).name()
                                                          == "SystemMonotonic");
}

TEST_CASE("TypeRegistry: lookup() returns the right type's entry") {
        // Same defence on the name-based lookup path — anyone
        // overloading on String name needs every registry's nameMap
        // to be its own.
        CHECK(VideoCodec::lookup("H264")  == VideoCodec(VideoCodec::H264));
        CHECK(AudioCodec::lookup("AAC")   == AudioCodec(AudioCodec::AAC));
        CHECK(ColorModel::lookup("sRGB")  == ColorModel(ColorModel::sRGB));
        CHECK(PixelDesc::lookup("RGBA8_sRGB")
                == PixelDesc(PixelDesc::RGBA8_sRGB));
        CHECK(PixelFormat::lookup("4x8")
                == PixelFormat(PixelFormat::I_4x8));

        // Cross-registry "wrong type" name lookups must miss.  If two
        // singletons collapsed, e.g. AudioCodec::lookup("H264") would
        // wrongly succeed.
        CHECK_FALSE(AudioCodec::lookup("H264").isValid());
        CHECK_FALSE(VideoCodec::lookup("AAC").isValid());
        CHECK_FALSE(ColorModel::lookup("H264").isValid());
        CHECK_FALSE(PixelDesc::lookup("AAC").isValid());
}
