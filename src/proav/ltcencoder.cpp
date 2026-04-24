/**
 * @file      ltcencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ltcencoder.h>

PROMEKI_NAMESPACE_BEGIN

LtcEncoder::LtcEncoder(int sampleRate, float level) {
        vtc_ltc_encoder_init(&_encoder, sampleRate, level);
}

void LtcEncoder::setLevel(float level) {
        vtc_ltc_encoder_init(&_encoder, _encoder.sample_rate, level);
        return;
}

List<int8_t> LtcEncoder::encode(const Timecode &tc) {
        const VtcFormat *fmt = tc.vtcFormat();
        if(fmt == nullptr) return List<int8_t>();

        // Allocate buffer with headroom
        size_t approx = vtc_ltc_audio_frame_size_approx(_encoder.sample_rate, fmt);
        size_t bufSize = approx + approx / 4 + 64; // 25% headroom

        List<int8_t> out;
        out.resize(bufSize);

        // Convert Timecode to VtcTimecode
        VtcTimecode vtc;
        vtc.format = fmt;
        vtc.hour = tc.hour();
        vtc.min = tc.min();
        vtc.sec = tc.sec();
        vtc.frame = tc.frame();
        vtc.userbits = 0;
        vtc.flags = 0;

        // Encode
        size_t written = vtc_ltc_audio_encode(&_encoder, &vtc, out.data(), bufSize);
        if(written == 0) return List<int8_t>();

        out.resize(written);
        return out;
}

size_t LtcEncoder::frameSizeApprox(const VtcFormat *format) const {
        return vtc_ltc_audio_frame_size_approx(_encoder.sample_rate, format);
}

PROMEKI_NAMESPACE_END
