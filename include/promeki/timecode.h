/**
 * @file      timecode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <tuple>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/enums.h>
#include <vtc/vtc.h>

PROMEKI_NAMESPACE_BEGIN

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
 * auto [str, err] = tc.toString();  // "01:00:00:01"
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
                using FrameNumber = uint64_t;  ///< Type used to represent absolute frame numbers.
                using DigitType = uint8_t;    ///< Type used for individual timecode digit fields.
                using FlagsType = uint32_t;   ///< Type used for timecode flag bitmasks.

                /** @brief Standard timecode types. */
                enum TimecodeType {
                        NDF24,  ///< 24 fps non-drop-frame (maps to VTC_FORMAT_24)
                        NDF25,  ///< 25 fps non-drop-frame (maps to VTC_FORMAT_25)
                        NDF30,  ///< 30 fps non-drop-frame (maps to VTC_FORMAT_30_NDF)
                        DF30    ///< 29.97 fps drop-frame (maps to VTC_FORMAT_29_97_DF)
                };

                /** @brief Timecode flag bits. */
                enum Flags {
                        DropFrame       = 0x00000001,  ///< Indicates drop-frame timecode
                        FirstField      = 0x00000002   ///< Indicates the first field of an interlaced frame
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
                                 * @param fps  Frames per second.
                                 * @param flags Bitmask of Flags values (e.g. DropFrame).
                                 */
                                Mode(uint32_t fps, uint32_t flags) : _valid(true) {
                                        if(fps > 0) {
                                                uint32_t vtcFlags = 0;
                                                if(flags & DropFrame) vtcFlags |= VTC_FORMAT_FLAG_DROP_FRAME;
                                                _format = vtc_format_find_or_create(fps, 1, fps, vtcFlags);
                                        }
                                }

                                /** @brief Constructs a mode from a standard TimecodeType.
                                 *  @param type One of the TimecodeType enum values. */
                                Mode(TimecodeType type) : _valid(true) {
                                        switch(type) {
                                                case NDF24: _format = &VTC_FORMAT_24;       break;
                                                case NDF25: _format = &VTC_FORMAT_25;       break;
                                                case NDF30: _format = &VTC_FORMAT_30_NDF;   break;
                                                case DF30:  _format = &VTC_FORMAT_29_97_DF; break;
                                        }
                                }

                                bool operator==(const Mode &other) const {
                                        if(_format == other._format) return _valid == other._valid;
                                        if(_format == nullptr || other._format == nullptr) {
                                                // Both valid with no format = equal
                                                return _valid == other._valid && _format == other._format;
                                        }
                                        return _format->tc_fps == other._format->tc_fps &&
                                               vtc_format_is_drop_frame(_format) == vtc_format_is_drop_frame(other._format);
                                }

                                bool operator!=(const Mode &other) const {
                                        return !(*this == other);
                                }

                                /** @brief Returns the frames-per-second rate, or 0 if no format is set. */
                                uint32_t fps() const { return _format ? vtc_format_fps(_format) : 0; }
                                /** @brief Returns true if this mode has been explicitly set. */
                                bool isValid() const { return _valid; }
                                /** @brief Returns true if this mode uses drop-frame counting. */
                                bool isDropFrame() const { return _format ? vtc_format_is_drop_frame(_format) : false; }
                                /** @brief Returns true if a VtcFormat pointer is assigned. */
                                bool hasFormat() const { return _format != nullptr; }

                                /** @brief Returns the underlying libvtc format pointer. */
                                const VtcFormat *vtcFormat() const { return _format; }

                        private:
                                const VtcFormat *_format = nullptr;
                                bool _valid = false;
                };

                /**
                 * @brief Constructs a Timecode from a mode and absolute frame number.
                 * @param mode        The timecode mode (frame rate / drop-frame).
                 * @param frameNumber The absolute frame number to convert.
                 * @return A Timecode representing the given frame number.
                 */
                static Timecode fromFrameNumber(const Mode &mode, FrameNumber frameNumber);

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
                Timecode(DigitType h, DigitType m, DigitType s, DigitType f) :
                        _mode(Mode(0u, 0u)), _hour(h), _min(m), _sec(s), _frame(f) {}
                /** @brief Constructs a timecode with a mode and explicit digit values.
                 *  @param md The timecode mode.
                 *  @param h Hour digit.
                 *  @param m Minute digit.
                 *  @param s Second digit.
                 *  @param f Frame digit. */
                Timecode(const Mode &md, DigitType h, DigitType m, DigitType s, DigitType f) :
                        _mode(md), _hour(h), _min(m), _sec(s), _frame(f) {}
                /** @brief Constructs a timecode by parsing a string representation.
                 *  @param str The string to parse. */
                Timecode(const String &str) {
                        auto [tc, err] = fromString(str);
                        if(err.isOk()) *this = tc;
                }

                bool operator==(const Timecode &other) const {
                        return _mode == other._mode &&
                               _hour == other._hour &&
                               _min == other._min &&
                               _sec == other._sec &&
                               _frame == other._frame;
                }

                bool operator!=(const Timecode &other) const {
                        return !(*this == other);
                }

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
                        if(compareByDigits(other)) return digitTuple() > other.digitTuple();
                        return toFrameNumber().first() > other.toFrameNumber().first();
                }

                bool operator<(const Timecode &other) const {
                        if(compareByDigits(other)) return digitTuple() < other.digitTuple();
                        return toFrameNumber().first() < other.toFrameNumber().first();
                }

                bool operator>=(const Timecode &other) const {
                        if(compareByDigits(other)) return digitTuple() >= other.digitTuple();
                        return toFrameNumber().first() >= other.toFrameNumber().first();
                }

                bool operator<=(const Timecode &other) const {
                        if(compareByDigits(other)) return digitTuple() <= other.digitTuple();
                        return toFrameNumber().first() <= other.toFrameNumber().first();
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
                void setMode(const Mode &md) { _mode = md; return; }

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

                /** @brief Returns the underlying libvtc format pointer from the mode. */
                const VtcFormat *vtcFormat() const { return _mode.vtcFormat(); }

                /** @brief Implicit conversion to String using SMPTE format. */
                operator String() const { return toString().first(); }
                /**
                 * @brief Converts the timecode to a string.
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
                 * @see fromString, TimecodePackFormat
                 */
                Result<String> toString(const VtcStringFormat *fmt = &VTC_STR_FMT_SMPTE) const;
                /**
                 * @brief Converts the timecode to an absolute frame number.
                 * @return A @ref Result holding the frame number on success or
                 *         zero and an error code on failure.
                 */
                Result<FrameNumber> toFrameNumber() const;

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
                 * In @ref TimecodePackFormat::Ltc "Ltc" mode this calls
                 * libvtc's @c vtc_ltc_pack and returns the lower 8 bytes of
                 * its 80-bit output (the 16-bit sync word at bits 64-79 is
                 * intentionally dropped — wire framing is the encoder's job,
                 * not this method's).  In @ref TimecodePackFormat::Vitc
                 * "Vitc" mode this packs directly so bit 27 carries the
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
                 *            (default: @ref TimecodePackFormat::Vitc).
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
                 *             (default: @ref TimecodePackFormat::Vitc).
                 * @param mode The timecode mode the result should adopt
                 *             (subject to the DF rules above).
                 * @return A @ref Result holding the unpacked Timecode on
                 *         success, or an Error on inconsistency.
                 * @see toBcd64, TimecodePackFormat
                 */
                static Result<Timecode> fromBcd64(uint64_t bcd,
                                                  TimecodePackFormat fmt,
                                                  const Mode &mode);

                /** @brief Convenience overload — defaults to @ref TimecodePackFormat::Vitc and unknown @ref Mode. */
                static Result<Timecode> fromBcd64(uint64_t bcd) {
                        return fromBcd64(bcd, TimecodePackFormat::Vitc, Mode());
                }

                /** @brief Convenience overload — uses @ref TimecodePackFormat::Vitc, with caller-supplied @ref Mode. */
                static Result<Timecode> fromBcd64(uint64_t bcd, const Mode &mode) {
                        return fromBcd64(bcd, TimecodePackFormat::Vitc, mode);
                }

        private:
                VtcTimecode toVtc() const;
                void fromVtc(const VtcTimecode &vtc);

                // Tuple of the four digit fields for lexicographic
                // ordering.  Used by the comparison operators whenever a
                // frame-number conversion would be undefined or
                // unnecessary (same mode, or either side lacks a rate).
                using DigitTuple = std::tuple<DigitType, DigitType, DigitType, DigitType>;
                DigitTuple digitTuple() const { return DigitTuple(_hour, _min, _sec, _frame); }

                // Returns true when @ref operator< / @c > / @c <= / @c >=
                // should use digit ordering rather than converting to
                // absolute frame numbers.  See the operator block above
                // for the full rules.
                bool compareByDigits(const Timecode &other) const {
                        if(_mode == other._mode) return true;
                        return !_mode.hasFormat() || !other._mode.hasFormat();
                }

                Mode            _mode;
                FlagsType       _flags  = 0;
                DigitType       _hour   = 0;
                DigitType       _min    = 0;
                DigitType       _sec    = 0;
                DigitType       _frame  = 0;
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
template <>
struct std::formatter<promeki::Timecode> {
        enum class Style {
                Smpte,         ///< @c VTC_STR_FMT_SMPTE (default).
                SmpteWithFps,  ///< @c VTC_STR_FMT_SMPTE_WITH_FPS.
                SmpteSpaceFps, ///< @c VTC_STR_FMT_SMPTE_SPACE_FPS.
                Field          ///< @c VTC_STR_FMT_FIELD.
        };

        Style _style = Style::Smpte;
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
                        while(*kw && p != end && *p == *kw) { ++p; ++kw; }
                        if(*kw == 0 && (p == end || *p == '}' || *p == ':')) {
                                it = p;
                                _style = s;
                                return true;
                        }
                        return false;
                };

                if(!tryKeyword("smpte-fps",   Style::SmpteWithFps)
                   && !tryKeyword("smpte-space", Style::SmpteSpaceFps)
                   && !tryKeyword("smpte",       Style::Smpte)
                   && !tryKeyword("field",       Style::Field)) {
                        // No keyword — leave _style at its default and let
                        // the base parser consume the entire remaining spec.
                }

                // A separating ':' between the hint and a standard string
                // format spec is consumed here so the base parser sees a
                // bare ">16" rather than ":>16".
                if(it != end && *it == ':') ++it;

                // Forward whatever remains to the standard string format
                // parser so width / fill / alignment / precision still work.
                ctx.advance_to(it);
                return _base.parse(ctx);
        }

        template <typename FormatContext>
        auto format(const promeki::Timecode &tc, FormatContext &ctx) const {
                const VtcStringFormat *fmt = &VTC_STR_FMT_SMPTE;
                switch(_style) {
                        case Style::Smpte:         fmt = &VTC_STR_FMT_SMPTE; break;
                        case Style::SmpteWithFps:  fmt = &VTC_STR_FMT_SMPTE_WITH_FPS; break;
                        case Style::SmpteSpaceFps: fmt = &VTC_STR_FMT_SMPTE_SPACE_FPS; break;
                        case Style::Field:         fmt = &VTC_STR_FMT_FIELD; break;
                }
                auto [s, err] = tc.toString(fmt);
                (void)err;  // Formatter consumers do not get the error path.
                return _base.format(std::string_view(s.cstr(), s.byteCount()), ctx);
        }
};
