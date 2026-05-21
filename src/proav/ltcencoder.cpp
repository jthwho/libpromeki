/**
 * @file      ltcencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ltcencoder.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Pick the libvtc format whose tc_fps + HFR_N + drop-frame flag match
        // the Timecode's mode, but whose NTSC flag matches the encoder's
        // configured FrameRate.  Returns nullptr if the Timecode has no
        // attached format.  Returns the Timecode's own format unchanged when
        // @p rate is invalid (no rate hint) or when the existing format
        // already matches.
        const VtcFormat *resolveEncodeFormat(const VtcFormat *tcFmt, const FrameRate &rate) {
                if (tcFmt == nullptr) return nullptr;
                if (!rate.isValid()) return tcFmt;

                // The encoder's FrameRate is NTSC when its denominator carries
                // the 1001 fraction; pick the libvtc sibling whose NTSC flag
                // matches.  All other format attributes (tc_fps, hfr_n, drop
                // frame) come from the Timecode.
                const bool encoderIsNtsc = (rate.denominator() == 1001);
                const bool fmtIsNtsc     = vtc_format_is_ntsc(tcFmt);
                if (fmtIsNtsc == encoderIsNtsc) return tcFmt;

                const uint32_t tcFps = tcFmt->tc_fps;
                const uint32_t hfrN  = vtc_format_hfr_n(tcFmt);
                const bool     df    = vtc_format_is_drop_frame(tcFmt);

                for (int i = 0; i < VTC_STANDARD_FORMATS_COUNT; ++i) {
                        const VtcFormat *f = VTC_STANDARD_FORMATS[i];
                        if (f->tc_fps != tcFps) continue;
                        if (vtc_format_hfr_n(f) != hfrN) continue;
                        if (vtc_format_is_drop_frame(f) != df) continue;
                        if (vtc_format_is_ntsc(f) != encoderIsNtsc) continue;
                        return f;
                }
                // No sibling at the requested NTSC orientation — fall back to
                // whatever the Timecode said.
                return tcFmt;
        }

        // Build a VtcTimecode from a libpromeki Timecode + a resolved libvtc
        // format pointer, copying the userbits, colorFrame flag and BGF mode
        // triple onto the libvtc flag set.
        VtcTimecode toVtcTimecode(const Timecode &tc, const VtcFormat *fmt) {
                VtcTimecode vtc;
                vtc.format = fmt;
                vtc.hour = tc.hour();
                vtc.min = tc.min();
                vtc.sec = tc.sec();
                vtc.frame = tc.frame();
                vtc.userbits = tc.userbits().toUint32();
                vtc.flags = 0;
                if (tc.colorFrame()) vtc.flags |= VTC_TC_FLAG_LTC_COLOR_FRAME;
                const uint8_t mode = static_cast<uint8_t>(tc.userbits().mode());
                if (mode & 0x01u) vtc.flags |= VTC_TC_FLAG_LTC_BGF0;
                if (mode & 0x02u) vtc.flags |= VTC_TC_FLAG_LTC_BGF1;
                if (mode & 0x04u) vtc.flags |= VTC_TC_FLAG_LTC_BGF2;
                return vtc;
        }

        // Encode a full libvtc LTC codeword to int8 audio, returning the
        // sample bytes.  Returns an empty list on failure.
        List<int8_t> encodeFullCodeword(VtcLTCEncoder *encoder, const VtcTimecode *vtc) {
                size_t approx = vtc_ltc_audio_frame_size_approx(encoder->sample_rate, vtc->format);
                size_t bufSize = approx + approx / 4 + 64; // 25 % headroom
                List<int8_t> out;
                out.resize(bufSize);
                size_t written = vtc_ltc_audio_encode(encoder, vtc, out.data(), bufSize);
                if (written == 0) return List<int8_t>();
                out.resize(written);
                return out;
        }

} // namespace

LtcEncoder::LtcEncoder(int sampleRate, const FrameRate &frameRate, float level)
    : _frameRate(frameRate) {
        vtc_ltc_encoder_init(&_encoder, sampleRate, level);
}

void LtcEncoder::setLevel(float level) {
        vtc_ltc_encoder_init(&_encoder, _encoder.sample_rate, level);
        return;
}

void LtcEncoder::resetSlicing() {
        _codewordBuf = List<int8_t>();
        _codewordCursor = 0;
        _videoFramesEmitted = 0;
        _samplesEmittedTotal = 0;
}

List<int8_t> LtcEncoder::encode(const Timecode &tc) {
        const VtcFormat *fmt = resolveEncodeFormat(tc.vtcFormat(), _frameRate);
        if (fmt == nullptr) return List<int8_t>();

        // Legacy path: no video FrameRate — emit one full codeword per call.
        // The codeword duration is 1/tc_fps, which equals one video frame at
        // standard rates and one super-frame at HFR rates.
        if (!_frameRate.isValid()) {
                VtcTimecode vtc = toVtcTimecode(tc, fmt);
                return encodeFullCodeword(&_encoder, &vtc);
        }

        // Per-video-frame chunked path.  Each call emits exactly the slice of
        // the current LTC codeword that occupies this video frame's audio
        // window.  At standard rates that's one full codeword; at HFR rates
        // it's a fraction.
        //
        // Compute the cumulative target sample count after this video frame
        // using exact rational math, then emit (target - emitted_so_far)
        // samples.  This keeps long-term sample totals exact even at NTSC
        // fractional video rates.
        const int64_t num = static_cast<int64_t>(_frameRate.numerator());
        const int64_t den = static_cast<int64_t>(_frameRate.denominator());
        const int64_t targetTotal =
            (static_cast<int64_t>(_videoFramesEmitted + 1) * static_cast<int64_t>(_encoder.sample_rate) * den) / num;
        const int64_t samplesToEmit = targetTotal - _samplesEmittedTotal;
        ++_videoFramesEmitted;
        _samplesEmittedTotal = targetTotal;

        if (samplesToEmit <= 0) return List<int8_t>();

        List<int8_t> out;
        out.reserve(static_cast<size_t>(samplesToEmit));

        // Pull as many samples from the cached codeword as needed, regenerating
        // when we exhaust it.
        //
        // Regeneration policy: a fresh codeword is only latched when the
        // caller's Timecode is on a super-frame boundary
        // (@ref Timecode::isSuperFrameBoundary).  At the start of a stream
        // that doesn't begin on a boundary — or when the caller skips frames
        // such that the next call would land mid-super-frame — the encoder
        // emits silence for that video frame instead of slicing a stale or
        // out-of-phase codeword.  This lets a downstream LTC decoder lock
        // cleanly: it sees silence until the next valid sync-word run starts.
        while (static_cast<int64_t>(out.size()) < samplesToEmit) {
                if (_codewordCursor >= _codewordBuf.size()) {
                        if (!tc.isSuperFrameBoundary()) {
                                // Mid-super-frame start or post-skip resync —
                                // fill this video frame with silence and leave
                                // the cursor at end-of-buffer so the next
                                // call retries the boundary check.
                                while (static_cast<int64_t>(out.size()) < samplesToEmit) {
                                        out.pushToBack(int8_t(0));
                                }
                                _codewordBuf = List<int8_t>();
                                _codewordCursor = 0;
                                return out;
                        }
                        VtcTimecode vtc = toVtcTimecode(tc, fmt);
                        _codewordBuf = encodeFullCodeword(&_encoder, &vtc);
                        _codewordCursor = 0;
                        if (_codewordBuf.isEmpty()) {
                                // libvtc rejected the encode — return what we
                                // have so far and let the caller deal with the
                                // short read.
                                return out;
                        }
                }
                const size_t remaining = _codewordBuf.size() - _codewordCursor;
                const size_t want = static_cast<size_t>(samplesToEmit) - out.size();
                const size_t take = remaining < want ? remaining : want;
                for (size_t i = 0; i < take; ++i) {
                        out.pushToBack(_codewordBuf[_codewordCursor + i]);
                }
                _codewordCursor += take;
        }
        return out;
}

size_t LtcEncoder::frameSizeApprox(const VtcFormat *format) const {
        return vtc_ltc_audio_frame_size_approx(_encoder.sample_rate, format);
}

PROMEKI_NAMESPACE_END
