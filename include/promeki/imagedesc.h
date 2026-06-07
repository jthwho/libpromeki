/**
 * @file      imagedesc.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/array.h>
#include <promeki/string.h>
#include <promeki/sharedptr.h>
#include <promeki/size2d.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>
#include <promeki/enums_video.h>

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
 * ImageDesc desc(1920, 1080, PixelFormat::RGBA8_sRGB);
 * size_t stride = desc.pixelFormat().lineStride(0, desc);
 * int planes = desc.planeCount();
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 * @c ImageDesc::Ptr uses an atomic refcount and is safe to share across
 * threads.
 */
class ImageDesc {
                PROMEKI_SHARED_FINAL(ImageDesc)
        public:
                /**
                 * @brief Maximum number of planes an ImageDesc can carry
                 *        per-plane stride state for.
                 *
                 * Sized for the widest planar layout the library defines
                 * (Y/U/V plus alpha).  Per-plane @ref linePad / @ref lineAlign /
                 * @ref lineFlip are stored for this many planes; plane indices
                 * at-or-above this bound fall back to the scalar defaults.
                 */
                static constexpr size_t MaxPlanes = 4;

                /** @brief Shared pointer type for ImageDesc. */
                using Ptr = SharedPtr<ImageDesc>;

                /** @brief List of ImageDesc values. */
                using List = ::promeki::List<ImageDesc>;

                /** @brief List of shared ImageDesc pointers. */
                using PtrList = ::promeki::List<Ptr>;

                /** @brief Constructs an invalid (default) image description. */
                ImageDesc() {}

                /**
                 * @brief Constructs an image description from a size and pixel description.
                 * @param sz The image dimensions.
                 * @param pd The pixel description.
                 */
                ImageDesc(const Size2Du32 &sz, const PixelFormat &pd) : _size(sz), _pixelFormat(pd) {}

                /**
                 * @brief Constructs an image description from width, height, and pixel description.
                 * @param w  The image width in pixels.
                 * @param h  The image height in pixels.
                 * @param pd The pixel description.
                 */
                ImageDesc(size_t w, size_t h, const PixelFormat &pd) : _size(Size2Du32(w, h)), _pixelFormat(pd) {}

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
                 *             @c JPEG_XS_* @ref PixelFormat entries.
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
                 * @brief Resolves a JPEG PixelFormat from ST 2110-20
                 * colorimetry and RANGE parameters plus subsampling.
                 *
                 * Used by the deferred JPEG geometry path in the RTP
                 * reader to pick the correct PixelFormat variant when
                 * colorimetry/RANGE are available from the SDP fmtp
                 * line.  When @p colorimetry is empty, defaults to
                 * Rec.601 full range (JFIF standard).
                 *
                 * @param colorimetry ST 2110-20 colorimetry string
                 *        (e.g. "BT601-5", "BT709-2").
                 * @param range       "FULL" or "NARROW" (empty = FULL).
                 * @param is420       True for 4:2:0, false for 4:2:2.
                 * @param isRgb       True for RGB (overrides subsampling).
                 * @return The matching PixelFormat::ID.
                 */
                static PixelFormat::ID jpegPixelFormatFromSdp(const String &colorimetry, const String &range,
                                                              bool is420, bool isRgb);

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
                 *    from the PixelFormat's ColorModel and component
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
                 * ImageDesc is invalid or the PixelFormat is not
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
                bool isValid() const { return _size.isValid() && _pixelFormat.isValid(); }

                /**
                 * @brief Returns the pixel description.
                 * @return A const reference to the PixelFormat.
                 */
                const PixelFormat &pixelFormat() const { return _pixelFormat; }

                /**
                 * @brief Sets the pixel description.
                 * @param pd The pixel description.
                 */
                void setPixelFormat(const PixelFormat &pd) { _pixelFormat = pd; }

                /**
                 * @brief Returns the pixel format (memory layout) from the pixel description.
                 * @return A const reference to the PixelMemLayout.
                 */
                const PixelMemLayout &memLayout() const { return _pixelFormat.memLayout(); }

                /**
                 * @brief Returns the color model from the pixel description.
                 * @return A const reference to the ColorModel.
                 */
                const ColorModel &colorModel() const { return _pixelFormat.colorModel(); }

                /**
                 * @brief Returns the image dimensions.
                 * @return A const reference to the Size2Du32.
                 */
                const Size2Du32 &size() const { return _size; }

                /**
                 * @brief Returns the image width in pixels.
                 * @return The width.
                 */
                size_t width() const { return _size.width(); }

                /**
                 * @brief Returns the image height in pixels.
                 * @return The height.
                 */
                size_t height() const { return _size.height(); }

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
                 * @brief Returns the padding bytes appended to scanlines of plane 0.
                 *
                 * Convenience that reports the plane-0 value; @ref linePad(size_t)
                 * reads any plane.  The per-plane storage lets an ImageDesc model
                 * externally-strided memory (FFmpeg @c linesize, V4L2
                 * @c bytesperline, a GPU/CUDA row pitch) exactly, where each plane
                 * may carry a different amount of trailing padding — a Y plane and
                 * its half-width chroma planes do not generally pad to the same
                 * byte count.  With @ref lineAlign left at 1, @c linePad alone
                 * expresses any line stride at-or-above the tightly-packed width:
                 * @c linePad[c] = externalStride[c] − tightStride[c].
                 *
                 * @return The plane-0 line padding in bytes.
                 */
                size_t linePad() const { return _linePad[0]; }

                /**
                 * @brief Returns the padding bytes appended to scanlines of @p plane.
                 * @param plane The plane index (clamped — out-of-range returns 0).
                 * @return The line padding in bytes for @p plane.
                 */
                size_t linePad(size_t plane) const { return plane < MaxPlanes ? _linePad[plane] : 0; }

                /**
                 * @brief Sets the scanline padding for @em all planes.
                 * @param val The line padding in bytes applied to every plane.
                 */
                void setLinePad(size_t val) {
                        for (size_t p = 0; p < MaxPlanes; ++p) _linePad[p] = static_cast<uint32_t>(val);
                        return;
                }

                /**
                 * @brief Sets the scanline padding for a single @p plane.
                 * @param plane The plane index (out-of-range is ignored).
                 * @param val   The line padding in bytes for @p plane.
                 */
                void setLinePad(size_t plane, size_t val) {
                        if (plane < MaxPlanes) _linePad[plane] = static_cast<uint32_t>(val);
                        return;
                }

                /**
                 * @brief Returns the scanline alignment requirement of plane 0 in bytes.
                 * @return The plane-0 line alignment (1 = no alignment, 16 = 16-byte, …).
                 */
                size_t lineAlign() const { return _lineAlign[0]; }

                /**
                 * @brief Returns the scanline alignment requirement of @p plane in bytes.
                 * @param plane The plane index (clamped — out-of-range returns 1).
                 * @return The line alignment in bytes for @p plane.
                 */
                size_t lineAlign(size_t plane) const { return plane < MaxPlanes ? _lineAlign[plane] : 1; }

                /**
                 * @brief Sets the scanline alignment for @em all planes.
                 * @param val The alignment in bytes applied to every plane.
                 */
                void setLineAlign(size_t val) {
                        for (size_t p = 0; p < MaxPlanes; ++p) _lineAlign[p] = static_cast<uint32_t>(val);
                        return;
                }

                /**
                 * @brief Sets the scanline alignment for a single @p plane.
                 * @param plane The plane index (out-of-range is ignored).
                 * @param val   The alignment in bytes for @p plane.
                 */
                void setLineAlign(size_t plane, size_t val) {
                        if (plane < MaxPlanes) _lineAlign[plane] = static_cast<uint32_t>(val);
                        return;
                }

                /**
                 * @brief Returns true when @p plane is stored bottom-to-top (flipped).
                 *
                 * A flipped plane's scanlines run in reverse: the slice that backs
                 * the plane begins at the @em lowest address (the bottom row), and
                 * row 0 (the top of the displayed image) is the @em last row in
                 * memory.  This mirrors the negative-@c linesize convention used by
                 * FFmpeg/libswscale and GL readback for bottom-up images.  The line
                 * pitch magnitude is unchanged (@ref PixelFormat::lineStride still
                 * reports a positive byte count); @ref PixelFormat::signedLineStride
                 * reports the signed pitch, negative when flipped.
                 *
                 * @param plane The plane index (clamped — out-of-range returns false).
                 * @return true if @p plane is vertically flipped in memory.
                 */
                bool lineFlip(size_t plane = 0) const {
                        return plane < MaxPlanes && (_lineFlipMask & (1u << plane)) != 0;
                }

                /**
                 * @brief Sets the vertical-flip flag for @em all planes.
                 * @param val true to mark every plane bottom-to-top.
                 */
                void setLineFlip(bool val) {
                        _lineFlipMask = val ? static_cast<uint8_t>((1u << MaxPlanes) - 1) : 0;
                        return;
                }

                /**
                 * @brief Sets the vertical-flip flag for a single @p plane.
                 * @param plane The plane index (out-of-range is ignored).
                 * @param val   true to mark @p plane bottom-to-top.
                 */
                void setLineFlip(size_t plane, bool val) {
                        if (plane >= MaxPlanes) return;
                        if (val) _lineFlipMask |= static_cast<uint8_t>(1u << plane);
                        else _lineFlipMask &= static_cast<uint8_t>(~(1u << plane));
                        return;
                }

                /**
                 * @brief Returns the video scan mode.
                 *
                 * Replaces the earlier @c bool interlaced flag with a
                 * full @ref VideoScanMode value so progressive, PsF,
                 * and the various interlaced field orders are all
                 * captured.
                 *
                 * @return The scan mode.
                 */
                VideoScanMode videoScanMode() const { return _videoScanMode; }

                /**
                 * @brief Sets the scan mode.
                 * @param mode The scan mode to store.
                 */
                void setVideoScanMode(const VideoScanMode &mode) {
                        _videoScanMode = mode;
                        return;
                }

                /** @brief Returns a const reference to the metadata. */
                const Metadata &metadata() const { return _metadata; }

                /** @brief Returns a mutable reference to the metadata. */
                Metadata &metadata() { return _metadata; }

                /**
                 * @brief Returns the number of image planes defined by the pixel format.
                 * @return The plane count.
                 */
                int planeCount() const { return _pixelFormat.planeCount(); }

                /**
                 * @brief Returns a human-readable string representation of this image description.
                 * @return A String containing the dimensions and pixel description name.
                 */
                String toString() const {
                        String ret = _size.toString();
                        if (_pixelFormat.isValid()) {
                                ret += ' ';
                                ret += _pixelFormat.name();
                        }
                        return ret;
                }

                /** @brief Implicit conversion to String via toString(). */
                operator String() const { return toString(); }

                /** @brief Returns true if every member of both descriptors is equal. */
                bool operator==(const ImageDesc &other) const {
                        return formatEquals(other) && _metadata == other._metadata;
                }

                /** @brief Returns true if any member differs. */
                bool operator!=(const ImageDesc &other) const { return !(*this == other); }

                /**
                 * @brief Compares structural image fields, ignoring @ref metadata.
                 *
                 * Covers size, line padding / alignment, scan mode, and
                 * pixel description — every field that influences the
                 * byte layout or colour interpretation of the image.
                 * Metadata (colour tags, creator fields, user data) is
                 * deliberately excluded so negotiation between stages
                 * can compare "are these the same shape" without being
                 * derailed by cosmetic metadata differences.
                 *
                 * @param other The ImageDesc to compare against.
                 * @return true if every structural field matches.
                 */
                bool formatEquals(const ImageDesc &other) const {
                        return _size == other._size && _linePad == other._linePad && _lineAlign == other._lineAlign &&
                               _lineFlipMask == other._lineFlipMask && _videoScanMode == other._videoScanMode &&
                               _pixelFormat == other._pixelFormat;
                }

        private:
                Size2Du32                  _size;
                Array<uint32_t, MaxPlanes> _linePad{};               // 0 = tightly packed
                Array<uint32_t, MaxPlanes> _lineAlign{1, 1, 1, 1};   // 1 = no extra alignment
                uint8_t                    _lineFlipMask = 0;        // bit p set = plane p bottom-up
                VideoScanMode              _videoScanMode = VideoScanMode::Unknown;
                PixelFormat                _pixelFormat;
                Metadata                   _metadata;
};

/**
 * @brief Writes an ImageDesc as tag + size + pixelFormat + per-plane linePad
 *        + per-plane lineAlign + lineFlip mask + videoScanMode + metadata.
 *
 * The line-padding and alignment are written one @c uint32 per plane
 * (@ref ImageDesc::MaxPlanes entries each), followed by a @c uint32 flip
 * mask, so an externally-strided / bottom-up layout round-trips exactly.
 *
 * @param stream The stream to write to.
 * @param desc   The ImageDesc to serialize.
 * @return The stream, for chaining.
 */
inline DataStream &operator<<(DataStream &stream, const ImageDesc &desc) {
        stream.beginFrame(DataTypeImageDesc, 1);
        stream << desc.size();
        stream << desc.pixelFormat();
        for (size_t p = 0; p < ImageDesc::MaxPlanes; ++p) stream << static_cast<uint32_t>(desc.linePad(p));
        for (size_t p = 0; p < ImageDesc::MaxPlanes; ++p) stream << static_cast<uint32_t>(desc.lineAlign(p));
        uint32_t flipMask = 0;
        for (size_t p = 0; p < ImageDesc::MaxPlanes; ++p)
                if (desc.lineFlip(p)) flipMask |= (1u << p);
        stream << flipMask;
        stream << static_cast<uint32_t>(desc.videoScanMode().value());
        stream << desc.metadata();
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads an ImageDesc from its tagged wire format.
 * @param stream The stream to read from.
 * @param desc   The ImageDesc to populate.
 * @return The stream, for chaining.
 */
inline DataStream &operator>>(DataStream &stream, ImageDesc &desc) {
        if (!stream.readFrame(DataTypeImageDesc)) {
                desc = ImageDesc();
                return stream;
        }
        Size2Du32   size;
        PixelFormat pd;
        uint32_t    linePad[ImageDesc::MaxPlanes] = {0};
        uint32_t    lineAlign[ImageDesc::MaxPlanes] = {0};
        uint32_t    flipMask = 0;
        uint32_t    scanValue = 0;
        Metadata    meta;
        stream >> size >> pd;
        for (size_t p = 0; p < ImageDesc::MaxPlanes; ++p) stream >> linePad[p];
        for (size_t p = 0; p < ImageDesc::MaxPlanes; ++p) stream >> lineAlign[p];
        stream >> flipMask >> scanValue >> meta;
        if (stream.status() != DataStream::Ok) {
                desc = ImageDesc();
                return stream;
        }
        desc = ImageDesc(size, pd);
        for (size_t p = 0; p < ImageDesc::MaxPlanes; ++p) {
                desc.setLinePad(p, static_cast<size_t>(linePad[p]));
                desc.setLineAlign(p, static_cast<size_t>(lineAlign[p]));
                desc.setLineFlip(p, (flipMask & (1u << p)) != 0);
        }
        desc.setVideoScanMode(VideoScanMode{static_cast<int>(scanValue)});
        desc.metadata() = std::move(meta);
        return stream;
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::ImageDesc);

#endif // PROMEKI_ENABLE_PROAV