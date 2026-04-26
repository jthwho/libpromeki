/**
 * @file      bytearray.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Fixed-size byte array with hex string conversion.
 * @ingroup strings
 *
 * A specialization of fixed-size array for uint8_t data, providing
 * conversion to and from hexadecimal strings.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 *
 * @tparam NumBytes Number of bytes (fixed at compile time).
 */
template <size_t NumBytes> class ByteArray {
                PROMEKI_SHARED_FINAL(ByteArray)
        public:
                using Ptr = SharedPtr<ByteArray>;
                using DataType = std::array<uint8_t, NumBytes>;

                /** @brief Default constructor.  Value-initializes all bytes to zero. */
                ByteArray() : d{} {}

                /** @brief Constructs from a std::array. */
                ByteArray(const DataType &val) : d(val) {}

                /** @brief Move-constructs from a std::array. */
                ByteArray(DataType &&val) noexcept : d(std::move(val)) {}

                /** @brief Constructs from a raw byte pointer.  Copies NumBytes from src. */
                explicit ByteArray(const uint8_t *src) { std::memcpy(d.data(), src, NumBytes); }

                /** @brief Destructor. */
                ~ByteArray() {}

                /** @brief Returns the number of bytes. */
                constexpr size_t size() const { return NumBytes; }

                /** @brief Returns a mutable reference to the byte at index. */
                uint8_t &operator[](size_t index) { return d[index]; }

                /** @brief Returns a const reference to the byte at index. */
                const uint8_t &operator[](size_t index) const { return d[index]; }

                /** @brief Returns a mutable pointer to the underlying data. */
                uint8_t *data() { return d.data(); }

                /** @brief Returns a const pointer to the underlying data. */
                const uint8_t *data() const { return d.data(); }

                /** @brief Returns true if all bytes are zero. */
                bool isZero() const {
                        for (size_t i = 0; i < NumBytes; i++) {
                                if (d[i] != 0) return false;
                        }
                        return true;
                }

                /** @brief Returns true if both arrays contain identical bytes. */
                friend bool operator==(const ByteArray &lhs, const ByteArray &rhs) { return lhs.d == rhs.d; }

                /** @brief Returns true if the arrays differ. */
                friend bool operator!=(const ByteArray &lhs, const ByteArray &rhs) { return lhs.d != rhs.d; }

                /**
                 * @brief Converts the byte array to a lowercase hexadecimal string.
                 * @return A String of length NumBytes * 2.
                 */
                String toHexString() const {
                        static const char digits[] = "0123456789abcdef";
                        std::string       ret;
                        ret.reserve(NumBytes * 2);
                        for (size_t i = 0; i < NumBytes; i++) {
                                ret += digits[d[i] >> 4];
                                ret += digits[d[i] & 0x0F];
                        }
                        return ret;
                }

                /**
                 * @brief Constructs a ByteArray from a hexadecimal C string.
                 * @param str The hex string (must be exactly NumBytes * 2 characters).
                 * @param err Optional error output.
                 * @return The parsed ByteArray, or a zero ByteArray on failure.
                 */
                static ByteArray fromHexString(const char *str, Error *err = nullptr) {
                        ByteArray ret;
                        if (str == nullptr) {
                                if (err != nullptr) *err = Error::Invalid;
                                return ret;
                        }
                        for (size_t i = 0; i < NumBytes; i++) {
                                int hi = hexCharToVal(str[i * 2]);
                                int lo = hexCharToVal(str[i * 2 + 1]);
                                if (hi < 0 || lo < 0) {
                                        if (err != nullptr) *err = Error::Invalid;
                                        return ByteArray();
                                }
                                ret.d[i] = static_cast<uint8_t>((hi << 4) | lo);
                        }
                        if (err != nullptr) *err = Error::Ok;
                        return ret;
                }

                /**
                 * @brief Constructs a ByteArray from a hexadecimal String.
                 * @param str The hex string (must be exactly NumBytes * 2 characters).
                 * @param err Optional error output.
                 * @return The parsed ByteArray, or a zero ByteArray on failure.
                 */
                static ByteArray fromHexString(const String &str, Error *err = nullptr) {
                        return fromHexString(str.cstr(), err);
                }

        private:
                DataType d;

                static int hexCharToVal(char c) {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                }
};

PROMEKI_NAMESPACE_END
