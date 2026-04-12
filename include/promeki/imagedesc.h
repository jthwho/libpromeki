/**
 * @file      imagedesc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/sharedptr.h>
#include <promeki/size2d.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/enums.h>

PROMEKI_NAMESPACE_BEGIN

class SdpMediaDescription;

/**
 * @brief Describes the format and layout of a single image.
 * @ingroup proav
 *
 * ImageDesc encapsulates image dimensions (Size2Du32), pixel description,
 * line padding and alignment, interlace mode, and associated metadata.
 * It is used by Image and MediaDesc to define the properties of image data.
 *
 * @par Example
 * @code
 * ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
 * size_t stride = desc.pixelDesc().lineStride(0, desc);
 * int planes = desc.planeCount();
 * @endcode
 */
class ImageDesc {
        PROMEKI_SHARED_FINAL(ImageDesc)
        public:
                /** @brief Shared pointer type for ImageDesc. */
                using Ptr = SharedPtr<ImageDesc>;

                /** @brief Constructs an invalid (default) image description. */
                ImageDesc() { }

                /**
                 * @brief Constructs an image description from a size and pixel description.
                 * @param sz The image dimensions.
                 * @param pd The pixel description.
                 */
                ImageDesc(const Size2Du32 &sz, const PixelDesc &pd) :
                        _size(sz), _pixelDesc(pd) { }

                /**
                 * @brief Constructs an image description from width, height, and pixel description.
                 * @param w  The image width in pixels.
                 * @param h  The image height in pixels.
                 * @param pd The pixel description.
                 */
                ImageDesc(size_t w, size_t h, const PixelDesc &pd) :
                        _size(Size2Du32(w, h)), _pixelDesc(pd) { }

                /**
                 * @brief Derives an ImageDesc from an SDP media description.
                 *
                 * Interprets the RTP payload encoding name in the
                 * @c a=rtpmap attribute and (when applicable) the
                 * @c a=fmtp parameters to build a video ImageDesc.
                 * Supported encodings:
                 *
                 *  - @c jxsv  (RFC 9134 JPEG XS) — geometry comes from
                 *             @c width= / @c height= in the fmtp line;
                 *             sampling and depth pick one of the
                 *             @c JPEG_XS_* @ref PixelDesc entries.
                 *             Defaults to @c JPEG_XS_YUV10_422_Rec709
                 *             when fmtp is incomplete.
                 *  - @c raw   (RFC 4175 uncompressed) — geometry and
                 *             pixel layout come from the fmtp line
                 *             (@c sampling, @c depth, @c width,
                 *             @c height).  Not yet implemented; this
                 *             overload returns an invalid ImageDesc
                 *             for now.
                 *  - @c JPEG  (RFC 2435 MJPEG) — geometry is carried
                 *             in the RFC 2435 packet header rather
                 *             than SDP, so this overload returns an
                 *             invalid ImageDesc.  Callers that need
                 *             MJPEG should read the geometry off the
                 *             first packet.
                 *
                 * Any other encoding (or a non-video @p md) yields an
                 * invalid @ref ImageDesc.  The interpretation is
                 * RTP-specific — these encoding names come from the
                 * IANA RTP payload registry and their respective
                 * RFCs — and lives on @ref ImageDesc because
                 * ImageDesc is the target type and the rest of
                 * proav already depends on the network layer.
                 *
                 * @param md The SDP media description to interpret.
                 * @return A populated ImageDesc, or an invalid
                 *         ImageDesc on any failure.
                 */
                static ImageDesc fromSdp(const SdpMediaDescription &md);

                /**
                 * @brief Resolves a JPEG PixelDesc from ST 2110-20
                 * colorimetry and RANGE parameters plus subsampling.
                 *
                 * Used by the deferred JPEG geometry path in the RTP
                 * reader to pick the correct PixelDesc variant when
                 * colorimetry/RANGE are available from the SDP fmtp
                 * line.  When @p colorimetry is empty, defaults to
                 * Rec.601 full range (JFIF standard).
                 *
                 * @param colorimetry ST 2110-20 colorimetry string
                 *        (e.g. "BT601-5", "BT709-2").
                 * @param range       "FULL" or "NARROW" (empty = FULL).
                 * @param is420       True for 4:2:0, false for 4:2:2.
                 * @param isRgb       True for RGB (overrides subsampling).
                 * @return The matching PixelDesc::ID.
                 */
                static PixelDesc::ID jpegPixelDescFromSdp(
                        const String &colorimetry,
                        const String &range,
                        bool is420,
                        bool isRgb);

                /**
                 * @brief Builds an SDP media description from this ImageDesc.
                 *
                 * The inverse of @ref fromSdp.  Populates the returned
                 * @c SdpMediaDescription with @c m=video, an
                 * @c a=rtpmap line, and (where applicable) an
                 * @c a=fmtp line carrying the format-specific
                 * parameters:
                 *
                 *  - @b JPEG — rtpmap @c JPEG/90000, fmtp with
                 *    ST 2110-20 @c colorimetry and @c RANGE derived
                 *    from the PixelDesc's ColorModel and component
                 *    range.
                 *  - @b JPEG @b XS — rtpmap @c jxsv/90000, fmtp
                 *    with @c sampling, @c depth, @c width, @c height,
                 *    @c colorimetry, and @c RANGE per RFC 9134.
                 *  - @b raw — rtpmap @c raw/90000, fmtp with
                 *    @c sampling, @c depth, @c width, @c height,
                 *    @c colorimetry, and @c RANGE per RFC 4175 /
                 *    ST 2110-20.
                 *
                 * Returns an empty @c SdpMediaDescription if the
                 * ImageDesc is invalid or the PixelDesc is not
                 * supported.
                 *
                 * @param payloadType RTP payload type (0-127).
                 * @return A populated SdpMediaDescription, or an
                 *         empty one on failure.
                 */
                SdpMediaDescription toSdp(uint8_t payloadType) const;

                /**
                 * @brief Returns true if this image description has valid dimensions and pixel description.
                 * @return true if valid.
                 */
                bool isValid() const {
                        return _size.isValid() && _pixelDesc.isValid();
                }

                /**
                 * @brief Returns the pixel description.
                 * @return A const reference to the PixelDesc.
                 */
                const PixelDesc &pixelDesc() const {
                        return _pixelDesc;
                }

                /**
                 * @brief Sets the pixel description.
                 * @param pd The pixel description.
                 */
                void setPixelDesc(const PixelDesc &pd) {
                        _pixelDesc = pd;
                }

                /**
                 * @brief Returns the pixel format (memory layout) from the pixel description.
                 * @return A const reference to the PixelFormat.
                 */
                const PixelFormat &pixelFormat() const {
                        return _pixelDesc.pixelFormat();
                }

                /**
                 * @brief Returns the color model from the pixel description.
                 * @return A const reference to the ColorModel.
                 */
                const ColorModel &colorModel() const {
                        return _pixelDesc.colorModel();
                }

                /**
                 * @brief Returns the image dimensions.
                 * @return A const reference to the Size2Du32.
                 */
                const Size2Du32 &size() const {
                        return _size;
                }

                /**
                 * @brief Returns the image width in pixels.
                 * @return The width.
                 */
                size_t width() const {
                        return _size.width();
                }

                /**
                 * @brief Returns the image height in pixels.
                 * @return The height.
                 */
                size_t height() const {
                        return _size.height();
                }

                /**
                 * @brief Sets the image dimensions.
                 * @param val The new Size2Du32 dimensions.
                 */
                void setSize(const Size2Du32 &val) {
                        _size = val;
                        return;
                }

                /**
                 * @brief Sets the image dimensions from width and height values.
                 * @param width  The new width in pixels.
                 * @param height The new height in pixels.
                 */
                void setSize(int width, int height) {
                        _size.set(width, height);
                        return;
                }

                /**
                 * @brief Returns the number of padding bytes appended to each scanline.
                 * @return The line padding in bytes.
                 */
                size_t linePad() const {
                        return _linePad;
                }

                /**
                 * @brief Sets the number of padding bytes appended to each scanline.
                 * @param val The line padding in bytes.
                 */
                void setLinePad(size_t val) {
                        _linePad = val;
                        return;
                }

                /**
                 * @brief Returns the scanline alignment requirement in bytes.
                 * @return The line alignment (e.g. 1 for no alignment, 16 for 16-byte alignment).
                 */
                size_t lineAlign() const {
                        return _lineAlign;
                }

                /**
                 * @brief Sets the scanline alignment requirement.
                 * @param val The alignment in bytes.
                 */
                void setLineAlign(size_t val) {
                        _lineAlign = val;
                        return;
                }

                /**
                 * @brief Returns the scan mode (progressive / interlaced / unknown).
                 *
                 * Replaces the earlier @c bool interlaced flag with a
                 * three-state @ref InterlaceMode value so the field
                 * order is captured alongside the interlaced-vs-
                 * progressive distinction.
                 *
                 * @return The scan mode.
                 */
                InterlaceMode interlaceMode() const {
                        return _interlaceMode;
                }

                /**
                 * @brief Sets the scan mode.
                 * @param mode The scan mode to store.
                 */
                void setInterlaceMode(const InterlaceMode &mode) {
                        _interlaceMode = mode;
                        return;
                }

                /** @brief Returns a const reference to the metadata. */
                const Metadata &metadata() const {
                        return _metadata;
                }

                /** @brief Returns a mutable reference to the metadata. */
                Metadata &metadata() {
                        return _metadata;
                }

                /**
                 * @brief Returns the number of image planes defined by the pixel format.
                 * @return The plane count.
                 */
                int planeCount() const {
                        return _pixelDesc.planeCount();
                }

                /**
                 * @brief Returns a human-readable string representation of this image description.
                 * @return A String containing the dimensions and pixel description name.
                 */
                String toString() const {
                        String ret = _size.toString();
                        if(_pixelDesc.isValid()) {
                                ret += ' ';
                                ret += _pixelDesc.name();
                        }
                        return ret;
                }

                /** @brief Implicit conversion to String via toString(). */
                operator String() const {
                        return toString();
                }

        private:
                Size2Du32               _size;
                size_t                  _linePad = 0;
                size_t                  _lineAlign = 1;
                InterlaceMode           _interlaceMode = InterlaceMode::Unknown;
                PixelDesc               _pixelDesc;
                Metadata                _metadata;
};

/**
 * @brief Writes an ImageDesc as tag + size + pixelDesc + linePad + lineAlign
 *        + interlaceMode + metadata.
 * @param stream The stream to write to.
 * @param desc   The ImageDesc to serialize.
 * @return The stream, for chaining.
 */
inline DataStream &operator<<(DataStream &stream, const ImageDesc &desc) {
        stream.writeTag(DataStream::TypeImageDesc);
        stream << desc.size();
        stream << desc.pixelDesc();
        stream << static_cast<uint64_t>(desc.linePad());
        stream << static_cast<uint64_t>(desc.lineAlign());
        stream << static_cast<uint32_t>(desc.interlaceMode().value());
        stream << desc.metadata();
        return stream;
}

/**
 * @brief Reads an ImageDesc from its tagged wire format.
 * @param stream The stream to read from.
 * @param desc   The ImageDesc to populate.
 * @return The stream, for chaining.
 */
inline DataStream &operator>>(DataStream &stream, ImageDesc &desc) {
        if(!stream.readTag(DataStream::TypeImageDesc)) { desc = ImageDesc(); return stream; }
        Size2Du32 size;
        PixelDesc pd;
        uint64_t linePad = 0, lineAlign = 1;
        uint32_t interlaceValue = 0;
        Metadata meta;
        stream >> size >> pd >> linePad >> lineAlign >> interlaceValue >> meta;
        if(stream.status() != DataStream::Ok) { desc = ImageDesc(); return stream; }
        desc = ImageDesc(size, pd);
        desc.setLinePad(static_cast<size_t>(linePad));
        desc.setLineAlign(static_cast<size_t>(lineAlign));
        desc.setInterlaceMode(InterlaceMode{static_cast<int>(interlaceValue)});
        desc.metadata() = std::move(meta);
        return stream;
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::ImageDesc);
