/**
 * @file      units.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/enums.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Auto-scaling unit formatter for byte counts, durations, frequencies, and sample counts.
 * @ingroup util
 *
 * Centralizes the metric-prefix (and IEC-binary-prefix) formatting that
 * was previously scattered across @c String::fromByteCount (now removed),
 * @ref Duration::toString, and ad-hoc helpers in benchmark code.
 *
 * Every static method picks the scale factor and unit suffix
 * automatically so the caller only provides a raw numeric value.
 *
 * @par Thread Safety
 * Fully thread-safe.  Units exposes only static formatter
 * functions over caller-provided values; no shared state.
 *
 * @par Example
 * @code
 * Units::fromByteCount(1500000);                     // "1.5 MB"
 * Units::fromByteCount(1048576, 3, ByteCountStyle::Binary); // "1 MiB"
 * Units::fromDuration(0.0015);                       // "1.5 ms"
 * Units::fromDurationNs(8970);                       // "8.97 us"
 * Units::fromFrequency(48000.0);                     // "48 kHz"
 * Units::fromSampleCount(96000, 48000);              // "2 s"
 * @endcode
 */
class Units {
        public:
                /**
                 * @brief Formats a byte count with metric (1000-based) units.
                 *
                 * @par Example
                 * @code
                 * Units::fromByteCount(0);                    // "0 B"
                 * Units::fromByteCount(1500);                 // "1.5 KB"
                 * Units::fromByteCount(1500000, 2);           // "1.5 MB"
                 * Units::fromByteCount(1234567890123ULL, 2);  // "1.23 TB"
                 * @endcode
                 *
                 * @param bytes       The byte count to format.
                 * @param maxDecimals Maximum fractional digits (trailing zeros trimmed).
                 * @return Formatted string with unit suffix.
                 */
                static String fromByteCount(uint64_t bytes, int maxDecimals = 3);

                /**
                 * @brief Formats a byte count with an explicit @ref ByteCountStyle.
                 *
                 * @par Example
                 * @code
                 * Units::fromByteCount(1048576, 3, ByteCountStyle::Binary); // "1 MiB"
                 * Units::fromByteCount(1024,    3, ByteCountStyle::Metric); // "1.024 KB"
                 * @endcode
                 *
                 * @param bytes       The byte count to format.
                 * @param maxDecimals Maximum fractional digits (trailing zeros trimmed).
                 * @param style       The unit family to use.
                 * @return Formatted string with unit suffix.
                 */
                static String fromByteCount(uint64_t bytes, int maxDecimals, const ByteCountStyle &style);

                /**
                 * @brief Formats a duration given in seconds.
                 *
                 * Auto-scales to ns, us, ms, s, m, or h.
                 *
                 * @par Example
                 * @code
                 * Units::fromDuration(0.0000015);  // "1.5 us"
                 * Units::fromDuration(0.0015);     // "1.5 ms"
                 * Units::fromDuration(1.5);        // "1.5 s"
                 * Units::fromDuration(3661.0);     // "1.01 h"
                 * @endcode
                 *
                 * @param seconds   Duration value in seconds.
                 * @param precision Number of significant decimal digits.
                 * @return Formatted string with unit suffix.
                 */
                static String fromDuration(double seconds, int precision = 2);

                /**
                 * @brief Formats a duration given in nanoseconds.
                 *
                 * Auto-scales to ns, us, ms, s, m, or h.
                 *
                 * @par Example
                 * @code
                 * Units::fromDurationNs(500);          // "500 ns"
                 * Units::fromDurationNs(8970);         // "8.97 us"
                 * Units::fromDurationNs(1500000000);   // "1.5 s"
                 * @endcode
                 *
                 * @param ns        Duration value in nanoseconds.
                 * @param precision Number of significant decimal digits.
                 * @return Formatted string with unit suffix.
                 */
                static String fromDurationNs(double ns, int precision = 2);

                /**
                 * @brief Formats a frequency in Hz.
                 *
                 * Auto-scales to Hz, kHz, MHz, or GHz.
                 *
                 * @par Example
                 * @code
                 * Units::fromFrequency(440.0);       // "440 Hz"
                 * Units::fromFrequency(48000.0);     // "48 kHz"
                 * Units::fromFrequency(2400000000.0); // "2.4 GHz"
                 * @endcode
                 *
                 * @param hz        Frequency in hertz.
                 * @param precision Number of significant decimal digits.
                 * @return Formatted string with unit suffix.
                 */
                static String fromFrequency(double hz, int precision = 2);

                /**
                 * @brief Formats a sample count as a time duration at a given sample rate.
                 *
                 * Converts @p samples to seconds via @p sampleRate and delegates
                 * to @ref fromDuration.
                 *
                 * @par Example
                 * @code
                 * Units::fromSampleCount(48000, 48000);   // "1 s"
                 * Units::fromSampleCount(24, 48000);      // "500 us"
                 * Units::fromSampleCount(96000, 48000);   // "2 s"
                 * @endcode
                 *
                 * @param samples    Number of audio samples.
                 * @param sampleRate Samples per second.
                 * @param precision  Number of significant decimal digits.
                 * @return Formatted string with unit suffix.
                 */
                static String fromSampleCount(uint64_t samples, uint32_t sampleRate, int precision = 2);

                /**
                 * @brief Formats a data rate in bytes per second.
                 *
                 * Auto-scales to B/s, KB/s, MB/s, or GB/s using 1024-based units.
                 *
                 * @par Example
                 * @code
                 * Units::fromBytesPerSec(1048576.0);  // "1 MB/s"
                 * Units::fromBytesPerSec(500.0);      // "500 B/s"
                 * @endcode
                 *
                 * @param bps       Rate in bytes per second.
                 * @param precision Number of significant decimal digits.
                 * @return Formatted string with unit suffix.
                 */
                static String fromBytesPerSec(double bps, int precision = 1);

                /**
                 * @brief Formats a data rate in bits per second.
                 *
                 * Auto-scales to bps, Kbps, Mbps, or Gbps using 1000-based units.
                 *
                 * @par Example
                 * @code
                 * Units::fromBitsPerSec(1000000.0);   // "1 Mbps"
                 * Units::fromBitsPerSec(125000000.0); // "125 Mbps"
                 * @endcode
                 *
                 * @param bps       Rate in bits per second.
                 * @param precision Number of significant decimal digits.
                 * @return Formatted string with unit suffix.
                 */
                static String fromBitsPerSec(double bps, int precision = 1);

                /**
                 * @brief Formats an items-per-second rate with metric suffixes.
                 *
                 * Auto-scales with k/M/G/T suffixes (1000-based).
                 *
                 * @par Example
                 * @code
                 * Units::fromItemsPerSec(500.0);       // "500"
                 * Units::fromItemsPerSec(1500.0);      // "1.5k"
                 * Units::fromItemsPerSec(2500000.0);   // "2.5M"
                 * @endcode
                 *
                 * @param ips       Rate in items per second.
                 * @param precision Number of significant decimal digits.
                 * @return Formatted string with suffix (no space before suffix).
                 */
                static String fromItemsPerSec(double ips, int precision = 1);
};

PROMEKI_NAMESPACE_END
