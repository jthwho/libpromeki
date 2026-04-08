/**
 * @file      encodeddesc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/fourcc.h>
#include <promeki/string.h>
#include <promeki/metadata.h>
#include <promeki/imagedesc.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Describes a compressed or encoded media format.
 * @ingroup proav
 *
 * EncodedDesc is analogous to ImageDesc and AudioDesc but for encoded
 * bitstreams such as JPEG, H.264, or HEVC. It identifies the codec,
 * the source image format the data was encoded from, and an optional
 * quality parameter.
 */
class EncodedDesc {
        PROMEKI_SHARED_FINAL(EncodedDesc)
        public:
                /** @brief Shared pointer type for EncodedDesc. */
                using Ptr = SharedPtr<EncodedDesc>;

                /** @brief Plain value list of EncodedDesc objects. */
                using List = promeki::List<EncodedDesc>;

                /** @brief List of shared pointers to EncodedDesc. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an invalid (default) encoded description. */
                EncodedDesc() = default;

                /**
                 * @brief Constructs an encoded description with the given codec.
                 * @param codec The codec identifier (e.g. "JPEG", "H264").
                 */
                EncodedDesc(const FourCC &codec) : _codec(codec) { }

                /**
                 * @brief Constructs an encoded description with codec and source image format.
                 * @param codec The codec identifier.
                 * @param sourceImageDesc The uncompressed image format this was encoded from.
                 */
                EncodedDesc(const FourCC &codec, const ImageDesc &sourceImageDesc) :
                        _codec(codec), _sourceImageDesc(sourceImageDesc) { }

                /**
                 * @brief Returns true if this encoded description has a valid (non-zero) codec.
                 * @return true if valid.
                 */
                bool isValid() const {
                        return _codec.value() != 0;
                }

                /**
                 * @brief Returns the codec identifier.
                 * @return The FourCC codec value.
                 */
                FourCC codec() const {
                        return _codec;
                }

                /**
                 * @brief Sets the codec identifier.
                 * @param codec The new codec FourCC.
                 */
                void setCodec(const FourCC &codec) {
                        _codec = codec;
                        return;
                }

                /**
                 * @brief Returns the source image description this was encoded from.
                 * @return The source ImageDesc.
                 */
                const ImageDesc &sourceImageDesc() const {
                        return _sourceImageDesc;
                }

                /**
                 * @brief Sets the source image description.
                 * @param desc The uncompressed image format.
                 */
                void setSourceImageDesc(const ImageDesc &desc) {
                        _sourceImageDesc = desc;
                        return;
                }

                /**
                 * @brief Returns the codec-specific quality parameter.
                 *
                 * For JPEG this is quality 1-100. Returns -1 if not applicable.
                 *
                 * @return The quality value.
                 */
                int quality() const {
                        return _quality;
                }

                /**
                 * @brief Sets the codec-specific quality parameter.
                 * @param q The quality value (e.g. JPEG quality 1-100, or -1 for not applicable).
                 */
                void setQuality(int q) {
                        _quality = q;
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
                 * @brief Returns true if the format fields match (codec, quality).
                 * @param other The EncodedDesc to compare against.
                 * @return true if the format matches, ignoring metadata.
                 */
                bool formatEquals(const EncodedDesc &other) const {
                        return _codec == other._codec &&
                               _quality == other._quality;
                }

                /**
                 * @brief Returns true if both encoded descriptions are fully equal, including metadata.
                 * @param other The EncodedDesc to compare against.
                 * @return true if equal.
                 */
                bool operator==(const EncodedDesc &other) const {
                        return formatEquals(other) &&
                               _metadata == other._metadata;
                }

                /**
                 * @brief Returns true if the encoded descriptions are not equal.
                 * @param other The EncodedDesc to compare against.
                 * @return true if not equal.
                 */
                bool operator!=(const EncodedDesc &other) const {
                        return !(*this == other);
                }

                /**
                 * @brief Returns a human-readable string representation.
                 * @return A String describing the codec and quality.
                 */
                String toString() const;

                /** @brief Implicit conversion to String via toString(). */
                operator String() const {
                        return toString();
                }

        private:
                FourCC          _codec = FourCC('\0', '\0', '\0', '\0');
                ImageDesc       _sourceImageDesc;
                int             _quality = -1;
                Metadata        _metadata;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::EncodedDesc);
