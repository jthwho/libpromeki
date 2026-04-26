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
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
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
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
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
                const String &filename() const { return _filename; }

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
                const Frame &frame() const { return _frame; }

                /**
                 * @brief Returns a mutable reference to the frame.
                 * @return The frame containing images, audio, and metadata.
                 */
                Frame &frame() { return _frame; }

                /**
                 * @brief Sets the frame.
                 * @param val The Frame to set.
                 */
                void setFrame(const Frame &val) {
                        _frame = val;
                        return;
                }

                /**
                 * @brief Returns the first video payload from the frame.
                 * @return The first video payload, or a null Ptr if empty.
                 */
                VideoPayload::Ptr videoPayload() const {
                        auto vps = _frame.videoPayloads();
                        if (vps.isEmpty()) return VideoPayload::Ptr();
                        return vps[0];
                }

                /**
                 * @brief Returns the first video payload as an uncompressed
                 *        payload, or null if the stored payload is compressed.
                 */
                UncompressedVideoPayload::Ptr uncompressedVideoPayload() const {
                        VideoPayload::Ptr vp = videoPayload();
                        if (!vp.isValid()) return UncompressedVideoPayload::Ptr();
                        return sharedPointerCast<UncompressedVideoPayload>(vp);
                }

                /**
                 * @brief Replaces the frame's payload list with a single
                 *        video payload.
                 */
                void setVideoPayload(const VideoPayload::Ptr &val) {
                        _frame.payloadList().clear();
                        if (val.isValid()) _frame.addPayload(val);
                        return;
                }

                /**
                 * @brief Returns a const reference to the frame metadata.
                 * @return The metadata container.
                 */
                const Metadata &metadata() const { return _frame.metadata(); }

                /**
                 * @brief Returns a mutable reference to the frame metadata.
                 * @return The metadata container.
                 */
                Metadata &metadata() { return _frame.metadata(); }

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
                String             _filename;
                Frame              _frame;
                const ImageFileIO *_io;
};

PROMEKI_NAMESPACE_END
