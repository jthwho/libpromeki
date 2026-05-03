/**
 * @file      util.h
 * @copyright Howard Logic. All rights reserved.
 * @ingroup util
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <array>
#include <stdexcept>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <promeki/namespace.h>
#include <promeki/platform.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class StringList;

// Runtime time assert
#define PROMEKI_ASSERT(x)                                                                                              \
        if (!(x)) {                                                                                                    \
                promekiErr("Assertion failed: " PROMEKI_STRINGIFY(x));                                                 \
                promekiLogStackTrace(Logger::Err);                                                                     \
                promekiLogSync();                                                                                      \
                throw std::runtime_error(__FILE__                                                                      \
                                         ":" PROMEKI_STRINGIFY(__LINE__) " Assertion failed: " PROMEKI_STRINGIFY(x));  \
        }

// Compile time assert
#define PROMEKI_STATIC_ASSERT(x)                                                                                       \
        static_assert(x, __LINE__ ":" PROMEKI_STRINGIFY(__LINE__) " Assertion failed: " PROMEKI_STRINGIFY(x));

// Macro string conversion and concatination
#define PROMEKI_STRINGIFY_ARGS(...) #__VA_ARGS__
#define PROMEKI_STRINGIFY_IMPL(value) #value
#define PROMEKI_STRINGIFY(value) PROMEKI_STRINGIFY_IMPL(value)
#define PROMEKI_CONCAT_IMPL(v1, v2) v1##v2
#define PROMEKI_CONCAT(v1, v2) PROMEKI_CONCAT_IMPL(v1, v2)

// Returns a unique ID that can be used to make unique macro items
#define PROMEKI_UNIQUE_ID PROMEKI_CONCAT(__LINE__, __COUNTER__)

// Returns the number of elements in a statically defined simple array.
#define PROMEKI_ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

// Marks a heap pointer as "intentionally leaked" so leak detectors
// (AddressSanitizer / LeakSanitizer) stop reporting it.  Use at every
// allocation site whose contents legitimately live for the process
// lifetime — registered Definitions, StringLiteralData caches, etc.
//
// Under sanitizer builds this delegates to __lsan_ignore_object() (a
// one-line registry entry, no runtime cost at the allocation site
// beyond the call).  In all other builds it evaluates its argument and
// discards the result, so the macro is safe to wrap any expression.
// Valgrind does not offer a runtime "intentional leak" API — ship a
// suppression file (tools/valgrind.supp) for that tool instead.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PROMEKI_ADDRESS_SANITIZER_ENABLED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define PROMEKI_ADDRESS_SANITIZER_ENABLED 1
#endif
#if defined(PROMEKI_ADDRESS_SANITIZER_ENABLED) && __has_include(<sanitizer/lsan_interface.h>)
#include <sanitizer/lsan_interface.h>
#define PROMEKI_INTENTIONAL_LEAK(ptr) __lsan_ignore_object(ptr)
#else
#define PROMEKI_INTENTIONAL_LEAK(ptr) ((void)(ptr))
#endif

// Some useful alignment macros
#define PROMEKI_ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

// Use the PROMEKI_PRINTF_FUNC to hint to the compiler a particular variadic function
// uses printf() symantics.  This will allow the compiler to check that the variable
// arguments match up with the format.
// The m argument should be the index that contains the format
// The n argument should be the index of the start of the variadic
// Index starts at 1
#if defined(PROMEKI_COMPILER_GCC_COMPAT)
#define PROMEKI_PRINTF_FUNC(m, n) __attribute__((format(printf, m, n)))
#elif defined(PROMEKI_COMPILER_MSVC)
#define PROMEKI_PRINTF_FUNC(m, n) _Printf_format_string_
#else
#define PROMEKI_PRINTF_FUNC(m, n)
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
#if defined(PROMEKI_COMPILER_GCC_COMPAT)
#define PROMEKI_PACKED_STRUCT_BEGIN
#define PROMEKI_PACKED_STRUCT_END __attribute__((packed))
#elif defined(PROMEKI_COMPILER_MSVC)
#define PROMEKI_PACKED_STRUCT_BEGIN __pragma(pack(push, 1))
#define PROMEKI_PACKED_STRUCT_END __pragma(pack(pop))
#else
#define PROMEKI_PACKED_STRUCT_BEGIN
#define PROMEKI_PACKED_STRUCT_END
#endif

StringList promekiStackTrace(bool demangle = true);

template <typename OutputType, typename InputType>
OutputType promekiConvert(const InputType &input, Error *err = nullptr) {
        static_assert(std::is_integral<InputType>::value || std::is_floating_point<InputType>::value,
                      "InputType must be an integer or floating point type");
        static_assert(std::is_integral<OutputType>::value || std::is_floating_point<OutputType>::value,
                      "OutputType must be an integer or floating point type");
        // For mixed signed/unsigned integer comparisons, the operands
        // would otherwise undergo C++'s usual arithmetic conversions and
        // produce a false "out of range" verdict (e.g. a uint64_t input
        // of 1 compared against int64_t::lowest() gets the negative
        // bound silently widened to a huge unsigned value, so 1 looks
        // smaller).  std::cmp_less / std::cmp_greater are the C++20
        // sign-correct comparison primitives that route around this.
        // bool is excluded from this path because std::cmp_* is
        // constrained to "standard integer" types and rejects bool with
        // a static_assert.
        if constexpr (std::is_integral_v<InputType> && std::is_integral_v<OutputType> &&
                      !std::is_same_v<InputType, bool> && !std::is_same_v<OutputType, bool>) {
                if (std::cmp_greater(input, std::numeric_limits<OutputType>::max()) ||
                    std::cmp_less(input, std::numeric_limits<OutputType>::lowest())) {
                        if (err != nullptr) *err = Error::Invalid;
                        return OutputType();
                }
        } else {
                if (input > std::numeric_limits<OutputType>::max() ||
                    input < std::numeric_limits<OutputType>::lowest()) {
                        if (err != nullptr) *err = Error::Invalid;
                        return OutputType();
                }
        }
        if (err != nullptr) *err = Error::Ok;
        return static_cast<OutputType>(input);
}

template <typename T> inline T promekiLerp(const T &a, const T &b, const double &t) {
        return a + t * (b - a);
}

template <typename T> T promekiCatmullRom(const std::array<T, 4> &points, T t) {
        T t2 = t * t;
        T t3 = t * t2;
        T c1 = -0.5 * points[0] + 1.5 * points[1] - 1.5 * points[2] + 0.5 * points[3];
        T c2 = points[0] - 2.5 * points[1] + 2 * points[2] - 0.5 * points[3];
        T c3 = -0.5 * points[0] + 0.5 * points[2];
        T c4 = points[1];
        return c1 * t3 + c2 * t2 + c3 * t + c4;
}

template <typename T> T promekiBezier(const std::array<T, 4> &points, T t) {
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

template <typename T> T promekiBicubic(const std::array<std::array<T, 4>, 4> &points, T x, T y) {
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

template <typename T> T promekiCubic(const std::array<T, 4> &points, T t) {
        T a = points[3] - points[2] - points[0] + points[1];
        T b = points[0] - points[1] - a;
        T c = points[2] - points[0];
        T d = points[1];
        return a * t * t * t + b * t * t + c * t + d;
}

PROMEKI_NAMESPACE_END
