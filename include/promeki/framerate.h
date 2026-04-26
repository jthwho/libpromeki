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
#include <promeki/list.h>
#include <promeki/rational.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

#define PROMEKI_WELL_KNOWN_FRAME_RATES                                                                                 \
        X(FPS_Invalid, "INV", 0, 1)                                                                                    \
        X(FPS_120, "120", 120, 1)                                                                                      \
        X(FPS_119_88, "119.88", 120000, 1001)                                                                          \
        X(FPS_100, "100", 100, 1)                                                                                      \
        X(FPS_60, "60", 60, 1)                                                                                         \
        X(FPS_59_94, "59.94", 60000, 1001)                                                                             \
        X(FPS_50, "50", 50, 1)                                                                                         \
        X(FPS_48, "48", 48, 1)                                                                                         \
        X(FPS_47_95, "47.95", 48000, 1001)                                                                             \
        X(FPS_30, "30", 30, 1)                                                                                         \
        X(FPS_29_97, "29.97", 30000, 1001)                                                                             \
        X(FPS_25, "25", 25, 1)                                                                                         \
        X(FPS_24, "24", 24, 1)                                                                                         \
        X(FPS_23_98, "23.98", 24000, 1001)

/**
 * @brief Represents a video frame rate as a rational number.
 * @ingroup time
 *
 * FrameRate wraps a Rational value and provides an enumeration of
 * well-known industry-standard frame rates.  A frame rate can be
 * constructed from a WellKnownRate enum or from an arbitrary
 * rational value.  FrameRate is a first-class Variant type and
 * can be stored directly in MediaConfig and other Variant-based
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

                struct WellKnown; ///< @brief See @ref FrameRate::WellKnown defined below.

                /**
                 * @brief Returns the canonical list of well-known frame rates.
                 *
                 * The list is sourced from the same internal table that
                 * backs the @ref WellKnownRate enum and @ref fromString
                 * parser, so it is deterministic and stable across calls
                 * — UI consumers can cache the result.
                 *
                 * The order is the canonical broadcast / display
                 * progression from slowest to fastest (23.98, 24, 25,
                 * 29.97, 30, ..., 119.88, 120).  The internal enum table
                 * lists rates in the opposite order for legacy reasons;
                 * this method reverses that so the dropdown reads
                 * naturally.
                 *
                 * @return List of (label, rate) pairs, one per well-known
                 *         entry.  Never empty; never contains
                 *         @ref FPS_Invalid.
                 */
                static List<WellKnown> wellKnownRates();

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
                 * FrameRate fps(FrameRate::FPS_29_97);     // 30000/1001
                 * Duration  d = fps.frameDuration();      // ~33_366_666 ns
                 * @endcode
                 *
                 * @return The frame period as a Duration.
                 */
                Duration frameDuration() const {
                        if (!isValid()) return Duration();
                        // Period in ns = denom * 1e9 / num.  Use 64-bit
                        // math so 1001 * 1e9 doesn't overflow.
                        int64_t num = static_cast<int64_t>(_fps.numerator());
                        int64_t den = static_cast<int64_t>(_fps.denominator());
                        int64_t ns = (den * INT64_C(1'000'000'000) + num / 2) / num;
                        return Duration::fromNanoseconds(ns);
                }

                /**
                 * @brief Returns the cumulative tick count at the start of frame N.
                 *
                 * Computes @c (frameIndex @c × @c tickRate @c ×
                 * @c denominator) @c / @c numerator using exact 64-bit
                 * integer arithmetic.  The result is the number of
                 * ticks of a clock running at @p tickRate that have
                 * elapsed between the start of the stream and the
                 * start of frame @p frameIndex.  This is the primitive
                 * that every "time-at-frame" computation in libpromeki
                 * should build on:
                 *
                 * - **Audio sample accounting** — pass the audio
                 *   sample rate.  @ref samplesPerFrame delegates here
                 *   and subtracts two consecutive calls to get the
                 *   exact per-frame sample count, producing the NTSC
                 *   1601/1602 cadence without drift.
                 * - **RTP timestamps** — pass the RTP clock rate
                 *   (typically 90000 for video, the sample rate for
                 *   AES67 audio).  The 64-bit result can be truncated
                 *   to 32 bits for the wire representation; drift is
                 *   bounded by the 1/@c numerator rational precision,
                 *   which is exact for well-known rates.
                 * - **Wall-clock scheduling** — pass a 1 GHz
                 *   "nanosecond tick" rate and the result is the
                 *   frame's start time in nanoseconds.
                 *
                 * For integer-cadence rates (24, 25, 30, 50, 60) the
                 * stride between consecutive frames is constant.  For
                 * fractional rates (29.97 = 30000/1001, 23.976 =
                 * 24000/1001) the stride alternates in a fixed
                 * period (1001 frames for NTSC) so the cumulative
                 * count stays aligned with wall-clock time across
                 * arbitrary durations.
                 *
                 * Int64 range notes: for any plausible tickRate
                 * (≤ 2 GHz) and @c denominator (≤ 1001) and frame
                 * index (≤ 2^30), the intermediate product fits in
                 * int64 with a wide margin.  Streams longer than
                 * 2^30 frames at > 24 fps last longer than a year,
                 * which is well beyond the relevant use cases.
                 *
                 * @param tickRate   Ticks per second (e.g. 48000 for
                 *                   audio sample accounting, 90000
                 *                   for standard video RTP).
                 * @param frameIndex Zero-based frame index since the
                 *                   start of the run.
                 * @return Cumulative tick count at the start of
                 *         @p frameIndex, or 0 if the frame rate is
                 *         invalid or @p tickRate is non-positive.
                 */
                int64_t cumulativeTicks(int64_t tickRate, int64_t frameIndex) const {
                        if (!isValid() || tickRate <= 0 || frameIndex < 0) return 0;
                        const int64_t num = static_cast<int64_t>(_fps.numerator());
                        const int64_t den = static_cast<int64_t>(_fps.denominator());
                        return (frameIndex * tickRate * den) / num;
                }

                /**
                 * @brief Returns the number of audio samples for the given frame index.
                 *
                 * Thin wrapper around @ref cumulativeTicks that
                 * returns @c cumulative(frameIndex+1) @c -
                 * @c cumulative(frameIndex), i.e. the exact number of
                 * audio samples belonging to frame N.  For 48 kHz @
                 * 29.97 fps this yields the standard
                 * 1601/1602/1601/1602/1602 cadence summing to exactly
                 * 8008 every 5 frames.
                 *
                 * For integer-cadence rates (24, 25, 30, 50, 60)
                 * every frame returns the same constant value.  For
                 * fractional rates the result alternates in a fixed
                 * cycle whose period equals the @c denominator (e.g.
                 * 1001 for NTSC — though the visible pattern repeats
                 * every 5 frames).
                 *
                 * @param sampleRate Audio sample rate in Hz (e.g. 48000).
                 * @param frameIndex Zero-based frame index since the start of the run.
                 * @return Number of samples to emit for that frame, or 0 if the
                 *         frame rate is invalid or @p sampleRate is non-positive.
                 */
                size_t samplesPerFrame(int64_t sampleRate, int64_t frameIndex) const {
                        if (!isValid() || sampleRate <= 0 || frameIndex < 0) return 0;
                        const int64_t cumNow = cumulativeTicks(sampleRate, frameIndex);
                        const int64_t cumNext = cumulativeTicks(sampleRate, frameIndex + 1);
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
                RationalType _fps;
};

/**
 * @brief One named entry in @ref FrameRate::wellKnownRates.
 *
 * @c label is a human-readable form ("23.98", "29.97", "60", etc.)
 * suitable for a dropdown UI; @c rate is the canonical FrameRate
 * value.  The label is intentionally label-style (not just the
 * numeric form) so future entries can carry context like "(NTSC)"
 * without polluting the Rational printer.
 *
 * Defined out-of-line because the @c FrameRate field requires the
 * enclosing class to be complete.
 */
struct FrameRate::WellKnown {
                String    label;
                FrameRate rate;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::FrameRate);
