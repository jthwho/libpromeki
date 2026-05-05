/**
 * @file      tests/audiodataencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include <string>
#include <promeki/audiodataencoder.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/crc.h>
#include <promeki/pcmaudiopayload.h>

using namespace promeki;

namespace {

        // Allocate a zero-filled PcmAudioPayload of the requested geometry.
        // Works for both interleaved and planar formats — the layout is
        // dictated by AudioDesc::bufferSize and channelBufferOffset.
        PcmAudioPayload::Ptr makePayload(const AudioDesc &desc, size_t samples) {
                const size_t bytes = desc.bufferSize(samples);
                Buffer  buf = Buffer(bytes);
                buf.setSize(bytes);
                std::memset(buf.data(), 0, bytes);
                BufferView view(buf, 0, bytes);
                return PcmAudioPayload::Ptr::create(desc, samples, view);
        }

        // Pull one sample of one channel out of a payload as a float — used by
        // both the bit-pattern decoder and the silence-fill check.  Mirrors
        // AudioDesc::channelBufferOffset / bytesPerSampleStride exactly so it
        // works for any layout.
        float readSample(const PcmAudioPayload &payload, uint32_t channel, size_t sampleIndex) {
                const AudioDesc   &desc = payload.desc();
                const AudioFormat &fmt = desc.format();
                const size_t       stride = desc.bytesPerSampleStride();
                const size_t       bufferSamples = payload.sampleCount();
                const uint8_t     *base = payload.data()[0].data();
                base += desc.channelBufferOffset(channel, bufferSamples);
                base += sampleIndex * stride;
                float out = 0.0f;
                fmt.samplesToFloat(&out, base, 1);
                return out;
        }

        // Decode a Manchester codeword by sampling each half-bit's centre
        // and comparing.  Used directly against the encoder output (no SRC
        // smoothing) — a simple sign-of-difference is enough.
        struct DecodedCodeword {
                        uint8_t  sync;
                        uint64_t payload;
                        uint8_t  crc;
        };

        DecodedCodeword decodeCodewordDirect(const PcmAudioPayload &payload, uint32_t channel, size_t firstSample,
                                             uint32_t samplesPerBit) {
                DecodedCodeword out{0, 0, 0};
                const size_t    H = samplesPerBit / 2;
                size_t          cursor = firstSample;
                for (uint32_t i = 0; i < AudioDataEncoder::BitsPerPacket; ++i) {
                        const float a = readSample(payload, channel, cursor + H / 2);
                        const float b = readSample(payload, channel, cursor + H + H / 2);
                        const bool  bit = a > b;
                        if (i < AudioDataEncoder::SyncBits) {
                                out.sync = static_cast<uint8_t>((out.sync << 1) | (bit ? 1u : 0u));
                        } else if (i < AudioDataEncoder::SyncBits + AudioDataEncoder::PayloadBits) {
                                out.payload = (out.payload << 1) | (bit ? 1u : 0u);
                        } else {
                                out.crc = static_cast<uint8_t>((out.crc << 1) | (bit ? 1u : 0u));
                        }
                        cursor += samplesPerBit;
                }
                return out;
        }

        // Recompute CRC the same way the encoder does so tests can build their
        // own "expected" codewords without leaning on a private helper.
        uint8_t crcOver(uint64_t payload) {
                uint8_t bytes[8];
                for (int b = 0; b < 8; b++) {
                        bytes[b] = static_cast<uint8_t>((payload >> ((7 - b) * 8)) & 0xffu);
                }
                Crc8 crc(CrcParams::Crc8Autosar);
                crc.update(bytes, 8);
                return crc.value();
        }

} // namespace

// ============================================================================
// Construction / geometry
// ============================================================================

TEST_CASE("AudioDataEncoder constructs with defaults for native float interleaved") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        CHECK(enc.samplesPerBit() == AudioDataEncoder::DefaultSamplesPerBit);
        CHECK(enc.amplitude() == doctest::Approx(AudioDataEncoder::DefaultAmplitude));
        CHECK(enc.packetSamples() == AudioDataEncoder::BitsPerPacket * AudioDataEncoder::DefaultSamplesPerBit);
}

TEST_CASE("AudioDataEncoder rejects out-of-range samplesPerBit") {
        AudioDesc desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        CHECK_FALSE(AudioDataEncoder(desc, 2).isValid()); // below min
        CHECK_FALSE(AudioDataEncoder(desc, 7).isValid()); // odd
        CHECK_FALSE(AudioDataEncoder(desc, 100).isValid()); // above max
}

TEST_CASE("AudioDataEncoder rejects non-positive amplitude") {
        AudioDesc desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        CHECK_FALSE(AudioDataEncoder(desc, 8, 0.0f).isValid());
        CHECK_FALSE(AudioDataEncoder(desc, 8, -0.1f).isValid());
        CHECK_FALSE(AudioDataEncoder(desc, 8, 2.0f).isValid());
}

TEST_CASE("AudioDataEncoder rejects compressed formats") {
        AudioDesc desc(AudioFormat(AudioFormat::Opus), 48000.0f, 2);
        CHECK_FALSE(AudioDataEncoder(desc).isValid());
}

// ============================================================================
// Wire format — bit pattern and CRC, native float
// ============================================================================

TEST_CASE("AudioDataEncoder produces correct sync nibble + CRC") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataEncoder enc(desc);
        REQUIRE(enc.isValid());

        const uint64_t payloadVal = 0x0123456789abcdefULL;
        auto           payload = makePayload(desc, 1024);
        REQUIRE(payload.isValid());

        AudioDataEncoder::Item item{};
        item.firstSample = 0;
        item.sampleCount = 1024;
        item.channel = 0;
        item.payload = payloadVal;
        REQUIRE(enc.encode(*payload.modify(), item).isOk());

        DecodedCodeword cw = decodeCodewordDirect(*payload, 0, 0, enc.samplesPerBit());
        CHECK(cw.sync == AudioDataEncoder::SyncNibble);
        CHECK(cw.payload == payloadVal);
        CHECK(cw.crc == crcOver(payloadVal));
}

TEST_CASE("AudioDataEncoder leaves the trailing pad as silence") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataEncoder enc(desc);
        REQUIRE(enc.isValid());

        const size_t samples = 1024;
        auto         payload = makePayload(desc, samples);
        REQUIRE(payload.isValid());

        AudioDataEncoder::Item item{};
        item.firstSample = 0;
        item.sampleCount = samples;
        item.channel = 1;
        item.payload = 0xdeadbeefcafebabeULL;
        REQUIRE(enc.encode(*payload.modify(), item).isOk());

        for (size_t s = enc.packetSamples(); s < samples; ++s) {
                CHECK(readSample(*payload, 1, s) == doctest::Approx(0.0f));
        }
}

TEST_CASE("AudioDataEncoder writes only the targeted channel") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 4);
        AudioDataEncoder enc(desc);
        REQUIRE(enc.isValid());

        const size_t samples = 1024;
        auto         payload = makePayload(desc, samples);
        REQUIRE(payload.isValid());

        AudioDataEncoder::Item item{};
        item.firstSample = 0;
        item.sampleCount = samples;
        item.channel = 2;
        item.payload = 0xa5a5a5a5a5a5a5a5ULL;
        REQUIRE(enc.encode(*payload.modify(), item).isOk());

        // Channels 0, 1, 3 should remain silent across the packet.
        for (uint32_t ch : {0u, 1u, 3u}) {
                for (size_t s = 0; s < enc.packetSamples(); ++s) {
                        CHECK(readSample(*payload, ch, s) == doctest::Approx(0.0f));
                }
        }
}

// ============================================================================
// Wire format — round-trip across PCM formats
// ============================================================================

TEST_CASE("AudioDataEncoder round-trips across PCM formats") {
        const AudioFormat::ID formats[] = {AudioFormat::PCMI_Float32LE, AudioFormat::PCMI_S16LE,
                                           AudioFormat::PCMI_S24LE,    AudioFormat::PCMI_S32LE,
                                           AudioFormat::PCMP_Float32LE, AudioFormat::PCMP_S16LE,
                                           AudioFormat::PCMP_S32LE};
        const uint64_t        kPayloads[] = {0x0000000000000000ULL, 0xffffffffffffffffULL, 0x0123456789abcdefULL,
                                             0xdeadbeefcafebabeULL};

        for (AudioFormat::ID fid : formats) {
                AudioFormat fmt(fid);
                if (!fmt.isValid()) continue;
                AudioDesc        desc(fmt, 48000.0f, 2);
                AudioDataEncoder enc(desc);
                REQUIRE_MESSAGE(enc.isValid(), fmt.name().cstr());

                auto payload = makePayload(desc, 1024);
                REQUIRE(payload.isValid());

                for (uint64_t p : kPayloads) {
                        INFO("format=" << std::string(fmt.name().cstr()) << " payload=0x" << std::hex << p);
                        AudioDataEncoder::Item item{0, 1024, 0, p};
                        REQUIRE(enc.encode(*payload.modify(), item).isOk());
                        DecodedCodeword cw = decodeCodewordDirect(*payload, 0, 0, enc.samplesPerBit());
                        CHECK(cw.sync == AudioDataEncoder::SyncNibble);
                        CHECK(cw.payload == p);
                        CHECK(cw.crc == crcOver(p));
                }
        }
}

// ============================================================================
// Item-validation paths
// ============================================================================

TEST_CASE("AudioDataEncoder rejects out-of-range channel") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        auto payload = makePayload(desc, 1024);

        AudioDataEncoder::Item item{0, 1024, 5, 0xabcd};
        CHECK(enc.encode(*payload.modify(), item) == Error::InvalidArgument);
}

TEST_CASE("AudioDataEncoder rejects sampleCount below codeword length") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        auto payload = makePayload(desc, 1024);

        AudioDataEncoder::Item item{0, enc.packetSamples() - 1, 0, 0xabcd};
        CHECK(enc.encode(*payload.modify(), item) == Error::OutOfRange);
}

TEST_CASE("AudioDataEncoder rejects items running past the buffer") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        auto payload = makePayload(desc, 1024);

        AudioDataEncoder::Item item{500, 800, 0, 0xabcd};
        CHECK(enc.encode(*payload.modify(), item) == Error::OutOfRange);
}

TEST_CASE("AudioDataEncoder rejects empty items as no-ops") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        auto payload = makePayload(desc, 1024);

        AudioDataEncoder::Item item{0, 0, 0, 0xabcd};
        CHECK(enc.encode(*payload.modify(), item).isOk());
        // Nothing got written.
        for (size_t s = 0; s < 1024; ++s) {
                CHECK(readSample(*payload, 0, s) == doctest::Approx(0.0f));
        }
}

// ============================================================================
// Static computeCrc matches per-encoder CRC
// ============================================================================

TEST_CASE("AudioDataEncoder::computeCrc matches the wire-format CRC") {
        const uint64_t payloads[] = {0, 1, 0xffffffffffffffffULL, 0x0123456789abcdefULL, 0xdeadbeefcafebabeULL};
        for (uint64_t p : payloads) {
                CHECK(AudioDataEncoder::computeCrc(p) == crcOver(p));
        }
}
