/*****************************************************************************
 * timer.h
 * April 10, 2023
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
#include <thread>

namespace promeki {

class TimeStamp {
        public:
                using Clock = std::chrono::steady_clock;
                using Value = std::chrono::time_point<Clock>;
                using Duration = Clock::duration;

                static Duration secondsToDuration(double val) {
                        std::chrono::duration<double> doubleDuration(val);
                        return std::chrono::duration_cast<Duration>(doubleDuration);
                }

                static void sleep(const Duration &d) {
                        std::this_thread::sleep_for(d);
                        return;
                }

                static TimeStamp now() {
                        return TimeStamp(Clock::now());
                }

                TimeStamp() { }

                TimeStamp(const Value &v) : _value(v) { }

                operator Value() const {
                        return _value;
                }

                TimeStamp& operator+=(const Duration& duration) {
                        _value += duration;
                        return *this;
                }

                TimeStamp& operator-=(const Duration& duration) {
                        _value -= duration;
                        return *this;
                }

                void setValue(const Value &v) {
                        _value = v;
                        return;
                }

                Value value() const {
                        return _value;
                }

                void update() {
                        _value = Clock::now();
                }

                void sleepUntil() const {
                        std::this_thread::sleep_until(_value);
                }

                Duration timeSinceEpoch() const {
                        return _value.time_since_epoch();
                }

                double seconds() const {
                        return std::chrono::duration_cast<std::chrono::seconds>(_value.time_since_epoch()).count();
                }

                double milliseconds() const {
                        return std::chrono::duration_cast<std::chrono::milliseconds>(_value.time_since_epoch()).count();
                }

                double microseconds() const {
                        return std::chrono::duration_cast<std::chrono::microseconds>(_value.time_since_epoch()).count();
                }

                double nanoseconds() const {
                        return std::chrono::duration_cast<std::chrono::nanoseconds>(_value.time_since_epoch()).count();
                }

                double elapsedSeconds() const {
                        return std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - _value).count();
                }

                double elapsedMilliseconds() const {
                        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - _value).count();
                }

                double elapsedMicroseconds() const {
                        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - _value).count();
                }

                double elapsedNanoseconds() const {
                        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - _value).count();
                }

        private:
                Value _value;
};

inline TimeStamp operator+(const TimeStamp& ts, const TimeStamp::Duration& duration) {
        TimeStamp result(ts);
        result += duration;
        return result;
}

inline TimeStamp operator-(const TimeStamp& ts, const TimeStamp::Duration& duration) {
        TimeStamp result(ts);
        result -= duration;
        return result;
}

} // namespace promeki


