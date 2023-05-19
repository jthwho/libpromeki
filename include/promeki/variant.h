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
#include <promeki/util.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/datetime.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/uuid.h>
#include <promeki/timecode.h>
#include <promeki/rational.h>

PROMEKI_NAMESPACE_BEGIN

#define PROMEKI_VARIANT_TYPES           \
        X(TypeInvalid, std::monostate)  \
        X(TypeBool, bool)               \
        X(TypeU8, uint8_t)              \
        X(TypeS8, int8_t)               \
        X(TypeU16, uint16_t)            \
        X(TypeS16, int16_t)             \
        X(TypeU32, uint32_t)            \
        X(TypeS32, int32_t)             \
        X(TypeU64, uint64_t)            \
        X(TypeS64, int64_t)             \
        X(TypeFloat, float)             \
        X(TypeDouble, double)           \
        X(TypeString, String)           \
        X(TypeDateTime, DateTime)       \
        X(TypeTimeStamp, TimeStamp)     \
        X(TypeSize2D, Size2D)           \
        X(TypeUUID, UUID)               \
        X(TypeTimecode, Timecode)       \
        X(TypeRational, Rational<int>)

class VariantDummy {
        public:

};

// Note, this template is never intended to be used directly, just to provide the template
// to be used by Variant
template <typename... Types> class __Variant {
        public:
                // Note: the enum list must be in the same order as the Variant type arguments.
                #define X(name, type) name,
                enum Type { PROMEKI_VARIANT_TYPES };
                #undef X

                #define X(name, type) PROMEKI_STRINGIFY(type),
                static const char *typeName(size_t id) {
                        static const char *items[] = { PROMEKI_VARIANT_TYPES };
                        return items[id];
                }
                #undef X

                __Variant() = default;
                template <typename T> __Variant(const T& value) : v(value) { }

                bool isValid() const {
                        return v.index() != 0;
                }

                template <typename T> void set(const T &value) { v = value; }
                
                template <typename To> To get(bool *ok = nullptr) const {
                        return std::visit([ok](auto &&arg) -> To {
                                using From = std::decay_t<decltype(arg)>;
                                if(ok != nullptr) *ok = true;
                                if constexpr (std::is_same_v<From, To>) {
                                        return arg;

                                } else if constexpr (std::is_same_v<To, bool>) {
                                        if constexpr (std::is_integral<From>::value || 
                                                std::is_floating_point<From>::value) return arg ? true : false;
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);
                                
                                } else if constexpr (std::is_same_v<To, int8_t>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value || 
                                                std::is_floating_point<From>::value) return promekiConvert<To>(arg, ok);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);

                                } else if constexpr (std::is_same_v<To, uint8_t>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value || 
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, ok);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);

                                } else if constexpr (std::is_same_v<To, int16_t>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value || 
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, ok);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);

                                } else if constexpr (std::is_same_v<To, uint16_t>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value || 
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, ok);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);

                                } else if constexpr (std::is_same_v<To, int32_t>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value || 
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, ok);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);

                                } else if constexpr (std::is_same_v<To, uint32_t>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value || 
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, ok);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);

                                } else if constexpr (std::is_same_v<To, int64_t>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value || 
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, ok);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);

                                } else if constexpr (std::is_same_v<To, uint64_t>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value || 
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, ok);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);

                                } else if constexpr (std::is_same_v<To, float>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value || 
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, ok);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);
                                        if constexpr (std::is_same_v<From, Rational<int>>) return arg.toDouble();

                                } else if constexpr (std::is_same_v<To, double>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value || 
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, ok);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(ok);
                                        if constexpr (std::is_same_v<From, Rational<int>>) return arg.toDouble();

                                } else if constexpr (std::is_same_v<To, DateTime>) {
                                        if constexpr (std::is_same_v<From, String>) return DateTime::fromString(
                                                        arg, DateTime::DefaultFormat, ok);

                                } else if constexpr (std::is_same_v<To, UUID>) {
                                        if constexpr (std::is_same_v<From, String>) {
                                                Error err;
                                                UUID ret = UUID::fromString(arg, &err);
                                                if(err.isError()) {
                                                        if(ok != nullptr) *ok = false;
                                                        return UUID();
                                                }
                                                return ret;
                                        }

                                } else if constexpr (std::is_same_v<To, Timecode>) {
                                        if constexpr (std::is_same_v<From, String>) {
                                                std::pair<Timecode, Error> ret = Timecode::fromString(arg);
                                                if(ret.second.isError()) {
                                                        if(ok != nullptr) *ok = false;
                                                        return Timecode();
                                                }
                                                return ret.first;
                                        }


                                } else if constexpr (std::is_same_v<To, String>) {
                                        if constexpr (std::is_same_v<From, bool>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, int8_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, uint8_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, int16_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, uint16_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, int32_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, uint32_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, int64_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, uint64_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, float>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, double>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, DateTime>) return arg.toString();
                                        if constexpr (std::is_same_v<From, TimeStamp>) return arg.toString();
                                        if constexpr (std::is_same_v<From, Size2D>) return arg.toString();
                                        if constexpr (std::is_same_v<From, UUID>) return arg.toString();
                                        if constexpr (std::is_same_v<From, Timecode>) return arg.toString().first;
                                        if constexpr (std::is_same_v<From, Rational<int>>) return arg.toString();

                                }
                                if(ok != nullptr) *ok = false;
                                return To{};
                        }, v);
                }

                Type type() const {
                        return static_cast<Type>(v.index());
                }

                const char *typeName() const {
                        return typeName(type());
                }

        private:
                std::variant<Types...> v;
};

#define X(name, type) type,
using Variant = __Variant< PROMEKI_VARIANT_TYPES VariantDummy >;
#undef X

PROMEKI_NAMESPACE_END

