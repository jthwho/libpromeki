/*****************************************************************************
 * timecode.h
 * May 06, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class Timecode {
        public:
                using FrameNumber = uint32_t;
                using DigitType = uint8_t;
                using FrameRate = uint8_t;
                using FlagsType = uint16_t;

                enum TimecodeType {
                        NDF24,
                        NDF25,
                        NDF30,
                        DF30
                };

                enum Flags {
                        Valid           = 0x00000001,
                        DropFrame       = 0x00000002,
                        FirstField      = 0x00000004
                };

                class Mode {
                        public:
                                Mode(FrameRate fps = 0, FlagsType flags = 0) : _fps(fps), _flags(flags) {}
                                Mode(TimecodeType type) {
                                        switch(type) {
                                                case NDF24:
                                                        _fps = 24;
                                                        _flags = Valid;
                                                        break;
                                                case NDF25:
                                                        _fps = 25;
                                                        _flags = Valid;
                                                        break;
                                                case NDF30:
                                                        _fps = 30;
                                                        _flags = Valid;
                                                        break;
                                                case DF30:
                                                        _fps = 30;
                                                        _flags = Valid | DropFrame;
                                                        break;
                                                default:
                                                        _fps = 0;
                                                        _flags = 0;
                                                        break;
                                        }
                                }
                                bool operator==(const Mode &other) const {
                                        return _fps == other._fps && _flags == other._flags;
                                }
                                bool operator!=(const Mode &other) const {
                                        return !(*this == other);
                                }
                                FrameRate fps() const { return _fps; }
                                FlagsType flags() const { return _flags; }
                                bool isValid() const { return _flags & Valid; }
                                bool isDropFrame() const { return _flags & DropFrame; }
                                bool isFirstField() const { return _flags & FirstField; }

                        private:
                                FrameRate       _fps;
                                FlagsType       _flags;
                };

                static Timecode fromFrameNumber(const Mode &mode, FrameNumber frameNumber);

                static std::pair<Timecode, Error> fromString(const String &str);

                Timecode() {}
                Timecode(const Mode &md) : _mode(md) {}
                Timecode(DigitType h, DigitType m, DigitType s, DigitType f) :
                        _mode(Mode(0, Valid)), _hour(h), _min(m), _sec(s), _frame(f) {}
                Timecode(const Mode &md, DigitType h, DigitType m, DigitType s, DigitType f) :
                        _mode(md), _hour(h), _min(m), _sec(s), _frame(f) {}
                Timecode(const String &str) {
                        auto [ tc, err] = fromString(str);
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
                bool isFirstField() const { return _mode.isFirstField(); }
                FrameRate fps() const { return _mode.fps(); }
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
                        return;
                }
   
                DigitType hour() const { return _hour; }
                DigitType min() const { return _min; }
                DigitType sec() const { return _sec; }
                DigitType frame() const { return _frame; }

                operator String() const { return toString().first; }
                std::pair<String, Error> toString() const;
                std::pair<FrameNumber, Error> toFrameNumber() const;

        private:
                Mode            _mode;
                DigitType       _hour   = 0;
                DigitType       _min    = 0;
                DigitType       _sec    = 0;
                DigitType       _frame  = 0;
};


PROMEKI_NAMESPACE_END
