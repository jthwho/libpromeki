/**
 * @file      timecode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/core/timecode.h>
#include <promeki/core/logger.h>

PROMEKI_NAMESPACE_BEGIN

VtcTimecode Timecode::toVtc() const {
        VtcTimecode vtc;
        vtc.hour = _hour;
        vtc.min = _min;
        vtc.sec = _sec;
        vtc.frame = _frame;
        vtc.userbits = 0;
        vtc.format = _mode.vtcFormat();
        vtc.flags = 0;
        if(_flags & FirstField) vtc.flags |= VTC_TC_FLAG_FIELD_1;
        return vtc;
}

void Timecode::fromVtc(const VtcTimecode &vtc) {
        _hour = vtc.hour;
        _min = vtc.min;
        _sec = vtc.sec;
        _frame = vtc.frame;
        if(vtc.format) {
                _mode = Mode(vtc.format);
        } else {
                // Parsed digits but no format determined — valid but format-less
                _mode = Mode(0u, 0u);
        }
        _flags = 0;
        if(vtc.flags & VTC_TC_FLAG_FIELD_1) _flags |= FirstField;
}

Timecode &Timecode::operator++() {
        VtcTimecode vtc = toVtc();
        vtc_timecode_increment(&vtc);
        fromVtc(vtc);
        return *this;
}

Timecode &Timecode::operator--() {
        VtcTimecode vtc = toVtc();
        vtc_timecode_decrement(&vtc);
        fromVtc(vtc);
        return *this;
}

Timecode Timecode::fromFrameNumber(const Mode &mode, FrameNumber frameNumber) {
        if(!mode.isValid() || mode.fps() == 0) return Timecode(mode);
        VtcTimecode vtc;
        VtcError err = vtc_timecode_from_frames(&vtc, mode.vtcFormat(), frameNumber);
        if(err != VTC_ERR_OK) return Timecode(mode);
        Timecode tc(mode);
        tc._hour = vtc.hour;
        tc._min = vtc.min;
        tc._sec = vtc.sec;
        tc._frame = vtc.frame;
        return tc;
}

std::pair<Timecode, Error> Timecode::fromString(const String &str) {
        VtcTimecode vtc;
        VtcError err = vtc_timecode_from_string(&vtc, str.cstr());
        if(err != VTC_ERR_OK) {
                promekiErr("Failed to parse timecode from '%s': %s", str.cstr(), vtc_error_string(err));
                return { Timecode(), Error::Invalid };
        }
        Timecode tc;
        tc.fromVtc(vtc);
        return { tc, Error() };
}

std::pair<String, Error> Timecode::toString(const VtcStringFormat *fmt) const {
        if(!isValid()) return { String(), Error::Invalid };
        if(!_mode.hasFormat()) return { String(), Error::NoFrameRate };
        VtcTimecode vtc = toVtc();
        char buf[64];
        VtcError err = vtc_timecode_to_string(&vtc, fmt, buf, sizeof(buf));
        if(err != VTC_ERR_OK) {
                return { String(), Error::Invalid };
        }
        return { String(buf), Error() };
}

std::pair<Timecode::FrameNumber, Error> Timecode::toFrameNumber() const {
        if(!isValid()) return { 0, Error::Invalid };
        if(!_mode.hasFormat()) return { 0, Error::NoFrameRate };
        VtcTimecode vtc = toVtc();
        uint64_t frameNum;
        VtcError err = vtc_timecode_to_frames(&vtc, &frameNum);
        if(err != VTC_ERR_OK) {
                if(err == VTC_ERR_NO_FRAME_RATE) return { 0, Error::NoFrameRate };
                return { 0, Error::Invalid };
        }
        return { frameNum, Error() };
}

PROMEKI_NAMESPACE_END
