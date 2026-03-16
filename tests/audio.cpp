/**
 * @file      audio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <doctest/doctest.h>
#include <promeki/proav/audio.h>
#include <promeki/proav/audiofile.h>
#include <promeki/proav/audiogen.h>
#include <promeki/core/rational.h>
#include <promeki/core/uuid.h>
#include <promeki/core/logger.h>
#include <promeki/core/file.h>
#include <promeki/core/buffer.h>
#include <promeki/core/bufferiodevice.h>
#include <promeki/core/fileiodevice.h>

using namespace promeki;

PROMEKI_DEBUG(AudioTest);

TEST_CASE("Audio") {
        Error err;
        AudioDesc desc(48000, 2);
        desc.metadata().set(Metadata::Description, "Testing Audio File");
        desc.metadata().set(Metadata::Originator, "libpromeki unit testing");
        desc.metadata().set(Metadata::CodingHistory, "Some sort of coding history");
        desc.metadata().set(Metadata::OriginatorReference, UUID::generate());
        desc.metadata().set(Metadata::Title, "This is the title");
        desc.metadata().set(Metadata::EnableBWF, true);
        desc.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF30, 10, 30, 00, 00));
        desc.metadata().set(Metadata::FrameRate, Rational(30000, 1001));

        CHECK(desc.isValid());
        CHECK(desc.isNative());
        Logger::defaultLogger().log(Logger::Info, __FILE__, __LINE__, desc.metadata().dump());
        AudioGen gen(desc);

        AudioFile file = AudioFile::createWriter("test.wav");
        REQUIRE(file.isValid());
        file.setDesc(desc);
        err = file.open();
        REQUIRE(err.isOk());
        for(int i = 0; i < 10; i++) {
                if(i % 2) {
                        gen.setConfig(0, { AudioGen::Sine, 1000.0, 0.3, 0.0, 0.5 });
                        gen.setConfig(1, { AudioGen::Silence, 1000.0, 0.5, 0.0, 0.5 });
                } else {
                        gen.setConfig(0, { AudioGen::Silence, 0.0, 0.3, 0.0, 0.5 });
                        gen.setConfig(1, { AudioGen::Sine, 750.0, 0.3, 0.0, 0.5 });
                }
                Audio audio = gen.generate(4800);
                CHECK(audio.isValid());
                err = file.write(audio);
                CHECK(err.isOk());
        }

        file.close();
        std::remove("test.wav");
}

TEST_CASE("Audio: Default construction is invalid") {
        Audio audio;
        CHECK_FALSE(audio.isValid());
}

TEST_CASE("Audio: Construction with AudioDesc and samples") {
        AudioDesc desc(48000, 2);
        Audio audio(desc, 1024);
        CHECK(audio.isValid());
        CHECK(audio.samples() == 1024);
        CHECK(audio.maxSamples() == 1024);
}

TEST_CASE("Audio: isValid on valid and invalid objects") {
        Audio invalid;
        CHECK_FALSE(invalid.isValid());

        AudioDesc desc(48000, 2);
        Audio valid(desc, 512);
        CHECK(valid.isValid());
}

TEST_CASE("Audio: isNative") {
        AudioDesc desc(48000, 2);
        Audio audio(desc, 256);
        CHECK(audio.isNative());

        AudioDesc nonNative(AudioDesc::PCMI_S16LE, 48000, 2);
        Audio audio2(nonNative, 256);
        CHECK_FALSE(audio2.isNative());
}

TEST_CASE("Audio: desc accessor") {
        AudioDesc desc(44100, 4);
        Audio audio(desc, 128);
        CHECK(audio.desc().sampleRate() == 44100);
        CHECK(audio.desc().channels() == 4);
}

TEST_CASE("Audio: samples and maxSamples") {
        AudioDesc desc(48000, 2);
        Audio audio(desc, 2048);
        CHECK(audio.samples() == 2048);
        CHECK(audio.maxSamples() == 2048);
}

TEST_CASE("Audio: frames equals samples times channels") {
        AudioDesc desc(48000, 2);
        Audio audio(desc, 1000);
        CHECK(audio.frames() == 2000);

        AudioDesc desc6(48000, 6);
        Audio audio6(desc6, 500);
        CHECK(audio6.frames() == 3000);
}

TEST_CASE("Audio: buffer accessor") {
        AudioDesc desc(48000, 2);
        Audio audio(desc, 512);
        const Buffer::Ptr &buf = audio.buffer();
        CHECK(buf->isValid());
        CHECK(buf->size() > 0);
}

TEST_CASE("Audio: zero method") {
        AudioDesc desc(48000, 1);
        Audio audio(desc, 64);
        audio.zero();
        float *p = audio.data<float>();
        bool allZero = true;
        for(size_t i = 0; i < 64; i++) {
                if(p[i] != 0.0f) { allZero = false; break; }
        }
        CHECK(allZero);
}

TEST_CASE("Audio: resize within range and out of range") {
        AudioDesc desc(48000, 2);
        Audio audio(desc, 1024);
        CHECK(audio.maxSamples() == 1024);

        // Resize smaller should succeed
        CHECK(audio.resize(512));
        CHECK(audio.samples() == 512);

        // Resize back to max should succeed
        CHECK(audio.resize(1024));
        CHECK(audio.samples() == 1024);

        // Resize beyond max should fail
        CHECK_FALSE(audio.resize(2048));
        CHECK(audio.samples() == 1024);

        // Resize to zero should succeed
        CHECK(audio.resize(0));
        CHECK(audio.samples() == 0);
}

TEST_CASE("Audio: data template accessor") {
        AudioDesc desc(48000, 1);
        Audio audio(desc, 16);
        float *p = audio.data<float>();
        CHECK(p != nullptr);

        // Write and read back through the typed pointer
        audio.zero();
        p[0] = 0.5f;
        p[1] = -0.25f;
        CHECK(audio.data<float>()[0] == 0.5f);
        CHECK(audio.data<float>()[1] == -0.25f);
}

TEST_CASE("Audio: copy semantics") {
        AudioDesc desc(48000, 2);
        Audio audio(desc, 256);

        Audio copy = audio;
        CHECK(copy.isValid());
        CHECK(copy.samples() == audio.samples());
}

TEST_CASE("AudioFile: Default construction is invalid") {
        AudioFile file;
        CHECK_FALSE(file.isValid());
        CHECK(file.operation() == AudioFile::InvalidOperation);
}

TEST_CASE("AudioFile: createWriter creates valid writer") {
        AudioFile file = AudioFile::createWriter("writer_test.wav");
        // createWriter uses factory lookup - if factory is registered, it should be valid
        if(file.isValid()) {
                CHECK(file.operation() == AudioFile::Writer);
                CHECK(file.filename() == "writer_test.wav");
        }
}

TEST_CASE("AudioFile: createReader creates valid reader") {
        AudioFile file = AudioFile::createReader("reader_test.wav");
        // createReader uses factory lookup - may return invalid if no factory found
        if(file.isValid()) {
                CHECK(file.operation() == AudioFile::Reader);
                CHECK(file.filename() == "reader_test.wav");
        }
}

TEST_CASE("AudioFile: setFilename on writer") {
        AudioFile file = AudioFile::createWriter("original.wav");
        if(file.isValid()) {
                CHECK(file.filename() == "original.wav");
                file.setFilename("renamed.wav");
                CHECK(file.filename() == "renamed.wav");
        }
}

TEST_CASE("AudioFile: desc and setDesc accessors on writer") {
        AudioFile file = AudioFile::createWriter("desc_test.wav");
        if(file.isValid()) {
                // Default desc should be invalid
                CHECK_FALSE(file.desc().isValid());

                // Set a valid desc and verify it comes back
                AudioDesc desc(48000, 2);
                file.setDesc(desc);
                AudioDesc retrieved = file.desc();
                CHECK(retrieved.isValid());
                CHECK(retrieved.sampleRate() == 48000);
                CHECK(retrieved.channels() == 2);
        }
}

TEST_CASE("AudioGen: Construction with valid AudioDesc") {
        AudioDesc desc(48000, 2);
        REQUIRE(desc.isValid());
        AudioGen gen(desc);
        // Should be constructable without error; verify config is accessible
        CHECK(gen.config(0).type == AudioGen::Silence);
        CHECK(gen.config(1).type == AudioGen::Silence);
}

TEST_CASE("AudioGen: config() returns default config") {
        AudioDesc desc(48000, 2);
        AudioGen gen(desc);
        const AudioGen::Config &cfg0 = gen.config(0);
        const AudioGen::Config &cfg1 = gen.config(1);
        // Default type should be Silence
        CHECK(cfg0.type == AudioGen::Silence);
        CHECK(cfg1.type == AudioGen::Silence);
        // Default amplitude and freq should be zero or a reasonable default
        CHECK(cfg0.amplitude == doctest::Approx(0.0f).epsilon(0.01));
        CHECK(cfg0.freq == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("AudioGen: setConfig() changes config for a channel") {
        AudioDesc desc(48000, 2);
        AudioGen gen(desc);
        AudioGen::Config newCfg = { AudioGen::Sine, 440.0f, 0.8f, 0.0f, 0.5f };
        gen.setConfig(0, newCfg);
        const AudioGen::Config &cfg = gen.config(0);
        CHECK(cfg.type == AudioGen::Sine);
        // Note: setConfig converts freq to radians/sample internally
        float expectedFreq = M_PI * 2 * 440.0f / 48000.0f;
        CHECK(cfg.freq == doctest::Approx(expectedFreq));
        CHECK(cfg.amplitude == doctest::Approx(0.8f));
        CHECK(cfg.phase == doctest::Approx(0.0f));
        CHECK(cfg.dutyCycle == doctest::Approx(0.5f));
        // Channel 1 should remain unchanged
        CHECK(gen.config(1).type == AudioGen::Silence);
}

TEST_CASE("AudioGen: generate() returns valid Audio object") {
        AudioDesc desc(48000, 2);
        AudioGen gen(desc);
        Audio audio = gen.generate(1024);
        CHECK(audio.isValid());
        CHECK(audio.samples() == 1024);
        CHECK(audio.desc().channels() == 2);
        CHECK(audio.desc().sampleRate() == doctest::Approx(48000.0f));
}

TEST_CASE("AudioGen: generate() with Silence type produces zero samples") {
        AudioDesc desc(48000, 1);
        AudioGen gen(desc);
        // Default config is Silence, so just generate
        Audio audio = gen.generate(512);
        REQUIRE(audio.isValid());
        REQUIRE(audio.samples() == 512);
        const float *samples = audio.data<float>();
        bool allZero = true;
        for(size_t i = 0; i < audio.frames(); i++) {
                if(samples[i] != 0.0f) {
                        allZero = false;
                        break;
                }
        }
        CHECK(allZero);
}

TEST_CASE("AudioGen: generate() with Sine type produces non-zero samples") {
        AudioDesc desc(48000, 1);
        AudioGen gen(desc);
        gen.setConfig(0, { AudioGen::Sine, 1000.0f, 0.5f, 0.0f, 0.5f });
        Audio audio = gen.generate(512);
        REQUIRE(audio.isValid());
        REQUIRE(audio.samples() == 512);
        const float *samples = audio.data<float>();
        bool hasNonZero = false;
        for(size_t i = 0; i < audio.frames(); i++) {
                if(samples[i] != 0.0f) {
                        hasNonZero = true;
                        break;
                }
        }
        CHECK(hasNonZero);
        // All samples should be within [-amplitude, +amplitude]
        for(size_t i = 0; i < audio.frames(); i++) {
                CHECK(samples[i] >= -0.5f);
                CHECK(samples[i] <= 0.5f);
        }
}

TEST_CASE("AudioGen: generate() respects sample count") {
        AudioDesc desc(48000, 2);
        AudioGen gen(desc);
        gen.setConfig(0, { AudioGen::Sine, 440.0f, 0.3f, 0.0f, 0.5f });
        gen.setConfig(1, { AudioGen::Sine, 880.0f, 0.3f, 0.0f, 0.5f });

        // Generate different sample counts and verify each
        size_t counts[] = { 1, 100, 4800, 48000 };
        for(size_t count : counts) {
                Audio audio = gen.generate(count);
                REQUIRE(audio.isValid());
                CHECK(audio.samples() == count);
                CHECK(audio.frames() == count * 2);
        }
}

TEST_CASE("AudioFile: Filename roundtrip via sf_open_virtual") {
        // Write and read back using filename path (internally creates File IODevice).
        AudioDesc desc(48000, 2);
        AudioGen gen(desc);
        gen.setConfig(0, { AudioGen::Sine, 1000.0f, 0.5f, 0.0f, 0.5f });
        gen.setConfig(1, { AudioGen::Sine, 500.0f, 0.3f, 0.0f, 0.5f });

        const char *testFile = "test_virtual_roundtrip.wav";

        // Write
        AudioFile writer = AudioFile::createWriter(testFile);
        REQUIRE(writer.isValid());
        writer.setDesc(desc);
        Error err = writer.open();
        REQUIRE(err.isOk());
        Audio writeAudio = gen.generate(4800);
        REQUIRE(writeAudio.isValid());
        err = writer.write(writeAudio);
        CHECK(err.isOk());
        writer.close();

        // Read back using filename path
        AudioFile reader = AudioFile::createReader(testFile);
        REQUIRE(reader.isValid());
        err = reader.open();
        REQUIRE(err.isOk());
        CHECK(reader.sampleCount() == 4800);
        Audio readAudio;
        err = reader.read(readAudio, 4800);
        CHECK(err.isOk());
        CHECK(readAudio.isValid());
        CHECK(readAudio.samples() == 4800);
        reader.close();

        std::remove(testFile);
}

TEST_CASE("AudioFile: File IODevice roundtrip") {
        // Write via filename, read back via explicit File IODevice.
        AudioDesc desc(48000, 2);
        AudioGen gen(desc);
        gen.setConfig(0, { AudioGen::Sine, 1000.0f, 0.5f, 0.0f, 0.5f });
        gen.setConfig(1, { AudioGen::Sine, 500.0f, 0.3f, 0.0f, 0.5f });

        const char *testFile = "test_iodevice_roundtrip.wav";

        // Write via filename
        AudioFile writer = AudioFile::createWriter(testFile);
        REQUIRE(writer.isValid());
        writer.setDesc(desc);
        Error err = writer.open();
        REQUIRE(err.isOk());
        Audio writeAudio = gen.generate(4800);
        REQUIRE(writeAudio.isValid());
        err = writer.write(writeAudio);
        CHECK(err.isOk());
        writer.close();

        // Read back using explicit File IODevice
        File readFile(testFile);
        err = readFile.open(IODevice::ReadOnly);
        REQUIRE(err.isOk());

        auto [reader, readErr] = AudioFile::createForOperation(
                AudioFile::Reader, &readFile, "wav");
        REQUIRE(readErr.isOk());
        REQUIRE(reader.isValid());
        err = reader.open();
        REQUIRE(err.isOk());
        CHECK(reader.sampleCount() == 4800);
        Audio readAudio;
        err = reader.read(readAudio, 4800);
        CHECK(err.isOk());
        CHECK(readAudio.isValid());
        CHECK(readAudio.samples() == 4800);
        reader.close();
        readFile.close();

        std::remove(testFile);
}

TEST_CASE("AudioFile: BufferIODevice roundtrip") {
        AudioDesc desc(AudioDesc::PCMI_S16LE, 48000, 1);

        AudioGen gen(AudioDesc(48000, 1));
        gen.setConfig(0, { AudioGen::Sine, 440.0f, 0.5f, 0.0f, 0.5f });
        Audio srcAudio = gen.generate(480);
        REQUIRE(srcAudio.isValid());

        // Convert to S16LE for writing
        AudioDesc writeDesc(AudioDesc::PCMI_S16LE, 48000, 1);

        // Pre-allocate a generous buffer for in-memory WAV.
        Buffer buf(1024 * 1024);
        buf.setSize(0);
        BufferIODevice bufDev(&buf);
        Error err = bufDev.open(IODevice::ReadWrite);
        REQUIRE(err.isOk());

        // Write
        auto [writer, wErr] = AudioFile::createForOperation(
                AudioFile::Writer, &bufDev, "wav");
        REQUIRE(wErr.isOk());
        REQUIRE(writer.isValid());
        writer.setDesc(writeDesc);
        err = writer.open();
        REQUIRE(err.isOk());

        // Generate S16LE audio
        Audio s16Audio(writeDesc, 480);
        int16_t *s16Data = s16Audio.data<int16_t>();
        const float *srcData = srcAudio.data<float>();
        for(size_t i = 0; i < 480; i++) {
                s16Data[i] = static_cast<int16_t>(srcData[i] * 32767.0f);
        }

        err = writer.write(s16Audio);
        CHECK(err.isOk());
        writer.close();

        // Read back from the same buffer.
        bufDev.seek(0);
        auto [reader, rErr] = AudioFile::createForOperation(
                AudioFile::Reader, &bufDev, "wav");
        REQUIRE(rErr.isOk());
        REQUIRE(reader.isValid());
        err = reader.open();
        REQUIRE(err.isOk());
        CHECK(reader.sampleCount() == 480);

        Audio readAudio;
        err = reader.read(readAudio, 480);
        CHECK(err.isOk());
        CHECK(readAudio.isValid());
        CHECK(readAudio.samples() == 480);
        reader.close();
        bufDev.close();
}

TEST_CASE("AudioFile: Sequential device rejection") {
        FileIODevice seqDev(stdin, IODevice::ReadOnly);
        auto [file, err] = AudioFile::createForOperation(
                AudioFile::Reader, &seqDev, "wav");
        CHECK(err == Error::NotSupported);
}

TEST_CASE("AudioFile: createForOperation with nullptr device returns error") {
        auto [file, err] = AudioFile::createForOperation(
                AudioFile::Writer, nullptr, "wav");
        CHECK(err == Error::InvalidArgument);
}

TEST_CASE("AudioFile: createForOperation with no hint and no filename returns error") {
        Buffer buf(1024);
        buf.setSize(0);
        BufferIODevice bufDev(&buf);
        bufDev.open(IODevice::ReadWrite);

        auto [file, err] = AudioFile::createForOperation(
                AudioFile::Writer, &bufDev, "");
        // No hint and no filename means no factory can match.
        CHECK(err == Error::NotSupported);
        bufDev.close();
}
