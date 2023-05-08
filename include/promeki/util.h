/*****************************************************************************
 * util.h
 * April 26, 2023
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

#include <array>
#include <cstdint>

namespace promeki {

class Error;

#if defined(_WIN32)
#define PROMEKI_PLATFORM_WINDOWS 32
#define PROMEKI_PLATFORM "Win32"
#elif defined(_WIN64)
#define PROMEKI_PLATFORM_WINDOWS 64
#define PROMEKI_PLATFORM "Win64"
#elif defined(__APPLE__)
#define PROMEKI_PLATFORM_APPLE 1
#define PROMEKI_PLATFORM "MacOS"
#elif defined(__linux__)
#define PROMEKI_PLATFORM_LINUX 1
#define PROMEKI_PLATFORM "Linux"
#else
#define PROMEKI_PLATFORM_UNKNOWN 1
#define PROMEKI_PLATFORM "Unknown"
#endif


// Macro string conversion and concatination
#define PROMEKI_STRINGIFY_IMPL(value) #value
#define PROMEKI_STRINGIFY(value) PROMEKI_STRINGIFY_IMPL(value)
#define PROMEKI_CONCAT_IMPL(v1, v2) v1##v2
#define PROMEKI_CONCAT(v1, v2) PROMEKI_CONCAT_IMPL(v1, v2)

// Returns a unique ID that can be used to make unique macro items
#define PROMEKI_UNIQUE_ID PROMEKI_CONCAT(__LINE__, __COUNTER__)

// Returns the number of elements in a statically defined simple array.
#define PROMEKI_ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

// Some useful alignment macros
#define PROMEKI_ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

// Use the PROMEKI_PRINTF_FUNC to hint to the compiler a particular variadic function
// uses printf() symantics.  This will allow the compiler to check that the variable
// arguments match up with the format.
// The m argument should be the index that contains the format
// The n argument should be the index of the start of the variadic
// Index starts at 1
#if defined(__GNUC__) || defined(__clang__)
#       define PROMEKI_PRINTF_FUNC(m, n) __attribute__((format(printf, m, n)))
#elif defined(_MSC_VER)
#       define PROMEKI_PRINTF_FUNC(m, n) _Printf_format_string_
#else
#       define PROMEKI_PRINTF_FUNC(m, n)
#endif

// If you'd like the compiler to implement the structure as 
// a packed data structure (i.e. no platform alignment)
// surround the declaration with PROMEKI_PACKED_STRUCT_BEGIN 
// and PROMEKI_PACKED_STRUCT_END
//
// Ex:
//
// PROMEKI_PACKED_STRUCT_BEGIN
// struct MyStruct {
//    int8_t value1;
//    int32_t value2;
//    int16_t value3;
// }
// PROMEKI_PACKED_STRUCT_END
//
// You can validate the packed-ness of the structure as it should
// now show a sizeof(MyStruct) == to the exact size of all the data
// in the structure.
#if defined(__GNUC__) || defined(__clang__)
#       define PROMEKI_PACKED_STRUCT_BEGIN
#       define PROMEKI_PACKED_STRUCT_END __attribute__((packed))
#elif defined(_MSC_VER)
#       define PROMEKI_PACKED_STRUCT_BEGIN __pragma(pack(push, 1)) 
#       define PROMEKI_PACKED_STRUCT_END __pragma(pack(pop))
#else
#       define PROMEKI_PACKED_STRUCT_BEGIN
#       define PROMEKI_PACKED_STRUCT_END
#endif

template<typename T>
inline T promekiLerp(const T& a, const T& b, const double& t) {
    return a + t * (b - a);
}

template<typename T>
T promekiCatmullRom(const std::array<T, 4>& points, T t) {
    T t2 = t * t;
    T t3 = t * t2;
    T c1 = -0.5 * points[0] + 1.5 * points[1] - 1.5 * points[2] + 0.5 * points[3];
    T c2 = points[0] - 2.5 * points[1] + 2 * points[2] - 0.5 * points[3];
    T c3 = -0.5 * points[0] + 0.5 * points[2];
    T c4 = points[1];
    return c1 * t3 + c2 * t2 + c3 * t + c4;
}

template<typename T>
T promekiBezier(const std::array<T, 4>& points, T t) {
    T u = 1 - t;
    T t2 = t * t;
    T u2 = u * u;
    T t3 = t2 * t;
    T u3 = u2 * u;
    T b0 = u3;
    T b1 = 3 * u2 * t;
    T b2 = 3 * u * t2;
    T b3 = t3;
    return b0 * points[0] + b1 * points[1] + b2 * points[2] + b3 * points[3];
}

template<typename T>
T promekiBicubic(const std::array<std::array<T, 4>, 4>& points, T x, T y) {
    std::array<T, 4> arr;
    for (int i = 0; i < 4; ++i) {
        std::array<T, 4> row;
        for (int j = 0; j < 4; ++j) {
            row[j] = points[i][j];
        }
        arr[i] = cubic_lerp(row, y);
    }
    return cubic_lerp(arr, x);
}

template<typename T>
T promekiCubic(const std::array<T, 4>& points, T t) {
    T a = points[3] - points[2] - points[0] + points[1];
    T b = points[0] - points[1] - a;
    T c = points[2] - points[0];
    T d = points[1];
    return a * t * t * t + b * t * t + c * t + d;
}

// Writes bytes worth of random data to buf
Error promekiRand(uint8_t *buf, size_t bytes);

} // namespace promeki

