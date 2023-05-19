/*****************************************************************************
 * audio.cpp
 * April 29, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <promeki/unittest.h>
#include <promeki/audio.h>
#include <promeki/audiofile.h>
#include <promeki/audiogen.h>

using namespace promeki;

PROMEKI_DEBUG(AudioTest);

PROMEKI_TEST_BEGIN(Audio)
        Error err;
        AudioDesc desc(48000, 2);
        desc.metadata().set(Metadata::Title, "This is the title");
        desc.metadata().set(Metadata::Timecode, Timecode(10, 30, 00, 00, Timecode::NDF30));

        PROMEKI_TEST(desc.isValid());
        PROMEKI_TEST(desc.isNative());
        Logger::defaultLogger().log(Logger::Info, __FILE__, __LINE__, desc.metadata().dump());
        AudioGen gen(desc);
        
        AudioFile file = AudioFile::createWriter("test.wav");
        PROMEKI_TEST(file.isValid());
        file.setDesc(desc);
        err = file.open();
        PROMEKI_TEST(err.isOk());
        for(int i = 0; i < 10; i++) {
                if(i % 2) {
                        gen.setConfig(0, { AudioGen::Sine, 1000.0, 0.3, 0.0, 0.5 });
                        gen.setConfig(1, { AudioGen::Silence, 1000.0, 0.5, 0.0, 0.5 });
                } else {
                        gen.setConfig(0, { AudioGen::Silence, 0.0, 0.3, 0.0, 0.5 });
                        gen.setConfig(1, { AudioGen::Sine, 750.0, 0.3, 0.0, 0.5 });
                }
                Audio audio = gen.generate(4800);
                PROMEKI_TEST(audio.isValid());
                err = file.write(audio);
                PROMEKI_TEST(err.isOk());
        }

PROMEKI_TEST_END()

