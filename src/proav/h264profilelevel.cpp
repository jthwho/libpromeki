/**
 * @file      h264profilelevel.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/h264profilelevel.h>

#if PROMEKI_ENABLE_PROAV

PROMEKI_NAMESPACE_BEGIN

H264Profile H264ProfileLevel::profileFromWire(const String &wire) {
        if (wire.isEmpty()) return H264Profile::Auto;
        if (wire == "baseline") return H264Profile::Baseline;
        if (wire == "main") return H264Profile::Main;
        if (wire == "high") return H264Profile::High;
        if (wire == "high10") return H264Profile::High10;
        if (wire == "high422") return H264Profile::High422;
        if (wire == "high444") return H264Profile::High444;
        if (wire == "progressive") return H264Profile::ProgressiveHigh;
        // Unrecognised token (typo / unsupported): fall back to Auto so the
        // backend derives a sensible profile from the input geometry rather
        // than failing.  (Enum::isValid() only checks the type, not the
        // value, so there is no usable "typed-but-invalid" sentinel to
        // distinguish this from Auto — and the recovery is equivalent.)
        return H264Profile::Auto;
}

String H264ProfileLevel::profileToWire(H264Profile profile) {
        if (profile == H264Profile::Baseline) return String("baseline");
        if (profile == H264Profile::Main) return String("main");
        if (profile == H264Profile::High) return String("high");
        if (profile == H264Profile::High10) return String("high10");
        if (profile == H264Profile::High422) return String("high422");
        if (profile == H264Profile::High444) return String("high444");
        // x264 has no separate progressive-high token; it is High with
        // frame-only coding.  Backends that signal it distinctly switch on
        // the H264Profile value instead.
        if (profile == H264Profile::ProgressiveHigh) return String("high");
        // Auto / invalid → no profile constraint.
        return String();
}

H264Profile H264ProfileLevel::autoProfile(int chromaFormatIDC, int bitDepth) {
        if (chromaFormatIDC == 3) return H264Profile::High444;
        if (chromaFormatIDC == 2) return H264Profile::High422;
        if (bitDepth > 8) return H264Profile::High10;
        return H264Profile::High;
}

int H264ProfileLevel::levelIdc(const String &level) {
        if (level.isEmpty()) return 0;
        if (level == "1.0" || level == "1") return 10;
        if (level == "1b") return 9;
        if (level == "1.1") return 11;
        if (level == "1.2") return 12;
        if (level == "1.3") return 13;
        if (level == "2.0" || level == "2") return 20;
        if (level == "2.1") return 21;
        if (level == "2.2") return 22;
        if (level == "3.0" || level == "3") return 30;
        if (level == "3.1") return 31;
        if (level == "3.2") return 32;
        if (level == "4.0" || level == "4") return 40;
        if (level == "4.1") return 41;
        if (level == "4.2") return 42;
        if (level == "5.0" || level == "5") return 50;
        if (level == "5.1") return 51;
        if (level == "5.2") return 52;
        if (level == "6.0" || level == "6") return 60;
        if (level == "6.1") return 61;
        if (level == "6.2") return 62;
        return 0;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
