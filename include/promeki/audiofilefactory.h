/**
 * @file      audiofilefactory.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/fileformatfactory.h>
#include <promeki/audiofile.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Factory type for creating AudioFile instances based on file format.
 * @ingroup proav
 *
 * AudioFileFactory is a typedef for FileFormatFactory<AudioFile>. Subclasses
 * register themselves via PROMEKI_REGISTER_AUDIOFILE_FACTORY to handle
 * specific audio file formats (e.g. WAV, FLAC).
 */
using AudioFileFactory = FileFormatFactory<AudioFile>;

/**
 * @brief Macro to register an AudioFileFactory subclass at static initialization time.
 * @ingroup proav
 *
 * @param name The AudioFileFactory subclass to instantiate and register.
 */
#define PROMEKI_REGISTER_AUDIOFILE_FACTORY(name) \
        PROMEKI_REGISTER_FILE_FORMAT_FACTORY(AudioFile, name)

PROMEKI_NAMESPACE_END
