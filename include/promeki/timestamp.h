/**
 * @file      timestamp.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <chrono>
#include <thread>
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A monotonic timestamp based on std::chrono::steady_clock.
 *
 * Provides high-resolution time measurement, elapsed time queries, and
 * thread sleep utilities. Uses steady_clock to guarantee monotonic
 * progression regardless of system clock adjustments.
 */
class TimeStamp {
        public:
                /** @brief Underlying clock type. */
                using Clock = std::chrono::steady_clock;
                /** @brief Time point value type. */
                using Value = std::chrono::time_point<Clock>;
                /** @brief Duration type derived from the clock. */
                using Duration = Clock::duration;

                /**
                 * @brief Converts a floating-point number of seconds to a Duration.
                 * @param val The time in seconds.
                 * @return The equivalent Duration value.
                 */
                static Duration secondsToDuration(double val) {
                        std::chrono::duration<double> doubleDuration(val);
                        return std::chrono::duration_cast<Duration>(doubleDuration);
                }

                /**
                 * @brief Sleeps the current thread for the given duration.
                 * @param d The duration to sleep.
                 */
                static void sleep(const Duration &d) {
                        std::this_thread::sleep_for(d);
                        return;
                }

                /**
                 * @brief Returns a TimeStamp representing the current time.
                 * @return A TimeStamp initialized to the current steady_clock time.
                 */
                static TimeStamp now() {
                        return TimeStamp(Clock::now());
                }

                /** @brief Constructs a default (epoch) TimeStamp. */
                TimeStamp() { }

                /**
                 * @brief Constructs a TimeStamp from the given time point value.
                 * @param v The time point value.
                 */
                TimeStamp(const Value &v) : _value(v) { }

                /** @brief Converts the TimeStamp to its underlying Value type. */
                operator Value() const {
                        return _value;
                }

                /**
                 * @brief Advances the timestamp by the given duration.
                 * @param duration The duration to add.
                 * @return Reference to this TimeStamp.
                 */
                TimeStamp& operator+=(const Duration& duration) {
                        _value += duration;
                        return *this;
                }

                /**
                 * @brief Moves the timestamp back by the given duration.
                 * @param duration The duration to subtract.
                 * @return Reference to this TimeStamp.
                 */
                TimeStamp& operator-=(const Duration& duration) {
                        _value -= duration;
                        return *this;
                }

                /**
                 * @brief Sets the underlying time point value.
                 * @param v The new time point value.
                 */
                void setValue(const Value &v) {
                        _value = v;
                        return;
                }

                /**
                 * @brief Returns the underlying time point value.
                 * @return The stored time point.
                 */
                Value value() const {
                        return _value;
                }

                /** @brief Updates the timestamp to the current time. */
                void update() {
                        _value = Clock::now();
                }

                /** @brief Sleeps the current thread until this timestamp is reached. */
                void sleepUntil() const {
                        std::this_thread::sleep_until(_value);
                }

                /**
                 * @brief Returns the duration since the clock's epoch.
                 * @return The duration from epoch to this timestamp.
                 */
                Duration timeSinceEpoch() const {
                        return _value.time_since_epoch();
                }

                /**
                 * @brief Returns the time since epoch in seconds as a double.
                 * @return Seconds since the clock's epoch.
                 */
                double seconds() const {
                        return std::chrono::duration_cast<std::chrono::duration<double>>(_value.time_since_epoch()).count();
                }

                /**
                 * @brief Returns the time since epoch in milliseconds.
                 * @return Milliseconds since the clock's epoch.
                 */
                int64_t milliseconds() const {
                        return std::chrono::duration_cast<std::chrono::milliseconds>(_value.time_since_epoch()).count();
                }

                /**
                 * @brief Returns the time since epoch in microseconds.
                 * @return Microseconds since the clock's epoch.
                 */
                int64_t microseconds() const {
                        return std::chrono::duration_cast<std::chrono::microseconds>(_value.time_since_epoch()).count();
                }

                /**
                 * @brief Returns the time since epoch in nanoseconds.
                 * @return Nanoseconds since the clock's epoch.
                 */
                int64_t nanoseconds() const {
                        return std::chrono::duration_cast<std::chrono::nanoseconds>(_value.time_since_epoch()).count();
                }

                /**
                 * @brief Returns the elapsed time since this timestamp in seconds.
                 * @return Elapsed seconds as a double.
                 */
                double elapsedSeconds() const {
                        return std::chrono::duration_cast<std::chrono::duration<double>>(Clock::now() - _value).count();
                }

                /**
                 * @brief Returns the elapsed time since this timestamp in milliseconds.
                 * @return Elapsed milliseconds.
                 */
                int64_t elapsedMilliseconds() const {
                        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - _value).count();
                }

                /**
                 * @brief Returns the elapsed time since this timestamp in microseconds.
                 * @return Elapsed microseconds.
                 */
                int64_t elapsedMicroseconds() const {
                        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - _value).count();
                }

                /**
                 * @brief Returns the elapsed time since this timestamp in nanoseconds.
                 * @return Elapsed nanoseconds.
                 */
                int64_t elapsedNanoseconds() const {
                        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - _value).count();
                }

                /**
                 * @brief Returns a string representation of the timestamp in seconds.
                 * @return A String containing the seconds value.
                 */
                String toString() const {
                        return String::number(seconds());
                }

                /** @brief Converts the TimeStamp to a String. */
                operator String() const {
                        return toString();
                }

        private:
                Value _value;
};

/**
 * @brief Returns a new TimeStamp advanced by the given duration.
 * @param ts       The base timestamp.
 * @param duration The duration to add.
 * @return A new TimeStamp offset forward by @p duration.
 */
inline TimeStamp operator+(const TimeStamp& ts, const TimeStamp::Duration& duration) {
        TimeStamp result(ts);
        result += duration;
        return result;
}

/**
 * @brief Returns a new TimeStamp moved back by the given duration.
 * @param ts       The base timestamp.
 * @param duration The duration to subtract.
 * @return A new TimeStamp offset backward by @p duration.
 */
inline TimeStamp operator-(const TimeStamp& ts, const TimeStamp::Duration& duration) {
        TimeStamp result(ts);
        result -= duration;
        return result;
}

PROMEKI_NAMESPACE_END

