/*****************************************************************************
 * fourcc.h
 * May 15, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

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

