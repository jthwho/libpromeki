/*****************************************************************************
 * datetime.h
 * April 28, 2023
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

#include <chrono>
#include <ctime>
#include <sstream>
#include <promeki/string.h>

namespace promeki {

class DateTime {
        public:
                constexpr static const char *DefaultFormat = "%F %T";

                using Value = std::chrono::system_clock::time_point;

                static DateTime now() {
                        return DateTime(std::chrono::system_clock::now());
                }
                
                static DateTime fromString(const String &str, const char *fmt = DefaultFormat) {
                        std::tm tm = {};
                        std::istringstream ss(str.stds());
                        ss >> std::get_time(&tm, fmt);
                        return ss.fail() ? DateTime() : DateTime(tm);
                }

                static DateTime fromNow(const String &description);

                static String strftime(const std::tm &tm, const char *format = DefaultFormat);

                DateTime() { }
                DateTime(const Value &val) : _value(val) { }
                DateTime(std::tm val) : _value(std::chrono::system_clock::from_time_t(std::mktime(&val))) { }
                DateTime(time_t val) : _value(std::chrono::system_clock::from_time_t(val)) { }

                DateTime operator+(const DateTime &other) const {
                        return DateTime(_value + other._value.time_since_epoch());
                }

                DateTime operator-(const DateTime &other) const {
                        return DateTime(_value - other._value.time_since_epoch());
                }
                
                DateTime &operator+=(const DateTime &other) {
                        _value += other._value.time_since_epoch();
                        return *this;
                }

                DateTime &operator-=(const DateTime &other) {
                        _value -= other._value.time_since_epoch();
                        return *this;
                }

                DateTime operator+(double seconds) const {
                        return DateTime(_value + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                        std::chrono::duration<double>(seconds)));
                }

                DateTime operator-(double seconds) const {
                        return DateTime(_value - std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                        std::chrono::duration<double>(seconds)));
                }

                DateTime &operator+=(double seconds) {
                        _value += std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                        std::chrono::duration<double>(seconds));
                        return *this;
                }

                DateTime& operator-=(double seconds) {
                        _value -= std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                        std::chrono::duration<double>(seconds));
                        return *this;
                }

                bool operator==(const DateTime &other) const {
                        return _value == other._value;
                }

                bool operator!=(const DateTime &other) const {
                        return _value != other._value;
                }

                bool operator<(const DateTime &other) const {
                        return _value < other._value;
                }

                bool operator<=(const DateTime &other) const {
                        return _value <= other._value;
                }

                bool operator>(const DateTime &other) const {
                        return _value > other._value;
                }

                bool operator>=(const DateTime& other) const {
                        return _value >= other._value;
                }

                String toString(const char *format = DefaultFormat) const;
                
                time_t toTimeT() const {
                        return std::chrono::system_clock::to_time_t(_value);
                }

                double toDouble() const {
                        return std::chrono::duration_cast<std::chrono::duration<double>>(
                                _value.time_since_epoch()).count();
                }

                Value value() const {
                        return _value;
                }

        private:
                Value   _value;
};


} // namespace promeki