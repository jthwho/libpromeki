/**
 * @file      httpmethod.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/enum.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Well-known Enum type for HTTP request methods.
 * @ingroup network
 *
 * Covers the nine methods defined by RFC 9110.  The integer values are
 * arbitrary internal IDs — the wire-format method is the registered
 * string name (e.g. @c "GET", @c "POST"), available via
 * @ref Enum::valueName and round-trippable via the @c TypedEnum
 * string constructor.
 *
 * @par Example
 * @code
 * HttpMethod m = HttpMethod::Post;
 * String onWire = m.valueName();      // "POST"
 * HttpMethod parsed{ String("PUT") }; // HttpMethod::Put
 * @endcode
 *
 * Default value is @ref Get — the most common request method, matching
 * the convention used elsewhere in the library for sensible defaults.
 */
class HttpMethod : public TypedEnum<HttpMethod> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("HttpMethod", 0, {"GET", 0}, {"HEAD", 1}, {"POST", 2}, {"PUT", 3},
                                           {"DELETE", 4}, {"PATCH", 5}, {"OPTIONS", 6}, {"CONNECT", 7}, {"TRACE", 8});

                using TypedEnum<HttpMethod>::TypedEnum;

                static const HttpMethod Get;
                static const HttpMethod Head;
                static const HttpMethod Post;
                static const HttpMethod Put;
                static const HttpMethod Delete;
                static const HttpMethod Patch;
                static const HttpMethod Options;
                static const HttpMethod Connect;
                static const HttpMethod Trace;

                /**
                 * @brief Returns the canonical wire-form method name.
                 *
                 * Convenience wrapper around @c valueName() that returns
                 * the upper-case ASCII string sent on the request line
                 * (@c "GET", @c "POST", ...).  An invalid HttpMethod
                 * returns an empty string.
                 */
                String wireName() const;

                /**
                 * @brief Whether the method has a meaningful request body.
                 *
                 * True for @ref Post, @ref Put, and @ref Patch; false for
                 * the rest.  Used by @ref HttpConnection to decide
                 * whether to wait for a body even when no
                 * @c Content-Length header is present.
                 */
                bool allowsBody() const;
};

inline const HttpMethod HttpMethod::Get{0};
inline const HttpMethod HttpMethod::Head{1};
inline const HttpMethod HttpMethod::Post{2};
inline const HttpMethod HttpMethod::Put{3};
inline const HttpMethod HttpMethod::Delete{4};
inline const HttpMethod HttpMethod::Patch{5};
inline const HttpMethod HttpMethod::Options{6};
inline const HttpMethod HttpMethod::Connect{7};
inline const HttpMethod HttpMethod::Trace{8};

PROMEKI_NAMESPACE_END
