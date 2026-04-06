/**
 * @file      imagedesc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/size2d.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

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
                 * @brief Returns true if the image is interlaced.
                 * @return true if interlaced, false if progressive.
                 */
                bool interlaced() const {
                        return _interlaced;
                }

                /**
                 * @brief Sets whether the image is interlaced.
                 * @param val true for interlaced, false for progressive.
                 */
                void setInterlaced(bool val) {
                        _interlaced = val;
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
                bool                    _interlaced = false;
                PixelDesc               _pixelDesc;
                Metadata                _metadata;
};

PROMEKI_NAMESPACE_END
