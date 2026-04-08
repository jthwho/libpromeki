/**
 * @file      timecode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/timecode.h>
#include <promeki/logger.h>

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

Result<Timecode> Timecode::fromString(const String &str) {
        VtcTimecode vtc;
        VtcError err = vtc_timecode_from_string(&vtc, str.cstr());
        if(err != VTC_ERR_OK) {
                promekiErr("Failed to parse timecode from '%s': %s", str.cstr(), vtc_error_string(err));
                return makeError<Timecode>(Error::Invalid);
        }
        Timecode tc;
        tc.fromVtc(vtc);
        return makeResult(tc);
}

Result<String> Timecode::toString(const VtcStringFormat *fmt) const {
        if(!isValid()) return makeError<String>(Error::Invalid);
        if(!_mode.hasFormat()) return makeError<String>(Error::NoFrameRate);
        VtcTimecode vtc = toVtc();
        char buf[64];
        VtcError err = vtc_timecode_to_string(&vtc, fmt, buf, sizeof(buf));
        if(err != VTC_ERR_OK) {
                return makeError<String>(Error::Invalid);
        }
        return makeResult(String(buf));
}

Result<Timecode::FrameNumber> Timecode::toFrameNumber() const {
        if(!isValid()) return makeError<FrameNumber>(Error::Invalid);
        if(!_mode.hasFormat()) return makeError<FrameNumber>(Error::NoFrameRate);
        VtcTimecode vtc = toVtc();
        uint64_t frameNum;
        VtcError err = vtc_timecode_to_frames(&vtc, &frameNum);
        if(err != VTC_ERR_OK) {
                if(err == VTC_ERR_NO_FRAME_RATE) return makeError<FrameNumber>(Error::NoFrameRate);
                return makeError<FrameNumber>(Error::Invalid);
        }
        return makeResult(static_cast<FrameNumber>(frameNum));
}

PROMEKI_NAMESPACE_END
