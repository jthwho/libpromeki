/**
 * @file      timecodegenerator.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/core/timecodegenerator.h>

PROMEKI_NAMESPACE_BEGIN

TimecodeGenerator::TimecodeGenerator(const FrameRate &frameRate, bool dropFrame) :
        _frameRate(frameRate), _dropFrame(dropFrame) {
        deriveMode();
}

void TimecodeGenerator::setFrameRate(const FrameRate &frameRate) {
        _frameRate = frameRate;
        deriveMode();
        return;
}

void TimecodeGenerator::setDropFrame(bool df) {
        _dropFrame = df;
        deriveMode();
        return;
}

void TimecodeGenerator::setTimecode(const Timecode &tc) {
        _timecode = tc;
        _startTimecode = tc;
        return;
}

void TimecodeGenerator::jam(const Timecode &tc) {
        _timecode = tc;
        return;
}

Timecode TimecodeGenerator::advance() {
        Timecode ret = _timecode;
        _frameCount++;
        switch(_runMode) {
                case Forward:
                        ++_timecode;
                        break;
                case Reverse:
                        --_timecode;
                        break;
                case Still:
                        break;
        }
        return ret;
}

void TimecodeGenerator::reset() {
        _frameCount = 0;
        _timecode = _startTimecode;
        return;
}

void TimecodeGenerator::deriveMode() {
        unsigned int num = _frameRate.numerator();
        unsigned int den = _frameRate.denominator();

        if(num == 0 || den == 0) {
                _mode = Timecode::Mode();
                return;
        }

        // Check for well-known rates
        // 24000/1001 or 24/1
        if((num == 24000 && den == 1001) || (num == 24 && den == 1)) {
                _mode = Timecode::Mode(Timecode::NDF24);
                _dropFrame = false;
                return;
        }

        // 25/1
        if(num == 25 && den == 1) {
                _mode = Timecode::Mode(Timecode::NDF25);
                _dropFrame = false;
                return;
        }

        // 30000/1001 (29.97) — only rate that supports drop-frame
        if(num == 30000 && den == 1001) {
                if(_dropFrame) {
                        _mode = Timecode::Mode(Timecode::DF30);
                } else {
                        _mode = Timecode::Mode(Timecode::NDF30);
                }
                return;
        }

        // 30/1
        if(num == 30 && den == 1) {
                _mode = Timecode::Mode(Timecode::NDF30);
                _dropFrame = false;
                return;
        }

        // Non-standard rate — use libvtc custom format registry
        // Drop-frame is only valid for 30000/1001
        _dropFrame = false;
        double fps = (double)num / (double)den;
        uint32_t tcFps = (uint32_t)(fps + 0.5);
        _mode = Timecode::Mode(vtc_format_find_or_create(num, den, tcFps, 0));
        return;
}

PROMEKI_NAMESPACE_END
