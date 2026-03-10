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
#include <promeki/image.h>

PROMEKI_NAMESPACE_BEGIN

class ImageFileIO;

/**
 * @brief Image file loader and saver.
 *
 * Provides a simple interface for loading and saving image files.
 * The file format is determined by the ID passed at construction,
 * which selects the corresponding ImageFileIO backend.
 */
class ImageFile {
        public:
                /** @brief Identifiers for supported image file formats. */
                enum ID {
                        Invalid = 0, ///< @brief No format / invalid.
                        PNG          ///< @brief PNG image format.
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
                 * @brief Returns the image data.
                 * @return A const reference to the Image object.
                 */
                const Image &image() const {
                        return _image;
                }

                /**
                 * @brief Sets the image data for saving.
                 * @param val The Image to set.
                 */
                void setImage(const Image &val) {
                        _image = val;
                        return;
                }

                /**
                 * @brief Loads an image from the file specified by filename().
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error load();

                /**
                 * @brief Saves the image to the file specified by filename().
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error save();

        private:
                String                  _filename;
                Image                   _image;
                const ImageFileIO       *_io;
};

PROMEKI_NAMESPACE_END


