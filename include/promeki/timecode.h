/**
 * @file      timecode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <vtc/vtc.h>

PROMEKI_NAMESPACE_BEGIN

// Class for holding and manipulating timecode.  While this class supports all the
// capabilities of SMPTE timecode, it exceeds it in many ways.  It makes no
// restrictions on hour counts above 23, or frame rates above 30.  It will, of course
// attempt to do the best it can when asked to output a SMPTE timecode (i.e. the
// hours will be modulo 24).
//
// Internally delegates all timecode logic to libvtc.
class Timecode {
        public:
                using FrameNumber = uint64_t;
                using DigitType = uint8_t;
                using FlagsType = uint32_t;

                enum TimecodeType {
                        NDF24,
                        NDF25,
                        NDF30,
                        DF30
                };

                enum Flags {
                        DropFrame       = 0x00000001,
                        FirstField      = 0x00000002
                };

                class Mode {
                        public:
                                Mode() = default;

                                Mode(const VtcFormat *format) : _format(format), _valid(format != nullptr) {}

                                Mode(uint32_t fps, uint32_t flags) : _valid(true) {
                                        if(fps > 0) {
                                                uint32_t vtcFlags = 0;
                                                if(flags & DropFrame) vtcFlags |= VTC_FORMAT_FLAG_DROP_FRAME;
                                                _format = vtc_format_find_or_create(fps, 1, fps, vtcFlags);
                                        }
                                }

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

                                uint32_t fps() const { return _format ? vtc_format_fps(_format) : 0; }
                                bool isValid() const { return _valid; }
                                bool isDropFrame() const { return _format ? vtc_format_is_drop_frame(_format) : false; }
                                bool hasFormat() const { return _format != nullptr; }

                                const VtcFormat *vtcFormat() const { return _format; }

                        private:
                                const VtcFormat *_format = nullptr;
                                bool _valid = false;
                };

                static Timecode fromFrameNumber(const Mode &mode, FrameNumber frameNumber);

                static std::pair<Timecode, Error> fromString(const String &str);

                Timecode() = default;
                Timecode(const Mode &md) : _mode(md) {}
                Timecode(DigitType h, DigitType m, DigitType s, DigitType f) :
                        _mode(Mode(0u, 0u)), _hour(h), _min(m), _sec(s), _frame(f) {}
                Timecode(const Mode &md, DigitType h, DigitType m, DigitType s, DigitType f) :
                        _mode(md), _hour(h), _min(m), _sec(s), _frame(f) {}
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

                bool isValid() const { return _mode.isValid(); }
                bool isDropFrame() const { return _mode.isDropFrame(); }
                bool isFirstField() const { return _flags & FirstField; }
                uint32_t fps() const { return _mode.fps(); }
                Mode mode() const { return _mode; }

                Timecode &operator++();
                Timecode operator++(int) {
                        Timecode ret = *this;
                        ++(*this);
                        return ret;
                }
                Timecode &operator--();
                Timecode operator--(int) {
                        Timecode ret = *this;
                        --(*this);
                        return ret;
                }

                void set(DigitType h, DigitType m, DigitType s, DigitType f) {
                        _hour = h;
                        _min = m;
                        _sec = s;
                        _frame = f;
                }

                DigitType hour() const { return _hour; }
                DigitType min() const { return _min; }
                DigitType sec() const { return _sec; }
                DigitType frame() const { return _frame; }

                const VtcFormat *vtcFormat() const { return _mode.vtcFormat(); }

                operator String() const { return toString().first; }
                std::pair<String, Error> toString(const VtcStringFormat *fmt = &VTC_STR_FMT_SMPTE) const;
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
