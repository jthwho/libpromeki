/**
 * @file      mediaio_imagefile.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaio.h>
#include <promeki/imagefile.h>
#include <promeki/pixeldesc.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend for single-image file formats.
 * @ingroup proav
 *
 * Wraps the ImageFile / ImageFileIO subsystem to provide read and
 * write access to image files through the MediaIO interface.
 * Supported formats include DPX, Cineon, TGA, SGI, PNM, PNG,
 * and raw YUV variants.
 *
 * Each file is treated as a single-frame source or sink:
 * readFrame() succeeds once then returns EndOfFile, and
 * writeFrame() writes exactly one file per call.
 *
 * For DPX files that contain embedded audio, the audio is carried
 * through in the Frame automatically.
 *
 * @par Config keys
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigFilename | String | — | File path (inherited from MediaIO). |
 * | ConfigImageFileID | int | Invalid | Explicit ImageFile::ID override. |
 * | ConfigVideoWidth | int | 0 | Image width for headerless formats (RawYUV). |
 * | ConfigVideoHeight | int | 0 | Image height for headerless formats (RawYUV). |
 * | ConfigPixelDesc | PixelDesc | — | Pixel description for headerless formats. |
 *
 * @par Example — Read a DPX file
 * @code
 * MediaIO *io = MediaIO::createForFileRead("frame.dpx");
 * if(io) {
 *         io->open(MediaIO::Reader);
 *         Frame frame;
 *         io->readFrame(frame);
 *         io->close();
 *         delete io;
 * }
 * @endcode
 *
 * @par Example — Write a TGA file
 * @code
 * MediaIO *io = MediaIO::createForFileWrite("output.tga");
 * if(io) {
 *         io->open(MediaIO::Writer);
 *         io->writeFrame(frame);
 *         io->close();
 *         delete io;
 * }
 * @endcode
 *
 * @par Example — Raw YUV via explicit config
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaIO::ConfigType, "ImageFile");
 * cfg.set(MediaIO::ConfigFilename, "frame.yuv");
 * cfg.set(MediaIO_ImageFile::ConfigImageFileID, (int)ImageFile::RawYUV);
 * cfg.set(MediaIO_ImageFile::ConfigVideoWidth, 1920);
 * cfg.set(MediaIO_ImageFile::ConfigVideoHeight, 1080);
 * cfg.set(MediaIO_ImageFile::ConfigPixelDesc, PixelDesc(PixelDesc::YUV8_422_UYVY_Rec709));
 * MediaIO *io = MediaIO::create(cfg);
 * @endcode
 */
class MediaIO_ImageFile : public MediaIO {
        PROMEKI_OBJECT(MediaIO_ImageFile, MediaIO)
        public:
                static const ConfigID ConfigImageFileID;   ///< @brief Explicit ImageFile::ID (int).
                static const ConfigID ConfigVideoWidth;    ///< @brief Image width for headerless formats (int).
                static const ConfigID ConfigVideoHeight;   ///< @brief Image height for headerless formats (int).
                static const ConfigID ConfigPixelDesc;     ///< @brief Pixel description for headerless formats (PixelDesc).

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc covering all supported image file extensions.
                 */
                static FormatDesc formatDesc();

                /**
                 * @brief Constructs a MediaIO_ImageFile.
                 * @param parent Optional parent object.
                 */
                MediaIO_ImageFile(ObjectBase *parent = nullptr) : MediaIO(parent) { setStep(0); }

                /** @brief Destructor. */
                ~MediaIO_ImageFile() override;

                Error onOpen(Mode mode) override;
                Error onClose() override;
                MediaDesc mediaDesc() const override;
                Metadata metadata() const override;
                Error setMediaDesc(const MediaDesc &desc) override;
                Error setMetadata(const Metadata &meta) override;
                Error onReadFrame(Frame &frame) override;
                Error onWriteFrame(const Frame &frame) override;
                int64_t frameCount() const override;
                uint64_t currentFrame() const override;

        private:
                MediaDesc       _mediaDesc;
                Metadata        _metadata;
                Frame           _frame;
                int             _imageFileID = ImageFile::Invalid;
                uint64_t        _currentFrame = 0;
                bool            _loaded = false;
};

PROMEKI_NAMESPACE_END
