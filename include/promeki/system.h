/**
 * @file      system.h
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
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class String;

class System {
        public:
                static String hostname();

                static constexpr bool isLittleEndian() {
                        return std::endian::native == std::endian::little;
                }

                static constexpr bool isBigEndian() {
                        return std::endian::native == std::endian::big;
                }

                // Does an endian swap of the value
                template<typename T> static void swapEndian(T &value) {
                        static_assert(std::is_arithmetic<T>::value, "swab() requires an arithmetic type");
                        constexpr size_t size = sizeof(T);
                        if constexpr (size == 1) return;
                        unsigned char *data = reinterpret_cast<unsigned char*>(&value);
                        #pragma unroll
                        for (size_t i = 0; i < size / 2; ++i) {
                                std::swap(data[i], data[size - i - 1]);
                                std::swap(data[i], data[size - i - 1]);
                        }
                        return;
                }

                // Swaps the endian of the value if the value and the machine are different endian
                // directions.
                template<typename T, bool ValueIsBigEndian> static void makeNativeEndian(T &value) {
                        if constexpr (ValueIsBigEndian && isBigEndian()) return;
                        swab(value);
                        return;
                }

                static String demangleSymbol(const char *symbol, bool useCache = true);
};

PROMEKI_NAMESPACE_END

