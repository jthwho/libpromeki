/**
 * @file      videodesc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/videodesc.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("VideoDesc_Default") {
    VideoDesc vd;
    CHECK(!vd.isValid());
    CHECK(vd.referenceCount() == 1);
}

// ============================================================================
// Set frame rate
// ============================================================================

TEST_CASE("VideoDesc_SetFrameRate") {
    VideoDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_2997));
    CHECK(vd.frameRate().isValid());
    CHECK(vd.frameRate().numerator() == 30000);
    CHECK(vd.frameRate().denominator() == 1001);
}

// ============================================================================
// Valid with image
// ============================================================================

TEST_CASE("VideoDesc_ValidWithImage") {
    VideoDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_24));
    CHECK(!vd.isValid());

    vd.imageList().pushToBack(ImageDesc(1920, 1080, PixelFormat::RGBA8));
    CHECK(vd.isValid());
    CHECK(vd.imageList().size() == 1);
}

// ============================================================================
// Valid with audio
// ============================================================================

TEST_CASE("VideoDesc_ValidWithAudio") {
    VideoDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_25));
    vd.audioList().pushToBack(AudioDesc(48000.0f, 2));
    CHECK(vd.isValid());
    CHECK(vd.audioList().size() == 1);
}

// ============================================================================
// Copy-on-write
// ============================================================================

TEST_CASE("VideoDesc_CopyOnWrite") {
    VideoDesc v1;
    v1.setFrameRate(FrameRate(FrameRate::FPS_24));
    v1.imageList().pushToBack(ImageDesc(1920, 1080, PixelFormat::RGBA8));

    VideoDesc v2 = v1;
    CHECK(v1.referenceCount() == 2);

    v2.setFrameRate(FrameRate(FrameRate::FPS_30));
    CHECK(v1.referenceCount() == 1);
    CHECK(v2.referenceCount() == 1);
    CHECK(v1.frameRate().numerator() == 24);
    CHECK(v2.frameRate().numerator() == 30);
}

// ============================================================================
// Metadata
// ============================================================================

TEST_CASE("VideoDesc_Metadata") {
    VideoDesc vd;
    CHECK(vd.metadata().isEmpty());
    vd.metadata().set(Metadata::Title, String("Test Video"));
    CHECK(!vd.metadata().isEmpty());
}

// ============================================================================
// Multiple images and audio
// ============================================================================

TEST_CASE("VideoDesc_MultipleStreams") {
    VideoDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_2398));
    vd.imageList().pushToBack(ImageDesc(1920, 1080, PixelFormat::RGBA8));
    vd.imageList().pushToBack(ImageDesc(3840, 2160, PixelFormat::RGB8));
    vd.audioList().pushToBack(AudioDesc(48000.0f, 2));
    vd.audioList().pushToBack(AudioDesc(48000.0f, 8));

    CHECK(vd.isValid());
    CHECK(vd.imageList().size() == 2);
    CHECK(vd.audioList().size() == 2);
}
