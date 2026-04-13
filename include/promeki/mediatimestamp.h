/**
 * @file      mediatimestamp.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/duration.h>
#include <promeki/clockdomain.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

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
 * Stored per-essence in Image and Audio metadata via
 * Metadata::MediaTimeStamp.  MediaIO guarantees that every essence in
 * every Frame carries a valid MediaTimeStamp — backends that have
 * hardware timestamps set them directly; MediaIO fills in a Synthetic
 * fallback for backends that do not.
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
                /** @brief Constructs an invalid MediaTimeStamp (no domain). */
                MediaTimeStamp() = default;

                /**
                 * @brief Constructs a MediaTimeStamp.
                 * @param ts     The timestamp value.
                 * @param domain The clock domain that produced @p ts.
                 * @param offset Fixed offset from the domain's epoch (default zero).
                 */
                MediaTimeStamp(const TimeStamp &ts, const ClockDomain &domain,
                               const Duration &offset = Duration());

                /**
                 * @brief Returns true if the domain is valid.
                 * @return True if this MediaTimeStamp carries a valid clock domain.
                 */
                bool isValid() const { return _domain.isValid(); }

                /** @brief Returns the timestamp value. */
                const TimeStamp &timeStamp() const { return _timeStamp; }

                /** @brief Returns the clock domain. */
                const ClockDomain &domain() const { return _domain; }

                /** @brief Returns the offset from the domain's epoch. */
                const Duration &offset() const { return _offset; }

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
                TimeStamp    _timeStamp;
                ClockDomain  _domain;
                Duration     _offset;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::MediaTimeStamp);
