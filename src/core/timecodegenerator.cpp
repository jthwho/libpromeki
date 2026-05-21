/**
 * @file      timecodegenerator.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/timecodegenerator.h>

PROMEKI_NAMESPACE_BEGIN

TimecodeGenerator::TimecodeGenerator(const FrameRate &frameRate, bool dropFrame)
    : _frameRate(frameRate), _dropFrame(dropFrame) {
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
        switch (_runMode) {
                case Forward: ++_timecode; break;
                case Reverse: --_timecode; break;
                case Still: break;
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

        if (num == 0 || den == 0) {
                _mode = Timecode::Mode();
                applyMode();
                return;
        }

        // Map rational rate → TimecodeType.  Drop-frame is only meaningful at
        // the three ST-defined fractional rates (29.97 / 59.94 / 119.88); for
        // every integer rate and every non-fractional HFR rate, drop-frame is
        // forced off because the digits would otherwise misalign with the
        // wall clock.
        // 24-family
        if ((num == 24000 && den == 1001) || (num == 24 && den == 1)) {
                _mode = Timecode::Mode(Timecode::NDF24);
                _dropFrame = false;
                applyMode();
                return;
        }

        // 25-family
        if (num == 25 && den == 1) {
                _mode = Timecode::Mode(Timecode::NDF25);
                _dropFrame = false;
                applyMode();
                return;
        }

        // 30-family: 29.97 supports drop-frame; integer 30 does not.
        if (num == 30000 && den == 1001) {
                _mode = Timecode::Mode(_dropFrame ? Timecode::DF30 : Timecode::NDF30);
                applyMode();
                return;
        }
        if (num == 30 && den == 1) {
                _mode = Timecode::Mode(Timecode::NDF30);
                _dropFrame = false;
                applyMode();
                return;
        }

        // 48-family (ST 12-1 §12 / ST 12-3): 47.95 (24000/1001 × 2) and 48.
        // Both share the NDF48 digit family.
        if ((num == 48000 && den == 1001) || (num == 48 && den == 1)) {
                _mode = Timecode::Mode(Timecode::NDF48);
                _dropFrame = false;
                applyMode();
                return;
        }

        // 50-family
        if (num == 50 && den == 1) {
                _mode = Timecode::Mode(Timecode::NDF50);
                _dropFrame = false;
                applyMode();
                return;
        }

        // 60-family: 59.94 (60000/1001) supports drop-frame; integer 60 does not.
        if (num == 60000 && den == 1001) {
                _mode = Timecode::Mode(_dropFrame ? Timecode::DF60 : Timecode::NDF60);
                applyMode();
                return;
        }
        if (num == 60 && den == 1) {
                _mode = Timecode::Mode(Timecode::NDF60);
                _dropFrame = false;
                applyMode();
                return;
        }

        // 72-family: integer 72 only (24 × 3).
        if (num == 72 && den == 1) {
                _mode = Timecode::Mode(Timecode::NDF72);
                _dropFrame = false;
                applyMode();
                return;
        }

        // 96-family: integer 96 only (24 × 4).
        if (num == 96 && den == 1) {
                _mode = Timecode::Mode(Timecode::NDF96);
                _dropFrame = false;
                applyMode();
                return;
        }

        // 100-family: integer 100 only (25 × 4).
        if (num == 100 && den == 1) {
                _mode = Timecode::Mode(Timecode::NDF100);
                _dropFrame = false;
                applyMode();
                return;
        }

        // 120-family: 119.88 (120000/1001) supports drop-frame; integer 120 does not.
        // Prefer the 30×4 variant for integer 120 to match the more common pro-video
        // pipeline (24×5 is selected explicitly via TimecodeType::NDF120_24x5).
        if (num == 120000 && den == 1001) {
                _mode = Timecode::Mode(_dropFrame ? Timecode::DF120 : Timecode::NDF120);
                applyMode();
                return;
        }
        if (num == 120 && den == 1) {
                _mode = Timecode::Mode(Timecode::NDF120);
                _dropFrame = false;
                applyMode();
                return;
        }

        // Non-standard rate — fall back to libvtc's custom-format registry.
        // Drop-frame is only valid for the three fractional rates above.
        _dropFrame = false;
        double   fps = (double)num / (double)den;
        uint32_t tcFps = (uint32_t)(fps + 0.5);
        _mode = Timecode::Mode(vtc_format_find_or_create(num, den, tcFps, 0));
        applyMode();
        return;
}

void TimecodeGenerator::applyMode() {
        _timecode.setMode(_mode);
        _startTimecode.setMode(_mode);
        return;
}

PROMEKI_NAMESPACE_END
