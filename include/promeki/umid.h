/**
 * @file      umid.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/array.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief SMPTE 330M Unique Material Identifier (UMID).
 * @ingroup util
 *
 * Represents a SMPTE 330M UMID in either its Basic (32-byte) or
 * Extended (64-byte) form.  A UMID is a globally unique identifier
 * for a piece of media content, used by MXF, BWF, and other
 * broadcast-adjacent file formats.
 *
 * The layout is:
 *
 * | Offset | Size | Field            | Description                               |
 * |-------:|-----:|:-----------------|:------------------------------------------|
 * |      0 |   12 | Universal Label  | Fixed SMPTE 330M prefix.                  |
 * |     12 |    1 | Length           | 0x13 for Basic, 0x33 for Extended.         |
 * |     13 |    3 | Instance Number  | 0 for the root instance.                  |
 * |     16 |   16 | Material Number  | 16 random bytes uniquely identifying the material. |
 * |     32 |    8 | Time/Date        | (Extended only) wall-clock time of creation. |
 * |     40 |   12 | Spatial Coords   | (Extended only) GPS or other spatial info; typically zero. |
 * |     52 |    4 | Country          | (Extended only) ISO 3166 country code; typically zero. |
 * |     56 |    4 | Organization     | (Extended only) organization identifier; libpromeki writes `"MEKI"`. |
 * |     60 |    4 | User             | (Extended only) user identifier; typically zero. |
 *
 * The @c "MEKI" organization tag in the Extended source pack
 * provides a durable libpromeki signature on every file the
 * library writes, independent of any caller-set Software or
 * Originator metadata.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance is
 * conditionally thread-safe: const operations may be called from
 * multiple threads, but any mutation must be externally synchronized.
 *
 * @par Example
 * @code
 * UMID id = UMID::generate();            // Extended UMID with random material.
 * String str = id.toString();            // 64-byte uppercase hex representation.
 * UMID parsed = UMID::fromString(str);   // Round-trip.
 * bool valid = id.isValid();
 * @endcode
 */
class UMID {
        public:
                /** @brief UMID form — Basic (32 bytes) or Extended (64 bytes). */
                enum Length {
                        Invalid = 0,  ///< @brief Not a valid UMID.
                        Basic = 32,   ///< @brief SMPTE 330M Basic UMID.
                        Extended = 64 ///< @brief SMPTE 330M Extended UMID with source pack.
                };

                /** @brief Size of the Basic UMID in bytes. */
                static constexpr size_t BasicSize = 32;

                /** @brief Size of the Extended UMID in bytes. */
                static constexpr size_t ExtendedSize = 64;

                /** @brief Size of the fixed Universal Label prefix in bytes. */
                static constexpr size_t UniversalLabelSize = 12;

                /** @brief Raw storage type — always sized for an Extended UMID. */
                using DataFormat = Array<uint8_t, ExtendedSize>;

                /**
                 * @brief Generates a fresh UMID with a random Material Number.
                 *
                 * For an Extended UMID the Source Pack is also populated:
                 * the Time/Date field is set from the current wall-clock
                 * time (UTC) and the Organization field is set to the
                 * four ASCII bytes @c "MEKI" as a libpromeki signature.
                 * Spatial, Country, and User fields are left zeroed.
                 *
                 * @param len Desired form (Basic or Extended).
                 * @return A valid UMID, or an invalid UMID if random
                 *         generation fails.
                 */
                static UMID generate(Length len = Extended);

                /**
                 * @brief Parses a UMID from its string representation.
                 *
                 * The string is expected to be hex-encoded, with an even
                 * number of characters equal to twice the UMID byte
                 * length (64 hex chars for Basic, 128 for Extended).
                 * Leading or trailing whitespace and any internal dashes
                 * are ignored, so both @c "0123..." and @c "01-23-..." are
                 * accepted.  Parsing is case-insensitive.
                 *
                 * @param str The hex string to parse.
                 * @param err Optional error output; set to
                 *            @c Error::Invalid on failure.
                 * @return The parsed UMID, or an invalid UMID on failure.
                 */
                static UMID fromString(const String &str, Error *err = nullptr);

                /**
                 * @brief Constructs a UMID from a raw byte buffer.
                 *
                 * @p byteLen must be either @c BasicSize (32) or
                 * @c ExtendedSize (64).  Any other size yields an
                 * invalid UMID.  For Basic input the trailing 32 bytes
                 * of the internal 64-byte storage are zeroed.
                 *
                 * @param bytes   Pointer to at least @p byteLen bytes.
                 * @param byteLen Either 32 (Basic) or 64 (Extended).
                 * @return The constructed UMID, or an invalid UMID on
                 *         size mismatch.
                 */
                static UMID fromBytes(const uint8_t *bytes, size_t byteLen);

                /** @brief Constructs an invalid (zero-length) UMID. */
                UMID() : d{}, _length(Invalid) {}

                /** @brief Copy constructor. */
                UMID(const UMID &other) : d(other.d), _length(other._length) {}

                /** @brief Copy assignment operator. */
                UMID &operator=(const UMID &other) {
                        d = other.d;
                        _length = other._length;
                        return *this;
                }

                /**
                 * @brief Returns true if this UMID is valid.
                 * @return True when @c length() is not @c Invalid.
                 */
                bool isValid() const { return _length != Invalid; }

                /**
                 * @brief Returns the UMID form.
                 * @return @c Basic, @c Extended, or @c Invalid.
                 */
                Length length() const { return _length; }

                /**
                 * @brief Returns the UMID size in bytes.
                 * @return 32 for Basic, 64 for Extended, 0 for Invalid.
                 */
                size_t byteSize() const { return static_cast<size_t>(_length); }

                /** @brief Returns true if this UMID uses the Extended form. */
                bool isExtended() const { return _length == Extended; }

                /**
                 * @brief Returns the standard hex string representation.
                 *
                 * The returned string contains @c 2 * byteSize() lowercase
                 * hex digits with no separators.
                 *
                 * @return A String holding the hex representation, or an
                 *         empty String if the UMID is invalid.
                 */
                String toString() const;

                /** @brief Implicit conversion to String via toString(). */
                operator String() const { return toString(); }

                /**
                 * @brief Returns a const reference to the raw 64-byte storage.
                 *
                 * Only the first @c byteSize() bytes are meaningful; the
                 * tail is zero-padded for Basic UMIDs.
                 *
                 * @return The underlying DataFormat array.
                 */
                const DataFormat &data() const { return d; }

                /**
                 * @brief Returns a pointer to the raw UMID bytes.
                 * @return A pointer to the first byte, or @c nullptr
                 *         semantics — always valid, but length is given
                 *         by @c byteSize().
                 */
                const uint8_t *raw() const { return d.data(); }

                /** @brief Returns true if both UMIDs have the same length and bytes. */
                bool operator==(const UMID &other) const {
                        if (_length != other._length) return false;
                        return std::memcmp(d.data(), other.d.data(), static_cast<size_t>(_length)) == 0;
                }

                /** @brief Returns true if the UMIDs differ. */
                bool operator!=(const UMID &other) const { return !(*this == other); }

                /** @brief Less-than comparison for ordering (length first, then bytes). */
                bool operator<(const UMID &other) const {
                        if (_length != other._length) return _length < other._length;
                        return std::memcmp(d.data(), other.d.data(), static_cast<size_t>(_length)) < 0;
                }

                /** @brief Greater-than comparison for ordering. */
                bool operator>(const UMID &other) const { return other < *this; }

                /** @brief Less-than-or-equal comparison. */
                bool operator<=(const UMID &other) const { return !(other < *this); }

                /** @brief Greater-than-or-equal comparison. */
                bool operator>=(const UMID &other) const { return !(*this < other); }

        private:
                DataFormat d;
                Length     _length;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::UMID);
