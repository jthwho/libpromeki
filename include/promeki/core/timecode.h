/**
 * @file      core/timecode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/error.h>
#include <vtc/vtc.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Class for holding and manipulating timecode.
 *
 * While this class supports all the capabilities of SMPTE timecode, it exceeds
 * it in many ways.  It makes no restrictions on hour counts above 23, or frame
 * rates above 30.  It will, of course, attempt to do the best it can when asked
 * to output a SMPTE timecode (i.e. the hours will be modulo 24).
 *
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
                 * @param str The string to parse (e.g. "01:00:00:00").
                 * @return A pair of the parsed Timecode and an Error indicating success or failure.
                 */
                static std::pair<Timecode, Error> fromString(const String &str);

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

                bool operator>(const Timecode &other) const {
                        return toFrameNumber() > other.toFrameNumber();
                }

                bool operator<(const Timecode &other) const {
                        return toFrameNumber() < other.toFrameNumber();
                }

                bool operator>=(const Timecode &other) const {
                        return toFrameNumber() >= other.toFrameNumber();
                }

                bool operator<=(const Timecode &other) const {
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
                operator String() const { return toString().first; }
                /**
                 * @brief Converts the timecode to a string.
                 * @param fmt The string format to use (default: SMPTE).
                 * @return A pair of the formatted string and an Error.
                 */
                std::pair<String, Error> toString(const VtcStringFormat *fmt = &VTC_STR_FMT_SMPTE) const;
                /**
                 * @brief Converts the timecode to an absolute frame number.
                 * @return A pair of the frame number and an Error.
                 */
                std::pair<FrameNumber, Error> toFrameNumber() const;

        private:
                VtcTimecode toVtc() const;
                void fromVtc(const VtcTimecode &vtc);

                Mode            _mode;
                FlagsType       _flags  = 0;
                DigitType       _hour   = 0;
                DigitType       _min    = 0;
                DigitType       _sec    = 0;
                DigitType       _frame  = 0;
};


PROMEKI_NAMESPACE_END
