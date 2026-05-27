/**
 * @file      rxpayloadbundle.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/imagedesc.h>
#include <promeki/metadata.h>
#include <promeki/ntptime.h>
#include <promeki/rxpayloadbundle.h>
#include <promeki/timestamp.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/videopayload.h>

using namespace promeki;

TEST_CASE("RxVideoFrame: default shape") {
        RxVideoFrame f;
        CHECK(f.payload.isNull());
        CHECK(f.rtpTimestamp == 0u);
        CHECK(f.packetCount == 0);
        CHECK_FALSE(f.wallclockNtp.isValid());
        CHECK_FALSE(f.captureTime.isValid());
        CHECK_FALSE(f.firstPacketArrival.isValid());
        CHECK_FALSE(f.keyframe);
        CHECK(f.streamFrameIndex.isUnknown());
}

TEST_CASE("RxVideoFrame: copy preserves every field") {
        RxVideoFrame f;
        f.payload = UncompressedVideoPayload::Ptr::create();
        f.imageDesc = ImageDesc(Size2Du32(1920, 1080), PixelFormat::YUV8_422_Rec709);
        f.rtpTimestamp = 0xDEADBEEFu;
        f.packetCount = 7;
        f.wallclockNtp = NtpTime(1234567890u, 0x80000000u);
        f.captureTime = TimeStamp::now();
        f.firstPacketArrival = TimeStamp::now();
        f.keyframe = true;
        f.streamFrameIndex = FrameNumber(123);

        RxVideoFrame g = f;
        CHECK(g.payload.ptr() == f.payload.ptr()); // CoW shares storage on copy
        CHECK(g.imageDesc.size() == f.imageDesc.size());
        CHECK(g.rtpTimestamp == 0xDEADBEEFu);
        CHECK(g.packetCount == 7);
        CHECK(g.wallclockNtp.seconds() == 1234567890u);
        CHECK(g.captureTime == f.captureTime);
        CHECK(g.firstPacketArrival == f.firstPacketArrival);
        CHECK(g.keyframe);
        CHECK(g.streamFrameIndex.value() == 123);
}

TEST_CASE("RxVideoFrame: move transfers payload handle without copy") {
        RxVideoFrame src;
        src.payload = UncompressedVideoPayload::Ptr::create();
        const VideoPayload *raw = src.payload.ptr();

        RxVideoFrame dst = std::move(src);
        CHECK(dst.payload.ptr() == raw);
        // Moved-from src may be in any valid state; only check that
        // the destination owns the same handle the source held.
}

TEST_CASE("RxAudioChunk: default shape") {
        RxAudioChunk c;
        CHECK_FALSE(c.pcmBytes.isValid());
        CHECK_FALSE(c.wireDesc.isValid());
        CHECK(c.rtpTimestamp == 0u);
        CHECK(c.sampleCount == 0u);
        CHECK_FALSE(c.wallclockNtp.isValid());
        CHECK_FALSE(c.captureTime.isValid());
        CHECK_FALSE(c.firstPacketArrival.isValid());
}

TEST_CASE("RxAudioChunk: copy preserves every field") {
        RxAudioChunk c;
        c.pcmBytes = Buffer(1024);
        c.wireDesc = AudioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        c.rtpTimestamp = 0xCAFEBABEu;
        c.sampleCount = 480;
        c.wallclockNtp = NtpTime(1u, 2u);
        c.captureTime = TimeStamp::now();
        c.firstPacketArrival = TimeStamp::now();

        RxAudioChunk d = c;
        // Buffer is a CoW handle — copy is a refcount bump.
        CHECK(d.pcmBytes.data() == c.pcmBytes.data());
        CHECK(d.wireDesc.sampleRate() == 48000.0f);
        CHECK(d.wireDesc.channels() == 2u);
        CHECK(d.rtpTimestamp == 0xCAFEBABEu);
        CHECK(d.sampleCount == 480u);
        CHECK(d.wallclockNtp.seconds() == 1u);
        CHECK(d.wallclockNtp.fraction() == 2u);
        CHECK(d.captureTime == c.captureTime);
        CHECK(d.firstPacketArrival == c.firstPacketArrival);
}

TEST_CASE("RxAudioChunk: move transfers buffer handle") {
        RxAudioChunk src;
        src.pcmBytes = Buffer(512);
        const void *raw = src.pcmBytes.data();

        RxAudioChunk dst = std::move(src);
        CHECK(dst.pcmBytes.data() == raw);
}

TEST_CASE("RxDataMessage: default shape") {
        RxDataMessage m;
        CHECK(m.rtpTimestamp == 0u);
        CHECK(m.packetCount == 0);
        CHECK_FALSE(m.wallclockNtp.isValid());
        CHECK_FALSE(m.captureTime.isValid());
        CHECK_FALSE(m.firstPacketArrival.isValid());
}

TEST_CASE("RxDataMessage: copy preserves every field") {
        RxDataMessage m;
        m.metadata.set(Metadata::Title, String("hello"));
        m.rtpTimestamp = 0x12345678u;
        m.packetCount = 3;
        m.wallclockNtp = NtpTime(9u, 10u);
        m.captureTime = TimeStamp::now();
        m.firstPacketArrival = TimeStamp::now();

        RxDataMessage n = m;
        CHECK(n.metadata.get(Metadata::Title).get<String>() == String("hello"));
        CHECK(n.rtpTimestamp == 0x12345678u);
        CHECK(n.packetCount == 3);
        CHECK(n.wallclockNtp.seconds() == 9u);
        CHECK(n.captureTime == m.captureTime);
        CHECK(n.firstPacketArrival == m.firstPacketArrival);
}
