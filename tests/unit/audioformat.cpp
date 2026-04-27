/**
 * @file      audioformat.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
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
// 24-bit-in-32 container formats (HB32 / LB32)
// ============================================================================

TEST_CASE("AudioFormat: PCMI_S24LE_HB32 metadata") {
        AudioFormat f(AudioFormat::PCMI_S24LE_HB32);
        CHECK(f.isValid());
        CHECK(f.bytesPerSample() == 4);
        CHECK(f.bitsPerSample() == 24);
        CHECK(f.isSigned());
        CHECK_FALSE(f.isFloat());
        CHECK_FALSE(f.isPlanar());
        CHECK_FALSE(f.isBigEndian());
}

TEST_CASE("AudioFormat: PCMI_U24BE_LB32 metadata") {
        AudioFormat f(AudioFormat::PCMI_U24BE_LB32);
        CHECK(f.isValid());
        CHECK(f.bytesPerSample() == 4);
        CHECK(f.bitsPerSample() == 24);
        CHECK_FALSE(f.isSigned());
        CHECK(f.isBigEndian());
}

TEST_CASE("AudioFormat: PCMP_S24BE_HB32 is planar") {
        AudioFormat f(AudioFormat::PCMP_S24BE_HB32);
        CHECK(f.isValid());
        CHECK(f.isPlanar());
        CHECK(f.isBigEndian());
        CHECK(f.isSigned());
        CHECK(f.bytesPerSample() == 4);
}

TEST_CASE("AudioFormat: HB32 LE byte layout — high 3 bytes carry data, low byte is zero") {
        AudioFormat f(AudioFormat::PCMI_S24LE_HB32);
        const float in[1] = {1.0f}; // -> max signed-24, i.e. 0x7FFFFF
        uint8_t     bytes[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        f.floatToSamples(bytes, in, 1);
        // 24-bit value 0x7FFFFF placed in high bytes -> word = 0x7FFFFF00 (LE)
        // Memory: byte0=0x00, byte1=0xFF, byte2=0xFF, byte3=0x7F
        CHECK(bytes[0] == 0x00);
        CHECK(bytes[1] == 0xFF);
        CHECK(bytes[2] == 0xFF);
        CHECK(bytes[3] == 0x7F);
}

TEST_CASE("AudioFormat: LB32 LE byte layout — low 3 bytes carry data, high byte is zero") {
        AudioFormat f(AudioFormat::PCMI_S24LE_LB32);
        const float in[1] = {1.0f}; // -> 0x7FFFFF
        uint8_t     bytes[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        f.floatToSamples(bytes, in, 1);
        // word = 0x007FFFFF (LE) -> byte0=0xFF, byte1=0xFF, byte2=0x7F, byte3=0x00
        CHECK(bytes[0] == 0xFF);
        CHECK(bytes[1] == 0xFF);
        CHECK(bytes[2] == 0x7F);
        CHECK(bytes[3] == 0x00);
}

TEST_CASE("AudioFormat: HB32 BE byte layout — high 3 bytes carry data, low byte is zero") {
        AudioFormat f(AudioFormat::PCMI_S24BE_HB32);
        const float in[1] = {1.0f};
        uint8_t     bytes[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        f.floatToSamples(bytes, in, 1);
        // word = 0x7FFFFF00 (BE) -> byte0=0x7F, byte1=0xFF, byte2=0xFF, byte3=0x00
        CHECK(bytes[0] == 0x7F);
        CHECK(bytes[1] == 0xFF);
        CHECK(bytes[2] == 0xFF);
        CHECK(bytes[3] == 0x00);
}

TEST_CASE("AudioFormat: LB32 BE byte layout — high byte is zero, low 3 bytes carry data") {
        AudioFormat f(AudioFormat::PCMI_S24BE_LB32);
        const float in[1] = {1.0f};
        uint8_t     bytes[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        f.floatToSamples(bytes, in, 1);
        // word = 0x007FFFFF (BE) -> byte0=0x00, byte1=0x7F, byte2=0xFF, byte3=0xFF
        CHECK(bytes[0] == 0x00);
        CHECK(bytes[1] == 0x7F);
        CHECK(bytes[2] == 0xFF);
        CHECK(bytes[3] == 0xFF);
}

TEST_CASE("AudioFormat: signed-24-in-32 negative full-scale round-trips through endpoints") {
        AudioFormat f(AudioFormat::PCMI_S24LE_HB32);
        const float in[2] = {-1.0f, 1.0f};
        uint8_t     bytes[8] = {};
        f.floatToSamples(bytes, in, 2);

        float out[2] = {};
        f.samplesToFloat(out, bytes, 2);
        // Endpoints land within one quantum of ±1.
        CHECK(out[0] == doctest::Approx(-1.0f).epsilon(2.0 / AudioFormat::MaxS24));
        CHECK(out[1] == doctest::Approx(1.0f).epsilon(2.0 / AudioFormat::MaxS24));
}

TEST_CASE("AudioFormat: 24-in-32 round-trip across all eight interleaved variants") {
        const AudioFormat::ID ids[] = {
                AudioFormat::PCMI_S24LE_HB32, AudioFormat::PCMI_S24LE_LB32, AudioFormat::PCMI_S24BE_HB32,
                AudioFormat::PCMI_S24BE_LB32, AudioFormat::PCMI_U24LE_HB32, AudioFormat::PCMI_U24LE_LB32,
                AudioFormat::PCMI_U24BE_HB32, AudioFormat::PCMI_U24BE_LB32,
        };
        const float in[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
        for (auto id : ids) {
                AudioFormat f(id);
                CAPTURE(f.name());
                uint8_t bytes[5 * 4] = {};
                f.floatToSamples(bytes, in, 5);
                float out[5] = {};
                f.samplesToFloat(out, bytes, 5);
                for (size_t i = 0; i < 5; ++i) {
                        CAPTURE(i);
                        CHECK(out[i] == doctest::Approx(in[i]).epsilon(2.0 / AudioFormat::MaxS24));
                }
        }
}

TEST_CASE("AudioFormat: HB32 unused byte is always zero after conversion") {
        // HB32 layouts must zero the low byte (LE: byte 0; BE: byte 3).
        AudioFormat le(AudioFormat::PCMI_S24LE_HB32);
        AudioFormat be(AudioFormat::PCMI_S24BE_HB32);
        const float in[3] = {-0.5f, 0.0f, 0.7f};
        uint8_t     leBytes[3 * 4] = {};
        uint8_t     beBytes[3 * 4] = {};
        // Pre-fill with garbage so we can confirm the converter overwrites cleanly.
        for (auto &b : leBytes) b = 0xCC;
        for (auto &b : beBytes) b = 0xCC;
        le.floatToSamples(leBytes, in, 3);
        be.floatToSamples(beBytes, in, 3);
        for (size_t i = 0; i < 3; ++i) {
                CHECK(leBytes[i * 4 + 0] == 0x00); // LE: low byte of word
                CHECK(beBytes[i * 4 + 3] == 0x00); // BE: low byte of word
        }
}

TEST_CASE("AudioFormat: LB32 unused byte is always zero after conversion") {
        AudioFormat le(AudioFormat::PCMI_S24LE_LB32);
        AudioFormat be(AudioFormat::PCMI_S24BE_LB32);
        const float in[3] = {-0.5f, 0.0f, 0.7f};
        uint8_t     leBytes[3 * 4] = {};
        uint8_t     beBytes[3 * 4] = {};
        for (auto &b : leBytes) b = 0xCC;
        for (auto &b : beBytes) b = 0xCC;
        le.floatToSamples(leBytes, in, 3);
        be.floatToSamples(beBytes, in, 3);
        for (size_t i = 0; i < 3; ++i) {
                CHECK(leBytes[i * 4 + 3] == 0x00); // LE: high byte of word
                CHECK(beBytes[i * 4 + 0] == 0x00); // BE: high byte of word
        }
}

TEST_CASE("AudioFormat: 24-in-32 names round-trip through registry") {
        const char *const names[] = {
                "PCMI_S24LE_HB32", "PCMI_S24LE_LB32", "PCMI_S24BE_HB32", "PCMI_S24BE_LB32",
                "PCMI_U24LE_HB32", "PCMI_U24LE_LB32", "PCMI_U24BE_HB32", "PCMI_U24BE_LB32",
                "PCMP_S24LE_HB32", "PCMP_S24LE_LB32", "PCMP_S24BE_HB32", "PCMP_S24BE_LB32",
                "PCMP_U24LE_HB32", "PCMP_U24LE_LB32", "PCMP_U24BE_HB32", "PCMP_U24BE_LB32",
        };
        for (const char *name : names) {
                CAPTURE(name);
                AudioFormat f = value(AudioFormat::lookup(name));
                CHECK(f.isValid());
                CHECK(f.name() == name);
        }
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

TEST_CASE("AudioFormat: Variant get<AudioFormat> from invalid string sets err") {
        Variant     v(String("NOT_A_REAL_FORMAT"));
        Error       err;
        AudioFormat back = v.get<AudioFormat>(&err);
        CHECK(err.isError());
        CHECK_FALSE(back.isValid());
}

// ============================================================================
// Direct (no-float) converter registry
// ============================================================================

TEST_CASE("AudioFormat: directConverter — same-format identity is registered and bit-accurate") {
        for (auto id : {AudioFormat::PCMI_S16LE, AudioFormat::PCMI_S24LE, AudioFormat::PCMI_S24LE_HB32,
                        AudioFormat::PCMI_S32LE, AudioFormat::PCMI_Float32LE}) {
                CAPTURE(id);
                CHECK(AudioFormat::directConverter(id, id) != nullptr);
                CHECK(AudioFormat::isBitAccurate(id, id));
        }
}

TEST_CASE("AudioFormat: directConverter — endian swap registered for PCMI_S16LE <-> PCMI_S16BE") {
        AudioFormat a(AudioFormat::PCMI_S16LE);
        AudioFormat b(AudioFormat::PCMI_S16BE);
        CHECK(a.hasDirectConverterTo(b));
        CHECK(b.hasDirectConverterTo(a));
        CHECK(a.isBitAccurateTo(b));
        CHECK(b.isBitAccurateTo(a));

        // Verify byte transformation: S16LE 0x1234 (little-endian: [0x34, 0x12]) -> S16BE [0x12, 0x34]
        const uint8_t in[2] = {0x34, 0x12};
        uint8_t       out[2] = {0, 0};
        auto          fn = AudioFormat::directConverter(a.id(), b.id());
        REQUIRE(fn != nullptr);
        fn(out, in, 1);
        CHECK(out[0] == 0x12);
        CHECK(out[1] == 0x34);
}

TEST_CASE("AudioFormat: directConverter — sign flip S16LE <-> U16LE") {
        AudioFormat s(AudioFormat::PCMI_S16LE);
        AudioFormat u(AudioFormat::PCMI_U16LE);
        CHECK(s.hasDirectConverterTo(u));
        CHECK(s.isBitAccurateTo(u));

        // S16LE -32768 (bytes [0x00, 0x80]) -> U16LE 0 (bytes [0x00, 0x00])
        const uint8_t in[2] = {0x00, 0x80};
        uint8_t       out[2] = {0xAA, 0xAA};
        auto          fn = AudioFormat::directConverter(s.id(), u.id());
        REQUIRE(fn != nullptr);
        fn(out, in, 1);
        CHECK(out[0] == 0x00);
        CHECK(out[1] == 0x00);
}

TEST_CASE("AudioFormat: directConverter — sign flip + endian swap S16LE -> U16BE") {
        AudioFormat src(AudioFormat::PCMI_S16LE);
        AudioFormat dst(AudioFormat::PCMI_U16BE);
        CHECK(src.hasDirectConverterTo(dst));
        CHECK(src.isBitAccurateTo(dst));

        // S16LE +1 (bytes [0x01, 0x00]) -> U16BE 0x8001 (bytes [0x80, 0x01])
        const uint8_t in[2] = {0x01, 0x00};
        uint8_t       out[2] = {0xAA, 0xAA};
        auto          fn = AudioFormat::directConverter(src.id(), dst.id());
        REQUIRE(fn != nullptr);
        fn(out, in, 1);
        CHECK(out[0] == 0x80);
        CHECK(out[1] == 0x01);
}

TEST_CASE("AudioFormat: directConverter — 24LE packed <-> 24LE_HB32 round-trip") {
        AudioFormat packed(AudioFormat::PCMI_S24LE);
        AudioFormat hb32(AudioFormat::PCMI_S24LE_HB32);
        CHECK(packed.hasDirectConverterTo(hb32));
        CHECK(hb32.hasDirectConverterTo(packed));
        CHECK(packed.isBitAccurateTo(hb32));
        CHECK(hb32.isBitAccurateTo(packed));

        // Packed 24-bit value 0x123456 stored as LE (bytes [0x56, 0x34, 0x12]).
        // HB32 places it in the high 3 bytes of an LE word, with byte 0 = 0:
        //   bytes [0x00, 0x56, 0x34, 0x12].
        const uint8_t in[3] = {0x56, 0x34, 0x12};
        uint8_t       out[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        auto          fn = AudioFormat::directConverter(packed.id(), hb32.id());
        REQUIRE(fn != nullptr);
        fn(out, in, 1);
        CHECK(out[0] == 0x00);
        CHECK(out[1] == 0x56);
        CHECK(out[2] == 0x34);
        CHECK(out[3] == 0x12);

        uint8_t back[3] = {0xAA, 0xAA, 0xAA};
        auto    rev = AudioFormat::directConverter(hb32.id(), packed.id());
        REQUIRE(rev != nullptr);
        rev(back, out, 1);
        CHECK(back[0] == 0x56);
        CHECK(back[1] == 0x34);
        CHECK(back[2] == 0x12);
}

TEST_CASE("AudioFormat: directConverter — cross-endian HB32 LE <-> BE is a 4-byte swap") {
        AudioFormat le(AudioFormat::PCMI_S24LE_HB32);
        AudioFormat be(AudioFormat::PCMI_S24BE_HB32);
        CHECK(le.hasDirectConverterTo(be));
        CHECK(le.isBitAccurateTo(be));
        // LE_HB32 bytes [0x00, 0x56, 0x34, 0x12] -> BE_HB32 [0x12, 0x34, 0x56, 0x00].
        const uint8_t in[4] = {0x00, 0x56, 0x34, 0x12};
        uint8_t       out[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        auto          fn = AudioFormat::directConverter(le.id(), be.id());
        REQUIRE(fn != nullptr);
        fn(out, in, 1);
        CHECK(out[0] == 0x12);
        CHECK(out[1] == 0x34);
        CHECK(out[2] == 0x56);
        CHECK(out[3] == 0x00);
}

TEST_CASE("AudioFormat: directConverter — cross-endian LB32 LE <-> BE is a 4-byte swap") {
        AudioFormat le(AudioFormat::PCMI_S24LE_LB32);
        AudioFormat be(AudioFormat::PCMI_S24BE_LB32);
        CHECK(le.hasDirectConverterTo(be));
        // LE_LB32 bytes [0x56, 0x34, 0x12, 0x00] -> BE_LB32 [0x00, 0x12, 0x34, 0x56].
        const uint8_t in[4] = {0x56, 0x34, 0x12, 0x00};
        uint8_t       out[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        auto          fn = AudioFormat::directConverter(le.id(), be.id());
        REQUIRE(fn != nullptr);
        fn(out, in, 1);
        CHECK(out[0] == 0x00);
        CHECK(out[1] == 0x12);
        CHECK(out[2] == 0x34);
        CHECK(out[3] == 0x56);
}

TEST_CASE("AudioFormat: directConverter — 24-in-32 HB32 <-> LB32 round-trip preserves data bits") {
        AudioFormat hb(AudioFormat::PCMI_S24LE_HB32);
        AudioFormat lb(AudioFormat::PCMI_S24LE_LB32);
        CHECK(hb.hasDirectConverterTo(lb));
        CHECK(hb.isBitAccurateTo(lb));

        // HB32 LE: value 0x123456 lives in bytes [0x00, 0x56, 0x34, 0x12].
        // LB32 LE: same value lives in bytes [0x56, 0x34, 0x12, 0x00].
        const uint8_t in[4] = {0x00, 0x56, 0x34, 0x12};
        uint8_t       out[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        auto          fn = AudioFormat::directConverter(hb.id(), lb.id());
        REQUIRE(fn != nullptr);
        fn(out, in, 1);
        CHECK(out[0] == 0x56);
        CHECK(out[1] == 0x34);
        CHECK(out[2] == 0x12);
        CHECK(out[3] == 0x00);
}

TEST_CASE("AudioFormat: directConverter — float32LE -> float32BE preserves bit pattern") {
        AudioFormat src(AudioFormat::PCMI_Float32LE);
        AudioFormat dst(AudioFormat::PCMI_Float32BE);
        CHECK(src.hasDirectConverterTo(dst));
        CHECK(src.isBitAccurateTo(dst));

        // 1.0f IEEE 754 = 0x3F800000.  LE: [0x00, 0x00, 0x80, 0x3F].
        // BE: [0x3F, 0x80, 0x00, 0x00].
        const uint8_t in[4] = {0x00, 0x00, 0x80, 0x3F};
        uint8_t       out[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        auto          fn = AudioFormat::directConverter(src.id(), dst.id());
        REQUIRE(fn != nullptr);
        fn(out, in, 1);
        CHECK(out[0] == 0x3F);
        CHECK(out[1] == 0x80);
        CHECK(out[2] == 0x00);
        CHECK(out[3] == 0x00);
}

TEST_CASE("AudioFormat: directConverter — no entry between unrelated formats") {
        // S16LE -> S24LE_HB32 isn't registered: differing widths, skip.
        AudioFormat a(AudioFormat::PCMI_S16LE);
        AudioFormat b(AudioFormat::PCMI_S24LE_HB32);
        CHECK(AudioFormat::directConverter(a.id(), b.id()) == nullptr);
        CHECK_FALSE(a.isBitAccurateTo(b));
}

TEST_CASE("AudioFormat: convertTo() uses direct path when available") {
        AudioFormat   src(AudioFormat::PCMI_S24LE);
        AudioFormat   dst(AudioFormat::PCMI_S24LE_HB32);
        const uint8_t in[3] = {0x56, 0x34, 0x12};
        uint8_t       out[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        Error         err = src.convertTo(dst, out, in, 1);
        CHECK(err.isOk());
        CHECK(out[0] == 0x00);
        CHECK(out[1] == 0x56);
        CHECK(out[2] == 0x34);
        CHECK(out[3] == 0x12);
}

TEST_CASE("AudioFormat: convertTo() falls back to via-float when no direct converter") {
        // S16LE -> S24LE_HB32 has no direct entry (different widths) — must go via float.
        AudioFormat   src(AudioFormat::PCMI_S16LE);
        AudioFormat   dst(AudioFormat::PCMI_S24LE_HB32);
        const int16_t inSamples[1] = {0x4000}; // half of full-scale signed-16
        uint8_t       out[4] = {0xAA, 0xAA, 0xAA, 0xAA};
        float         scratch[1] = {0.0f};
        Error         err = src.convertTo(dst, out, inSamples, 1, scratch);
        CHECK(err.isOk());
        // After via-float, the result should be approximately 0.5 → S24LE_HB32 of ~0x400000.
        // Read back through samplesToFloat to verify amplitude rather than exact bits.
        float verified[1] = {0.0f};
        dst.samplesToFloat(verified, out, 1);
        CHECK(verified[0] == doctest::Approx(0.5f).epsilon(0.001));
}

TEST_CASE("AudioFormat: convertTo() with no scratch and no direct path returns InvalidArgument") {
        AudioFormat src(AudioFormat::PCMI_S16LE);
        AudioFormat dst(AudioFormat::PCMI_S24LE_HB32);
        int16_t     inSamples[1] = {0};
        uint8_t     out[4] = {0};
        Error       err = src.convertTo(dst, out, inSamples, 1, nullptr);
        CHECK(err == Error::InvalidArgument);
}

TEST_CASE("AudioFormat: convertTo() refuses compressed sides via fallback") {
        AudioFormat pcm(AudioFormat::PCMI_S16LE);
        AudioFormat opus(AudioFormat::Opus);
        uint8_t     buf[16] = {};
        float       scratch[8] = {};
        Error       err = pcm.convertTo(opus, buf, buf, 1, scratch);
        CHECK(err == Error::NotSupported);
}

TEST_CASE("AudioFormat: registerDirectConverter installs a custom path") {
        AudioFormat::ID   userSrc = AudioFormat::registerType();
        AudioFormat::ID   userDst = AudioFormat::registerType();
        AudioFormat::Data dSrc;
        dSrc.id = userSrc;
        dSrc.name = "TestUserSrc";
        dSrc.bytesPerSample = 1;
        AudioFormat::registerData(std::move(dSrc));
        AudioFormat::Data dDst;
        dDst.id = userDst;
        dDst.name = "TestUserDst";
        dDst.bytesPerSample = 1;
        AudioFormat::registerData(std::move(dDst));

        bool called = false;
        AudioFormat::registerDirectConverter(
                userSrc, userDst,
                +[](void *dst, const void *src, size_t n) {
                        std::memcpy(dst, src, n);
                        static_cast<uint8_t *>(dst)[0] ^= 0xFF;
                },
                /*bitAccurate=*/false);

        uint8_t buf[1] = {0x55};
        uint8_t out[1] = {0};
        AudioFormat::directConverter(userSrc, userDst)(out, buf, 1);
        CHECK(out[0] == (0x55 ^ 0xFF));
        CHECK_FALSE(AudioFormat::isBitAccurate(userSrc, userDst));
        (void)called;
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
