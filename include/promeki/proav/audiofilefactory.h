/**
 * @file      proav/audiofilefactory.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/stringlist.h>
#include <promeki/core/util.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Macro to register an AudioFileFactory subclass at static initialization time.
 * @ingroup proav_media
 *
 * @param name The AudioFileFactory subclass to instantiate and register.
 */
#define PROMEKI_REGISTER_AUDIOFILE_FACTORY(name) [[maybe_unused]] static int \
        PROMEKI_CONCAT(__promeki_audiofilefactory_, PROMEKI_UNIQUE_ID) = \
        AudioFileFactory::registerFactory(new name);

class AudioFile;
class Error;

/**
 * @brief Factory for creating AudioFile instances based on file format.
 *
 * Subclasses register themselves via PROMEKI_REGISTER_AUDIOFILE_FACTORY
 * to handle specific audio file formats (e.g. WAV, FLAC). The factory
 * system selects the appropriate backend based on the filename extension
 * and the requested operation (read or write).
 */
class AudioFileFactory {
        public:
                /**
                 * @brief Registers a factory instance in the global registry.
                 * @param object Pointer to the factory to register. Ownership is taken.
                 * @return A non-zero value (used for static initialization).
                 */
                static int registerFactory(AudioFileFactory *object);

                /**
                 * @brief Looks up a factory that can handle the given operation and filename.
                 * @param operation The AudioFile::Operation value (Reader or Writer).
                 * @param filename The path to the audio file.
                 * @return A pointer to a matching factory, or nullptr if none found.
                 */
                static const AudioFileFactory *lookup(int operation, const String &filename);

                /** @brief Default constructor. */
                AudioFileFactory() = default;

                /** @brief Virtual destructor. */
                virtual ~AudioFileFactory() {};

                /**
                 * @brief Returns the human-readable name of this factory.
                 * @return The factory name as a String.
                 */
                String name() const { return _name; }

                /**
                 * @brief Returns true if this factory can perform the given operation on the file.
                 * @param operation The AudioFile::Operation value (Reader or Writer).
                 * @param filename The path to the audio file.
                 * @return true if the operation is supported for the given file.
                 */
                virtual bool canDoOperation(int operation, const String &filename) const;

                /**
                 * @brief Creates an AudioFile configured for the given operation.
                 * @param operation The AudioFile::Operation value (Reader or Writer).
                 * @return An AudioFile with the appropriate backend implementation.
                 */
                virtual AudioFile createForOperation(int operation) const;

                /**
                 * @brief Returns true if the filename has a supported extension.
                 * @param filename The path to check.
                 * @return true if the file extension matches one of this factory's supported extensions.
                 */
                bool isExtensionSupported(const String &filename) const;

        protected:
                String          _name;           ///< @brief Human-readable factory name.
                StringList      _exts;           ///< @brief List of supported file extensions.

};

PROMEKI_NAMESPACE_END

