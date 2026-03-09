/**
 * @file      fourcc.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once
#include <cstdint>
#include <iostream>
#include <string>
#include <promeki/namespace.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

class FourCC {
        public:
                constexpr FourCC(char c0, char c1, char c2, char c3)
                        : d((static_cast<uint32_t>(c0) << 24) |
                            (static_cast<uint32_t>(c1) << 16) |
                            (static_cast<uint32_t>(c2) << 8)  |
                             static_cast<uint32_t>(c3)) {}

                template <size_t N> constexpr FourCC(const char (&str)[N])
                        : FourCC(str[0], str[1], str[2], str[3]) {
                                static_assert(N == 5, "FourCC string must have exactly 4 characters");
                        }

                constexpr uint32_t value() const { return d; }

                friend constexpr bool operator==(const FourCC &a, const FourCC &b) {
                        return a.d == b.d;
                }

                friend constexpr bool operator!=(const FourCC &a, const FourCC &b) {
                        return a.d != b.d;
                }

                friend constexpr bool operator<(const FourCC &a, const FourCC &b) {
                        return a.d < b.d;
                }

                friend constexpr bool operator>(const FourCC &a, const FourCC &b) {
                        return a.d > b.d;
                }

                friend constexpr bool operator<=(const FourCC &a, const FourCC &b) {
                        return a.d <= b.d;
                }

                friend constexpr bool operator>=(const FourCC &a, const FourCC &b) {
                        return a.d >= b.d;
                }

        private:
                uint32_t d;
};

using FourCCList = List<FourCC>;

PROMEKI_NAMESPACE_END

