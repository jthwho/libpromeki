/**
 * @file      rtpaudiopacketizerthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>

#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/frame.h>
#include <promeki/list.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/queue.h>
#include <promeki/rtpaudiopacketizerthread.h>
#include <promeki/rtppacketizerthread.h>
#include <promeki/sharedptr.h>

using namespace promeki;

namespace {

// Constructs a Frame holding a single stereo PCMI_S16LE payload
// whose samples form a deterministic ramp.  The packetizer's FIFO
// is configured (via storageDesc) for PCMI_S16BE — converting on
// push exercises the conversion path.  The ramp lets unit tests
// verify sample-exact wire content after the packetizer slices
// the FIFO into AES67 chunks.
Frame makeRampFrame(size_t samplesPerChannel, float sampleRate, unsigned channels) {
        AudioDesc    desc(AudioFormat::PCMI_S16LE, sampleRate, channels);
        const size_t bytes = desc.bufferSize(samplesPerChannel);
        Buffer       buf(bytes);
        buf.setSize(bytes);
        auto *p = static_cast<int16_t *>(buf.data());
        for (size_t i = 0; i < samplesPerChannel; ++i) {
                const int16_t v = static_cast<int16_t>(i & 0x7FFF);
                for (unsigned c = 0; c < channels; ++c) *p++ = v;
        }
        BufferView planes;
        planes.pushToBack(buf, 0, bytes);
        auto pcm = PcmAudioPayload::Ptr::create(desc, samplesPerChannel, planes);
        Frame frame;
        frame.addPayload(pcm);
        return frame;
}

// Reads a 16-bit big-endian sample from @p data at byte offset
// @p index*2.  Used to decode the wire bytes back into S16 values
// for byte-by-byte ramp comparison.
int16_t s16BeAt(const uint8_t *data, size_t sampleIdx) {
        const uint8_t hi = data[sampleIdx * 2];
        const uint8_t lo = data[sampleIdx * 2 + 1];
        return static_cast<int16_t>((static_cast<uint16_t>(hi) << 8) | lo);
}

} // namespace

TEST_CASE("RtpAudioPacketizerThread: produces sample-exact wire content from a ramp") {
        // Wire-format storage = PCMI_S16BE (AES67); inbound payload
        // is PCMI_S16LE so the FIFO push converts on the fly.
        AudioDesc storage(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        REQUIRE(storage.isValid());

        // 1 ms / 48 samples per stereo packet, so packetBytes =
        // 48 × 2 × 2 = 192.  No preroll — emit on the first push.
        constexpr size_t   kPacketSamples = 48;
        constexpr size_t   kPacketBytes = kPacketSamples * 2 /*ch*/ * 2 /*bytes*/;
        Queue<Buffer> txQueue;

        RtpAudioPacketizerContext ctx;
        ctx.storageDesc = storage;
        ctx.packetSamples = kPacketSamples;
        ctx.packetBytes = kPacketBytes;
        ctx.prerollSamples = 0;
        ctx.streamIdx = 0;
        ctx.txPacketQueue = &txQueue;

        RtpAudioPacketizerThread pkt(std::move(ctx));
        pkt.openForTest(); // build the FIFO before the first packetize

        // Push three packets' worth of ramp samples in one go.
        constexpr size_t kPushSamples = kPacketSamples * 3;
        Frame            frame = makeRampFrame(kPushSamples, 48000.0f, 2);
        RtpFrameWork     work;
        work.frame = frame;
        work.frameIndex = FrameNumber(0);
        pkt.packetizeForTest(work);

        // Three chunks should have been pushed onto txQueue.
        CHECK(txQueue.size() == 3);

        // Decode each chunk's wire bytes back to S16 and compare
        // against the expected ramp value (the LE→BE conversion
        // path inside AudioBuffer::push has to reproduce the input
        // sample values bit-exactly for signed PCM).
        for (size_t chunkIdx = 0; chunkIdx < 3; ++chunkIdx) {
                auto popped = txQueue.tryPop();
                REQUIRE(popped.second().isOk());
                const Buffer &chunk = popped.first();
                REQUIRE(chunk.size() == kPacketBytes);
                const auto *bytes = static_cast<const uint8_t *>(chunk.data());
                for (size_t s = 0; s < kPacketSamples; ++s) {
                        // Stereo → 2 samples per "frame index"; chunk
                        // sample index is s*2 / s*2+1 for left/right.
                        const size_t  globalSample = chunkIdx * kPacketSamples + s;
                        const int16_t expected =
                                static_cast<int16_t>(globalSample & 0x7FFF);
                        CHECK(s16BeAt(bytes, s * 2) == expected);
                        CHECK(s16BeAt(bytes, s * 2 + 1) == expected);
                }
        }
        CHECK(txQueue.size() == 0);
}

TEST_CASE("RtpAudioPacketizerThread: preroll holds off emissions until threshold") {
        AudioDesc storage(AudioFormat::PCMI_S16BE, 48000.0f, 2);

        constexpr size_t kPacketSamples = 48;
        constexpr size_t kPacketBytes = kPacketSamples * 2 * 2;
        constexpr size_t kPreroll = 2 * kPacketSamples; // 96 samples

        Queue<Buffer> txQueue;
        RtpAudioPacketizerContext ctx;
        ctx.storageDesc = storage;
        ctx.packetSamples = kPacketSamples;
        ctx.packetBytes = kPacketBytes;
        ctx.prerollSamples = kPreroll;
        ctx.streamIdx = 0;
        ctx.txPacketQueue = &txQueue;

        RtpAudioPacketizerThread pkt(std::move(ctx));
        pkt.openForTest();

        // First push: 48 samples (= 1 packet's worth, < preroll) → no emissions.
        {
                Frame        f = makeRampFrame(kPacketSamples, 48000.0f, 2);
                RtpFrameWork w;
                w.frame = f;
                w.frameIndex = FrameNumber(0);
                pkt.packetizeForTest(w);
        }
        CHECK(txQueue.size() == 0);
        CHECK_FALSE(pkt.isPrerollDone());

        // Second push: another 48 samples → FIFO now holds 96 ≥ preroll;
        // packetizer drains both packets onto the TX queue.
        {
                Frame        f = makeRampFrame(kPacketSamples, 48000.0f, 2);
                RtpFrameWork w;
                w.frame = f;
                w.frameIndex = FrameNumber(1);
                pkt.packetizeForTest(w);
        }
        CHECK(pkt.isPrerollDone());
        CHECK(txQueue.size() == 2);
}

TEST_CASE("RtpAudioPacketizerThread: handles wrong streamIdx as a no-op") {
        AudioDesc storage(AudioFormat::PCMI_S16BE, 48000.0f, 2);

        Queue<Buffer> txQueue;
        RtpAudioPacketizerContext ctx;
        ctx.storageDesc = storage;
        ctx.packetSamples = 48;
        ctx.packetBytes = 48 * 2 * 2;
        ctx.prerollSamples = 0;
        ctx.streamIdx = 5; // Frame only has one payload at index 0.
        ctx.txPacketQueue = &txQueue;

        RtpAudioPacketizerThread pkt(std::move(ctx));
        pkt.openForTest();

        Frame        f = makeRampFrame(48, 48000.0f, 2);
        RtpFrameWork w;
        w.frame = f;
        w.frameIndex = FrameNumber(0);
        pkt.packetizeForTest(w); // must not crash; nothing emitted.
        CHECK(txQueue.size() == 0);
}

TEST_CASE("RtpAudioPacketizerThread: zero-sample payload is a no-op") {
        AudioDesc storage(AudioFormat::PCMI_S16BE, 48000.0f, 2);

        Queue<Buffer> txQueue;
        RtpAudioPacketizerContext ctx;
        ctx.storageDesc = storage;
        ctx.packetSamples = 48;
        ctx.packetBytes = 48 * 2 * 2;
        ctx.prerollSamples = 0;
        ctx.streamIdx = 0;
        ctx.txPacketQueue = &txQueue;

        RtpAudioPacketizerThread pkt(std::move(ctx));
        pkt.openForTest();

        AudioDesc desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        Frame     frame;
        frame.addPayload(PcmAudioPayload::Ptr::create(desc, size_t(0)));
        RtpFrameWork w;
        w.frame = frame;
        w.frameIndex = FrameNumber(0);
        pkt.packetizeForTest(w);
        CHECK(txQueue.size() == 0);
}
