/**
 * @file      aacbitstream.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <promeki/aacbitstream.h>

using namespace promeki;

namespace {

Buffer makePayload(std::initializer_list<uint8_t> bytes) {
        Buffer b(bytes.size() == 0 ? 1 : bytes.size());
        if (bytes.size() > 0) {
                std::memcpy(b.data(), &*bytes.begin(), bytes.size());
                b.setSize(bytes.size());
        }
        return b;
}

bool buffersEqual(const Buffer &a, const Buffer &b) {
        if (a.size() != b.size()) return false;
        if (a.size() == 0) return true;
        return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

} // namespace

// ----------------------------------------------------------------------------
// Frequency table helpers
// ----------------------------------------------------------------------------

TEST_CASE("AacDecoderConfig: indexToFrequency standard rates") {
        CHECK(AacDecoderConfig::indexToFrequency(0) == 96000);
        CHECK(AacDecoderConfig::indexToFrequency(3) == 48000);
        CHECK(AacDecoderConfig::indexToFrequency(4) == 44100);
        CHECK(AacDecoderConfig::indexToFrequency(11) == 8000);
        CHECK(AacDecoderConfig::indexToFrequency(12) == 7350);
        CHECK(AacDecoderConfig::indexToFrequency(13) == 0);  // reserved
        CHECK(AacDecoderConfig::indexToFrequency(15) == 0);  // explicit-marker
}

TEST_CASE("AacDecoderConfig: frequencyToIndex round-trip") {
        for (uint8_t i = 0; i < 13; ++i) {
                uint32_t hz = AacDecoderConfig::indexToFrequency(i);
                CHECK(AacDecoderConfig::frequencyToIndex(hz) == i);
        }
        // Non-standard rate falls back to the explicit marker.
        CHECK(AacDecoderConfig::frequencyToIndex(50000) == 15);
}

// ----------------------------------------------------------------------------
// LC config — 2-byte form
// ----------------------------------------------------------------------------

TEST_CASE("AacDecoderConfig: LC 44.1k stereo serializes to 0x12 0x10") {
        AacDecoderConfig c;
        c.audioObjectType        = 2;
        c.samplingFrequencyIndex = 4;       // 44.1k
        c.samplingFrequency      = 44100;
        c.channelConfiguration   = 2;
        Buffer b;
        REQUIRE(c.serialize(b).isOk());
        REQUIRE(b.size() == 2);
        const uint8_t *p = static_cast<const uint8_t *>(b.data());
        CHECK(p[0] == 0x12);
        CHECK(p[1] == 0x10);
}

TEST_CASE("AacDecoderConfig: LC 48k stereo round-trip") {
        AacDecoderConfig c;
        c.audioObjectType        = 2;
        c.samplingFrequencyIndex = 3;       // 48k
        c.samplingFrequency      = 48000;
        c.channelConfiguration   = 2;
        Buffer b;
        REQUIRE(c.serialize(b).isOk());
        REQUIRE(b.size() == 2);
        const uint8_t *p = static_cast<const uint8_t *>(b.data());
        // (2<<3 | (3>>1)) = 0x11
        CHECK(p[0] == 0x11);
        // ((3&1)<<7) | (2<<3) = 0x90
        CHECK(p[1] == 0x90);

        AacDecoderConfig parsed;
        REQUIRE(AacDecoderConfig::parse(BufferView(b, 0, b.size()), parsed).isOk());
        CHECK(parsed.audioObjectType == 2);
        CHECK(parsed.samplingFrequencyIndex == 3);
        CHECK(parsed.samplingFrequency == 48000);
        CHECK(parsed.channelConfiguration == 2);
}

TEST_CASE("AacDecoderConfig: parse standard rate × channel matrix") {
        // Walk every (rate-index, channel-config) combination FLV would
        // realistically carry.  We synthesize a 2-byte config per pair,
        // parse it, and verify the structured fields recover.
        for (uint8_t sfi = 0; sfi <= 12; ++sfi) {
                for (uint8_t cc = 1; cc <= 7; ++cc) {
                        AacDecoderConfig in;
                        in.audioObjectType        = 2;
                        in.samplingFrequencyIndex = sfi;
                        in.samplingFrequency      = AacDecoderConfig::indexToFrequency(sfi);
                        in.channelConfiguration   = cc;

                        Buffer wire;
                        REQUIRE(in.serialize(wire).isOk());
                        AacDecoderConfig out;
                        REQUIRE(AacDecoderConfig::parse(BufferView(wire, 0, wire.size()), out).isOk());
                        CHECK(out.audioObjectType == 2);
                        CHECK(out.samplingFrequencyIndex == sfi);
                        CHECK(out.samplingFrequency == AacDecoderConfig::indexToFrequency(sfi));
                        CHECK(out.channelConfiguration == cc);
                }
        }
}

TEST_CASE("AacDecoderConfig: explicit 24-bit sample rate round-trip") {
        AacDecoderConfig c;
        c.audioObjectType        = 2;
        c.samplingFrequencyIndex = 15;
        c.samplingFrequency      = 96001;  // non-standard rate
        c.channelConfiguration   = 2;

        Buffer b;
        REQUIRE(c.serialize(b).isOk());
        AacDecoderConfig parsed;
        REQUIRE(AacDecoderConfig::parse(BufferView(b, 0, b.size()), parsed).isOk());
        CHECK(parsed.samplingFrequencyIndex == 15);
        CHECK(parsed.samplingFrequency == 96001);
}

TEST_CASE("AacDecoderConfig: rawConfig fast-path replays bytes verbatim") {
        AacDecoderConfig c;
        c.rawConfig = makePayload({0xAB, 0xCD, 0xEF});

        Buffer b;
        REQUIRE(c.serialize(b).isOk());
        REQUIRE(b.size() == 3);
        CHECK(static_cast<const uint8_t *>(b.data())[0] == 0xAB);
        CHECK(static_cast<const uint8_t *>(b.data())[1] == 0xCD);
        CHECK(static_cast<const uint8_t *>(b.data())[2] == 0xEF);
}

TEST_CASE("AacDecoderConfig: parse preserves rawConfig for round-trip") {
        AacDecoderConfig c;
        c.audioObjectType        = 2;
        c.samplingFrequencyIndex = 4;
        c.samplingFrequency      = 44100;
        c.channelConfiguration   = 2;

        Buffer wire;
        REQUIRE(c.serialize(wire).isOk());
        AacDecoderConfig parsed;
        REQUIRE(AacDecoderConfig::parse(BufferView(wire, 0, wire.size()), parsed).isOk());
        REQUIRE(buffersEqual(parsed.rawConfig, wire));

        // Re-serialize: should match original exactly.
        Buffer reemitted;
        REQUIRE(parsed.serialize(reemitted).isOk());
        CHECK(buffersEqual(reemitted, wire));
}

TEST_CASE("AacDecoderConfig: fromAudioDesc derives LC config") {
        AudioDesc        desc(AudioFormat(AudioFormat::AAC), 48000.0f, 2u);
        AacDecoderConfig c = AacDecoderConfig::fromAudioDesc(desc);
        CHECK(c.audioObjectType == 2);
        CHECK(c.samplingFrequencyIndex == 3);
        CHECK(c.samplingFrequency == 48000);
        CHECK(c.channelConfiguration == 2);
}

TEST_CASE("AacDecoderConfig: toAudioDesc maps back to AudioDesc") {
        AacDecoderConfig c;
        c.audioObjectType        = 2;
        c.samplingFrequency      = 48000;
        c.samplingFrequencyIndex = 3;
        c.channelConfiguration   = 2;
        AudioDesc d = c.toAudioDesc();
        CHECK(d.sampleRate() == 48000.0f);
        CHECK(d.channels() == 2);
}

TEST_CASE("AacDecoderConfig: toAudioDesc reports SBR doubled rate when present") {
        AacDecoderConfig c;
        c.audioObjectType                   = 2;
        c.samplingFrequency                 = 24000;
        c.samplingFrequencyIndex            = 6;
        c.channelConfiguration              = 2;
        c.sbr                               = true;
        c.extensionSamplingFrequency        = 48000;
        c.extensionSamplingFrequencyIndex   = 3;
        AudioDesc d = c.toAudioDesc();
        CHECK(d.sampleRate() == 48000.0f);
}

// ----------------------------------------------------------------------------
// HE-AAC v1 / v2
// ----------------------------------------------------------------------------

TEST_CASE("AacDecoderConfig: HE-AAC v1 explicit SBR parse") {
        // Construct one ourselves and verify parse recovers the fields.
        AacDecoderConfig c;
        c.audioObjectType                 = 2;       // underlying LC layer
        c.samplingFrequencyIndex          = 6;       // 24k SBR-domain core
        c.samplingFrequency               = 24000;
        c.channelConfiguration            = 2;
        c.sbr                             = true;
        c.extensionSamplingFrequencyIndex = 3;       // 48k decode rate
        c.extensionSamplingFrequency      = 48000;

        Buffer wire;
        REQUIRE(c.serialize(wire).isOk());

        AacDecoderConfig parsed;
        REQUIRE(AacDecoderConfig::parse(BufferView(wire, 0, wire.size()), parsed).isOk());
        CHECK(parsed.sbr);
        CHECK_FALSE(parsed.ps);
        CHECK(parsed.extensionSamplingFrequencyIndex == 3);
        CHECK(parsed.extensionSamplingFrequency == 48000);
        CHECK(parsed.audioObjectType == 2);
}

TEST_CASE("AacDecoderConfig: HE-AAC v2 explicit PS parse") {
        AacDecoderConfig c;
        c.audioObjectType                 = 2;
        c.samplingFrequencyIndex          = 6;
        c.samplingFrequency               = 24000;
        c.channelConfiguration            = 1;       // mono core, PS doubles to stereo
        c.ps                              = true;
        c.sbr                             = true;
        c.extensionSamplingFrequencyIndex = 3;
        c.extensionSamplingFrequency      = 48000;

        Buffer wire;
        REQUIRE(c.serialize(wire).isOk());
        AacDecoderConfig parsed;
        REQUIRE(AacDecoderConfig::parse(BufferView(wire, 0, wire.size()), parsed).isOk());
        CHECK(parsed.sbr);
        CHECK(parsed.ps);
}

// ----------------------------------------------------------------------------
// Error paths
// ----------------------------------------------------------------------------

TEST_CASE("AacDecoderConfig: empty input returns OutOfRange") {
        AacDecoderConfig out;
        CHECK(AacDecoderConfig::parse(BufferView(), out) == Error::OutOfRange);
}

TEST_CASE("AacDecoderConfig: 1-byte input returns OutOfRange") {
        Buffer b = makePayload({0x12});
        AacDecoderConfig out;
        CHECK(AacDecoderConfig::parse(BufferView(b, 0, b.size()), out) == Error::OutOfRange);
}

TEST_CASE("AacDecoderConfig: reserved sampling-frequency index returns CorruptData") {
        // AOT=2, SFI=13 (reserved), CC=2.  Bits: 00010 1101 0010 ...
        // byte0 = (2<<3 | 13>>1) = 0x16
        // byte1 = ((13&1)<<7 | 2<<3 | 0) = 0x90
        Buffer b = makePayload({0x16, 0x90});
        AacDecoderConfig out;
        CHECK(AacDecoderConfig::parse(BufferView(b, 0, b.size()), out) == Error::CorruptData);
}

TEST_CASE("AacDecoderConfig: extended AOT (escape value 31) returns NotSupported") {
        // AOT=31 escape: 11111 ...
        Buffer b = makePayload({0xF8, 0x00});
        AacDecoderConfig out;
        CHECK(AacDecoderConfig::parse(BufferView(b, 0, b.size()), out) == Error::NotSupported);
}

// ----------------------------------------------------------------------------
// AdtsParser
// ----------------------------------------------------------------------------

namespace {

// Build a minimal ADTS-framed AAC blob: 7-byte header (no CRC) + payload.
Buffer makeAdtsFrame(uint8_t profile, uint8_t sfi, uint8_t cc, std::initializer_list<uint8_t> payload) {
        size_t  frameLen = 7 + payload.size();
        Buffer  b(frameLen);
        uint8_t hdr[7]   = {0};
        hdr[0]           = 0xFF;
        hdr[1]           = 0xF1;  // syncword tail | ID=0 (MPEG-4) | layer=00 | protection_absent=1
        hdr[2]           = static_cast<uint8_t>((profile << 6) | (sfi << 2) | ((cc >> 2) & 0x01));
        hdr[3]           = static_cast<uint8_t>(((cc & 0x03) << 6) |
                               ((frameLen >> 11) & 0x03));
        hdr[4]           = static_cast<uint8_t>((frameLen >> 3) & 0xFF);
        hdr[5]           = static_cast<uint8_t>(((frameLen & 0x07) << 5) | 0x1F);
        hdr[6]           = 0xFC;  // buffer fullness 0x7FF | num_raw_data_blocks_in_frame = 0
        std::memcpy(b.data(), hdr, 7);
        if (payload.size() > 0) {
                std::memcpy(static_cast<uint8_t *>(b.data()) + 7, &*payload.begin(), payload.size());
        }
        b.setSize(frameLen);
        return b;
}

} // namespace

TEST_CASE("AdtsParser: detects ADTS via syncword") {
        Buffer adts = makeAdtsFrame(1, 4, 2, {0x01, 0x02});  // profile=1 (LC), 44.1k, stereo
        Buffer raw  = makePayload({0xAA, 0xBB});
        CHECK(AdtsParser::isAdts(BufferView(adts, 0, adts.size())));
        CHECK_FALSE(AdtsParser::isAdts(BufferView(raw, 0, raw.size())));
        CHECK_FALSE(AdtsParser::isAdts(BufferView()));
}

TEST_CASE("AdtsParser: strip recovers raw frame and config") {
        Buffer adts = makeAdtsFrame(1, 3, 2, {0x10, 0x20, 0x30, 0x40});  // profile=LC, 48k, stereo
        Buffer raw;
        AacDecoderConfig cfg;
        REQUIRE(AdtsParser::strip(BufferView(adts, 0, adts.size()), raw, cfg).isOk());
        REQUIRE(raw.size() == 4);
        const uint8_t *p = static_cast<const uint8_t *>(raw.data());
        CHECK(p[0] == 0x10);
        CHECK(p[3] == 0x40);
        CHECK(cfg.audioObjectType == 2);
        CHECK(cfg.samplingFrequencyIndex == 3);
        CHECK(cfg.samplingFrequency == 48000);
        CHECK(cfg.channelConfiguration == 2);
        CHECK(cfg.rawConfig.size() == 2);  // freshly serialized 2-byte AudioSpecificConfig
}

TEST_CASE("AdtsParser: strip concatenates multiple frames") {
        Buffer f1 = makeAdtsFrame(1, 4, 2, {0xAA});
        Buffer f2 = makeAdtsFrame(1, 4, 2, {0xBB, 0xCC});

        // Build a multi-frame stream by concatenating.
        Buffer combined(f1.size() + f2.size());
        std::memcpy(combined.data(), f1.data(), f1.size());
        std::memcpy(static_cast<uint8_t *>(combined.data()) + f1.size(), f2.data(), f2.size());
        combined.setSize(f1.size() + f2.size());

        Buffer raw;
        AacDecoderConfig cfg;
        REQUIRE(AdtsParser::strip(BufferView(combined, 0, combined.size()), raw, cfg).isOk());
        REQUIRE(raw.size() == 3);
        const uint8_t *p = static_cast<const uint8_t *>(raw.data());
        CHECK(p[0] == 0xAA);
        CHECK(p[1] == 0xBB);
        CHECK(p[2] == 0xCC);
}

TEST_CASE("AdtsParser: passes raw input through unchanged") {
        Buffer raw = makePayload({0x01, 0x02, 0x03});
        Buffer out;
        AacDecoderConfig cfg;
        REQUIRE(AdtsParser::strip(BufferView(raw, 0, raw.size()), out, cfg).isOk());
        REQUIRE(out.size() == 3);
        const uint8_t *p = static_cast<const uint8_t *>(out.data());
        CHECK(p[0] == 0x01);
        CHECK(p[2] == 0x03);
}

TEST_CASE("AdtsParser: invalid layer field is CorruptData") {
        Buffer  bad = makeAdtsFrame(1, 4, 2, {0xAA});
        // Twist bit so layer != 00.
        static_cast<uint8_t *>(bad.data())[1] |= 0x04;
        Buffer raw;
        AacDecoderConfig cfg;
        CHECK(AdtsParser::strip(BufferView(bad, 0, bad.size()), raw, cfg) == Error::CorruptData);
}

TEST_CASE("AdtsParser: wrapFrame builds a valid 7-byte header") {
        AacDecoderConfig cfg = AacDecoderConfig::fromAudioDesc(AudioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2u));
        Buffer rawIn  = makePayload({0x10, 0x20, 0x30, 0x40});
        Buffer framed;
        REQUIRE(AdtsParser::wrapFrame(cfg, BufferView(rawIn, 0, rawIn.size()), framed).isOk());

        REQUIRE(framed.size() == 7 + 4);
        const uint8_t *p = static_cast<const uint8_t *>(framed.data());
        // Syncword 0xFFF and MPEG-4 ID + layer=00 + protection_absent=1
        CHECK(p[0] == 0xFF);
        CHECK((p[1] & 0xF0) == 0xF0);
        CHECK((p[1] & 0x06) == 0x00);
        CHECK((p[1] & 0x01) == 0x01);

        // aac_frame_length spans p[3].low2 | p[4] | p[5].high3 = 11 bytes.
        const uint32_t frameLen = (static_cast<uint32_t>(p[3] & 0x03) << 11) |
                                  (static_cast<uint32_t>(p[4]) << 3) |
                                  (static_cast<uint32_t>(p[5] >> 5) & 0x07);
        CHECK(frameLen == 11);

        // Payload bytes follow the header verbatim.
        CHECK(p[7] == 0x10);
        CHECK(p[10] == 0x40);

        // isAdts agrees on the wrapped output.
        CHECK(AdtsParser::isAdts(BufferView(framed, 0, framed.size())));
}

TEST_CASE("AdtsParser: wrapFrame → strip round-trip recovers raw + config") {
        // 48 kHz stereo AAC-LC config → ADTS profile=1, sfi=3, cc=2.
        AacDecoderConfig srcCfg = AacDecoderConfig::fromAudioDesc(AudioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2u));
        Buffer rawIn = makePayload({0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03});

        Buffer framed;
        REQUIRE(AdtsParser::wrapFrame(srcCfg, BufferView(rawIn, 0, rawIn.size()), framed).isOk());

        Buffer           rawOut;
        AacDecoderConfig outCfg;
        REQUIRE(AdtsParser::strip(BufferView(framed, 0, framed.size()), rawOut, outCfg).isOk());

        REQUIRE(rawOut.size() == rawIn.size());
        const uint8_t *a = static_cast<const uint8_t *>(rawIn.data());
        const uint8_t *b = static_cast<const uint8_t *>(rawOut.data());
        for (size_t i = 0; i < rawIn.size(); ++i) {
                CHECK(a[i] == b[i]);
        }
        CHECK(outCfg.audioObjectType == srcCfg.audioObjectType);
        CHECK(outCfg.samplingFrequencyIndex == srcCfg.samplingFrequencyIndex);
        CHECK(outCfg.samplingFrequency == srcCfg.samplingFrequency);
        CHECK(outCfg.channelConfiguration == srcCfg.channelConfiguration);
}

TEST_CASE("AdtsParser: wrapFrame round-trip across rate × channel matrix") {
        // Walk the standard SFI table; pair each rate with mono / stereo / 5.1.
        const float    rates[]    = {44100.0f, 48000.0f, 32000.0f, 24000.0f, 22050.0f, 16000.0f, 8000.0f};
        const unsigned chans[]    = {1u, 2u, 6u};
        Buffer         rawIn      = makePayload({0xA5, 0x5A, 0xC3, 0x3C});

        for (float r : rates) {
                for (unsigned c : chans) {
                        AacDecoderConfig cfg = AacDecoderConfig::fromAudioDesc(
                                AudioDesc(AudioFormat(AudioFormat::AAC), r, c));

                        Buffer framed;
                        REQUIRE(AdtsParser::wrapFrame(cfg, BufferView(rawIn, 0, rawIn.size()), framed).isOk());

                        Buffer           rawOut;
                        AacDecoderConfig outCfg;
                        REQUIRE(AdtsParser::strip(BufferView(framed, 0, framed.size()), rawOut, outCfg).isOk());

                        REQUIRE(rawOut.size() == rawIn.size());
                        CHECK(outCfg.samplingFrequency == static_cast<uint32_t>(r));
                        CHECK(outCfg.channelConfiguration == static_cast<uint8_t>(c));
                }
        }
}

TEST_CASE("AdtsParser: wrapFrame rejects oversize frame") {
        AacDecoderConfig cfg = AacDecoderConfig::fromAudioDesc(AudioDesc(AudioFormat(AudioFormat::AAC), 48000.0f, 2u));

        // 13-bit aac_frame_length tops out at 0x1FFF = 8191 bytes total
        // (header + payload).  A payload of 8185 already overflows.
        Buffer big(8200);
        big.setSize(8185);
        std::memset(big.data(), 0xAB, 8185);

        Buffer framed;
        CHECK(AdtsParser::wrapFrame(cfg, BufferView(big, 0, big.size()), framed) == Error::OutOfRange);
}

TEST_CASE("AdtsParser: truncated frame returns OutOfRange") {
        Buffer adts = makeAdtsFrame(1, 4, 2, {0xAA, 0xBB, 0xCC, 0xDD});
        // Trim two bytes off — frame_length still claims the original size.
        BufferView truncated(adts, 0, adts.size() - 2);
        Buffer     raw;
        AacDecoderConfig cfg;
        CHECK(AdtsParser::strip(truncated, raw, cfg) == Error::OutOfRange);
}
