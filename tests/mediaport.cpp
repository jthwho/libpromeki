/**
 * @file      mediaport.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/mediaport.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("MediaPort_Default") {
    MediaPort port;
    CHECK(port.name().isEmpty());
    CHECK(port.direction() == MediaPort::Input);
    CHECK(port.mediaType() == MediaPort::Frame);
    CHECK(!port.isConnected());
    CHECK(port.node() == nullptr);
}

// ============================================================================
// Construction with parameters
// ============================================================================

TEST_CASE("MediaPort_Construct") {
    MediaPort port("video_out", MediaPort::Output, MediaPort::Image);
    CHECK(port.name() == "video_out");
    CHECK(port.direction() == MediaPort::Output);
    CHECK(port.mediaType() == MediaPort::Image);
}

// ============================================================================
// Setters
// ============================================================================

TEST_CASE("MediaPort_SetName") {
    MediaPort port;
    port.setName("audio_in");
    CHECK(port.name() == "audio_in");
}

TEST_CASE("MediaPort_SetConnected") {
    MediaPort port;
    CHECK(!port.isConnected());
    port.setConnected(true);
    CHECK(port.isConnected());
}

// ============================================================================
// Descriptor assignment
// ============================================================================

TEST_CASE("MediaPort_SetAudioDesc") {
    MediaPort port("audio", MediaPort::Output, MediaPort::Audio);
    AudioDesc desc(48000.0f, 2);
    port.setAudioDesc(desc);
    CHECK(port.audioDesc().isValid());
    CHECK(port.audioDesc().channels() == 2);
}

TEST_CASE("MediaPort_SetImageDesc") {
    MediaPort port("image", MediaPort::Output, MediaPort::Image);
    ImageDesc desc(1920, 1080, PixelFormat::RGB8);
    port.setImageDesc(desc);
    CHECK(port.imageDesc().isValid());
    CHECK(port.imageDesc().width() == 1920);
}

TEST_CASE("MediaPort_SetEncodedDesc") {
    MediaPort port("encoded", MediaPort::Output, MediaPort::Encoded);
    EncodedDesc desc(FourCC("JPEG"));
    port.setEncodedDesc(desc);
    CHECK(port.encodedDesc().isValid());
    CHECK(port.encodedDesc().codec() == FourCC("JPEG"));
}

// ============================================================================
// Compatibility: same media type
// ============================================================================

TEST_CASE("MediaPort_CompatibleSameType_Audio") {
    AudioDesc desc(48000.0f, 2);
    MediaPort out("out", MediaPort::Output, MediaPort::Audio);
    out.setAudioDesc(desc);
    MediaPort in("in", MediaPort::Input, MediaPort::Audio);
    in.setAudioDesc(desc);
    CHECK(out.isCompatible(in));
    CHECK(in.isCompatible(out));
}

TEST_CASE("MediaPort_IncompatibleSameDirection") {
    MediaPort a("a", MediaPort::Output, MediaPort::Audio);
    MediaPort b("b", MediaPort::Output, MediaPort::Audio);
    CHECK(!a.isCompatible(b));
}

TEST_CASE("MediaPort_CompatibleSameType_Image") {
    ImageDesc desc(1920, 1080, PixelFormat::RGB8);
    MediaPort out("out", MediaPort::Output, MediaPort::Image);
    out.setImageDesc(desc);
    MediaPort in("in", MediaPort::Input, MediaPort::Image);
    in.setImageDesc(desc);
    CHECK(out.isCompatible(in));
}

TEST_CASE("MediaPort_CompatibleSameType_Encoded") {
    EncodedDesc desc(FourCC("JPEG"));
    MediaPort out("out", MediaPort::Output, MediaPort::Encoded);
    out.setEncodedDesc(desc);
    MediaPort in("in", MediaPort::Input, MediaPort::Encoded);
    in.setEncodedDesc(desc);
    CHECK(out.isCompatible(in));
}

TEST_CASE("MediaPort_CompatibleSameType_Frame") {
    MediaPort out("out", MediaPort::Output, MediaPort::Frame);
    MediaPort in("in", MediaPort::Input, MediaPort::Frame);
    CHECK(out.isCompatible(in));
}

// ============================================================================
// Compatibility: Frame -> Image/Audio extraction
// ============================================================================

TEST_CASE("MediaPort_CompatibleFrameToImage") {
    MediaPort out("out", MediaPort::Output, MediaPort::Frame);
    MediaPort in("in", MediaPort::Input, MediaPort::Image);
    CHECK(out.isCompatible(in));
}

TEST_CASE("MediaPort_CompatibleFrameToAudio") {
    MediaPort out("out", MediaPort::Output, MediaPort::Frame);
    MediaPort in("in", MediaPort::Input, MediaPort::Audio);
    CHECK(out.isCompatible(in));
}

// ============================================================================
// Compatibility: incompatible combinations
// ============================================================================

TEST_CASE("MediaPort_IncompatibleImageToFrame") {
    MediaPort out("out", MediaPort::Output, MediaPort::Image);
    MediaPort in("in", MediaPort::Input, MediaPort::Frame);
    CHECK(!out.isCompatible(in));
}

TEST_CASE("MediaPort_IncompatibleAudioToFrame") {
    MediaPort out("out", MediaPort::Output, MediaPort::Audio);
    MediaPort in("in", MediaPort::Input, MediaPort::Frame);
    CHECK(!out.isCompatible(in));
}

TEST_CASE("MediaPort_IncompatibleEncodedToImage") {
    MediaPort out("out", MediaPort::Output, MediaPort::Encoded);
    MediaPort in("in", MediaPort::Input, MediaPort::Image);
    CHECK(!out.isCompatible(in));
}

TEST_CASE("MediaPort_IncompatibleImageToEncoded") {
    MediaPort out("out", MediaPort::Output, MediaPort::Image);
    MediaPort in("in", MediaPort::Input, MediaPort::Encoded);
    CHECK(!out.isCompatible(in));
}

TEST_CASE("MediaPort_IncompatibleAudioToImage") {
    MediaPort out("out", MediaPort::Output, MediaPort::Audio);
    MediaPort in("in", MediaPort::Input, MediaPort::Image);
    CHECK(!out.isCompatible(in));
}

// ============================================================================
// Copy semantics
// ============================================================================

TEST_CASE("MediaPort_CopyIsIndependent") {
    MediaPort p1("original", MediaPort::Output, MediaPort::Image);
    MediaPort p2 = p1;

    p2.setName("copy");
    CHECK(p1.name() == "original");
    CHECK(p2.name() == "copy");
}

// ============================================================================
// VideoDesc setter/getter
// ============================================================================

TEST_CASE("MediaPort_SetVideoDesc") {
    MediaPort port("video", MediaPort::Output, MediaPort::Frame);
    VideoDesc desc;
    port.setVideoDesc(desc);
    // Just verify we can set and get a VideoDesc
    CHECK(port.videoDesc().isValid() == desc.isValid());
}

// ============================================================================
// Audio format mismatch
// ============================================================================

TEST_CASE("MediaPort_IncompatibleAudioFormats") {
    AudioDesc desc1(48000.0f, 2);
    AudioDesc desc2(44100.0f, 2);
    MediaPort out("out", MediaPort::Output, MediaPort::Audio);
    out.setAudioDesc(desc1);
    MediaPort in("in", MediaPort::Input, MediaPort::Audio);
    in.setAudioDesc(desc2);
    CHECK(!out.isCompatible(in));
}

// ============================================================================
// Image format mismatch
// ============================================================================

TEST_CASE("MediaPort_IncompatibleImageFormats") {
    MediaPort out("out", MediaPort::Output, MediaPort::Image);
    out.setImageDesc(ImageDesc(1920, 1080, PixelFormat::RGB8));
    MediaPort in("in", MediaPort::Input, MediaPort::Image);
    in.setImageDesc(ImageDesc(1280, 720, PixelFormat::RGB8));
    CHECK(!out.isCompatible(in));
}

// ============================================================================
// Encoded codec mismatch
// ============================================================================

TEST_CASE("MediaPort_IncompatibleEncodedCodecs") {
    MediaPort out("out", MediaPort::Output, MediaPort::Encoded);
    out.setEncodedDesc(EncodedDesc(FourCC("JPEG")));
    MediaPort in("in", MediaPort::Input, MediaPort::Encoded);
    in.setEncodedDesc(EncodedDesc(FourCC("H264")));
    CHECK(!out.isCompatible(in));
}

// ============================================================================
// Frame-to-Encoded incompatibility
// ============================================================================

TEST_CASE("MediaPort_IncompatibleFrameToEncoded") {
    MediaPort out("out", MediaPort::Output, MediaPort::Frame);
    MediaPort in("in", MediaPort::Input, MediaPort::Encoded);
    CHECK(!out.isCompatible(in));
}

TEST_CASE("MediaPort_IncompatibleEncodedToAudio") {
    MediaPort out("out", MediaPort::Output, MediaPort::Encoded);
    MediaPort in("in", MediaPort::Input, MediaPort::Audio);
    CHECK(!out.isCompatible(in));
}
