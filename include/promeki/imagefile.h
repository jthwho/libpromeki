/**
 * @file      imagefile.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/mediaconfig.h>

PROMEKI_NAMESPACE_BEGIN

class ImageFileIO;

/**
 * @brief Media file loader and saver.
 * @ingroup proav
 *
 * Provides a simple interface for loading and saving media files.
 * The file format is determined by the ID passed at construction,
 * which selects the corresponding ImageFileIO backend. Internally
 * holds a Frame which can carry images, audio, and metadata.
 */
class ImageFile {
        public:
                /** @brief Identifiers for supported image file formats. */
                enum ID {
                        Invalid = 0, ///< @brief No format / invalid.
                        PNG,         ///< @brief PNG image format.
                        RawYUV,      ///< @brief Raw YUV image format (headerless).
                        DPX,         ///< @brief SMPTE 268M DPX image format.
                        Cineon,      ///< @brief Kodak Cineon image format.
                        TGA,         ///< @brief Targa image format.
                        SGI,         ///< @brief Silicon Graphics image format.
                        PNM,         ///< @brief Portable AnyMap (PPM/PGM/PBM).
                        JPEG,        ///< @brief JPEG / JFIF image format.
                        JpegXS       ///< @brief JPEG XS (ISO/IEC 21122) image format.
                };

                /**
                 * @brief Constructs an ImageFile for the given format.
                 * @param id The format identifier (e.g. ImageFile::PNG). Defaults to Invalid.
                 */
                ImageFile(int id = 0);

                /**
                 * @brief Returns the filename associated with this image file.
                 * @return A const reference to the filename string.
                 */
                const String &filename() const {
                        return _filename;
                }

                /**
                 * @brief Sets the filename for loading or saving.
                 * @param val The filename to set.
                 */
                void setFilename(const String &val) {
                        _filename = val;
                        return;
                }

                /**
                 * @brief Returns a const reference to the frame.
                 * @return The frame containing images, audio, and metadata.
                 */
                const Frame &frame() const {
                        return _frame;
                }

                /**
                 * @brief Returns a mutable reference to the frame.
                 * @return The frame containing images, audio, and metadata.
                 */
                Frame &frame() {
                        return _frame;
                }

                /**
                 * @brief Sets the frame.
                 * @param val The Frame to set.
                 */
                void setFrame(const Frame &val) {
                        _frame = val;
                        return;
                }

                /**
                 * @brief Returns the first image from the frame, or an invalid Image if empty.
                 *
                 * Convenience accessor for formats that carry a single image.
                 * @return The first image by value.
                 */
                Image image() const {
                        if(_frame.imageList().isEmpty()) return Image();
                        return *_frame.imageList()[0];
                }

                /**
                 * @brief Sets the frame to contain a single image.
                 *
                 * Convenience setter for formats that carry a single image.
                 * Clears any existing images and audio in the frame.
                 * @param val The Image to set.
                 */
                void setImage(const Image &val) {
                        _frame.imageList().clear();
                        _frame.imageList().pushToBack(Image::Ptr::create(val));
                        return;
                }

                /**
                 * @brief Returns a const reference to the frame metadata.
                 * @return The metadata container.
                 */
                const Metadata &metadata() const {
                        return _frame.metadata();
                }

                /**
                 * @brief Returns a mutable reference to the frame metadata.
                 * @return The metadata container.
                 */
                Metadata &metadata() {
                        return _frame.metadata();
                }

                /**
                 * @brief Loads media from the file specified by filename().
                 * @param config Optional configuration hints forwarded to
                 *               the resolved @ref ImageFileIO backend.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error load(const MediaConfig &config = MediaConfig());

                /**
                 * @brief Saves media to the file specified by filename().
                 * @param config Optional configuration hints forwarded to
                 *               the resolved @ref ImageFileIO backend
                 *               (e.g. JpegQuality, JpegSubsampling).
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error save(const MediaConfig &config = MediaConfig());

        private:
                String                  _filename;
                Frame                   _frame;
                const ImageFileIO       *_io;
};

PROMEKI_NAMESPACE_END


