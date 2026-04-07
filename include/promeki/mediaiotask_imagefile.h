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
#include <promeki/framerate.h>
#include <promeki/size2d.h>

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
 * @par Frame rate
 *
 * A still image file has no intrinsic temporal rate, but MediaIO's
 * downstream consumers (pacing, playback, muxers) expect a valid
 * @c FrameRate on the cached @c MediaDesc.  This backend reports
 * @c FrameRate::FPS_30 by default and accepts an explicit override
 * via @c ConfigFrameRate.  For a writer, if the caller already set a
 * @c MediaDesc with a valid frame rate via @c MediaIO::setMediaDesc(),
 * that takes precedence over both the config value and the default.
 *
 * @par Config keys
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigFilename | String | — | File path (inherited from MediaIO). |
 * | ConfigImageFileID | int | Invalid | Explicit ImageFile::ID override. |
 * | ConfigVideoSize | Size2Du32 | 0x0 | Image size hint for headerless formats. |
 * | ConfigPixelDesc | PixelDesc | — | Pixel description for headerless formats. |
 * | ConfigFrameRate | FrameRate | 30/1 | Reported frame rate for the still image. |
 */
class MediaIOTask_ImageFile : public MediaIOTask {
        public:
                static const MediaIO::ConfigID ConfigImageFileID;   ///< @brief Explicit ImageFile::ID (int).
                static const MediaIO::ConfigID ConfigVideoSize;     ///< @brief Image size hint for headerless formats (Size2Du32).
                static const MediaIO::ConfigID ConfigPixelDesc;     ///< @brief Pixel description for headerless formats.
                static const MediaIO::ConfigID ConfigFrameRate;     ///< @brief Reported frame rate (FrameRate).

                /** @brief Default frame rate when no config or caller override is supplied. */
                static inline const FrameRate DefaultFrameRate{FrameRate::FPS_30};

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
