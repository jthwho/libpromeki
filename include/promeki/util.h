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


