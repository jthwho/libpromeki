/*****************************************************************************
 * timecode.cpp
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

#include <sstream>
#include <promeki/timecode.h>
#include <promeki/logger.h>

namespace promeki {

Timecode &Timecode::operator++() {
        _frame++;
        if(_frame >= fps()) {
                _frame = 0;
                _sec++;
                if(_sec >= 60) {
                        _sec = 0;
                        _min++;
                        if(_min >= 60) {
                                _min = 0;
                                _hour++;
                                if(_hour >= 24) _hour = 0;
                        }
                }
        }
        if(isDropFrame() && (_min % 10 != 0) && (_sec == 0) && (_frame == 0)) _frame = 2;
        return *this;
}

Timecode &Timecode::operator--() {
        if(isDropFrame() && (_min % 10 != 0) && (_sec == 0) && (_frame == 2)) _frame = 0;
        if(_frame == 0) {
                _frame = fps() - 1;
                if(_sec == 0) {
                        _sec = 59;
                        if(_min == 0) {
                                _min = 59;
                                if(_hour == 0) _hour = 23;
                                else _hour--;
                        } else _min--;
                } else _sec--;
        } else _frame--;
        return *this;
}

Timecode Timecode::fromFrameNumber(const Mode &mode, FrameNumber frameNumber) {
        if(!mode.isValid() || mode.fps() == 0) return Timecode(mode); 
        DigitType h, m, s, f;
        int framesPerSec = mode.fps();
        int framesPerMin = framesPerSec * 60;
        int framesPerHour = framesPerMin * 60;
        FrameNumber ct = frameNumber;
        h = ct / framesPerHour; ct %= framesPerHour;
        m = ct / framesPerMin;  ct %= framesPerMin;       
        s = ct / framesPerSec;
        f = ct % framesPerSec;
        Timecode tc(mode, h, m, s, f);
        if(mode.isDropFrame() && mode.fps() == 30) {
                // FIXME: There's got to be a better way to do this.
                int drop = (h * 108) + ((m - (m / 10)) * 2);
                for(int i = 0; i < drop; i++) ++tc;
        }
        return tc;
}

std::pair<Timecode, Error> Timecode::fromString(const String &str) {
        std::istringstream iss(str);
        int h, m, s, f;
        char sv[4];
        FlagsType flags = Valid;
        int fps = 0;
        if(!(iss >> h)) { 
                promekiErr("Failed to parse hour from '%s'", str.cstr());
                return { Timecode(), Error::Invalid };

        }
        if(!(iss >> sv[0])) {
                promekiErr("Failed to parse h:m delimiter from '%s'", str.cstr());
                return { Timecode(), Error::Invalid };
        }
        if(!(iss >> m)) {
                promekiErr("Failed to parse minute from '%s'", str.cstr());
                return { Timecode(), Error::Invalid };
        }
        if(!(iss >> sv[1])) {
                promekiErr("Failed to parse m:s delimiter from '%s'", str.cstr());
                return { Timecode(), Error::Invalid };
        }
        if(!(iss >> s)) {
                promekiErr("Failed to parse seconds from '%s'", str.cstr());
                return { Timecode(), Error::Invalid };
        }
        if(!(iss >> sv[2])) {
                promekiErr("Failed to parse s:f delimiter from '%s'", str.cstr());
                return { Timecode(), Error::Invalid };
        }
        if(!(iss >> f)) {
                promekiErr("Failed to parse frame from '%s'", str.cstr());
                return { Timecode(), Error::Invalid };
        }
        if((iss >> sv[3]) && sv[3] == '/') {
                if(!(iss >> fps)) {
                        promekiErr("Failed to parse fps in '%s'", str.cstr());
                        return { Timecode(), Error::Invalid };
                }
        }
        if(sv[2] == ';' || sv[2] == ',' || sv[1] == ';' || sv[0] == ';') {
                flags |= DropFrame;
                if(fps == 0) fps = 30;
        }
        if(sv[2] == '.' || sv[2] == ',') {
                flags |= FirstField;
        }
        //promekiInfo("Parsed %d fps, 0x%X flags, %02d:%02d:%02d:%02d", (int)fps, (unsigned int)flags, (int)h, (int)m, (int)s, (int)f);
        return { Timecode(Mode(fps, flags), h, m, s, f), Error() };
}

std::pair<String, Error> Timecode::toString() const {
        if(!isValid()) return { String(), Error::Invalid };
        FrameRate rate = fps();
        if(_hour > 99 || _min > 99 || _sec > 99 || _frame > 99 || rate > 99) {
                return { String(), Error::TooLarge };
        }
        char div2;
        char div1;
        if(isDropFrame()) {
                div1 = ',';
                div2 = ';';
        } else {
                div1 = '.';
                div2 = ':';
        }
        char buf[32];
        char *b = buf;
        *b++  = _hour / 10 + '0';
        *b++  = _hour % 10 + '0';
        *b++  = div2;
        *b++  = _min / 10 + '0';
        *b++  = _min % 10 + '0';
        *b++  = div2;
        *b++  = _sec / 10 + '0';
        *b++  = _sec % 10 + '0';
        *b++  = isFirstField() ? div1 : div2;
        *b++  = _frame / 10 + '0';
        *b++ = _frame % 10 + '0';
        *b++ = '/';
        *b++ = rate / 10 + '0';
        *b++ = rate % 10 + '0';
        *b++ = 0;
        return { buf, Error() };
}

std::pair<Timecode::FrameNumber, Error> Timecode::toFrameNumber() const {
        if(!isValid()) return { 0, Error::Invalid };
        if(fps() == 0) return { 0, Error::NoFrameRate };
        int framesPerSec = fps();
        int framesPerMin = framesPerSec * 60;
        int framesPerHour = framesPerMin * 60;
        FrameNumber ret = _frame + (_sec * framesPerSec) + (_min * framesPerMin) + (_hour * framesPerHour);
	/* If this is drop frame timecode make changes to the frame number */
	if(isDropFrame() && fps() == 30) {
		ret -= (_hour * 108);
		ret -= ((_min - (_min / 10)) * 2);
	}
        return { ret, Error() };
}

} // namespace promeki

