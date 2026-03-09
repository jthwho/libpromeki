/**
 * @file      videodesc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#include <promeki/unittest.h>
#include <promeki/videodesc.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

PROMEKI_TEST_BEGIN(VideoDesc_Default)
    VideoDesc vd;
    PROMEKI_TEST(!vd.isValid());
    PROMEKI_TEST(vd.referenceCount() == 1);
PROMEKI_TEST_END()

// ============================================================================
// Set frame rate
// ============================================================================

PROMEKI_TEST_BEGIN(VideoDesc_SetFrameRate)
    VideoDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_2997));
    PROMEKI_TEST(vd.frameRate().isValid());
    PROMEKI_TEST(vd.frameRate().numerator() == 30000);
    PROMEKI_TEST(vd.frameRate().denominator() == 1001);
PROMEKI_TEST_END()

// ============================================================================
// Valid with image
// ============================================================================

PROMEKI_TEST_BEGIN(VideoDesc_ValidWithImage)
    VideoDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_24));
    PROMEKI_TEST(!vd.isValid());

    vd.imageList().pushToBack(ImageDesc(1920, 1080, PixelFormat::RGBA8));
    PROMEKI_TEST(vd.isValid());
    PROMEKI_TEST(vd.imageList().size() == 1);
PROMEKI_TEST_END()

// ============================================================================
// Valid with audio
// ============================================================================

PROMEKI_TEST_BEGIN(VideoDesc_ValidWithAudio)
    VideoDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_25));
    vd.audioList().pushToBack(AudioDesc(48000.0f, 2));
    PROMEKI_TEST(vd.isValid());
    PROMEKI_TEST(vd.audioList().size() == 1);
PROMEKI_TEST_END()

// ============================================================================
// Copy-on-write
// ============================================================================

PROMEKI_TEST_BEGIN(VideoDesc_CopyOnWrite)
    VideoDesc v1;
    v1.setFrameRate(FrameRate(FrameRate::FPS_24));
    v1.imageList().pushToBack(ImageDesc(1920, 1080, PixelFormat::RGBA8));

    VideoDesc v2 = v1;
    PROMEKI_TEST(v1.referenceCount() == 2);

    v2.setFrameRate(FrameRate(FrameRate::FPS_30));
    PROMEKI_TEST(v1.referenceCount() == 1);
    PROMEKI_TEST(v2.referenceCount() == 1);
    PROMEKI_TEST(v1.frameRate().numerator() == 24);
    PROMEKI_TEST(v2.frameRate().numerator() == 30);
PROMEKI_TEST_END()

// ============================================================================
// Metadata
// ============================================================================

PROMEKI_TEST_BEGIN(VideoDesc_Metadata)
    VideoDesc vd;
    PROMEKI_TEST(vd.metadata().isEmpty());
    vd.metadata().set(Metadata::Title, String("Test Video"));
    PROMEKI_TEST(!vd.metadata().isEmpty());
PROMEKI_TEST_END()

// ============================================================================
// Multiple images and audio
// ============================================================================

PROMEKI_TEST_BEGIN(VideoDesc_MultipleStreams)
    VideoDesc vd;
    vd.setFrameRate(FrameRate(FrameRate::FPS_2398));
    vd.imageList().pushToBack(ImageDesc(1920, 1080, PixelFormat::RGBA8));
    vd.imageList().pushToBack(ImageDesc(3840, 2160, PixelFormat::RGB8));
    vd.audioList().pushToBack(AudioDesc(48000.0f, 2));
    vd.audioList().pushToBack(AudioDesc(48000.0f, 8));

    PROMEKI_TEST(vd.isValid());
    PROMEKI_TEST(vd.imageList().size() == 2);
    PROMEKI_TEST(vd.audioList().size() == 2);
PROMEKI_TEST_END()
