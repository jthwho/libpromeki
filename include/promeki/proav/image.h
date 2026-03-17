/**
 * @file      proav/image.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/sharedptr.h>
#include <promeki/core/buffer.h>
#include <promeki/proav/imagedesc.h>
#include <promeki/proav/paintengine.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Raster image with pixel format, planes, and metadata.
 * @ingroup proav_media
 *
 * Holds image pixel data organized into one or more memory planes
 * according to the image's pixel format. Provides convenience
 * accessors for dimensions, pixel format, and metadata, as well as
 * pixel format conversion and paint engine creation.
 * When shared ownership is needed, use Image::Ptr.
 *
 * @par Compressed images
 * Image also represents compressed (encoded) image data such as JPEG.
 * A compressed image uses a compressed pixel format (e.g.
 * PixelFormat::JPEG_RGB8) and stores the encoded bitstream in its
 * single plane buffer. Use isCompressed() to test whether an image
 * is compressed, compressedSize() to get the encoded byte count,
 * and data() to access the raw encoded bytes. The preferred way to
 * create a compressed image is the fromCompressedData() factory.
 *
 * @par Example — creating a compressed image
 * @code
 * // After compressing with libjpeg or another codec:
 * Image jpeg = Image::fromCompressedData(jpegBytes, jpegSize,
 *                                        1920, 1080,
 *                                        PixelFormat::JPEG_RGB8,
 *                                        srcImage.metadata());
 * assert(jpeg.isCompressed());
 * assert(jpeg.compressedSize() == jpegSize);
 * @endcode
 */
class Image {
        PROMEKI_SHARED_FINAL(Image)
        public:
                /** @brief Shared pointer type for Image. */
                using Ptr = SharedPtr<Image>;

                /** @brief List of Image values. */
                using List = promeki::List<Image>;

                /** @brief List of shared Image pointers. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an invalid (empty) image. */
                Image() = default;

                /**
                 * @brief Constructs an image from an image descriptor.
                 * @param desc Image descriptor specifying size, pixel format, and metadata.
                 * @param ms   Memory space to allocate plane buffers from.
                 */
                Image(const ImageDesc &desc, const MemSpace &ms = MemSpace::Default);

                /**
                 * @brief Constructs an image from a size and pixel format ID.
                 * @param s      Image dimensions.
                 * @param pixfmt Pixel format identifier.
                 * @param ms     Memory space to allocate from.
                 */
                Image(const Size2Du32 &s, int pixfmt, const MemSpace &ms = MemSpace::Default) :
                        Image(ImageDesc(s, pixfmt), ms) { }

                /**
                 * @brief Constructs an image from width, height, and pixel format ID.
                 * @param w      Image width in pixels.
                 * @param h      Image height in pixels.
                 * @param pixfmt Pixel format identifier.
                 * @param ms     Memory space to allocate from.
                 */
                Image(size_t w, size_t h, int pixfmt, const MemSpace &ms = MemSpace::Default) :
                        Image(ImageDesc(w, h, pixfmt), ms) { }

                /**
                 * @brief Returns true if the image has a valid descriptor and allocated planes.
                 * @return true if the image descriptor is valid.
                 */
                bool isValid() const {
                        return _desc.isValid();
                }

                /**
                 * @brief Returns the image descriptor.
                 * @return A const reference to the ImageDesc.
                 */
                const ImageDesc &desc() const {
                        return _desc;
                }

                /**
                 * @brief Returns the pixel format identifier.
                 * @return The pixel format ID integer.
                 */
                int pixelFormatID() const {
                        return _desc.pixelFormatID();
                }

                /**
                 * @brief Returns a pointer to the PixelFormat object.
                 * @return The PixelFormat pointer, or nullptr if unknown.
                 */
                const PixelFormat *pixelFormat() const {
                        return _desc.pixelFormat();
                }

                /**
                 * @brief Returns the image dimensions.
                 * @return A const reference to the Size2Du32.
                 */
                const Size2Du32 &size() const {
                        return _desc.size();
                }

                /**
                 * @brief Returns the image width in pixels.
                 * @return The width.
                 */
                size_t width() const {
                        return _desc.width();
                }

                /**
                 * @brief Returns the image height in pixels.
                 * @return The height.
                 */
                size_t height() const {
                        return _desc.height();
                }

                /**
                 * @brief Returns a const reference to the image metadata.
                 * @return The metadata.
                 */
                const Metadata &metadata() const {
                        return _desc.metadata();
                }

                /**
                 * @brief Returns a mutable reference to the image metadata.
                 * @return The metadata.
                 */
                Metadata &metadata() {
                        return _desc.metadata();
                }

                /**
                 * @brief Returns the line stride in bytes for the given plane.
                 * @param plane The plane index (defaults to 0).
                 * @return The number of bytes per scanline.
                 */
                size_t lineStride(int plane = 0) const {
                        return _desc.pixelFormat()->lineStride(plane, _desc);
                }

                /**
                 * @brief Returns the buffer for the given image plane.
                 * @param index The plane index (defaults to 0).
                 * @return A const reference to the Buffer shared pointer.
                 */
                const Buffer::Ptr &plane(int index = 0) const {
                        return _planeList[index];
                }

                /**
                 * @brief Returns the list of all plane buffers.
                 * @return A const reference to the Buffer::PtrList.
                 */
                const Buffer::PtrList &planes() const {
                        return _planeList;
                }

                /**
                 * @brief Returns a raw data pointer for the given plane.
                 * @param index The plane index (defaults to 0).
                 * @return A void pointer to the plane's pixel data.
                 */
                void *data(int index = 0) const {
                        return _planeList[index]->data();
                }

                /**
                 * @brief Fills all image planes with the given byte value.
                 * @param value The byte value to fill with.
                 * @return Error::Ok on success, or an error if no planes exist or a fill fails.
                 */
                Error fill(char value) const {
                        if(_planeList.isEmpty()) return Error::Invalid;
                        for(auto &p : _planeList) {
                                Error err = p->fill(value);
                                if(err.isError()) return err;
                        }
                        return Error::Ok;
                }

                /**
                 * @brief Returns true if all plane buffers are exclusively owned.
                 *
                 * A plane is exclusive when its Buffer::Ptr has a reference count
                 * of 1 (or is null). This is useful for determining whether it is
                 * safe to mutate the pixel data in place without affecting other
                 * consumers that may share the same buffers.
                 *
                 * @return true if every plane buffer has referenceCount() <= 1.
                 */
                bool isExclusive() const {
                        for(const auto &p : _planeList) {
                                if(p.referenceCount() > 1) return false;
                        }
                        return true;
                }

                /**
                 * @brief Ensures exclusive ownership of all plane buffers.
                 *
                 * For each plane Buffer::Ptr, calls modify() to trigger a
                 * copy-on-write detach if the reference count is greater than 1.
                 * In a linear pipeline where only one consumer holds a reference,
                 * this is a no-op. In fan-out scenarios where multiple consumers
                 * share the same buffers, this creates private copies so that
                 * the caller can safely mutate the pixel data.
                 */
                void ensureExclusive() {
                        for(auto &p : _planeList) {
                                p.modify();
                        }
                        return;
                }

                /**
                 * @brief Returns true if this image uses a compressed pixel format.
                 * @return true if the pixel format is compressed (e.g. JPEG).
                 */
                bool isCompressed() const {
                        const PixelFormat *pf = _desc.pixelFormat();
                        return pf != nullptr && pf->isCompressed();
                }

                /**
                 * @brief Returns the compressed data size in bytes.
                 *
                 * For compressed images, the encoded bitstream lives in the
                 * first plane buffer. This method returns that buffer's logical
                 * size, which is the actual encoded byte count. Use data() to
                 * obtain a pointer to the encoded bytes.
                 *
                 * Returns 0 for uncompressed images or if no planes are allocated.
                 *
                 * @return The compressed data size, or 0.
                 */
                size_t compressedSize() const {
                        if(!isCompressed() || _planeList.isEmpty()) return 0;
                        return _planeList[0]->size();
                }

                /**
                 * @brief Creates a compressed image from pre-encoded data.
                 *
                 * This is the preferred way to construct a compressed Image.
                 * It allocates a plane buffer sized to hold the encoded data,
                 * copies the bytes in, sets the buffer's logical size to
                 * @p size, and attaches the supplied metadata. The resulting
                 * Image reports isCompressed() == true and compressedSize() == @p size.
                 *
                 * The @p width and @p height describe the original uncompressed
                 * dimensions — they are stored in the ImageDesc so that
                 * downstream consumers know the frame size without decoding.
                 *
                 * @param data       Pointer to the compressed data.
                 * @param size       Size of the compressed data in bytes.
                 * @param width      Original image width in pixels.
                 * @param height     Original image height in pixels.
                 * @param pixfmt     Compressed pixel format ID (e.g. PixelFormat::JPEG_RGB8).
                 * @param metadata   Optional metadata to attach (e.g. timecode).
                 * @return A valid compressed Image, or an invalid Image on failure.
                 */
                static Image fromCompressedData(const void *data, size_t size,
                                                size_t width, size_t height, int pixfmt,
                                                const Metadata &metadata = Metadata());

                /**
                 * @brief Creates a paint engine for drawing on this image.
                 * @return A PaintEngine configured for this image's pixel format.
                 */
                PaintEngine createPaintEngine() const {
                        return _desc.pixelFormat()->createPaintEngine(*this);
                }

                /**
                 * @brief Converts this image to a different pixel format.
                 * @param pixelFormat The target pixel format ID.
                 * @param metadata    Metadata to attach to the converted image.
                 * @return A new Image in the target pixel format.
                 */
                Image convert(PixelFormat::ID pixelFormat, const Metadata &metadata) const;

        private:
                ImageDesc       _desc;
                Buffer::PtrList _planeList;

                bool allocate(const MemSpace &ms);
};

PROMEKI_NAMESPACE_END
