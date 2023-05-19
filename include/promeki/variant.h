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
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/datetime.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/uuid.h>
#include <promeki/timecode.h>
#include <promeki/rational.h>

PROMEKI_NAMESPACE_BEGIN

class StringVisitor {
        public:

        String operator()(const std::monostate &) {
                return String();
        }
        
        String operator()(const bool &val) const {
                return String::number(val);
        }
        
        String operator()(const uint8_t &val) const {
                return String::number(val);
        }

        String operator()(const int8_t &val) const {
                return String::number(val);
        }

        String operator()(const uint16_t &val) const {
                return String::number(val);
        }

        String operator()(const int16_t &val) const {
                return String::number(val);
        }

        String operator()(const uint32_t &val) const {
                return String::number(val);
        }

        String operator()(const int32_t &val) const {
                return String::number(val);
        }

        String operator()(const uint64_t &val) const {
                return String::number(val);
        }

        String operator()(const int64_t &val) const {
                return String::number(val);
        }

        String operator()(const float &val) const {
                return String::number(val);
        }

        String operator()(const double &val) const {
                return String::number(val);
        }
        
        String operator()(const String &val) const {
                return val;
        }

        String operator()(const DateTime &val) const {
                return val.toString();
        }

        String operator()(const TimeStamp &val) const {
                return val.toString();
        }

        String operator()(const Size2D &val) const {
                return val.toString();
        }

        String operator()(const UUID &val) const {
                return val.toString();
        }

        String operator()(const Timecode &val) const {
                return val.toString().first;
        }

        String operator()(const Rational<int> &val) const {
                return val.toString();
        }

};

// Note, this template is never intended to be used directly, just to provide the template
// to be used by Variant
template <typename... Types> class __Variant {
        public:
                // Note: the enum list must be in the same order as the Variant type arguments.
                enum Type {
                        TypeInvalid = 0,
                        TypeBool,
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
                        TypeSize2D,
                        TypeUUID,
                        TypeTimecode,
                        TypeRational
                };

                __Variant() = default;
                template <typename T> __Variant(const T& value) : v(value) { }

                bool isValid() const {
                        return v.index() != 0;
                }

                template <typename T> void set(const T &value) { v = value; }
                template <typename T> T get(bool *ok = nullptr) {
                        T ret;
                        try {
                                ret = std::get<T>(v);
                                if(ok != nullptr) *ok = true;
                        } catch(std::bad_variant_access const &) {
                                ret = T();
                                if(ok != nullptr) *ok = false;
                        }
                        return ret;
                }

                Type type() const {
                        return static_cast<Type>(v.index());
                }

                String toString() const {
                        return std::visit(StringVisitor{}, v);
                }

        private:
                std::variant<Types...> v;
};

using Variant = __Variant<
        std::monostate, bool, uint8_t, int8_t, uint16_t, int16_t,
        uint32_t, int32_t, uint64_t, int64_t, float, double, 
        String, DateTime, TimeStamp, Size2D, UUID, Timecode,
        Rational<int>>;

PROMEKI_NAMESPACE_END

