/**
 * @file      imagefileio.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/mediaconfig.h>

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
                /** @brief List of registered ImageFileIO backend IDs. */
                using IDList = List<int>;

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

                /**
                 * @brief Returns every registered backend ID in registration order.
                 *
                 * Used by callers that need to enumerate all known image
                 * formats — e.g. the MediaIO layer wires up one
                 * @ref MediaIO::FormatDesc per backend ID.  Iterates the
                 * live registry, so calling this before every backend
                 * has registered (mid-static-init) returns a partial
                 * list; normal runtime use sees the complete set.
                 *
                 * @return A list of all registered backend IDs.
                 */
                static IDList registeredIDs();

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
                 * @brief Returns a short human-readable description of the backend.
                 *
                 * Drives the @c description field on the MediaIO
                 * FormatDesc; populated by the backend's constructor
                 * via the @c _description protected member.
                 *
                 * @return The backend description (may be empty).
                 */
                String description() const {
                        return _description;
                }

                /**
                 * @brief Returns the lowercase file extensions this backend handles.
                 *
                 * A single backend may claim multiple extensions that
                 * are treated as the same format by the decoder/encoder
                 * (e.g. SGI claims both @c "sgi" and @c "rgb"; PNM
                 * claims @c "pnm", @c "ppm", and @c "pgm").  Always
                 * returned in lowercase.
                 *
                 * @return The list of extensions claimed by the backend.
                 */
                const StringList &extensions() const {
                        return _extensions;
                }

                /**
                 * @brief Returns the preferred backend name for MediaIO registration.
                 *
                 * The MediaIO registry exposes one
                 * @ref MediaIO::FormatDesc per @ref ImageFileIO
                 * backend; each backend carries the string used to
                 * identify it there (@c "ImgSeqDPX", @c "ImgSeqPNG",
                 * @c "ImgSeqCineon", ...).  Defaults to
                 * @c "ImgSeq" + @ref name() when the backend's
                 * constructor doesn't override it.
                 *
                 * @return The backend's MediaIO name.
                 */
                String mediaIoName() const {
                        if(!_mediaIoName.isEmpty()) return _mediaIoName;
                        return String("ImgSeq") + _name;
                }

                /**
                 * @brief Loads an image from a file into the given ImageFile.
                 * @param imageFile The ImageFile to populate with the loaded image data.
                 * @param config    Optional configuration hints (e.g. headerless
                 *                  format size, codec preferences).  Backends
                 *                  that don't read any keys can ignore the
                 *                  parameter; the default value is an empty
                 *                  @ref MediaConfig.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error load(ImageFile &imageFile,
                                   const MediaConfig &config = MediaConfig()) const;

                /**
                 * @brief Saves the image from the given ImageFile to disk.
                 * @param imageFile The ImageFile containing the image data and filename.
                 * @param config    Optional configuration hints honoured at write
                 *                  time (codec quality, subsampling, sequence
                 *                  head, etc.).  Backends that don't read any
                 *                  keys can ignore the parameter; the default
                 *                  value is an empty @ref MediaConfig.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error save(ImageFile &imageFile,
                                   const MediaConfig &config = MediaConfig()) const;

        protected:
                int             _id = 0;         ///< @brief Format identifier.
                bool            _canLoad = false; ///< @brief Whether loading is supported.
                bool            _canSave = false; ///< @brief Whether saving is supported.
                String          _name;            ///< @brief Human-readable format name.
                String          _description;    ///< @brief One-line human-readable description.
                StringList      _extensions;     ///< @brief Lowercase extensions claimed by this backend.
                String          _mediaIoName;    ///< @brief Override for @ref mediaIoName() (empty = default).

};

PROMEKI_NAMESPACE_END

