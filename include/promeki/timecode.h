/**
 * @file      timecode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/enums_timecode.h>
#include <promeki/framenumber.h>
#include <promeki/array.h>
#include <promeki/datatype.h>
#include <promeki/timecodeuserbits.h>
#include <vtc/vtc.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;
class FrameRate;
class Duration;

/**
 * @brief Class for holding and manipulating timecode.
 * @ingroup time
 *
 * While this class supports all the capabilities of SMPTE timecode, it exceeds
 * it in many ways.  It makes no restrictions on hour counts above 23, or frame
 * rates above 30.  It will, of course, attempt to do the best it can when asked
 * to output a SMPTE timecode (i.e. the hours will be modulo 24).
 *
 *
 * @par Example
 * @code
 * // Create a 24fps timecode at 01:00:00:00
 * Timecode tc(Timecode::NDF24, 1, 0, 0, 0);
 * ++tc;  // advance to 01:00:00:01
 *
 * // Convert to string
 * String str = tc.toString();  // "01:00:00:01"
 *
 * // From absolute frame number
 * Timecode tc2 = Timecode::fromFrameNumber(Timecode::NDF24, 86400);
 *
 * // Parse from string
 * auto [tc3, err2] = Timecode::fromString("10:30:00:00");
 * @endcode
 * Internally delegates all timecode logic to libvtc.
 */
class Timecode {
        public:
                PROMEKI_DATATYPE(Timecode, DataTypeTimecode, 2)

                using DigitType = uint8_t;  ///< Type used for individual timecode digit fields.
                using FlagsType = uint32_t; ///< Type used for timecode flag bitmasks.

                /** @brief Writes the canonical wire body. */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads the canonical wire body for wire version @p V. */
                template <uint32_t V> static Result<Timecode> readFromStream(DataStream &s);

                /**
                 * @brief Standard timecode-rate families covered by ST 12-1 / ST 12-3.
                 *
                 * The naming reflects the digit-family (the BCD rate emitted on the wire)
                 * rather than the wall-clock rate.  An @c NDF30 timecode counts 30 frames
                 * per second; whether that wall-clock rate is exactly 30 fps or the NTSC
                 * 30000/1001 fps is a wall-clock-conversion concern that lives on the
                 * @ref toRuntime / LTC encoder side, not on the value type.
                 *
                 * @c DF30 / @c DF60 / @c DF120 are the only ST-defined drop-frame rates
                 * (29.97, 59.94, 119.88); their digit math is identical to their
                 * integer-rate NDF counterparts, with drop-frame compensation on top.
                 */
                enum TimecodeType {
                        NDF24,        ///< 24 fps non-drop-frame (VTC_FORMAT_24).
                        NDF25,        ///< 25 fps non-drop-frame (VTC_FORMAT_25).
                        NDF30,        ///< 30 fps non-drop-frame (VTC_FORMAT_30_NDF).
                        DF30,         ///< 29.97 fps drop-frame  (VTC_FORMAT_29_97_DF).
                        NDF48,        ///< 48 fps non-drop-frame (VTC_FORMAT_48, 24×2).
                        NDF50,        ///< 50 fps non-drop-frame (VTC_FORMAT_50, 25×2).
                        NDF60,        ///< 60 fps non-drop-frame (VTC_FORMAT_60, 30×2).
                        DF60,         ///< 59.94 fps drop-frame  (VTC_FORMAT_59_94_DF, 30×2).
                        NDF72,        ///< 72 fps non-drop-frame (VTC_FORMAT_72, 24×3).
                        NDF96,        ///< 96 fps non-drop-frame (VTC_FORMAT_96, 24×4).
                        NDF100,       ///< 100 fps non-drop-frame (VTC_FORMAT_100, 25×4).
                        NDF120,       ///< 120 fps non-drop-frame (VTC_FORMAT_120_30X4, 30×4).
                        DF120,        ///< 119.88 fps drop-frame  (VTC_FORMAT_119_88_DF, 30×4).
                        NDF120_24x5,  ///< 120 fps non-drop-frame (VTC_FORMAT_120_24X5, 24×5).
                };

                /** @brief Timecode flag bits. */
                enum Flags {
                        DropFrame = 0x00000001, ///< Indicates drop-frame timecode
                        FirstField = 0x00000002 ///< Indicates the first field of an interlaced frame
                };

                /**
                 * @brief Describes the timecode mode (frame rate and drop-frame status).
                 *
                 * Wraps a `const VtcFormat*` from libvtc together with a validity flag.
                 */
                class Mode {
                        public:
                                /** @brief Constructs an invalid, default mode. */
                                Mode() = default;

                                /** @brief Constructs a mode from a libvtc format pointer.
                                 *  @param format Pointer to a VtcFormat, or nullptr for an invalid mode. */
                                Mode(const VtcFormat *format) : _format(format), _valid(format != nullptr) {}

                                /**
                                 * @brief Constructs a mode from a frame rate and flag set.
                                 *
                                 * Walks the libvtc @c VTC_STANDARD_FORMATS table and selects
                                 * the format whose actual fps (@c tc_fps × (hfr_n + 1)) and
                                 * drop-frame flag match the request.  This is how HFR rates
                                 * (48 / 50 / 60 / 72 / 96 / 100 / 120) lock onto the
                                 * correct ST 12-3 format pointer with HFR_N populated; the
                                 * previous implementation hit
                                 * @c vtc_format_find_or_create(fps, 1, fps, …) which never
                                 * matched any HFR format and silently created a malformed
                                 * custom one that bypassed libvtc's HFR machinery.
                                 *
                                 * Falls back to @c vtc_format_find_or_create only when no
                                 * standard format matches the requested rate.
                                 *
                                 * @param fps  Frames per second (wall-clock integer rate).
                                 * @param flags Bitmask of Flags values (e.g. DropFrame).
                                 */
                                Mode(uint32_t fps, uint32_t flags) : _valid(true) {
                                        if (fps == 0) return;
                                        const bool wantDf = (flags & DropFrame) != 0;
                                        // Prefer integer-rate formats over their NTSC
                                        // (1000/1001) siblings.  Without an explicit
                                        // wall-clock-rate hint a caller asking for "30 fps
                                        // NDF" wants exact 30, not 29.97.
                                        for (int i = 0; i < VTC_STANDARD_FORMATS_COUNT; ++i) {
                                                const VtcFormat *f = VTC_STANDARD_FORMATS[i];
                                                if (vtc_format_fps(f) != fps) continue;
                                                if (vtc_format_is_drop_frame(f) != wantDf) continue;
                                                if (vtc_format_is_ntsc(f)) continue;
                                                _format = f;
                                                return;
                                        }
                                        // Second pass: accept NTSC variants if no integer
                                        // match exists at this rate (the only DF HFR rates
                                        // 59.94 DF and 119.88 DF are NTSC-only).
                                        for (int i = 0; i < VTC_STANDARD_FORMATS_COUNT; ++i) {
                                                const VtcFormat *f = VTC_STANDARD_FORMATS[i];
                                                if (vtc_format_fps(f) != fps) continue;
                                                if (vtc_format_is_drop_frame(f) != wantDf) continue;
                                                _format = f;
                                                return;
                                        }
                                        uint32_t vtcFlags = 0;
                                        if (wantDf) vtcFlags |= VTC_FORMAT_FLAG_DROP_FRAME;
                                        _format = vtc_format_find_or_create(fps, 1, fps, vtcFlags);
                                }

                                /** @brief Constructs a mode from a standard TimecodeType.
                                 *  @param type One of the TimecodeType enum values. */
                                Mode(TimecodeType type) : _valid(true) {
                                        switch (type) {
                                                case NDF24:       _format = &VTC_FORMAT_24;          break;
                                                case NDF25:       _format = &VTC_FORMAT_25;          break;
                                                case NDF30:       _format = &VTC_FORMAT_30_NDF;      break;
                                                case DF30:        _format = &VTC_FORMAT_29_97_DF;    break;
                                                case NDF48:       _format = &VTC_FORMAT_48;          break;
                                                case NDF50:       _format = &VTC_FORMAT_50;          break;
                                                case NDF60:       _format = &VTC_FORMAT_60;          break;
                                                case DF60:        _format = &VTC_FORMAT_59_94_DF;    break;
                                                case NDF72:       _format = &VTC_FORMAT_72;          break;
                                                case NDF96:       _format = &VTC_FORMAT_96;          break;
                                                case NDF100:      _format = &VTC_FORMAT_100;         break;
                                                case NDF120:      _format = &VTC_FORMAT_120_30X4;    break;
                                                case DF120:       _format = &VTC_FORMAT_119_88_DF;   break;
                                                case NDF120_24x5: _format = &VTC_FORMAT_120_24X5;    break;
                                        }
                                }

                                bool operator==(const Mode &other) const {
                                        if (_format == other._format) return _valid == other._valid;
                                        if (_format == nullptr || other._format == nullptr) {
                                                // Both valid with no format = equal
                                                return _valid == other._valid && _format == other._format;
                                        }
                                        return _format->tc_fps == other._format->tc_fps &&
                                               vtc_format_is_drop_frame(_format) ==
                                                       vtc_format_is_drop_frame(other._format);
                                }

                                bool operator!=(const Mode &other) const { return !(*this == other); }

                                /** @brief Returns the frames-per-second rate, or 0 if no format is set. */
                                uint32_t fps() const { return _format ? vtc_format_fps(_format) : 0; }
                                /** @brief Returns true if this mode has been explicitly set. */
                                bool isValid() const { return _valid; }
                                /** @brief Returns true if this mode uses drop-frame counting. */
                                bool isDropFrame() const { return _format ? vtc_format_is_drop_frame(_format) : false; }
                                /** @brief Returns true if a VtcFormat pointer is assigned. */
                                bool hasFormat() const { return _format != nullptr; }

                                /**
                                 * @brief Returns the number of physical frames per ST 12-3 super-frame.
                                 *
                                 * For non-HFR formats this is 1 (one physical frame per super-frame).
                                 * For HFR formats this is @c vtc_format_hfr_n(format) + 1, i.e. the
                                 * "N" of the super-frame group per ST 12-3 §6.1 — 2 for 48/50/60,
                                 * 3 for 72, 4 for 96/100/120(30×4)/119.88, 5 for 120(24×5).
                                 *
                                 * Returns 1 when no format is attached.
                                 */
                                uint32_t framesPerSuperFrame() const {
                                        return _format ? vtc_format_hfr_n(_format) + 1u : 1u;
                                }

                                /**
                                 * @brief Returns the super-frame rate (the rate at which audio LTC
                                 *        codewords are emitted and the BCD super-frame digits cycle).
                                 *
                                 * Equals @c format->tc_fps — 24, 25 or 30 for every standard format.
                                 * Returns 0 when no format is attached.
                                 *
                                 * At HFR rates this is the rate audio LTC actually runs at; the
                                 * physical frame rate is @c fps() = superFrameRate() ×
                                 * framesPerSuperFrame().  See @ref Timecode::isHfr.
                                 */
                                uint32_t superFrameRate() const {
                                        return _format ? _format->tc_fps : 0u;
                                }

                                /** @brief Returns the underlying libvtc format pointer. */
                                const VtcFormat *vtcFormat() const { return _format; }

                        private:
                                const VtcFormat *_format = nullptr;
                                bool             _valid = false;
                };

                /**
                 * @brief Constructs a Timecode from a mode and absolute frame number.
                 *
                 * If @p frameNumber is @c Unknown the result is a default
                 * (invalid) Timecode.
                 *
                 * @param mode        The timecode mode (frame rate / drop-frame).
                 * @param frameNumber The absolute frame number to convert.
                 * @return A Timecode representing the given frame number,
                 *         or an invalid Timecode if the frame number is @c Unknown.
                 */
                static Timecode fromFrameNumber(const Mode &mode, const FrameNumber &frameNumber);

                /**
                 * @brief Parses a Timecode from its string representation.
                 *
                 * Recognised inputs:
                 * - Standard SMPTE strings such as @c "01:00:00:00" (NDF) or
                 *   @c "01:00:00;00" (DF, semicolon before frames).
                 * - The canonical invalid-timecode sentinel @c "--:--:--:--"
                 *   — returns a default-constructed (invalid) Timecode with
                 *   @c Error::Ok, so the round-trip @c toString() →
                 *   @c fromString() is lossless for invalid timecodes.
                 * - An empty string — treated identically to the invalid
                 *   sentinel: returns an invalid Timecode with @c Error::Ok.
                 * - Any other string that cannot be parsed returns a
                 *   default-constructed Timecode and a non-ok error code.
                 *
                 * @param str The string to parse (e.g. @c "01:00:00:00").
                 * @return A @ref Result holding the parsed Timecode on success
                 *         or a default-constructed Timecode and an error code
                 *         on parse failure.
                 * @see toString, toBcd64
                 */
                static Result<Timecode> fromString(const String &str);

                /** @brief Constructs a default (invalid) timecode. */
                Timecode() = default;
                /** @brief Constructs a timecode with the given mode and zeroed digits.
                 *  @param md The timecode mode. */
                Timecode(const Mode &md) : _mode(md) {}
                /** @brief Constructs a timecode with explicit digit values and no specific mode.
                 *  @param h Hour digit.
                 *  @param m Minute digit.
                 *  @param s Second digit.
                 *  @param f Frame digit. */
                Timecode(DigitType h, DigitType m, DigitType s, DigitType f)
                    : _mode(Mode(0u, 0u)), _hour(h), _min(m), _sec(s), _frame(f) {}
                /** @brief Constructs a timecode with a mode and explicit digit values.
                 *  @param md The timecode mode.
                 *  @param h Hour digit.
                 *  @param m Minute digit.
                 *  @param s Second digit.
                 *  @param f Frame digit. */
                Timecode(const Mode &md, DigitType h, DigitType m, DigitType s, DigitType f)
                    : _mode(md), _hour(h), _min(m), _sec(s), _frame(f) {}
                /** @brief Constructs a timecode by parsing a string representation.
                 *  @param str The string to parse. */
                Timecode(const String &str) {
                        auto [tc, err] = fromString(str);
                        if (err.isOk()) *this = tc;
                }

                bool operator==(const Timecode &other) const {
                        return _mode == other._mode && _hour == other._hour && _min == other._min &&
                               _sec == other._sec && _frame == other._frame &&
                               _colorFrame == other._colorFrame && _userbits == other._userbits;
                }

                bool operator!=(const Timecode &other) const { return !(*this == other); }

                /**
                 * @brief Ordering operators.
                 *
                 * Comparison strategy depends on the modes of the two
                 * operands:
                 *  - If both sides carry the same @ref Mode, digits
                 *    alone determine the order.  Drop-frame gaps do not
                 *    affect digit ordering within a single mode (they
                 *    only collapse frame counts).
                 *  - If either side lacks a libvtc format (i.e. the
                 *    timecode is invalid, or was parsed / constructed
                 *    with an unknown frame rate), digits are used
                 *    because no frame-number conversion is possible.
                 *  - Otherwise the two sides are at different valid
                 *    rates and we convert each to an absolute frame
                 *    number using its own rate and compare those.
                 */
                bool operator>(const Timecode &other) const {
                        if (compareByDigits(other)) return digitTuple() > other.digitTuple();
                        return toFrameNumber() > other.toFrameNumber();
                }

                bool operator<(const Timecode &other) const {
                        if (compareByDigits(other)) return digitTuple() < other.digitTuple();
                        return toFrameNumber() < other.toFrameNumber();
                }

                bool operator>=(const Timecode &other) const {
                        if (compareByDigits(other)) return digitTuple() >= other.digitTuple();
                        return toFrameNumber() >= other.toFrameNumber();
                }

                bool operator<=(const Timecode &other) const {
                        if (compareByDigits(other)) return digitTuple() <= other.digitTuple();
                        return toFrameNumber() <= other.toFrameNumber();
                }

                /** @brief Returns true if the timecode mode is valid. */
                bool isValid() const { return _mode.isValid(); }
                /** @brief Returns true if the timecode uses drop-frame counting. */
                bool isDropFrame() const { return _mode.isDropFrame(); }
                /** @brief Returns true if this timecode represents the first field of an interlaced frame. */
                bool isFirstField() const { return _flags & FirstField; }
                /** @brief Returns the frames-per-second rate. */
                uint32_t fps() const { return _mode.fps(); }
                /** @brief Returns the timecode mode. */
                Mode mode() const { return _mode; }
                /**
                 * @brief Sets the timecode mode, preserving the digit values.
                 * @param md The new mode.
                 */
                void setMode(const Mode &md) {
                        _mode = md;
                        return;
                }

                /** @brief Pre-increment: advances the timecode by one frame. */
                Timecode &operator++();
                /** @brief Post-increment: advances the timecode by one frame, returning the previous value. */
                Timecode operator++(int) {
                        Timecode ret = *this;
                        ++(*this);
                        return ret;
                }
                /** @brief Pre-decrement: moves the timecode back by one frame. */
                Timecode &operator--();
                /** @brief Post-decrement: moves the timecode back by one frame, returning the previous value. */
                Timecode operator--(int) {
                        Timecode ret = *this;
                        --(*this);
                        return ret;
                }

                /** @brief Sets the timecode digit fields directly.
                 *  @param h Hour digit.
                 *  @param m Minute digit.
                 *  @param s Second digit.
                 *  @param f Frame digit. */
                void set(DigitType h, DigitType m, DigitType s, DigitType f) {
                        _hour = h;
                        _min = m;
                        _sec = s;
                        _frame = f;
                }

                /** @brief Returns the hour digit. */
                DigitType hour() const { return _hour; }
                /** @brief Returns the minute digit. */
                DigitType min() const { return _min; }
                /** @brief Returns the second digit. */
                DigitType sec() const { return _sec; }
                /** @brief Returns the frame digit. */
                DigitType frame() const { return _frame; }

                /**
                 * @brief Returns the color-frame flag (ST 12-1 §8.3.2 / Table 2 bit 11).
                 *
                 * The color-frame flag is part of the time-address word, not the ATC
                 * envelope.  Lives on @ref Timecode so it round-trips through every
                 * codec — LTC, VITC, ATC — without each one having to surface it
                 * separately.
                 */
                bool colorFrame() const { return _colorFrame; }
                /** @brief Sets the color-frame flag. */
                void setColorFrame(bool on) { _colorFrame = on; }

                /** @brief Returns the binary-group user bits (and BGF mode triple). */
                const TimecodeUserbits &userbits() const { return _userbits; }
                /** @brief Replaces the binary-group user bits. */
                void setUserbits(const TimecodeUserbits &ub) { _userbits = ub; }

                /**
                 * @brief Returns the super-frame index for this Timecode's @ref frame.
                 *
                 * Computed as <tt>frame() / mode().framesPerSuperFrame()</tt>.  At
                 * non-HFR rates this is identical to @ref frame.  At HFR rates this
                 * is the value that BCD-encodes into the codeword's super-frame
                 * digit slot — 0..23 at 24×N, 0..24 at 25×N, 0..29 at 30×N.
                 *
                 * Returns @ref frame unchanged when no Mode is attached.
                 */
                uint32_t superFrameIndex() const {
                        const uint32_t n = _mode.framesPerSuperFrame();
                        return n > 0u ? static_cast<uint32_t>(_frame) / n : static_cast<uint32_t>(_frame);
                }

                /**
                 * @brief Returns the frame-identifier number per ST 12-3 §6.3.
                 *
                 * Computed as <tt>frame() % mode().framesPerSuperFrame()</tt>.  At
                 * non-HFR rates this is always 0.  At HFR rates it ranges 0..N-1
                 * and drives the sub-frame identifier bits in the LTC / ATC_HFRTC
                 * codeword (per Phase 1 libvtc support).
                 *
                 * Returns 0 when no Mode is attached.
                 */
                uint32_t subFrameIndex() const {
                        const uint32_t n = _mode.framesPerSuperFrame();
                        return n > 0u ? static_cast<uint32_t>(_frame) % n : 0u;
                }

                /** @brief Returns @c true when this Timecode is at an HFR rate
                 *         (@c framesPerSuperFrame() > 1). */
                bool isHfr() const { return _mode.framesPerSuperFrame() > 1u; }

                /** @brief Returns @c true when @ref subFrameIndex is 0 — the first
                 *         physical frame of a super-frame.  Used by HFR codecs to
                 *         decide when to latch a new LTC codeword or ATC_HFRTC
                 *         packet payload. */
                bool isSuperFrameBoundary() const { return subFrameIndex() == 0u; }

                /** @brief Returns the underlying libvtc format pointer from the mode. */
                const VtcFormat *vtcFormat() const { return _mode.vtcFormat(); }

                /** @brief Implicit conversion to String using SMPTE format. */
                operator String() const { return toString(); }
                /**
                 * @brief Canonical SMPTE string form (cannot fail).
                 *
                 * The no-arg shape required by the project-wide
                 * @c String @c toString convention.  Delegates to
                 * @ref toFormatString with the default SMPTE format and
                 * discards the (extremely rare) libvtc internal error,
                 * returning an empty String in that case.  Callers that
                 * need the error path or a non-default format style
                 * should call @ref toFormatString directly.
                 *
                 * @return The formatted string, or empty on libvtc
                 *         internal failure.
                 */
                String toString() const { return toFormatString().first(); }

                /**
                 * @brief Converts the timecode to a string with a chosen format.
                 *
                 * Always returns a non-empty, printable string:
                 * - An **invalid** timecode (no mode, not constructed with
                 *   valid digits) returns @c "--:--:--:--", the canonical
                 *   invalid-timecode sentinel.  @ref fromString recognises
                 *   this sentinel and round-trips it back to a
                 *   default-constructed invalid Timecode.
                 * - A **format-less** timecode (mode is valid but has no
                 *   associated @c VtcFormat, e.g. constructed with fps=0)
                 *   returns the bare digits @c "HH:MM:SS:FF" without a
                 *   frame-rate suffix.
                 * - A **valid** timecode with a known format uses libvtc
                 *   and the supplied @p fmt to produce the standard
                 *   SMPTE-style string (e.g. @c "01:02:03:04" or
                 *   @c "01:02:03;04" for drop-frame).
                 *
                 * @param fmt The string format to use (default: SMPTE).
                 * @return A @ref Result holding the formatted string.
                 *         The error code in the returned @ref Result is
                 *         @c Error::Ok for all three cases above; an
                 *         error is only returned when libvtc itself
                 *         reports an internal failure (extremely rare).
                 * @see toString, fromString, TimecodePackFormat
                 */
                Result<String> toFormatString(const VtcStringFormat *fmt = &VTC_STR_FMT_SMPTE) const;
                /**
                 * @brief Converts the timecode to an absolute frame number.
                 *
                 * Returns an @c Unknown @ref FrameNumber when the timecode
                 * is invalid, has no associated frame rate, or libvtc
                 * fails to convert it (rare).
                 *
                 * @return The absolute frame number, or @c Unknown on failure.
                 */
                FrameNumber toFrameNumber() const;

                /**
                 * @brief Converts the timecode to a wall-clock duration at @p rate.
                 *
                 * The @c TimecodeType / @ref Mode of a Timecode carries only the
                 * digit-family (24, 25, 30, …), not whether the wall-clock rate is
                 * integer or NTSC fractional.  Wall-clock conversion is therefore
                 * a function of an externally-supplied @ref FrameRate: a 30-fps
                 * Timecode renders to one wall-clock duration at exactly 30 fps and
                 * to a slightly longer one at 30000/1001 fps.
                 *
                 * The duration is computed as @c (frameNumber × rate.frameDuration())
                 * after converting to an absolute frame number, so drop-frame
                 * compensation in the digit math is preserved.
                 *
                 * @param rate The wall-clock rate to interpret the digits against.
                 * @return A @ref Duration on success, or an Error if either the
                 *         Timecode or @p rate is invalid, or the frame-number
                 *         conversion fails.
                 */
                Result<Duration> toRuntime(const FrameRate &rate) const;

                /**
                 * @brief Packs the timecode into the 64-bit BCD time-address word.
                 *
                 * Produces the same set of fields that SMPTE 12M-1 (LTC) and
                 * SMPTE 12M-2 (VITC) carry in their respective time-address
                 * fields — eight BCD time digits, the binary groups
                 * (32 bits of user bits), the drop-frame flag, the color
                 * frame flag, and the binary group flags BGF0/BGF1/BGF2 —
                 * minus the wire-level framing (sync words, CRC, biphase
                 * mark transitions).
                 *
                 * The bit positions inside the returned 64-bit word match
                 * the bit positions defined for the chosen variant in
                 * SMPTE 12M (so a downstream consumer that already knows
                 * "LTC bit 11 = color frame flag" can apply that knowledge
                 * directly to bit 11 of the returned word).  The
                 * specification only differs at one bit position, bit 27 —
                 * see @ref TimecodePackFormat for the table.
                 *
                 * In @c TimecodePackFormat::Ltc mode this calls
                 * libvtc's @c vtc_ltc_pack and returns the lower 8 bytes of
                 * its 80-bit output (the 16-bit sync word at bits 64-79 is
                 * intentionally dropped — wire framing is the encoder's job,
                 * not this method's).  In @c TimecodePackFormat::Vitc
                 * mode this packs directly so bit 27 carries the
                 * field marker bit (sourced from @ref isFirstField), per
                 * SMPTE 12M-2 / 12-3.
                 *
                 * The bit ordering in the returned 64-bit word is the
                 * same as in @c vtc_ltc_pack: bit 0 = LSB of frame units,
                 * bit 63 = MSB of user-bit nibble 8.  Callers that want
                 * to transmit the word over a wire should pick a bit
                 * order convention and document it; libpromeki's
                 * @c ImageDataEncoder transmits MSB-first.
                 *
                 * @param fmt Which variant's bit interpretation to use
                 *            (default: @c TimecodePackFormat::Vitc).
                 * @return The 64-bit BCD time-address word.
                 * @see fromBcd64, TimecodePackFormat
                 */
                uint64_t toBcd64(TimecodePackFormat fmt = TimecodePackFormat::Vitc) const;

                /**
                 * @brief Unpacks a 64-bit BCD time-address word into a Timecode.
                 *
                 * Inverse of @ref toBcd64.  The supplied @p mode supplies
                 * the frame rate; the BCD word's drop-frame flag is
                 * resolved against @p mode according to these rules:
                 *
                 * - If the DF flag is **clear**, @p mode is used as-is.
                 * - If the DF flag is **set** and @p mode is invalid /
                 *   unknown, the result is at @c VTC_FORMAT_29_97_DF
                 *   (i.e. an unset DF flag combined with an unknown rate
                 *   yields the canonical 29.97 DF rate).
                 * - If the DF flag is **set** and @p mode is at a rate
                 *   that has a drop-frame sister format
                 *   (29.97 NDF → 29.97 DF, 30 NDF → 30 DF, 59.94 NDF →
                 *   59.94 DF), the result is upgraded to that DF sister.
                 * - If @p mode is already at the matching DF rate, the
                 *   result keeps that mode unchanged.
                 * - If the DF flag is **set** and @p mode is at a rate
                 *   that does not support drop-frame counting (24, 25,
                 *   integer 24/25-multiple HFR rates, etc.), an
                 *   @c Error::ConversionFailed is returned because the
                 *   BCD word and the requested mode are inconsistent.
                 *
                 * The @p fmt argument selects which standard's bit
                 * positions are used to extract the digits and flag
                 * bits.  The two variants only differ at bit 27 (see
                 * @ref TimecodePackFormat); for typical Vitc/Ltc data
                 * exchanged through @ref ImageDataEncoder, both produce
                 * identical digits.
                 *
                 * @param bcd  The 64-bit BCD word.
                 * @param fmt  Which variant's bit interpretation to use
                 *             (default: @c TimecodePackFormat::Vitc).
                 * @param mode The timecode mode the result should adopt
                 *             (subject to the DF rules above).
                 * @return A @ref Result holding the unpacked Timecode on
                 *         success, or an Error on inconsistency.
                 * @see toBcd64, TimecodePackFormat
                 */
                static Result<Timecode> fromBcd64(uint64_t bcd, TimecodePackFormat fmt, const Mode &mode);

                /** @brief Convenience overload — defaults to @c TimecodePackFormat::Vitc and unknown @ref Mode. */
                static Result<Timecode> fromBcd64(uint64_t bcd) {
                        return fromBcd64(bcd, TimecodePackFormat::Vitc, Mode());
                }

                /** @brief Convenience overload — uses @c TimecodePackFormat::Vitc, with caller-supplied @ref Mode. */
                static Result<Timecode> fromBcd64(uint64_t bcd, const Mode &mode) {
                        return fromBcd64(bcd, TimecodePackFormat::Vitc, mode);
                }

        private:
                VtcTimecode toVtc() const;
                void        fromVtc(const VtcTimecode &vtc);

                // Tuple of the four digit fields for lexicographic
                // ordering.  Used by the comparison operators whenever a
                // frame-number conversion would be undefined or
                // unnecessary (same mode, or either side lacks a rate).
                using DigitTuple = Array<DigitType, 4>;
                DigitTuple digitTuple() const { return DigitTuple(_hour, _min, _sec, _frame); }

                // Returns true when @ref operator< / @c > / @c <= / @c >=
                // should use digit ordering rather than converting to
                // absolute frame numbers.  See the operator block above
                // for the full rules.
                bool compareByDigits(const Timecode &other) const {
                        if (_mode == other._mode) return true;
                        return !_mode.hasFormat() || !other._mode.hasFormat();
                }

                Mode             _mode;
                FlagsType        _flags = 0;
                DigitType        _hour = 0;
                DigitType        _min = 0;
                DigitType        _sec = 0;
                DigitType        _frame = 0;
                bool             _colorFrame = false;
                TimecodeUserbits _userbits;
};


PROMEKI_NAMESPACE_END

/**
 * @brief @c std::formatter specialization for @ref promeki::Timecode.
 *
 * Demonstrates the bespoke-formatter pattern for a library type whose
 * @c toString() takes a parameter and returns a non-@c String type
 * (here, @c Result<String>).
 *
 * @par Format spec syntax
 * @code
 *   {}                  // default SMPTE  e.g. "01:00:00:00"
 *   {:smpte}            // explicit SMPTE
 *   {:smpte-fps}        // SMPTE with frame rate, e.g. "01:00:00:00/24"
 *   {:smpte-space}      // SMPTE with space-separated frame rate
 *   {:field}            // field-style separator, e.g. "01:00:00.00"
 * @endcode
 *
 * The hint may be followed by a colon and a standard string format spec
 * to apply width / fill / alignment to the rendered timecode:
 * @code
 *   {:smpte:>16}        // right-justified, width 16
 *   {:>16}              // default SMPTE, right-justified width 16
 *   {:smpte:*<16}       // left-justified, width 16, '*' fill
 * @endcode
 *
 * Unrecognized hints fall through to the standard string format parser,
 * so a stray spec like @c {:>16} still works.
 */
template <> struct std::formatter<promeki::Timecode> {
                enum class Style {
                        Smpte,         ///< @c VTC_STR_FMT_SMPTE (default).
                        SmpteWithFps,  ///< @c VTC_STR_FMT_SMPTE_WITH_FPS.
                        SmpteSpaceFps, ///< @c VTC_STR_FMT_SMPTE_SPACE_FPS.
                        Field          ///< @c VTC_STR_FMT_FIELD.
                };

                Style                            _style = Style::Smpte;
                std::formatter<std::string_view> _base;

                constexpr auto parse(std::format_parse_context &ctx) {
                        auto it = ctx.begin();
                        auto end = ctx.end();

                        // Try to match a recognised style keyword at the start of
                        // the spec.  Each candidate is checked in longest-first
                        // order so prefixes do not collide ("smpte-fps" must beat
                        // "smpte").  On match, advance past the keyword.
                        auto tryKeyword = [&](const char *kw, Style s) {
                                auto p = it;
                                while (*kw && p != end && *p == *kw) {
                                        ++p;
                                        ++kw;
                                }
                                if (*kw == 0 && (p == end || *p == '}' || *p == ':')) {
                                        it = p;
                                        _style = s;
                                        return true;
                                }
                                return false;
                        };

                        if (!tryKeyword("smpte-fps", Style::SmpteWithFps) &&
                            !tryKeyword("smpte-space", Style::SmpteSpaceFps) && !tryKeyword("smpte", Style::Smpte) &&
                            !tryKeyword("field", Style::Field)) {
                                // No keyword — leave _style at its default and let
                                // the base parser consume the entire remaining spec.
                        }

                        // A separating ':' between the hint and a standard string
                        // format spec is consumed here so the base parser sees a
                        // bare ">16" rather than ":>16".
                        if (it != end && *it == ':') ++it;

                        // Forward whatever remains to the standard string format
                        // parser so width / fill / alignment / precision still work.
                        ctx.advance_to(it);
                        return _base.parse(ctx);
                }

                template <typename FormatContext> auto format(const promeki::Timecode &tc, FormatContext &ctx) const {
                        const VtcStringFormat *fmt = &VTC_STR_FMT_SMPTE;
                        switch (_style) {
                                case Style::Smpte: fmt = &VTC_STR_FMT_SMPTE; break;
                                case Style::SmpteWithFps: fmt = &VTC_STR_FMT_SMPTE_WITH_FPS; break;
                                case Style::SmpteSpaceFps: fmt = &VTC_STR_FMT_SMPTE_SPACE_FPS; break;
                                case Style::Field: fmt = &VTC_STR_FMT_FIELD; break;
                        }
                        auto [s, err] = tc.toFormatString(fmt);
                        (void)err; // Formatter consumers do not get the error path.
                        return _base.format(std::string_view(s.cstr(), s.byteCount()), ctx);
                }
};

#endif // PROMEKI_ENABLE_CORE
