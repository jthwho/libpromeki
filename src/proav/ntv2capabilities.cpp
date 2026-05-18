/**
 * @file      ntv2capabilities.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/ntv2capabilities.h>

#include <promeki/ntv2format.h>
#include <promeki/string.h>

#include <ntv2card.h>
#include <ntv2devicecapabilities.h>
#include <ntv2enums.h>

PROMEKI_NAMESPACE_BEGIN

bool Ntv2Capabilities::probe(CNTV2Card &card) {
        if (!card.IsDeviceReady()) {
                _valid = false;
                return false;
        }

        DeviceCapabilities &feat = card.features();

        _sdiInputs           = static_cast<int>(feat.GetNumVideoInputs());
        _sdiOutputs          = static_cast<int>(feat.GetNumVideoOutputs());
        _hdmiInputs          = static_cast<int>(feat.GetNumHDMIVideoInputs());
        _hdmiOutputs         = static_cast<int>(feat.GetNumHDMIVideoOutputs());
        _audioSystems        = static_cast<int>(feat.GetNumAudioSystems());
        _channels            = static_cast<int>(feat.GetNumFrameStores());
        _cscs                = static_cast<int>(feat.GetNumCSCs());
        _canDoMultiFormat    = feat.CanDoMultiFormat();
        _hasBiDirectionalSdi = feat.HasBiDirectionalSDI();
        _canDoCustomAnc      = feat.CanDoCustomAnc();
        _canCapture          = feat.CanDoCapture();
        _canPlayout          = feat.CanDoPlayback();

        // Walk the NTV2 frame-buffer-format range and record which ones
        // the card claims it can drive.  The bitmap is consulted on the
        // open hot path; precomputing here trades a few µs at acquire
        // time for a constant-time lookup later.
        for (int i = 0; i < kFbfMapSize; ++i) {
                _fbfSupported[i] = feat.CanDoFrameBufferFormat(
                        static_cast<NTV2FrameBufferFormat>(i));
        }

        _valid = true;
        return true;
}

bool Ntv2Capabilities::supportsLinkStandard(const SdiLinkStandard &standard) const {
        if (!_valid) return false;
        return Ntv2Format::standardFitsCableCount(standard, _sdiInputs > _sdiOutputs ? _sdiInputs : _sdiOutputs);
}

bool Ntv2Capabilities::supportsPixelFormat(PixelFormat::ID pixelFormat) const {
        if (!_valid) return false;
        const int fbf = Ntv2Format::toNtv2PixelFormat(pixelFormat);
        if (fbf == NTV2_FBF_INVALID) return false;
        if (fbf < 0 || fbf >= kFbfMapSize) return false;
        return _fbfSupported[fbf];
}

Ntv2Capabilities Ntv2Capabilities::createForTest(int channelCount, int audioSystemCount, int sdiInputs,
                                                 int sdiOutputs, bool canMultiFormat, bool hasBiSdi,
                                                 bool canDoAnc, int cscCount) {
        Ntv2Capabilities c;
        c._sdiInputs           = sdiInputs;
        c._sdiOutputs          = sdiOutputs;
        c._hdmiInputs          = 0;
        c._hdmiOutputs         = 0;
        c._audioSystems        = audioSystemCount;
        c._channels            = channelCount;
        c._cscs                = cscCount;
        c._canDoMultiFormat    = canMultiFormat;
        c._hasBiDirectionalSdi = hasBiSdi;
        c._canDoCustomAnc      = canDoAnc;
        c._canCapture          = sdiInputs > 0;
        c._canPlayout          = sdiOutputs > 0;
        // Pretend every NTV2 frame-buffer format is supported — test
        // cases never exercise the pixel-format negotiator.
        for (int i = 0; i < kFbfMapSize; ++i) c._fbfSupported[i] = true;
        c._valid = true;
        return c;
}

String Ntv2Capabilities::toString() const {
        if (!_valid) return String("invalid");
        String s = String::format("SDI in {} / out {}, HDMI in {} / out {}, audio sys {}, ch {}, csc {}",
                                  _sdiInputs, _sdiOutputs, _hdmiInputs, _hdmiOutputs,
                                  _audioSystems, _channels, _cscs);
        if (_canDoMultiFormat)    s += ", multifmt";
        if (_hasBiDirectionalSdi) s += ", biSDI";
        if (_canDoCustomAnc)      s += ", anc";
        return s;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
