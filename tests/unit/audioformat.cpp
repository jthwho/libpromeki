/**
 * @file      audioformat.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/audioformat.h>
#include <promeki/audiocodec.h>
#include <promeki/variant.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>

using namespace promeki;

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("AudioFormat: default is Invalid") {
        AudioFormat f;
        CHECK_FALSE(f.isValid());
        CHECK(f.id() == AudioFormat::Invalid);
}

TEST_CASE("AudioFormat: PCMI_S16LE populates PCM fields") {
        AudioFormat f(AudioFormat::PCMI_S16LE);
        CHECK(f.isValid());
        CHECK(f.bytesPerSample() == 2);
        CHECK(f.bitsPerSample() == 16);
        CHECK(f.isSigned());
        CHECK_FALSE(f.isFloat());
        CHECK_FALSE(f.isPlanar());
        CHECK_FALSE(f.isBigEndian());
        CHECK_FALSE(f.isCompressed());
}

TEST_CASE("AudioFormat: PCMI_Float32LE is float + signed") {
        AudioFormat f(AudioFormat::PCMI_Float32LE);
        CHECK(f.isSigned());
        CHECK(f.isFloat());
        CHECK(f.bytesPerSample() == 4);
        CHECK(f.bitsPerSample() == 32);
}

TEST_CASE("AudioFormat: PCMP_* are planar") {
        AudioFormat f(AudioFormat::PCMP_S16LE);
        CHECK(f.isValid());
        CHECK(f.isPlanar());
        CHECK(f.bytesPerSample() == 2);
}

TEST_CASE("AudioFormat: NativeFloat is a valid interleaved float") {
        AudioFormat f(AudioFormat::NativeFloat);
        CHECK(f.isValid());
        CHECK(f.isFloat());
        CHECK_FALSE(f.isPlanar());
}

// ============================================================================
// Compressed formats
// ============================================================================

TEST_CASE("AudioFormat: Opus is compressed, references codec") {
        AudioFormat f(AudioFormat::Opus);
        CHECK(f.isValid());
        CHECK(f.isCompressed());
        CHECK(f.audioCodec().id() == AudioCodec::Opus);
}

TEST_CASE("AudioFormat: AAC / FLAC / MP3 / AC3 are compressed") {
        CHECK(AudioFormat(AudioFormat::AAC).isCompressed());
        CHECK(AudioFormat(AudioFormat::FLAC).isCompressed());
        CHECK(AudioFormat(AudioFormat::MP3).isCompressed());
        CHECK(AudioFormat(AudioFormat::AC3).isCompressed());
}

// ============================================================================
// Name / lookup round-trip
// ============================================================================

TEST_CASE("AudioFormat: lookup by name") {
        AudioFormat f = value(AudioFormat::lookup("PCMI_S24BE"));
        CHECK(f.isValid());
        CHECK(f.id() == AudioFormat::PCMI_S24BE);
}

TEST_CASE("AudioFormat: lookup unknown returns Invalid") {
        AudioFormat f = value(AudioFormat::lookup("Bogus"));
        CHECK_FALSE(f.isValid());
}

TEST_CASE("AudioFormat: every registered format's name round-trips") {
        auto ids = AudioFormat::registeredIDs();
        CHECK_FALSE(ids.isEmpty());
        for (auto id : ids) {
                AudioFormat f(id);
                CAPTURE(f.name());
                AudioFormat back = value(AudioFormat::lookup(f.name()));
                CHECK(back.id() == id);
        }
}

// ============================================================================
// FourCC lookup
// ============================================================================

TEST_CASE("AudioFormat: lookupByFourCC finds Opus") {
        AudioFormat f = AudioFormat::lookupByFourCC(FourCC("Opus"));
        CHECK(f.id() == AudioFormat::Opus);
}

TEST_CASE("AudioFormat: lookupByFourCC finds AAC via mp4a") {
        AudioFormat f = AudioFormat::lookupByFourCC(FourCC("mp4a"));
        CHECK(f.id() == AudioFormat::AAC);
}

TEST_CASE("AudioFormat: lookupByFourCC unknown returns Invalid") {
        AudioFormat f = AudioFormat::lookupByFourCC(FourCC("xxxx"));
        CHECK_FALSE(f.isValid());
}

// ============================================================================
// Sample conversion
// ============================================================================

TEST_CASE("AudioFormat: S16LE round-trip silence") {
        AudioFormat f(AudioFormat::PCMI_S16LE);
        float       in[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        uint8_t     bytes[4 * sizeof(int16_t)] = {};
        f.floatToSamples(bytes, in, 4);
        float out[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        f.samplesToFloat(out, bytes, 4);
        for (size_t i = 0; i < 4; ++i) {
                CHECK(out[i] == doctest::Approx(0.0f).epsilon(0.001));
        }
}

TEST_CASE("AudioFormat: integerToFloat maps endpoints correctly") {
        // int16 full-range endpoints
        CHECK(AudioFormat::integerToFloat<int16_t>(-32768) == doctest::Approx(-1.0f).epsilon(0.001));
        CHECK(AudioFormat::integerToFloat<int16_t>(32767) == doctest::Approx(1.0f).epsilon(0.001));
        CHECK(AudioFormat::integerToFloat<int16_t>(0) == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("AudioFormat: floatToInteger clamps out-of-range") {
        CHECK(AudioFormat::floatToInteger<int16_t>(2.0f) == 32767);
        CHECK(AudioFormat::floatToInteger<int16_t>(-2.0f) == -32768);
}

// ============================================================================
// Integration with DataStream (length-prefixed name)
// ============================================================================

TEST_CASE("AudioFormat: DataStream round-trip by name") {
        Buffer         buf(1024);
        BufferIODevice dev(&buf);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());
        {
                DataStream  ws = DataStream::createWriter(&dev);
                AudioFormat f(AudioFormat::PCMI_S24BE);
                ws << f;
        }
        dev.seek(0);
        DataStream  rs = DataStream::createReader(&dev);
        AudioFormat out;
        rs >> out;
        CHECK(out.id() == AudioFormat::PCMI_S24BE);
}

// ============================================================================
// Integration with Variant
// ============================================================================

TEST_CASE("AudioFormat: Variant holds an AudioFormat") {
        Variant v(AudioFormat(AudioFormat::PCMI_S16LE));
        CHECK(v.type() == Variant::TypeAudioFormat);
        AudioFormat back = v.get<AudioFormat>();
        CHECK(back.id() == AudioFormat::PCMI_S16LE);
}

TEST_CASE("AudioFormat: Variant<->String round-trip") {
        Variant v(AudioFormat(AudioFormat::PCMI_S16LE));
        String  s = v.get<String>();
        CHECK(s == "PCMI_S16LE");

        Variant     fromStr(s);
        AudioFormat back = fromStr.get<AudioFormat>();
        CHECK(back.id() == AudioFormat::PCMI_S16LE);
}

// ============================================================================
// User-registered formats
// ============================================================================

TEST_CASE("AudioFormat: registerType + registerData adds a new format") {
        AudioFormat::ID id = AudioFormat::registerType();
        CHECK(id >= AudioFormat::UserDefined);

        AudioFormat::Data d;
        d.id = id;
        d.name = "TestFmt_UserReg";
        d.desc = "User-registered test format";
        d.bytesPerSample = 2;
        d.bitsPerSample = 16;
        d.isSigned = true;
        AudioFormat::registerData(std::move(d));

        AudioFormat back = value(AudioFormat::lookup("TestFmt_UserReg"));
        CHECK(back.id() == id);
        CHECK(back.bytesPerSample() == 2);
        CHECK(back.bitsPerSample() == 16);

        // Confirm it shows up in registeredIDs().
        auto ids = AudioFormat::registeredIDs();
        bool found = false;
        for (auto otherId : ids)
                if (otherId == id) {
                        found = true;
                        break;
                }
        CHECK(found);
}
