/**
 * @file      core/fourcc.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once
#include <cstdint>
#include <string>
#include <promeki/core/namespace.h>
#include <promeki/core/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A four-character code (FourCC) identifier.
 * @ingroup core_util
 *
 * Stores four ASCII characters packed into a 32-bit integer in
 * big-endian order. Commonly used to identify codecs, pixel formats,
 * and container formats in multimedia applications. All operations
 * are constexpr and suitable for use as compile-time constants.
 */
class FourCC {
        public:
                /**
                 * @brief Constructs a FourCC from four individual characters.
                 * @param c0 First (most significant) character.
                 * @param c1 Second character.
                 * @param c2 Third character.
                 * @param c3 Fourth (least significant) character.
                 */
                constexpr FourCC(char c0, char c1, char c2, char c3)
                        : d((static_cast<uint32_t>(c0) << 24) |
                            (static_cast<uint32_t>(c1) << 16) |
                            (static_cast<uint32_t>(c2) << 8)  |
                             static_cast<uint32_t>(c3)) {}

                /**
                 * @brief Constructs a FourCC from a string literal.
                 * @tparam N The size of the string literal (must be 5, including null terminator).
                 * @param str A 4-character string literal (e.g. "RIFF").
                 */
                template <size_t N> constexpr FourCC(const char (&str)[N])
                        : FourCC(str[0], str[1], str[2], str[3]) {
                                static_assert(N == 5, "FourCC string must have exactly 4 characters");
                        }

                /**
                 * @brief Returns the packed 32-bit integer value.
                 * @return The FourCC as a uint32_t in big-endian byte order.
                 */
                constexpr uint32_t value() const { return d; }

                /** @brief Returns true if both FourCC values are equal. */
                friend constexpr bool operator==(const FourCC &a, const FourCC &b) {
                        return a.d == b.d;
                }

                /** @brief Returns true if the FourCC values are not equal. */
                friend constexpr bool operator!=(const FourCC &a, const FourCC &b) {
                        return a.d != b.d;
                }

                /** @brief Less-than comparison by numeric value. */
                friend constexpr bool operator<(const FourCC &a, const FourCC &b) {
                        return a.d < b.d;
                }

                /** @brief Greater-than comparison by numeric value. */
                friend constexpr bool operator>(const FourCC &a, const FourCC &b) {
                        return a.d > b.d;
                }

                /** @brief Less-than-or-equal comparison by numeric value. */
                friend constexpr bool operator<=(const FourCC &a, const FourCC &b) {
                        return a.d <= b.d;
                }

                /** @brief Greater-than-or-equal comparison by numeric value. */
                friend constexpr bool operator>=(const FourCC &a, const FourCC &b) {
                        return a.d >= b.d;
                }

        private:
                uint32_t d; ///< The packed 32-bit FourCC value.
};

/** @brief A list of FourCC values. */
using FourCCList = List<FourCC>;

PROMEKI_NAMESPACE_END

