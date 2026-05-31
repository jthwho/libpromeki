/**
 * @file      tests/smpte302m.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Round-trip tests for the SMPTE 302M packer in @ref Smpte302M.
 */

#include <doctest/doctest.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/mpegts.h>
#include <promeki/smpte302m.h>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace promeki;

namespace {

// Build a deterministic interleaved PCM frame of @p sampleCount samples
// across @p channels channels in the given @p format.  Each sample is
// chan_idx * 1000 + frame_idx, masked to fit in the format's bit width.
// Returns the bytes packed in the format's wire order.
std::vector<uint8_t> makePcm(AudioFormat::ID id, unsigned channels, size_t sampleCount) {
        AudioFormat fmt(id);
        const size_t bytesPerSample = fmt.bytesPerSample();
        std::vector<uint8_t> out(bytesPerSample * channels * sampleCount, 0);
        for (size_t f = 0; f < sampleCount; ++f) {
                for (unsigned c = 0; c < channels; ++c) {
                        uint32_t v = static_cast<uint32_t>(c * 1000u + f);
                        uint8_t *p = out.data() + (f * channels + c) * bytesPerSample;
                        switch (id) {
                                case AudioFormat::PCMI_S16LE: {
                                        uint16_t s = static_cast<uint16_t>(v & 0xFFFFu);
                                        p[0] = static_cast<uint8_t>(s & 0xFF);
                                        p[1] = static_cast<uint8_t>((s >> 8) & 0xFF);
                                        break;
                                }
                                case AudioFormat::PCMI_S16BE: {
                                        uint16_t s = static_cast<uint16_t>(v & 0xFFFFu);
                                        p[0] = static_cast<uint8_t>((s >> 8) & 0xFF);
                                        p[1] = static_cast<uint8_t>(s & 0xFF);
                                        break;
                                }
                                case AudioFormat::PCMI_S24LE: {
                                        uint32_t s = v & 0xFFFFFFu;
                                        p[0] = static_cast<uint8_t>(s & 0xFF);
                                        p[1] = static_cast<uint8_t>((s >> 8) & 0xFF);
                                        p[2] = static_cast<uint8_t>((s >> 16) & 0xFF);
                                        break;
                                }
                                case AudioFormat::PCMI_S24BE: {
                                        uint32_t s = v & 0xFFFFFFu;
                                        p[0] = static_cast<uint8_t>((s >> 16) & 0xFF);
                                        p[1] = static_cast<uint8_t>((s >> 8) & 0xFF);
                                        p[2] = static_cast<uint8_t>(s & 0xFF);
                                        break;
                                }
                                default: break;
                        }
                }
        }
        return out;
}

// Compare an unpacked 16/24-bit output sample against the value
// pattern makePcm produced.  For 16-bit input we mask v to 16 bits;
// for 24-bit we mask to 24.  The 302M parser always emits little-endian
// output (PCMI_S16LE / PCMI_S24LE).
uint32_t expectedSampleValue(unsigned channels, unsigned channelIdx, size_t frameIdx, unsigned bits) {
        (void)channels;
        const uint32_t v = static_cast<uint32_t>(channelIdx * 1000u + frameIdx);
        if (bits == 16) return v & 0xFFFFu;
        return v & 0xFFFFFFu;
}

uint32_t readOutputSample(const uint8_t *p, AudioFormat::ID id) {
        switch (id) {
                case AudioFormat::PCMI_S16LE:
                        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
                case AudioFormat::PCMI_S24LE:
                        return static_cast<uint32_t>(p[0]) |
                               (static_cast<uint32_t>(p[1]) << 8) |
                               (static_cast<uint32_t>(p[2]) << 16);
                default: return 0;
        }
}

} // namespace

TEST_CASE("Smpte302M::isFormatSupported / bitsPerSampleCode") {
        CHECK(Smpte302M::isFormatSupported(AudioFormat(AudioFormat::PCMI_S16LE)));
        CHECK(Smpte302M::isFormatSupported(AudioFormat(AudioFormat::PCMI_S16BE)));
        CHECK(Smpte302M::isFormatSupported(AudioFormat(AudioFormat::PCMI_S24LE)));
        CHECK(Smpte302M::isFormatSupported(AudioFormat(AudioFormat::PCMI_S24BE)));
        CHECK(Smpte302M::isFormatSupported(AudioFormat(AudioFormat::PCMI_S32LE)));

        CHECK_FALSE(Smpte302M::isFormatSupported(AudioFormat(AudioFormat::AAC)));
        CHECK_FALSE(Smpte302M::isFormatSupported(AudioFormat(AudioFormat::Opus)));
        CHECK_FALSE(Smpte302M::isFormatSupported(AudioFormat(AudioFormat::PCMI_Float32LE)));
        CHECK_FALSE(Smpte302M::isFormatSupported(AudioFormat(AudioFormat::PCMP_S16LE)));

        CHECK(Smpte302M::bitsPerSampleCode(AudioFormat(AudioFormat::PCMI_S16LE)) == Smpte302M::Bits16);
        CHECK(Smpte302M::bitsPerSampleCode(AudioFormat(AudioFormat::PCMI_S24LE)) == Smpte302M::Bits24);
        CHECK(Smpte302M::bitsPerSampleCode(AudioFormat(AudioFormat::PCMI_S32LE)) == Smpte302M::Bits24);
}

TEST_CASE("Smpte302M::bytesPerAes3Frame matches the spec") {
        // §5.9: 16-bit + VUCF = 20 bits per subframe, × 2 subframes
        // per channel pair, packed = 5 bytes per AES3 frame for stereo.
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits16, 2) == 5);
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits16, 4) == 10);
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits16, 6) == 15);
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits16, 8) == 20);
        // 20-bit: 24 bits per subframe → 6 bytes for stereo.
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits20, 2) == 6);
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits20, 8) == 24);
        // 24-bit: 28 bits per subframe → 7 bytes for stereo.
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits24, 2) == 7);
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits24, 8) == 28);
        // Invalid channel counts.
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits16, 0) == 0);
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits16, 1) == 0);
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits16, 10) == 0);
        CHECK(Smpte302M::bytesPerAes3Frame(Smpte302M::Bits16, 3) == 0);
}

TEST_CASE("Smpte302M::pack — header fields match spec §6.7") {
        AudioFormat       fmt(AudioFormat::PCMI_S16LE);
        AudioDesc         desc(fmt, Smpte302M::RequiredSampleRate, 2u);
        const size_t      samples = 48; // arbitrary; small to keep checks fast.
        const auto        pcm = makePcm(AudioFormat::PCMI_S16LE, 2, samples);
        uint32_t          phase = 0;
        Buffer            out;
        REQUIRE(Smpte302M::pack(pcm.data(), desc, samples, phase, /*firstChannelId=*/0, out).isOk());

        REQUIRE(out.size() >= Smpte302M::HeaderSize);
        const uint8_t *p = static_cast<const uint8_t *>(out.data());
        const uint16_t audioBytes = static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
        CHECK(audioBytes == 5u * samples);                                  // 5 bytes per AES3 frame for stereo 16-bit.
        CHECK(((p[2] >> 6) & 0x03) == 0);                                   // number_channels = 00 (2 ch).
        CHECK((((p[2] & 0x3F) << 2) | ((p[3] >> 6) & 0x03)) == 0);          // channel_identification = 0.
        CHECK(((p[3] >> 4) & 0x03) == Smpte302M::Bits16);                   // bits_per_sample = 00.
        CHECK((p[3] & 0x0F) == 0);                                          // alignment = 0.
        CHECK(out.size() == Smpte302M::HeaderSize + audioBytes);
}

TEST_CASE("Smpte302M::pack / parse round-trip") {
        struct Case {
                        AudioFormat::ID fmt;
                        unsigned        channels;
                        unsigned        bits; // logical word width in the parsed output.
        };

        const Case cases[] = {
                {AudioFormat::PCMI_S16LE, 2, 16},
                {AudioFormat::PCMI_S16LE, 4, 16},
                {AudioFormat::PCMI_S16LE, 6, 16},
                {AudioFormat::PCMI_S16LE, 8, 16},
                {AudioFormat::PCMI_S16BE, 2, 16},
                {AudioFormat::PCMI_S24LE, 2, 24},
                {AudioFormat::PCMI_S24LE, 4, 24},
                {AudioFormat::PCMI_S24LE, 8, 24},
                {AudioFormat::PCMI_S24BE, 2, 24},
        };

        for (const Case &c : cases) {
                CAPTURE(c.fmt);
                CAPTURE(c.channels);
                AudioFormat       fmt(c.fmt);
                AudioDesc         desc(fmt, Smpte302M::RequiredSampleRate, c.channels);
                const size_t      samples = 64;
                const auto        pcm = makePcm(c.fmt, c.channels, samples);
                uint32_t          phase = 0;
                Buffer            packed;
                REQUIRE(Smpte302M::pack(pcm.data(), desc, samples, phase,
                                        /*firstChannelId=*/0, packed)
                                .isOk());

                Buffer          decoded;
                AudioDesc       outDesc;
                size_t          outSamples = 0;
                uint8_t         firstCh = 0xFF;
                BufferView      pv(packed, 0, packed.size());
                REQUIRE(Smpte302M::parse(pv, decoded, outDesc, outSamples, &firstCh).isOk());
                CHECK(outSamples == samples);
                CHECK(outDesc.channels() == c.channels);
                CHECK(outDesc.sampleRate() == Smpte302M::RequiredSampleRate);
                CHECK(firstCh == 0);

                // Per-sample value comparison.
                const AudioFormat::ID outFid = outDesc.format().id();
                CHECK((outFid == AudioFormat::PCMI_S16LE || outFid == AudioFormat::PCMI_S24LE));
                const size_t   outStride = outDesc.format().bytesPerSample();
                const uint8_t *dp = static_cast<const uint8_t *>(decoded.data());
                for (size_t f = 0; f < samples; ++f) {
                        for (unsigned ch = 0; ch < c.channels; ++ch) {
                                const uint32_t got = readOutputSample(dp, outFid);
                                const uint32_t exp = expectedSampleValue(c.channels, ch, f, c.bits);
                                CHECK(got == exp);
                                dp += outStride;
                        }
                }
        }
}

TEST_CASE("Smpte302M::pack — F bit is set on first frame of each 192-frame block") {
        AudioFormat   fmt(AudioFormat::PCMI_S16LE);
        AudioDesc     desc(fmt, Smpte302M::RequiredSampleRate, 2u);
        // 1 frame: F=1 on frame 0 only.
        const auto    pcm = makePcm(AudioFormat::PCMI_S16LE, 2, 1);
        uint32_t      phase = 0;
        Buffer        packed;
        REQUIRE(Smpte302M::pack(pcm.data(), desc, 1, phase,
                                /*firstChannelId=*/0, packed)
                        .isOk());
        CHECK(phase == 1);

        // For 16-bit stereo at phase=0, F=1 on subframe A of the
        // first AES3 stream.  Layout (bits) within the 5-byte
        // payload after the 4-byte header:
        //   bits 0..15: A data (LSB-first)
        //   bit  16: V_a, 17: U_a, 18: C_a, 19: F_a
        //   bits 20..35: B data
        //   bits 36..39: V_b U_b C_b F_b
        // With sample value 0 (channel 0, frame 0) the data bits
        // are all zero, so the only non-zero bit on the entire
        // payload is bit 19 = F_a → byte 2 bit (7 - (19 - 16)) = bit 4.
        const uint8_t *p = static_cast<const uint8_t *>(packed.data()) + Smpte302M::HeaderSize;
        CHECK((p[2] & 0x10) != 0);

        // Pack one more frame; F bit should now be 0 because the
        // block phase is past 0.
        Buffer packed2;
        REQUIRE(Smpte302M::pack(pcm.data(), desc, 1, phase,
                                /*firstChannelId=*/0, packed2)
                        .isOk());
        const uint8_t *p2 = static_cast<const uint8_t *>(packed2.data()) + Smpte302M::HeaderSize;
        CHECK((p2[2] & 0x10) == 0);
}

TEST_CASE("Smpte302M::pack — registers errors on bad inputs") {
        AudioFormat fmt(AudioFormat::PCMI_S16LE);
        Buffer      out;
        uint32_t    phase = 0;

        SUBCASE("wrong sample rate") {
                AudioDesc desc(fmt, 44100.0f, 2u);
                CHECK(Smpte302M::pack(nullptr, desc, 0, phase, 0, out) == Error::InvalidArgument);
        }
        SUBCASE("odd channel count") {
                AudioDesc desc(fmt, Smpte302M::RequiredSampleRate, 3u);
                CHECK(Smpte302M::pack(nullptr, desc, 0, phase, 0, out) == Error::InvalidArgument);
        }
        SUBCASE("unsupported format (float)") {
                AudioDesc desc(AudioFormat(AudioFormat::PCMI_Float32LE), Smpte302M::RequiredSampleRate, 2u);
                const std::vector<uint8_t> pcm(2 * 4, 0);
                CHECK(Smpte302M::pack(pcm.data(), desc, 1, phase, 0, out) == Error::NotSupported);
        }
}

TEST_CASE("Smpte302M::parseHeader — small input is OutOfRange") {
        Buffer                     b(2);
        b.setSize(2);
        BufferView                 v(b, 0, b.size());
        Smpte302M::ParsedHeader    hdr;
        CHECK(Smpte302M::parseHeader(v, &hdr) == Error::OutOfRange);
}

TEST_CASE("Smpte302M::parseHeader — reserved bits_per_sample code is CorruptData") {
        // Header with bits_per_sample = 11 (reserved).
        Buffer b(Smpte302M::HeaderSize);
        b.setSize(Smpte302M::HeaderSize);
        uint8_t *p = static_cast<uint8_t *>(b.data());
        p[0] = 0;
        p[1] = 0;
        p[2] = 0;        // number_channels = 00, channel_id high = 0
        p[3] = 0x30;     // channel_id low = 0, bps = 11 (reserved)
        BufferView                 v(b, 0, b.size());
        Smpte302M::ParsedHeader    hdr;
        CHECK(Smpte302M::parseHeader(v, &hdr) == Error::CorruptData);
}

TEST_CASE("Smpte302M: V/U/C bits round-trip via parseWithVuc") {
        AudioFormat            fmt(AudioFormat::PCMI_S16LE);
        AudioDesc              desc(fmt, Smpte302M::RequiredSampleRate, 2u);
        const size_t           samples = 16;
        const auto             pcm = makePcm(AudioFormat::PCMI_S16LE, 2, samples);

        // Build a per-subframe VUC array: alternate V / U / C bits
        // across channels and frames so each subframe is distinct.
        std::vector<uint8_t> vuc(samples * 2, 0);
        for (size_t f = 0; f < samples; ++f) {
                for (unsigned c = 0; c < 2; ++c) {
                        uint8_t v = 0;
                        if ((f + c) & 1) v |= Smpte302M::VucValidity;
                        if ((f + c) & 2) v |= Smpte302M::VucUser;
                        if ((f + c) & 4) v |= Smpte302M::VucChannelStatus;
                        vuc[f * 2 + c] = v;
                }
        }

        uint32_t phase = 0;
        Buffer   packed;
        REQUIRE(Smpte302M::pack(pcm.data(), desc, samples, phase, /*firstChannelId=*/0,
                                packed, vuc.data())
                        .isOk());

        Buffer    decodedPcm;
        AudioDesc outDesc;
        size_t    outSamples = 0;
        Buffer    decodedVuc;
        REQUIRE(Smpte302M::parseWithVuc(BufferView(packed, 0, packed.size()), decodedPcm, outDesc,
                                        outSamples, &decodedVuc)
                        .isOk());
        REQUIRE(outSamples == samples);
        REQUIRE(decodedVuc.size() == samples * 2);
        const uint8_t *gp = static_cast<const uint8_t *>(decodedVuc.data());
        for (size_t i = 0; i < vuc.size(); ++i) {
                CAPTURE(i);
                CHECK(gp[i] == vuc[i]);
        }
}

TEST_CASE("Smpte302M: pack with null VUC source emits all-zero V/U/C") {
        AudioFormat       fmt(AudioFormat::PCMI_S16LE);
        AudioDesc         desc(fmt, Smpte302M::RequiredSampleRate, 2u);
        const size_t      samples = 8;
        const auto        pcm = makePcm(AudioFormat::PCMI_S16LE, 2, samples);
        uint32_t          phase = 0;
        Buffer            packed;
        REQUIRE(Smpte302M::pack(pcm.data(), desc, samples, phase, 0, packed, /*vuc=*/nullptr).isOk());

        Buffer    decodedPcm;
        AudioDesc outDesc;
        size_t    outSamples = 0;
        Buffer    decodedVuc;
        REQUIRE(Smpte302M::parseWithVuc(BufferView(packed, 0, packed.size()), decodedPcm, outDesc,
                                        outSamples, &decodedVuc)
                        .isOk());
        REQUIRE(decodedVuc.size() == samples * 2);
        const uint8_t *gp = static_cast<const uint8_t *>(decodedVuc.data());
        for (size_t i = 0; i < decodedVuc.size(); ++i) CHECK(gp[i] == 0);
}
