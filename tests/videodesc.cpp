/**
 * @file      videodesc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediadesc.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("MediaDesc_Default") {
    MediaDesc vd;
    CHECK(!vd.isValid());
}

// ============================================================================
// Set frame rate
// ============================================================================

TEST_CASE("MediaDesc_SetFrameRate") {
    MediaDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_2997));
    CHECK(vd.frameRate().isValid());
    CHECK(vd.frameRate().numerator() == 30000);
    CHECK(vd.frameRate().denominator() == 1001);
}

// ============================================================================
// Valid with image
// ============================================================================

TEST_CASE("MediaDesc_ValidWithImage") {
    MediaDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_24));
    CHECK(!vd.isValid());

    vd.imageList().pushToBack(ImageDesc(1920, 1080, PixelDesc::RGBA8_sRGB));
    CHECK(vd.isValid());
    CHECK(vd.imageList().size() == 1);
}

// ============================================================================
// Valid with audio
// ============================================================================

TEST_CASE("MediaDesc_ValidWithAudio") {
    MediaDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_25));
    vd.audioList().pushToBack(AudioDesc(48000.0f, 2));
    CHECK(vd.isValid());
    CHECK(vd.audioList().size() == 1);
}

// ============================================================================
// Copy semantics (plain value, no internal COW)
// ============================================================================

TEST_CASE("MediaDesc_CopyIsIndependent") {
    MediaDesc v1;
    v1.setFrameRate(FrameRate(FrameRate::FPS_24));
    v1.imageList().pushToBack(ImageDesc(1920, 1080, PixelDesc::RGBA8_sRGB));

    MediaDesc v2 = v1;

    v2.setFrameRate(FrameRate(FrameRate::FPS_30));
    CHECK(v1.frameRate().numerator() == 24);
    CHECK(v2.frameRate().numerator() == 30);
}

// ============================================================================
// Metadata
// ============================================================================

TEST_CASE("MediaDesc_Metadata") {
    MediaDesc vd;
    CHECK(vd.metadata().isEmpty());
    vd.metadata().set(Metadata::Title, String("Test Video"));
    CHECK(!vd.metadata().isEmpty());
}

// ============================================================================
// Multiple images and audio
// ============================================================================

TEST_CASE("MediaDesc_MultipleStreams") {
    MediaDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_2398));
    vd.imageList().pushToBack(ImageDesc(1920, 1080, PixelDesc::RGBA8_sRGB));
    vd.imageList().pushToBack(ImageDesc(3840, 2160, PixelDesc::RGB8_sRGB));
    vd.audioList().pushToBack(AudioDesc(48000.0f, 2));
    vd.audioList().pushToBack(AudioDesc(48000.0f, 8));

    CHECK(vd.isValid());
    CHECK(vd.imageList().size() == 2);
    CHECK(vd.audioList().size() == 2);
}
