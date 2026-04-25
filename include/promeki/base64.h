/**
 * @file      base64.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/buffer.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief RFC 4648 Base64 encode / decode helpers.
 * @ingroup util
 *
 * Standard Base64 alphabet (@c "+/", padding with @c '='): the
 * encoder always pads, the decoder accepts both padded and
 * unpadded input.  All overloads operate on raw byte ranges and
 * @ref Buffer / @ref String containers — no allocation surprises.
 */
class Base64 {
        public:
                /**
                 * @brief Encodes @p len bytes from @p data as a Base64 string.
                 *
                 * Output is padded to a multiple of four characters
                 * with @c '='.  Suitable for HTTP headers, JSON, and
                 * any other text protocol where the standard alphabet
                 * is expected.
                 */
                static String encode(const void *data, size_t len);

                /** @brief Convenience: encode a @ref Buffer. */
                static String encode(const Buffer &buf);

                /**
                 * @brief Decodes a Base64 string into a @ref Buffer.
                 *
                 * Whitespace inside @p text is ignored so callers can
                 * feed PEM-style line-wrapped data directly.  Padding
                 * is optional.
                 *
                 * @param text Input string.
                 * @param err  Optional output set to @ref Error::Invalid
                 *             if the input contains characters outside
                 *             the Base64 alphabet (excluding whitespace
                 *             and padding).
                 * @return The decoded byte buffer, or an empty buffer
                 *         on failure.
                 */
                static Buffer decode(const String &text, Error *err = nullptr);
};

PROMEKI_NAMESPACE_END
