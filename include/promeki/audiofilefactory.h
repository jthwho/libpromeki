/*****************************************************************************
 * audiofilefactory.h
 * May 18, 2023
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

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

#define PROMEKI_REGISTER_AUDIOFILE_FACTORY(name) [[maybe_unused]] static int \
        PROMEKI_CONCAT(__promeki_audiofilefactory_, PROMEKI_UNIQUE_ID) = \
        AudioFileFactory::registerFactory(new name);

class AudioFile;
class Error;

class AudioFileFactory {
        public:
                static int registerFactory(AudioFileFactory *object);

                static const AudioFileFactory *lookup(int operation, const String &filename);

                AudioFileFactory() = default;
                virtual ~AudioFileFactory() {};
                String name() const { return _name; }

                virtual bool canDoOperation(int operation, const String &filename) const;
                virtual AudioFile createForOperation(int operation) const;

                bool isExtensionSupported(const String &filename) const;

        protected:
                String          _name;
                StringList      _exts;          // List of valid file extensions

};

PROMEKI_NAMESPACE_END

