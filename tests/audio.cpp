/**
 * @file      audio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/audio.h>
#include <promeki/audiofile.h>
#include <promeki/audiogen.h>
#include <promeki/rational.h>
#include <promeki/uuid.h>
#include <promeki/logger.h>

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

}
