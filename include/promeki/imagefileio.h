/**
 * @file      imagefileio.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>

/**
 * @brief Macro to register an ImageFileIO subclass at static initialization time.
 * @ingroup proav
 *
 * @param name The ImageFileIO subclass to instantiate and register.
 */
#define PROMEKI_REGISTER_IMAGEFILEIO(name) [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_imagefileio_, PROMEKI_UNIQUE_ID) = \
        ImageFileIO::registerImageFileIO(new name);


PROMEKI_NAMESPACE_BEGIN

class Error;
class ImageFile;

/**
 * @brief Abstract backend for image file format I/O.
 *
 * Subclasses implement format-specific loading and saving of images
 * (e.g. PNG). Backends register themselves via PROMEKI_REGISTER_IMAGEFILEIO
 * and are looked up by their format ID.
 */
class ImageFileIO {
        public:
                /**
                 * @brief Registers an ImageFileIO backend in the global registry.
                 * @param object Pointer to the backend to register. Ownership is taken.
                 * @return A non-zero value (used for static initialization).
                 */
                static int registerImageFileIO(ImageFileIO *object);

                /**
                 * @brief Looks up a registered backend by format ID.
                 * @param id The format identifier (e.g. ImageFile::PNG).
                 * @return A pointer to the matching backend, or nullptr if not found.
                 */
                static const ImageFileIO *lookup(int id);

                /** @brief Default constructor. */
                ImageFileIO() = default;

                /** @brief Virtual destructor. */
                virtual ~ImageFileIO() {};

                /**
                 * @brief Returns the format identifier for this backend.
                 * @return The format ID.
                 */
                int id() const {
                        return _id;
                }

                /**
                 * @brief Returns true if this backend has a valid (non-zero) format ID.
                 * @return true if the ID is not zero.
                 */
                bool isValid() const {
                        return _id != 0;
                }

                /**
                 * @brief Returns true if this backend supports loading images.
                 * @return true if loading is supported.
                 */
                bool canLoad() const {
                        return _canLoad;
                }

                /**
                 * @brief Returns true if this backend supports saving images.
                 * @return true if saving is supported.
                 */
                bool canSave() const {
                        return _canSave;
                }

                /**
                 * @brief Returns the human-readable name of this backend.
                 * @return The backend name as a String.
                 */
                String name() const {
                        return _name;
                }

                /**
                 * @brief Loads an image from a file into the given ImageFile.
                 * @param imageFile The ImageFile to populate with the loaded image data.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error load(ImageFile &imageFile) const;

                /**
                 * @brief Saves the image from the given ImageFile to disk.
                 * @param imageFile The ImageFile containing the image data and filename.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error save(ImageFile &imageFile) const;

        protected:
                int             _id = 0;         ///< @brief Format identifier.
                bool            _canLoad = false; ///< @brief Whether loading is supported.
                bool            _canSave = false; ///< @brief Whether saving is supported.
                String          _name;            ///< @brief Human-readable format name.

};

PROMEKI_NAMESPACE_END

