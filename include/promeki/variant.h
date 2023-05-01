/*****************************************************************************
 * variant.h
 * April 30, 2023
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

#include <variant>
#include <cstdint>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/datetime.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>

namespace promeki {

// Note, this template is never intended to be used directly, just to provide the template
// to be used by Variant
template <typename... Types> class __Variant {
        public:
                // Note: the enum list must be in the same order as the Variant type arguments.
                enum Type {
                        TypeInvalid = 0,
                        TypeU8,
                        TypeS8,
                        TypeU16,
                        TypeS16,
                        TypeU32,
                        TypeS32,
                        TypeU64,
                        TypeS64,
                        TypeFloat,
                        TypeDouble,
                        TypeString,
                        TypeDateTime,
                        TypeTimeStamp,
                        TypePixelFormat,
                        TypeSize2D
                };

                __Variant() = default;
                template <typename T> __Variant(const T& value) : v(value) { }

                bool isValid() const {
                        return v.index() != 0;
                }

                template <typename T> void set(const T& value) { v = value; }
                template <typename T> T get() const { return v; }


                template <typename T> T get(bool *ok = nullptr) const {
                        if(std::holds_alternative<T>(v)) {
                                if(ok != nullptr) *ok = true;
                                return std::get<T>(v);
                        } else {
                                return std::visit([ok](const auto &value) -> T {
                                        if constexpr (std::is_convertible_v<decltype(value), T>) {
                                                if(ok != nullptr) *ok = true;
                                                return static_cast<T>(value);
                                        } 
                                }, v);
                        }
                        if(ok != nullptr) *ok = true;
                        return T();
                }

                Type type() const {
                        return static_cast<Type>(v.index());
                }

                template <typename T> bool canConvertTo() const {
                        return std::visit([](const auto &value) -> bool {
                                return std::is_convertible_v<decltype(value), T>; 
                        }, v);
                }

        private:
                std::variant<Types...> v;
};

using Variant = __Variant<
        std::monostate, uint8_t, int8_t, uint16_t, int16_t,
        uint32_t, int32_t, uint64_t, int64_t, float, double, 
        String, DateTime, TimeStamp, PixelFormat, Size2D>;

} // namespace promeki
