/**
 * @file      audiofilefactory.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

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

