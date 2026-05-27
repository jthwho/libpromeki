/**
 * @file      pcmsilencefiller.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/pcmsilencefiller.h>

using namespace promeki;

namespace {

// Returns true when every byte of @p buf is exactly @p val.
bool allBytesEqual(const Buffer &buf, uint8_t val) {
        const uint8_t *p = static_cast<const uint8_t *>(buf.data());
        for (size_t i = 0; i < buf.size(); ++i) {
                if (p[i] != val) return false;
        }
        return true;
}

} // namespace

TEST_CASE("PcmSilenceFiller: default-constructed is empty") {
        PcmSilenceFiller s;
        CHECK(s.size() == 0u);
        CHECK_FALSE(s.desc().isValid());
        CHECK(s.samplesPerPacket() == 0u);
        CHECK(s.payload().size() == 0u);
}

TEST_CASE("PcmSilenceFiller: L16 (PCMI_S16BE) silence is all-zero bytes") {
        AudioDesc        desc(AudioFormat(AudioFormat::PCMI_S16BE), 48000.0f, 2);
        const size_t     samples = 48;  // 1 ms at 48 kHz
        PcmSilenceFiller s(desc, samples);
        CHECK(s.size() == 48u * 2u * 2u);  // samples * channels * bytes-per-sample
        CHECK(allBytesEqual(s.payload(), 0));
}

TEST_CASE("PcmSilenceFiller: L24 (PCMI_S24BE) silence is all-zero bytes") {
        AudioDesc        desc(AudioFormat(AudioFormat::PCMI_S24BE), 48000.0f, 2);
        const size_t     samples = 48;
        PcmSilenceFiller s(desc, samples);
        CHECK(s.size() == 48u * 2u * 3u);
        CHECK(allBytesEqual(s.payload(), 0));
}

TEST_CASE("PcmSilenceFiller: float silence is all-zero bytes (IEEE 754 +0.0)") {
        AudioDesc        desc(AudioFormat(AudioFormat::PCMI_Float32LE), 48000.0f, 2);
        const size_t     samples = 16;
        PcmSilenceFiller s(desc, samples);
        CHECK(s.size() == 16u * 2u * 4u);
        CHECK(allBytesEqual(s.payload(), 0));
}

TEST_CASE("PcmSilenceFiller: unsigned 8-bit silence is 0x80 midpoint") {
        AudioDesc        desc(AudioFormat(AudioFormat::PCMI_U8), 48000.0f, 2);
        const size_t     samples = 4;
        PcmSilenceFiller s(desc, samples);
        CHECK(s.size() == 4u * 2u);
        // 8 unsigned bytes, every one == 0x80.
        CHECK(allBytesEqual(s.payload(), 0x80));
}

TEST_CASE("PcmSilenceFiller: unsigned 16-bit big-endian silence pattern") {
        // PCMI_U16BE silence is 0x8000 per sample → bytes 0x80 0x00.
        AudioDesc        desc(AudioFormat(AudioFormat::PCMI_U16BE), 48000.0f, 2);
        const size_t     samples = 4;
        PcmSilenceFiller s(desc, samples);
        CHECK(s.size() == 4u * 2u * 2u);
        const uint8_t *p = static_cast<const uint8_t *>(s.payload().data());
        for (size_t i = 0; i < s.size(); i += 2) {
                CHECK(p[i] == 0x80);
                CHECK(p[i + 1] == 0x00);
        }
}

TEST_CASE("PcmSilenceFiller: unsigned 16-bit little-endian silence pattern") {
        // PCMI_U16LE silence is 0x8000 per sample → bytes 0x00 0x80
        // (little-endian).
        AudioDesc        desc(AudioFormat(AudioFormat::PCMI_U16LE), 48000.0f, 2);
        const size_t     samples = 4;
        PcmSilenceFiller s(desc, samples);
        CHECK(s.size() == 4u * 2u * 2u);
        const uint8_t *p = static_cast<const uint8_t *>(s.payload().data());
        for (size_t i = 0; i < s.size(); i += 2) {
                CHECK(p[i] == 0x00);
                CHECK(p[i + 1] == 0x80);
        }
}

TEST_CASE("PcmSilenceFiller: zero samples produces empty payload") {
        AudioDesc        desc(AudioFormat(AudioFormat::PCMI_S16BE), 48000.0f, 2);
        PcmSilenceFiller s(desc, 0);
        CHECK(s.size() == 0u);
        CHECK(s.samplesPerPacket() == 0u);
}

TEST_CASE("PcmSilenceFiller: invalid descriptor leaves filler empty") {
        PcmSilenceFiller s;
        Error            err = s.reset(AudioDesc(), 100);
        CHECK(err == Error::InvalidArgument);
        CHECK(s.size() == 0u);
}

TEST_CASE("PcmSilenceFiller: reset re-initialises in place") {
        AudioDesc        desc1(AudioFormat(AudioFormat::PCMI_S16BE), 48000.0f, 2);
        AudioDesc        desc2(AudioFormat(AudioFormat::PCMI_S24BE), 48000.0f, 1);
        PcmSilenceFiller s(desc1, 48);
        CHECK(s.size() == 48u * 2u * 2u);
        REQUIRE(s.reset(desc2, 96).isOk());
        CHECK(s.size() == 96u * 1u * 3u);
        CHECK(s.samplesPerPacket() == 96u);
        CHECK(s.desc().channels() == 1u);
}

TEST_CASE("PcmSilenceFiller: copies share storage (CoW Buffer)") {
        AudioDesc        desc(AudioFormat(AudioFormat::PCMI_S16BE), 48000.0f, 2);
        PcmSilenceFiller s(desc, 48);
        Buffer           a = s.payload();
        Buffer           b = s.payload();
        // Both buffers reference the same underlying storage —
        // the silence buffer is built once and handed out by
        // reference.  Multiple emitters can share without copying.
        CHECK(a.data() == b.data());
}
