/**
 * @file      datetime.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <chrono>
#include <ctime>
#include <sstream>
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class DateTime {
        public:
                constexpr static const char *DefaultFormat = "%F %T";

                using Value = std::chrono::system_clock::time_point;

                static DateTime now() {
                        return DateTime(std::chrono::system_clock::now());
                }
                
                static DateTime fromString(const String &str, const char *fmt = DefaultFormat, bool *ok = nullptr) {
                        std::tm tm = {};
                        std::istringstream ss(str.stds());
                        ss >> std::get_time(&tm, fmt);
                        if(ss.fail()) {
                                if(ok != nullptr) *ok = false;
                                return DateTime();
                        }
                        if(ok != nullptr) *ok = true;
                        return DateTime(tm);
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

                operator String() const {
                        return toString();
                }
                
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


PROMEKI_NAMESPACE_END

