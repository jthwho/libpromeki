/**
 * @file      mediatimestamp.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/duration.h>
#include <promeki/clockdomain.h>
#include <promeki/result.h>
#include <promeki/datatype.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Self-describing timestamp that pairs a time value with its clock domain and offset.
 * @ingroup time
 *
 * MediaTimeStamp bundles a TimeStamp with the ClockDomain that produced
 * it and a fixed offset from that domain's epoch.  Two MediaTimeStamps
 * sharing the same ClockDomain have directly comparable TimeStamp
 * values; the offset expresses a known fixed delay (e.g. pipeline
 * latency) relative to the domain.
 *
 * Stored per-essence as the native @c pts() / @c dts() of each
 * @ref MediaPayload.  MediaIO guarantees that every essence in every
 * Frame carries a valid @c pts — backends that have hardware
 * timestamps set them directly; MediaIO fills in a Synthetic fallback
 * for backends that do not.
 *
 * @par String format
 * `"<domain_name> <timestamp_seconds> <+/-offset_ns>"`
 *
 * @par Example
 * @code
 * MediaTimeStamp mts(TimeStamp::now(), ClockDomain::SystemMonotonic);
 * assert(mts.isValid());
 * String s = mts.toString();  // "SystemMonotonic 12345.678900 +0"
 *
 * auto [parsed, err] = MediaTimeStamp::fromString(s);
 * assert(err.isOk());
 * assert(parsed.domain() == ClockDomain::SystemMonotonic);
 * @endcode
 */
class MediaTimeStamp {
        public:
                PROMEKI_DATATYPE(MediaTimeStamp, DataTypeMediaTimeStamp, 1)

                /** @brief Writes the canonical "domain ts offset" string. */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads the canonical "domain ts offset" string. */
                template <uint32_t V> static Result<MediaTimeStamp> readFromStream(DataStream &s);

                /** @brief Constructs an invalid MediaTimeStamp (no domain). */
                MediaTimeStamp() = default;

                /**
                 * @brief Constructs a MediaTimeStamp.
                 * @param ts     The timestamp value.
                 * @param domain The clock domain that produced @p ts.
                 * @param offset Fixed offset from the domain's epoch (default zero).
                 */
                MediaTimeStamp(const TimeStamp &ts, const ClockDomain &domain, const Duration &offset = Duration::zero());

                /**
                 * @brief Returns true if both the clock domain and the
                 *        underlying @ref TimeStamp are valid.
                 *
                 * The offset is not part of the validity check — a
                 * default-constructed offset is treated as
                 * @ref Duration::zero by the constructors used in
                 * practice — but a missing domain or a missing
                 * @ref TimeStamp would make the stamp meaningless to
                 * any consumer doing arithmetic on it.
                 */
                bool isValid() const { return _domain.isValid() && _timeStamp.isValid(); }

                /** @brief Returns the timestamp value. */
                const TimeStamp &timeStamp() const { return _timeStamp; }

                /** @brief Returns the clock domain. */
                const ClockDomain &domain() const { return _domain; }

                /** @brief Returns the offset from the domain's epoch. */
                const Duration &offset() const { return _offset; }

                /**
                 * @brief Absolute time in nanoseconds — @c timeStamp + @c offset.
                 *
                 * Returns @ref TimeStamp::Invalid when the underlying
                 * @ref timeStamp is invalid.  An invalid @ref offset
                 * is treated as zero (the constructor default is
                 * @ref Duration::zero in every code path that
                 * constructs a MediaTimeStamp explicitly, so a
                 * sentinel offset can only appear on a half-
                 * initialised instance).
                 *
                 * Shorthand for the
                 * @c timeStamp().nanoseconds() + offset().nanoseconds()
                 * idiom that appears throughout the inspector / sync
                 * paths.
                 *
                 * @return Combined nanoseconds, or @ref TimeStamp::Invalid
                 *         if the underlying TimeStamp is invalid.
                 */
                int64_t nanoseconds() const {
                        if (!_timeStamp.isValid()) return TimeStamp::Invalid;
                        const int64_t offsetNs = _offset.isValid() ? _offset.nanoseconds() : 0;
                        return _timeStamp.nanoseconds() + offsetNs;
                }

                /** @brief Sets the timestamp value.
                 *  @param ts The new timestamp. */
                void setTimeStamp(const TimeStamp &ts) { _timeStamp = ts; }

                /** @brief Sets the clock domain.
                 *  @param domain The new clock domain. */
                void setDomain(const ClockDomain &domain) { _domain = domain; }

                /** @brief Sets the offset from the domain's epoch.
                 *  @param offset The new offset. */
                void setOffset(const Duration &offset) { _offset = offset; }

                /**
                 * @brief Returns a string representation.
                 *
                 * Format: `"<domain_name> <timestamp_seconds> <+/-offset_ns>"`
                 *
                 * @return The formatted string.
                 */
                String toString() const;

                /**
                 * @brief Parses a MediaTimeStamp from its string representation.
                 * @param str The string to parse.
                 * @return The parsed MediaTimeStamp and an error code.
                 */
                static Result<MediaTimeStamp> fromString(const String &str);

                /** @brief Equality comparison. */
                bool operator==(const MediaTimeStamp &other) const;

                /** @brief Inequality comparison. */
                bool operator!=(const MediaTimeStamp &other) const;

        private:
                TimeStamp   _timeStamp;
                ClockDomain _domain;
                Duration    _offset;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::MediaTimeStamp);

#endif // PROMEKI_ENABLE_CORE
