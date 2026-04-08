/**
 * @file      framerate.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/duration.h>
#include <promeki/rational.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

#define PROMEKI_WELL_KNOWN_FRAME_RATES \
    X(FPS_Invalid,  "INV",      0,          1) \
    X(FPS_120,      "120",      120,        1) \
    X(FPS_11988,    "119.88",   120000,     1001) \
    X(FPS_100,      "100",      100,        1) \
    X(FPS_60,       "60",       60,         1) \
    X(FPS_5994,     "59.94",    60000,      1001) \
    X(FPS_50,       "50",       50,         1) \
    X(FPS_48,       "48",       48,         1) \
    X(FPS_4795,     "47.95",    48000,      1001) \
    X(FPS_30,       "30",       30,         1) \
    X(FPS_2997,     "29.97",    30000,      1001) \
    X(FPS_25,       "25",       25,         1) \
    X(FPS_24,       "24",       24,         1) \
    X(FPS_2398,     "23.98",    24000,      1001)

/**
 * @brief Represents a video frame rate as a rational number.
 * @ingroup time
 *
 * FrameRate wraps a Rational value and provides an enumeration of
 * well-known industry-standard frame rates.  A frame rate can be
 * constructed from a WellKnownRate enum or from an arbitrary
 * rational value.  FrameRate is a first-class Variant type and
 * can be stored directly in MediaNodeConfig and other Variant-based
 * containers.
 *
 * @par Why rational numbers?
 *
 * Video frame rates must be represented as exact rational numbers
 * (numerator / denominator), never as floating-point approximations.
 * This requirement originates from the 1953 transition to NTSC color
 * television in the United States.
 *
 * The original black-and-white NTSC standard operated at exactly
 * 30 frames per second (60 fields/s), derived from the 60 Hz AC
 * power line frequency.  When color was added, engineers needed to
 * multiplex a 3.579545 MHz color subcarrier into the existing signal
 * without creating visible interference patterns.  A beat frequency
 * between the color subcarrier and the 4.5 MHz audio carrier would
 * have produced objectionable artifacts in the picture.  To eliminate
 * this beat, the frame rate was reduced by a factor of exactly
 * 1000/1001, shifting the color subcarrier frequency just enough to
 * place the beat outside the visible spectrum.  The resulting frame
 * rate is exactly 30000/1001 frames per second -- not 29.97, which
 * is merely a convenient approximation.
 *
 * This 1000/1001 relationship propagates to every frame rate derived
 * from the NTSC family:
 *
 * | Common name | Exact rational  | Decimal (approx.) |
 * |-------------|-----------------|-------------------|
 * | "59.94"     | 60000/1001      | 59.94005994...    |
 * | "29.97"     | 30000/1001      | 29.97002997...    |
 * | "23.976"    | 24000/1001      | 23.97602397...    |
 *
 * The names "29.97" and "23.976" (sometimes written "23.98") are
 * widespread in the industry but are not the actual frame rates --
 * they are approximate shorthands.  Using the floating-point value
 * 29.97 instead of the rational 30000/1001 introduces a small but
 * compounding error: over the course of a one-hour program, the
 * accumulated drift amounts to approximately 3.6 frames (about
 * 108 milliseconds at 30000/1001).  In professional workflows
 * involving timecode, edit decision lists, and long-form content,
 * this drift causes frame-accurate synchronization to fail.
 *
 * By storing the frame rate as an exact rational, FrameRate avoids
 * this class of error entirely.  Frame interval calculations,
 * timecode conversions, and duration computations remain exact
 * regardless of program length.
 *
 * @par Well-known rates
 *
 * The WellKnownRate enum identifies industry-standard frame rates
 * so that code can branch on them without comparing rationals.
 * When a FrameRate is constructed from a rational that matches a
 * well-known rate, the enum is set automatically.
 *
 * The fromString() factory accepts both the approximate common
 * names ("29.97", "23.976", "23.98") and exact fraction strings
 * ("30000/1001"), always producing the correct rational internally.
 */
class FrameRate {
        public:
                /** @brief Underlying rational type used to store the frame rate. */
                using RationalType = Rational<unsigned int>;

#define X(type, string, num, den) type,
                /** @brief Enumeration of well-known industry-standard frame rates. */
                enum WellKnownRate {
                        FPS_NotWellKnown = 0, ///< Not a well-known rate.
                        PROMEKI_WELL_KNOWN_FRAME_RATES
                };
#undef X

                /** @brief Default constructor. Creates an invalid (zero) frame rate. */
                FrameRate() = default;

                /**
                 * @brief Constructs a FrameRate from a well-known rate enum.
                 * @param rate The well-known rate to use.
                 */
                FrameRate(WellKnownRate rate);

                /**
                 * @brief Constructs a FrameRate from an arbitrary rational value.
                 * @param r The rational frame rate (numerator / denominator).
                 */
                FrameRate(const RationalType &r);

                /**
                 * @brief Returns true if this frame rate is valid (numerator > 0).
                 * @return true if valid.
                 */
                bool isValid() const { return _fps.numerator() > 0; }

                /** @brief Returns the numerator of the frame rate rational. */
                unsigned int numerator() const { return _fps.numerator(); }

                /** @brief Returns the denominator of the frame rate rational. */
                unsigned int denominator() const { return _fps.denominator(); }

                /**
                 * @brief Returns the frame rate as a double-precision floating point value.
                 * @return The frame rate in frames per second.
                 */
                double toDouble() const { return _fps.toDouble(); }

                /**
                 * @brief Returns the period of one frame as a Duration.
                 *
                 * Computed from the exact rational form as
                 * @c denominator / @c numerator seconds, rounded to the
                 * nearest nanosecond.  Returns a zero Duration if the
                 * frame rate is invalid.
                 *
                 * @par Example
                 * @code
                 * FrameRate fps(FrameRate::FPS_2997);     // 30000/1001
                 * Duration  d = fps.frameDuration();      // ~33_366_666 ns
                 * @endcode
                 *
                 * @return The frame period as a Duration.
                 */
                Duration frameDuration() const {
                        if(!isValid()) return Duration();
                        // Period in ns = denom * 1e9 / num.  Use 64-bit
                        // math so 1001 * 1e9 doesn't overflow.
                        int64_t num = static_cast<int64_t>(_fps.numerator());
                        int64_t den = static_cast<int64_t>(_fps.denominator());
                        int64_t ns  = (den * INT64_C(1'000'000'000) + num / 2) / num;
                        return Duration::fromNanoseconds(ns);
                }

                /**
                 * @brief Returns the number of audio samples for the given frame index.
                 *
                 * Uses exact rational arithmetic so the cumulative
                 * sample count stays perfectly aligned with wall-clock
                 * time over arbitrary durations — including for
                 * fractional NTSC rates where the per-frame count must
                 * alternate.  For 48 kHz @ 29.97 fps the function emits
                 * the standard cadence of 1601/1602/1601/1602/1602
                 * (which sums to exactly 8008 every 5 frames), instead
                 * of the constant 1602 you would get from a naive
                 * @c round(sampleRate/fps).
                 *
                 * The implementation is:
                 *
                 * @code
                 * cumulative(n) = (n * sampleRate * denominator) / numerator   // integer div
                 * samplesPerFrame(n) = cumulative(n + 1) - cumulative(n)
                 * @endcode
                 *
                 * For integer-cadence rates (24, 25, 30, 50, 60) every
                 * frame returns the same constant value.  For fractional
                 * rates the result alternates in a fixed cycle whose
                 * period equals the @c denominator (e.g. 1001 for NTSC
                 * — though the visible pattern repeats every 5 frames).
                 *
                 * @param sampleRate Audio sample rate in Hz (e.g. 48000).
                 * @param frameIndex Zero-based frame index since the start of the run.
                 * @return Number of samples to emit for that frame, or 0 if the
                 *         frame rate is invalid or @p sampleRate is non-positive.
                 */
                size_t samplesPerFrame(int64_t sampleRate, int64_t frameIndex) const {
                        if(!isValid() || sampleRate <= 0 || frameIndex < 0) return 0;
                        const int64_t num = static_cast<int64_t>(_fps.numerator());
                        const int64_t den = static_cast<int64_t>(_fps.denominator());
                        // Cumulative samples through end of frame N = N * sr * den / num.
                        // Cast to __int128 would be overkill — for any plausible
                        // sample rate (<= 768 kHz) and fps numerator (<= 120000),
                        // sr * den fits in 60 bits and a multiply by frameIndex up
                        // to 2^31 still fits in int64_t.
                        const int64_t cumNext = ((frameIndex + 1) * sampleRate * den) / num;
                        const int64_t cumNow  = (frameIndex * sampleRate * den) / num;
                        return static_cast<size_t>(cumNext - cumNow);
                }

                /**
                 * @brief Returns a string representation of the frame rate.
                 * @return The frame rate as a String (e.g. "30000/1001").
                 */
                String toString() const { return _fps.toString(); }

                /**
                 * @brief Returns true if this is a well-known industry frame rate.
                 * @return true if the rate matches a WellKnownRate entry.
                 */
                bool isWellKnownRate() const { return wellKnownRate() != FPS_NotWellKnown; }

                /**
                 * @brief Returns the WellKnownRate enum value for this frame rate.
                 *
                 * Compares the current rational value against all well-known rates
                 * using reduced form, so e.g. 30000/1000 matches FPS_30 (30/1).
                 *
                 * @par Example
                 * @code
                 * FrameRate fr(FrameRate::RationalType(30000, 1000));
                 * assert(fr.wellKnownRate() == FrameRate::FPS_30); // 30000/1000 reduces to 30/1
                 *
                 * FrameRate custom(FrameRate::RationalType(90, 1));
                 * assert(custom.wellKnownRate() == FrameRate::FPS_NotWellKnown);
                 * @endcode
                 *
                 * @return The matching WellKnownRate, or FPS_NotWellKnown.
                 */
                WellKnownRate wellKnownRate() const;

                /**
                 * @brief Returns the underlying rational value.
                 * @return The rational frame rate.
                 */
                const RationalType &rational() const { return _fps; }

                /**
                 * @brief Parses a frame rate from a string.
                 *
                 * Accepts well-known rate strings ("23.976", "23.98", "24",
                 * "25", "29.97", "30", "47.95", "48", "50", "59.94", "60",
                 * "100", "119.88", "120") and fraction strings ("30000/1001",
                 * "24/1").
                 *
                 * @param str The string to parse.
                 * @return A Result containing the parsed FrameRate or an Error.
                 */
                static Result<FrameRate> fromString(const String &str);

                /** @brief Returns true if both frame rates represent the same rational value. */
                bool operator==(const FrameRate &other) const { return _fps == other._fps; }

                /** @brief Returns true if the frame rates differ. */
                bool operator!=(const FrameRate &other) const { return _fps != other._fps; }

        private:
                RationalType    _fps;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::FrameRate);

