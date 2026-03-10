/**
 * @file      uuid.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/array.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Universally Unique Identifier (UUID) version 4.
 *
 * Generates and manipulates RFC 4122 version 4 (random) UUIDs.
 * Supports creation, string conversion, comparison, and validity checks.
 */
class UUID {
        public:
                /** @brief Raw 16-byte storage format for a UUID. */
                using DataFormat = Array<uint8_t, 16>;

                /**
                 * @brief Generates a random version 4 UUID.
                 * @return A valid UUID, or an invalid (all-zero) UUID on failure.
                 */
                static UUID generate() {
                        DataFormat d;
                        Error err = promekiRand(d.data(), d.size());
                        if(err.isOk()) {
                                d[6] = (d[6] & 0x0F) | 0x40; // Set to verison 4
                                d[8] = (d[8] & 0x3F) | 0x80; // Set the variant to 2
                                return UUID(d);
                        }
                        return UUID();
                }

                /**
                 * @brief Parses a UUID from a string representation.
                 * @param string The UUID string (e.g. "550e8400-e29b-41d4-a716-446655440000").
                 * @param err    Optional error output.
                 * @return The parsed UUID, or an invalid UUID on failure.
                 */
                static UUID fromString(const char *string, Error *err = nullptr);

                /** @brief Constructs an invalid (all-zero) UUID. */
                UUID() : d{} { }

                /** @brief Copy constructor. */
                UUID(const UUID &u) : d(u.d) { }

                /** @brief Move constructor. */
                UUID(const UUID &&u) : d(std::move(u.d)) { }

                /** @brief Constructs a UUID from raw 16-byte data. */
                UUID(const DataFormat &val) : d(val) { }

                /** @brief Move-constructs a UUID from raw 16-byte data. */
                UUID(const DataFormat &&val) : d(std::move(val)) { }

                /** @brief Constructs a UUID by parsing a C-string. */
                explicit UUID(const char *str) : d(fromString(str).data()) { }

                /** @brief Constructs a UUID by parsing a String. */
                explicit UUID(const String &str) : d(fromString(str.cstr()).data()) { }

                /** @brief Copy assignment operator. */
                UUID &operator=(const UUID &val) {
                        d = val.d;
                        return *this;
                }

                /** @brief Move assignment operator. */
                UUID &operator=(const UUID &&val) {
                        d = std::move(val.d);
                        return *this;
                }

                /** @brief Assigns from raw 16-byte data. */
                UUID &operator=(const DataFormat &val) {
                        d = val;
                        return *this;
                }

                /** @brief Move-assigns from raw 16-byte data. */
                UUID &operator=(const DataFormat &&val) {
                        d = std::move(val);
                        return *this;
                }

                /** @brief Returns true if both UUIDs are equal. */
                bool operator==(const UUID &other) const {
                        return d == other.d;
                }

                /** @brief Returns true if the UUIDs are not equal. */
                bool operator!=(const UUID &other) const {
                        return d != other.d;
                }

                /** @brief Less-than comparison for ordering (lexicographic). */
                bool operator<(const UUID &other) const {
                        return std::memcmp(d.data(), other.d.data(), 16) < 0;
                }

                /** @brief Greater-than comparison for ordering (lexicographic). */
                bool operator>(const UUID &other) const {
                        return std::memcmp(d.data(), other.d.data(), 16) > 0;
                }

                /** @brief Less-than-or-equal comparison for ordering (lexicographic). */
                bool operator<=(const UUID &other) const {
                        return std::memcmp(d.data(), other.d.data(), 16) <= 0;
                }

                /** @brief Greater-than-or-equal comparison for ordering (lexicographic). */
                bool operator>=(const UUID &other) const {
                        return std::memcmp(d.data(), other.d.data(), 16) >= 0;
                }

                /** @brief Implicit conversion to String via toString(). */
                operator String() const {
                        return toString();
                }

                /**
                 * @brief Returns true if this UUID is not all-zero.
                 * @return true if the UUID contains at least one non-zero byte.
                 */
                bool isValid() const {
                        return !d.isZero();
                }

                /**
                 * @brief Returns the standard string representation of the UUID.
                 * @return A String in the format "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
                 */
                String toString() const;

                /**
                 * @brief Returns a const reference to the raw 16-byte data.
                 * @return The underlying DataFormat array.
                 */
                const DataFormat &data() const {
                        return d;
                }

                /**
                 * @brief Returns a pointer to the raw byte data.
                 * @return A pointer to the first byte of the UUID.
                 */
                const uint8_t *raw() const {
                        return d.data();
                }

        private:
                DataFormat d;
};

PROMEKI_NAMESPACE_END

