/*****************************************************************************
 * uuid.h
 * May 03, 2023
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

