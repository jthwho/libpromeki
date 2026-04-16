/**
 * @file      image.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <optional>
#include <type_traits>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/buffer.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/paintengine.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Raster image with pixel description, planes, and metadata.
 * @ingroup proav
 *
 * Holds image pixel data organized into one or more memory planes
 * according to the image's pixel description. Provides convenience
 * accessors for dimensions, pixel format, and metadata, as well as
 * pixel format conversion and paint engine creation.
 * When shared ownership is needed, use Image::Ptr.
 *
 * @par Compressed images
 * Image also represents compressed (encoded) image data such as JPEG.
 * A compressed image uses a compressed pixel description (e.g.
 * PixelDesc::JPEG_RGB8_sRGB) and stores the encoded bitstream in its
 * single plane buffer. Use isCompressed() to test whether an image
 * is compressed, compressedSize() to get the encoded byte count,
 * and data() to access the raw encoded bytes. The preferred way to
 * create a compressed image is the fromCompressedData() factory.
 *
 * @par Example — creating a compressed image
 * @code
 * Image jpeg = Image::fromCompressedData(jpegBytes, jpegSize,
 *                                        1920, 1080,
 *                                        PixelDesc::JPEG_RGB8_sRGB,
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
                 * @param desc Image descriptor specifying size, pixel description, and metadata.
                 * @param ms   Memory space to allocate plane buffers from.
                 */
                Image(const ImageDesc &desc, const MemSpace &ms = MemSpace::Default);

                /**
                 * @brief Constructs an image from a size and pixel description.
                 * @param s  Image dimensions.
                 * @param pd Pixel description.
                 * @param ms Memory space to allocate from.
                 */
                Image(const Size2Du32 &s, const PixelDesc &pd, const MemSpace &ms = MemSpace::Default) :
                        Image(ImageDesc(s, pd), ms) { }

                /**
                 * @brief Constructs an image from width, height, and pixel description.
                 * @param w  Image width in pixels.
                 * @param h  Image height in pixels.
                 * @param pd Pixel description.
                 * @param ms Memory space to allocate from.
                 */
                Image(size_t w, size_t h, const PixelDesc &pd, const MemSpace &ms = MemSpace::Default) :
                        Image(ImageDesc(w, h, pd), ms) { }

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
                 * @brief Returns the pixel description.
                 * @return A const reference to the PixelDesc.
                 */
                const PixelDesc &pixelDesc() const {
                        return _desc.pixelDesc();
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
                        return _desc.pixelDesc().lineStride(plane, _desc);
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
                 */
                void ensureExclusive() {
                        for(auto &p : _planeList) {
                                p.modify();
                        }
                        return;
                }

                /**
                 * @brief Returns true if this image uses a compressed pixel description.
                 * @return true if the pixel description is compressed (e.g. JPEG).
                 */
                bool isCompressed() const {
                        return _desc.pixelDesc().isCompressed();
                }

                /**
                 * @brief Returns the compressed data size in bytes.
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
                 * @param data       Pointer to the compressed data.
                 * @param size       Size of the compressed data in bytes.
                 * @param width      Original image width in pixels.
                 * @param height     Original image height in pixels.
                 * @param pd         Compressed pixel description (e.g. PixelDesc::JPEG_RGB8_sRGB).
                 * @param metadata   Optional metadata to attach (e.g. timecode).
                 * @return A valid compressed Image, or an invalid Image on failure.
                 */
                static Image fromCompressedData(const void *data, size_t size,
                                                size_t width, size_t height, const PixelDesc &pd,
                                                const Metadata &metadata = Metadata());

                /**
                 * @brief Creates an Image that adopts an existing @c Buffer::Ptr as plane 0.
                 *
                 * Zero-copy factory — the supplied @p buffer becomes the
                 * image's first plane directly, with no allocation or
                 * memcpy. Works for both compressed and uncompressed
                 * pixel descriptions: for compressed formats the buffer
                 * holds the encoded bitstream, for uncompressed formats
                 * it holds the raw pixel data.
                 *
                 * This is the preferred path for wrapping sample data
                 * that a container reader has already brought into
                 * memory — flows like
                 *   @c File::readBulk → @c Buffer → @c Image::fromBuffer
                 * are fully zero-copy from disk to image.
                 *
                 * @param buffer   The existing buffer to adopt. Must be
                 *                 non-null and valid. Its @c size() must
                 *                 be at least @c pd.planeSize(0, ImageDesc{...}).
                 *                 For uncompressed formats, @p buffer's
                 *                 contents are expected to match the
                 *                 pixel description's layout.
                 * @param width    Image width in pixels.
                 * @param height   Image height in pixels.
                 * @param pd       Pixel description (compressed or raster).
                 * @param metadata Optional metadata to attach.
                 * @return A valid Image that shares @p buffer, or an
                 *         invalid Image on failure.
                 */
                static Image fromBuffer(const Buffer::Ptr &buffer,
                                        size_t width, size_t height, const PixelDesc &pd,
                                        const Metadata &metadata = Metadata());

                /**
                 * @brief Creates a paint engine for drawing on this image.
                 * @return A PaintEngine configured for this image's pixel description.
                 */
                PaintEngine createPaintEngine() const {
                        return _desc.pixelDesc().createPaintEngine(*this);
                }

                /**
                 * @brief Converts this image to a different pixel description.
                 *
                 * Handles every combination of compressed and uncompressed pixel
                 * descriptions:
                 *
                 * - **Uncompressed → uncompressed:** Creates a @ref CSCPipeline
                 *   for this image's pixel description and the target, then
                 *   executes the conversion.  For common broadcast pairs
                 *   (e.g. RGBA8 ↔ YUV8 Rec.709), a registered fast-path kernel
                 *   is used automatically.  Set @ref MediaConfig::CscPath to
                 *   ``"scalar"`` in @p config to force the generic float
                 *   pipeline (useful for debugging or reference comparison
                 *   against @ref Color::convert).
                 * - **Uncompressed → compressed:** If the source format is not
                 *   one of the target codec's @ref PixelDesc::encodeSources,
                 *   a CSC is run first to land on one that is.  The codec
                 *   (currently only JPEG) then encodes the intermediate.
                 *   JPEG quality and chroma subsampling can be overridden via
                 *   @ref MediaConfig::JpegQuality and
                 *   @ref MediaConfig::JpegSubsampling in @p config.
                 * - **Compressed → uncompressed:** The codec is asked to
                 *   decode directly to the target format.  If the codec does
                 *   not support that target natively, it decodes to one of its
                 *   preferred @ref PixelDesc::decodeTargets and a CSC finishes
                 *   the job.
                 * - **Compressed → compressed:** Decodes to an uncompressed
                 *   intermediate shared by both sides (or RGBA8_sRGB when
                 *   there is no overlap) and re-encodes.
                 *
                 * @param pd       The target pixel description.
                 * @param metadata Metadata to attach to the converted image.
                 * @param config   Optional configuration hints
                 *                 (@ref MediaConfig::CscPath,
                 *                 @ref MediaConfig::JpegQuality,
                 *                 @ref MediaConfig::JpegSubsampling).
                 * @return A new Image in the target pixel description, or an
                 *         invalid Image on failure.
                 *
                 * @see CSCPipeline, ImageCodec, JpegImageCodec, @ref csc "CSC Framework"
                 */
                Image convert(const PixelDesc &pd, const Metadata &metadata,
                              const MediaConfig &config = MediaConfig()) const;

                /**
                 * @brief Resolves a single template key against this image's structure.
                 *
                 * Used by @ref makeString and by enclosing containers
                 * (e.g. @ref Frame::resolveTemplateKey for the
                 * @c Image[N].xxx subscript syntax) to render a single
                 * @c {Key[:spec]} placeholder in isolation.  The
                 * returned @c std::optional is populated when the key
                 * names something this image can describe:
                 *
                 *  - @b "@<Pseudo>" — one of the introspection pseudo
                 *    keys listed in @ref makeString.  The @c "@" prefix
                 *    keeps these from colliding with metadata keys.
                 *  - Any registered metadata key — looked up in
                 *    @ref metadata and rendered with
                 *    @ref Variant::format.
                 *
                 * Returns @c std::nullopt when the key matches neither.
                 *
                 * @param key  The placeholder key (no braces, no colon).
                 * @param spec The format spec (may be empty).
                 */
                std::optional<String> resolveTemplateKey(const String &key, const String &spec) const;

                /**
                 * @brief Substitutes @c {Key[:spec]} placeholders against this image.
                 *
                 * Delegates to @ref Metadata::format for direct
                 * metadata keys.  Adds an introspection layer that
                 * resolves @c "@"-prefixed pseudo keys describing the
                 * image's own structure (so templates can pull values
                 * out of the image even when nothing has been written
                 * to its metadata).
                 *
                 * Recognised pseudo keys:
                 *
                 *  - @c \@Width — image width in pixels (uint32).
                 *  - @c \@Height — image height in pixels (uint32).
                 *  - @c \@Size — @c "WxH" via @ref Size2Du32.
                 *  - @c \@PixelDesc — pixel description name.
                 *  - @c \@PixelFormat — pixel format name.
                 *  - @c \@ColorModel — color model name.
                 *  - @c \@LinePad — line padding in bytes (uint64).
                 *  - @c \@LineAlign — scanline alignment in bytes (uint64).
                 *  - @c \@ScanMode — scan mode (progressive / interlaced / unknown).
                 *  - @c \@PlaneCount — number of memory planes (int).
                 *  - @c \@IsValid — bool.
                 *  - @c \@IsCompressed — bool.
                 *  - @c \@IsExclusive — bool.
                 *  - @c \@CompressedSize — encoded byte count, or 0 (uint64).
                 *
                 * @par Example
                 * @code
                 * Image img(1920, 1080, PixelDesc::RGBA8_sRGB);
                 * String s = img.makeString("{@Size} {@PixelDesc}");
                 * // "1920x1080 RGBA8_sRGB"
                 * @endcode
                 *
                 * @tparam Resolver Callable with signature
                 *                  @c std::optional<String>(const String &, const String &).
                 *                  Pass @c nullptr (or use the no-resolver overload) to
                 *                  skip the user fallback path.
                 * @param tmpl     Template string with @c {Key[:spec]} placeholders.
                 * @param resolver Optional fallback resolver consulted for keys that
                 *                 are neither @c "@"-prefixed pseudo keys nor present
                 *                 in @ref metadata.
                 * @param err      Optional error output (set to @c Error::IdNotFound
                 *                 when any key is unresolved).
                 */
                template <typename Resolver>
                String makeString(const String &tmpl, Resolver &&resolver, Error *err = nullptr) const {
                        return _desc.metadata().format(tmpl,
                                [this, &resolver](const String &key, const String &spec) -> std::optional<String> {
                                        if(!key.isEmpty() && key.cstr()[0] == '@') {
                                                auto v = resolvePseudoKey(key, spec);
                                                if(v.has_value()) return v;
                                        }
                                        if constexpr (!std::is_same_v<std::decay_t<Resolver>, std::nullptr_t>) {
                                                return resolver(key, spec);
                                        }
                                        return std::nullopt;
                                }, err);
                }

                /** @brief Convenience overload of @ref makeString with no fallback resolver. */
                String makeString(const String &tmpl, Error *err = nullptr) const {
                        return makeString(tmpl, nullptr, err);
                }

        private:
                ImageDesc       _desc;
                Buffer::PtrList _planeList;

                bool allocate(const MemSpace &ms);
                std::optional<String> resolvePseudoKey(const String &key, const String &spec) const;
};

PROMEKI_NAMESPACE_END
