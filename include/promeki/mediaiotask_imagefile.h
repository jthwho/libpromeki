/**
 * @file      mediaiotask_imagefile.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/imagefile.h>
#include <promeki/pixeldesc.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend for single-image file formats.
 * @ingroup proav
 *
 * Wraps the ImageFile / ImageFileIO subsystem to provide read and
 * write access to image files through the MediaIO interface.
 * Supported formats include DPX, Cineon, TGA, SGI, PNM, PNG,
 * and raw YUV variants.
 *
 * Each file is treated as a single-frame source or sink:
 * read commands deliver the loaded frame; with step != 0, a second
 * read returns EndOfFile.  Write commands write exactly one file
 * per call.
 *
 * @par Config keys
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigFilename | String | — | File path (inherited from MediaIO). |
 * | ConfigImageFileID | int | Invalid | Explicit ImageFile::ID override. |
 * | ConfigVideoWidth | int | 0 | Image width for headerless formats. |
 * | ConfigVideoHeight | int | 0 | Image height for headerless formats. |
 * | ConfigPixelDesc | PixelDesc | — | Pixel description for headerless formats. |
 */
class MediaIOTask_ImageFile : public MediaIOTask {
        public:
                static const MediaIO::ConfigID ConfigImageFileID;   ///< @brief Explicit ImageFile::ID (int).
                static const MediaIO::ConfigID ConfigVideoWidth;    ///< @brief Image width for headerless formats (int).
                static const MediaIO::ConfigID ConfigVideoHeight;   ///< @brief Image height for headerless formats (int).
                static const MediaIO::ConfigID ConfigPixelDesc;     ///< @brief Pixel description for headerless formats.

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc covering all supported image file extensions.
                 */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs a MediaIOTask_ImageFile. */
                MediaIOTask_ImageFile() = default;

                /** @brief Destructor. */
                ~MediaIOTask_ImageFile() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;

                String          _filename;
                Frame::Ptr      _frame;
                int             _imageFileID = ImageFile::Invalid;
                MediaIOMode     _mode = MediaIO_NotOpen;
                int64_t         _readCount = 0;
                int64_t         _writeCount = 0;
                bool            _loaded = false;
};

PROMEKI_NAMESPACE_END
