/**
 * @file      timecodeuserbits.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstdint>
#include <promeki/array.h>
#include <promeki/datatype.h>
#include <promeki/datetime.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief 32-bit user bits + BGF mode triple, per SMPTE ST 12-1 §8.4.
 * @ingroup time
 *
 * The eight 4-bit nibbles ("binary groups") in a timecode codeword
 * occupy bits 4-7, 12-15, 20-23, 28-31, 36-39, 44-47, 52-55, 60-63 of
 * the 80-bit LTC word; their interpretation is selected by the three
 * binary-group flag bits BGF0 / BGF1 / BGF2 per ST 12-1 Table 1.
 *
 * @c TimecodeUserbits packages the eight nibbles together with a
 * @ref Mode value that encodes (BGF2, BGF1, BGF0) as a single 3-bit
 * field.  This lets a single value rep ride on every Timecode and
 * round-trip through LTC pack/unpack, ATC, ImageDataEncoder/Decoder,
 * etc. without each carriage having to track BGF flags separately.
 *
 * Mode meanings per ST 12-1 §8.4.4:
 *
 *  - @ref Unspecified — character set unspecified (default; bits not
 *    interpreted).
 *  - @ref EightBitChars — eight-bit character set (ISO/IEC 646),
 *    typically used to carry four ASCII characters in the eight nibbles.
 *  - @ref ClockTime — binary groups carry a clock-time reference.
 *  - @ref Reserved — reserved by SMPTE; producers must not emit it.
 *  - @ref DateTimeZone — binary groups carry an ST 309 date / time-zone
 *    record.
 *  - @ref PageLine — binary groups carry an ST 262 page / line
 *    multiplexed record.
 *  - @ref DateTimeZoneClock — ST 309 date / time-zone combined with a
 *    clock-time reference.
 *  - @ref PageLineClock — ST 262 page / line combined with a clock-time
 *    reference.
 *
 * The class is plain-value; copies are independent.  Factories cover
 * the common BGF modes; the raw constructor is available for callers
 * (codec layers) that need to round-trip arbitrary bit content with a
 * mode tag they decode separately.
 *
 * @par Example
 * @code
 * // ASCII chars in BGF=001 mode
 * auto ub = TimecodeUserbits::fromAsciiChars("TAKE");
 * // Raw bits with an explicit mode
 * auto raw = TimecodeUserbits::fromRawBits(0xDEADBEEF, TimecodeUserbits::Unspecified);
 * // Round-trip through a different interpretation
 * auto re  = raw.reinterpret(TimecodeUserbits::EightBitChars);
 * @endcode
 */
class TimecodeUserbits {
        public:
                PROMEKI_DATATYPE(TimecodeUserbits, DataTypeTimecodeUserbits, 1)

                /// @brief Number of 4-bit nibbles carried by a user-bits value (eight).
                static constexpr size_t NibbleCount = 8;

                /// @brief Array of the eight binary-group nibbles, each masked to its
                ///        low 4 bits.  Index 0 corresponds to the first nibble
                ///        emitted on LTC (bits 4-7).
                using Nibbles = ::promeki::Array<uint8_t, NibbleCount>;

                /**
                 * @brief Binary-group-flag mode triple per ST 12-1 §8.4 Table 1.
                 *
                 * Encoded as the three-bit field (BGF2 BGF1 BGF0) so the numeric
                 * value matches the row index in Table 1.  Values @ref Reserved
                 * and any other unassigned future combinations may appear on the
                 * wire and should be preserved untouched on round-trip.
                 */
                enum Mode : uint8_t {
                        Unspecified       = 0b000, ///< BGF=000 — character set unspecified.
                        EightBitChars     = 0b001, ///< BGF=001 — ISO/IEC 646 eight-bit chars.
                        ClockTime         = 0b010, ///< BGF=010 — clock time reference.
                        Reserved          = 0b011, ///< BGF=011 — reserved (must not be emitted).
                        DateTimeZone      = 0b100, ///< BGF=100 — ST 309 date / time-zone.
                        PageLine          = 0b101, ///< BGF=101 — ST 262 page / line multiplex.
                        DateTimeZoneClock = 0b110, ///< BGF=110 — ST 309 + clock time.
                        PageLineClock     = 0b111, ///< BGF=111 — ST 262 + clock time.
                };

                /** @brief Default-constructs zeros + @ref Unspecified mode. */
                TimecodeUserbits() = default;

                /** @brief Constructs raw bits with an explicit mode.
                 *  @param bits Packed 32-bit value (nibble 0 occupies bits 0-3,
                 *              nibble 7 occupies bits 28-31).
                 *  @param m    BGF mode to attach. */
                static TimecodeUserbits fromRawBits(uint32_t bits, Mode m = Unspecified);

                /** @brief Constructs from explicit nibble values.
                 *  @param n Eight nibble values (low 4 bits of each used).
                 *  @param m BGF mode to attach. */
                static TimecodeUserbits fromNibbles(const Nibbles &n, Mode m = Unspecified);

                /**
                 * @brief Constructs ASCII-character user bits per ST 12-1 §8.4.4 BGF=001.
                 *
                 * The first four ASCII characters of @p s are packed into the
                 * eight nibbles, two nibbles per character (low nibble first).
                 * Shorter strings are right-padded with @c 0x00 nibbles.
                 *
                 * @param s String of up to four 7-bit ASCII characters.
                 * @return User bits with @ref EightBitChars mode.
                 */
                static TimecodeUserbits fromAsciiChars(const String &s);

                /**
                 * @brief Stub for ST 309 date / time-zone encoding (BGF=100).
                 *
                 * The ST 309 packing pass is implemented incrementally; until
                 * then this factory returns @ref Error::NotSupported so callers
                 * detect the gap without needing a separate capability probe.
                 *
                 * @param dt Date/time to encode (must be valid).
                 * @return On success a populated @c TimecodeUserbits with
                 *         @ref DateTimeZone mode; otherwise an Error.
                 */
                static Result<TimecodeUserbits> fromDateTimeZone(const DateTime &dt);

                /** @brief Returns the eight nibbles (low 4 bits each). */
                const Nibbles &nibbles() const { return _nibbles; }

                /** @brief Returns the packed 32-bit representation of the eight nibbles. */
                uint32_t toUint32() const;

                /** @brief Returns the BGF mode. */
                Mode mode() const { return _mode; }

                /** @brief Returns a copy of @c *this with @p m replacing the mode bits
                 *         and the raw nibbles preserved verbatim.
                 *
                 *  Useful when a parser wants to recover an arbitrary 32-bit user
                 *  payload and later reinterpret it under a different BGF mode. */
                TimecodeUserbits reinterpret(Mode m) const {
                        TimecodeUserbits out = *this;
                        out._mode = m;
                        return out;
                }

                /**
                 * @brief Returns the four ASCII characters if mode is @ref EightBitChars.
                 *
                 * @return The decoded string on success, or @ref Error::Invalid if the
                 *         current mode is not @ref EightBitChars.
                 */
                Result<String> asAsciiChars() const;

                /**
                 * @brief Returns the ST 309 date / time-zone if mode is one of the
                 *        ST 309-bearing modes (@ref DateTimeZone or
                 *        @ref DateTimeZoneClock).
                 *
                 * The decoder is implemented incrementally; until then this returns
                 * @ref Error::NotSupported when the mode would otherwise match.
                 *
                 * @return The decoded DateTime on success, or an Error.
                 */
                Result<DateTime> asDateTimeZone() const;

                /** @brief Returns @c true if the mode declares a clock-time reference
                 *         (any of @ref ClockTime / @ref DateTimeZoneClock /
                 *         @ref PageLineClock). */
                bool hasClockTimeReference() const {
                        return _mode == ClockTime || _mode == DateTimeZoneClock || _mode == PageLineClock;
                }

                /** @brief Field-wise equality. */
                bool operator==(const TimecodeUserbits &other) const {
                        return _mode == other._mode && _nibbles == other._nibbles;
                }

                /** @brief Inequality. */
                bool operator!=(const TimecodeUserbits &other) const { return !(*this == other); }

                /** @brief Returns a short human-readable summary suitable for logs. */
                String toString() const;

                /** @brief Returns a structured JSON representation. */
                JsonObject toJson() const;

                /** @brief Writes the canonical wire body via @ref PROMEKI_DATATYPE. */
                Error writeToStream(DataStream &s) const;

                /** @brief Reads the canonical wire body for wire version @p V. */
                template <uint32_t V> static Result<TimecodeUserbits> readFromStream(DataStream &s);

        private:
                Nibbles _nibbles{};
                Mode    _mode = Unspecified;
};

PROMEKI_NAMESPACE_END

/**
 * @brief @c std::formatter specialization for @ref promeki::TimecodeUserbits.
 *
 * Defaults to the @ref promeki::TimecodeUserbits::toString output (e.g.
 * @c "ub:U:0xDEADBEEF" where @c U is the one-letter mode code).  The
 * standard format spec (width / fill / alignment) is forwarded to the
 * underlying string formatter.
 */
template <> struct std::formatter<promeki::TimecodeUserbits> {
                std::formatter<std::string_view> _base;

                constexpr auto parse(std::format_parse_context &ctx) {
                        return _base.parse(ctx);
                }

                template <typename FormatContext>
                auto format(const promeki::TimecodeUserbits &ub, FormatContext &ctx) const {
                        promeki::String s = ub.toString();
                        return _base.format(std::string_view(s.cstr(), s.byteCount()), ctx);
                }
};

#endif // PROMEKI_ENABLE_CORE
