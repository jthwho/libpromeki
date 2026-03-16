/**
 * @file      core/system.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <bit>
#include <limits>
#include <algorithm>
#include <type_traits>
#include <cstdint>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>

PROMEKI_NAMESPACE_BEGIN

class String;

/**
 * @brief Provides system-level utility functions.
 * @ingroup core_util
 *
 * Static utility class for querying host information, byte-order helpers,
 * and C++ symbol demangling.
 */
class System {
        public:
                /**
                 * @brief Returns the hostname of the machine.
                 * @return The hostname as a String.
                 */
                static String hostname();

                /**
                 * @brief Returns true if the host byte order is little-endian.
                 * @return true if little-endian, false otherwise.
                 */
                static constexpr bool isLittleEndian() {
                        return std::endian::native == std::endian::little;
                }

                /**
                 * @brief Returns true if the host byte order is big-endian.
                 * @return true if big-endian, false otherwise.
                 */
                static constexpr bool isBigEndian() {
                        return std::endian::native == std::endian::big;
                }

                /**
                 * @brief Reverses the byte order of an arithmetic value in place.
                 * @tparam T An arithmetic type (integer or floating-point).
                 * @param value The value whose bytes will be swapped.
                 */
                template<typename T> static void swapEndian(T &value) {
                        static_assert(std::is_arithmetic<T>::value, "swab() requires an arithmetic type");
                        constexpr size_t size = sizeof(T);
                        if constexpr (size == 1) return;
                        unsigned char *data = reinterpret_cast<unsigned char*>(&value);
                        #pragma unroll
                        for (size_t i = 0; i < size / 2; ++i) {
                                std::swap(data[i], data[size - i - 1]);
                        }
                        return;
                }

                /**
                 * @brief Converts a value to native endian if its byte order differs.
                 * @tparam T An arithmetic type.
                 * @tparam ValueIsBigEndian true if the value is stored in big-endian order.
                 * @param value The value to convert in place.
                 */
                template<typename T, bool ValueIsBigEndian> static void makeNativeEndian(T &value) {
                        if constexpr (ValueIsBigEndian && isBigEndian()) return;
                        swab(value);
                        return;
                }

                /**
                 * @brief Demangles a C++ symbol name into a human-readable form.
                 * @param symbol The mangled symbol name.
                 * @param useCache If true, cache the result for faster repeated lookups.
                 * @return The demangled symbol name as a String.
                 */
                static String demangleSymbol(const char *symbol, bool useCache = true);
};

PROMEKI_NAMESPACE_END

