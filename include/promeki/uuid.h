/**
 * @file      uuid.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <array>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class UUID {
        public:
                using DataFormat = std::array<uint8_t, 16>;

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

                static UUID fromString(const char *string, Error *err = nullptr);

                UUID() : d{} { }
                UUID(const UUID &u) : d(u.d) { }
                UUID(const UUID &&u) : d(std::move(u.d)) { }
                UUID(const DataFormat &val) : d(val) { }
                UUID(const DataFormat &&val) : d(std::move(val)) { }
                UUID(const String &str) : d(fromString(str.cstr()).data()) { }

                UUID &operator=(const UUID &val) {
                        d = val.d;
                        return *this;
                }

                UUID &operator=(const UUID &&val) {
                        d = std::move(val.d);
                        return *this;
                }

                UUID &operator=(const DataFormat &val) {
                        d = val;
                        return *this;
                }

                UUID &operator=(const DataFormat &&val) {
                        d = std::move(val);
                        return *this;
                }

                bool operator==(const UUID &other) const {
                        return d == other.d;
                }

                bool operator!=(const UUID &other) const {
                        return d != other.d;
                }

                bool operator<(const UUID &other) const {
                        return d < other.d;
                }

                bool operator>(const UUID &other) const {
                        return d < other.d;
                }

                bool operator<=(const UUID &other) const {
                        return d < other.d;
                }

                bool operator>=(const UUID &other) const {
                        return d < other.d;
                }

                operator String() const {
                        return toString();
                }

                bool isValid() const {
                        for(size_t i = 0; i < d.size(); i++) {
                                if(d[i] != 0) return true;
                        }
                        return false;
                }

                String toString() const;

                const DataFormat &data() const {
                        return d;
                }

                const uint8_t *raw() const {
                        return d.data();
                }

        private:
                DataFormat d;
};

PROMEKI_NAMESPACE_END

