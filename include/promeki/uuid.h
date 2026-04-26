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
 * @brief Universally Unique Identifier (UUID).
 * @ingroup util
 *
 * Generates and manipulates RFC 4122 / RFC 9562 UUIDs.
 * Supports versions 1 (stub), 3, 4, 5, and 7.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance is
 * conditionally thread-safe: const operations (toString, comparisons,
 * accessors) may be called from multiple threads, but any mutation
 * must be externally synchronized.
 *
 * @par Example
 * @code
 * UUID id = UUID::generate();          // random v4
 * String str = id.toString();          // "550e8400-e29b-..."
 * UUID parsed = UUID::fromString(str); // round-trip
 * bool valid = id.isValid();
 * @endcode
 */
class UUID {
        public:
                /** @brief Raw 16-byte storage format for a UUID. */
                using DataFormat = Array<uint8_t, 16>;

                /**
                 * @brief Convenience generator that dispatches to the appropriate generateVn() function.
                 * @param version UUID version to generate (default: 4).
                 *
                 * For versions 3 and 5, the namespace UUID and name are taken from the
                 * Application object (appUUID and appName).
                 *
                 * @return A valid UUID, or an invalid UUID if the version is unsupported
                 *         or if generation fails.
                 */
                static UUID generate(int version = 4);

                /**
                 * @brief Generates a version 1 (timestamp + MAC) UUID.
                 *
                 * @warning Not yet implemented.  Calling this currently logs
                 *          a warning and returns an invalid (all-zero) UUID.
                 *          Use @ref generateV4 or @ref generateV7 for unique
                 *          IDs in the meantime.
                 *
                 * @return An invalid UUID until v1 is implemented.
                 */
                static UUID generateV1();

                /**
                 * @brief Generates a version 3 (MD5 namespace) UUID.
                 * @param ns   The namespace UUID.
                 * @param name The name to hash within the namespace.
                 * @return A deterministic version 3 UUID.
                 */
                static UUID generateV3(const UUID &ns, const String &name);

                /**
                 * @brief Generates a random version 4 UUID.
                 * @return A valid UUID, or an invalid (all-zero) UUID on failure.
                 */
                static UUID generateV4();

                /**
                 * @brief Generates a version 5 (SHA-1 namespace) UUID.
                 * @param ns   The namespace UUID.
                 * @param name The name to hash within the namespace.
                 * @return A deterministic version 5 UUID.
                 */
                static UUID generateV5(const UUID &ns, const String &name);

                /**
                 * @brief Generates a version 7 (Unix timestamp + random) UUID.
                 * @param timestampMs Unix timestamp in milliseconds. If negative,
                 *        the current system time is used.
                 * @return A valid, time-sortable UUID.
                 */
                static UUID generateV7(int64_t timestampMs = -1);

                /**
                 * @brief Parses a UUID from a string representation.
                 * @param string The UUID string (e.g. "550e8400-e29b-41d4-a716-446655440000").
                 * @param err    Optional error output.
                 * @return The parsed UUID, or an invalid UUID on failure.
                 */
                static UUID fromString(const char *string, Error *err = nullptr);

                /**
                 * @brief Parses a UUID from a String.
                 * @param string The UUID string (e.g. "550e8400-e29b-41d4-a716-446655440000").
                 * @param err    Optional error output.
                 * @return The parsed UUID, or an invalid UUID on failure.
                 */
                static UUID fromString(const String &string, Error *err = nullptr) {
                        return fromString(string.cstr(), err);
                }

                /** @brief Constructs an invalid (all-zero) UUID. */
                UUID() : d{} { }

                /** @brief Copy constructor. */
                UUID(const UUID &u) : d(u.d) { }

                /** @brief Move constructor. */
                UUID(UUID &&u) noexcept : d(std::move(u.d)) { }

                /** @brief Constructs a UUID from raw 16-byte data. */
                UUID(const DataFormat &val) : d(val) { }

                /** @brief Move-constructs a UUID from raw 16-byte data. */
                UUID(DataFormat &&val) noexcept : d(std::move(val)) { }

                /** @brief Copy assignment operator. */
                UUID &operator=(const UUID &val) {
                        d = val.d;
                        return *this;
                }

                /** @brief Move assignment operator. */
                UUID &operator=(UUID &&val) noexcept {
                        d = std::move(val.d);
                        return *this;
                }

                /** @brief Assigns from raw 16-byte data. */
                UUID &operator=(const DataFormat &val) {
                        d = val;
                        return *this;
                }

                /** @brief Move-assigns from raw 16-byte data. */
                UUID &operator=(DataFormat &&val) noexcept {
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
                 * @brief Returns the UUID version number.
                 * @return The version (1-8) encoded in the UUID, or 0 if invalid.
                 */
                int version() const {
                        if(!isValid()) return 0;
                        return (d[6] >> 4) & 0x0F;
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

PROMEKI_FORMAT_VIA_TOSTRING(promeki::UUID);
