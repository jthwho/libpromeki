/**
 * @file      fdkaaccodec.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * fdk-aac AudioEncoder / AudioDecoder backends for the
 * @ref AudioCodec::AAC codec ID.  Mirrors @ref OpusAudioCodec — one
 * file registers @c "FdkAac" backend, encoder, and decoder factories
 * at process startup with a Vendored weight.
 *
 * v1 scope: AAC-LC mono / stereo at 8–96 kHz, raw output (no ADTS,
 * no LATM) for direct framing into FLV / RTP.  HE-AAC v1 / v2 are
 * accessible via the AOT MediaConfig key but the v1 RtpMediaIO /
 * RtmpMediaIO paths default to LC.  Frame size is the fdk-aac default
 * (1024 samples for LC, 2048 for HE-AAC).
 */

#include <fdk-aac/aacenc_lib.h>
#include <fdk-aac/aacdecoder_lib.h>
#include <cstdint>
#include <cstring>
#include <promeki/audiocodec.h>
#include <promeki/audiodecoder.h>
#include <promeki/audiodesc.h>
#include <promeki/audioencoder.h>
#include <promeki/audioformat.h>
#include <promeki/frame.h>
#include <promeki/buffer.h>
#include <promeki/deque.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediatimestamp.h>
#include <promeki/logger.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/aacbitstream.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Map FDK-AAC channel count to its CHANNEL_MODE constant.
        CHANNEL_MODE channelModeFor(unsigned int channels) {
                switch (channels) {
                        case 1: return MODE_1;
                        case 2: return MODE_2;
                        case 3: return MODE_1_2;
                        case 4: return MODE_1_2_1;
                        case 5: return MODE_1_2_2;
                        case 6: return MODE_1_2_2_1;
                        case 8: return MODE_1_2_2_2_1;
                        default: return MODE_INVALID;
                }
        }

        const char *aacEncErrName(AACENC_ERROR e) {
                switch (e) {
                        case AACENC_OK:                return "OK";
                        case AACENC_INVALID_HANDLE:    return "INVALID_HANDLE";
                        case AACENC_MEMORY_ERROR:      return "MEMORY_ERROR";
                        case AACENC_UNSUPPORTED_PARAMETER: return "UNSUPPORTED_PARAMETER";
                        case AACENC_INVALID_CONFIG:    return "INVALID_CONFIG";
                        case AACENC_INIT_ERROR:        return "INIT_ERROR";
                        case AACENC_INIT_AAC_ERROR:    return "INIT_AAC_ERROR";
                        case AACENC_INIT_SBR_ERROR:    return "INIT_SBR_ERROR";
                        case AACENC_INIT_TP_ERROR:     return "INIT_TP_ERROR";
                        case AACENC_INIT_META_ERROR:   return "INIT_META_ERROR";
                        case AACENC_INIT_MPS_ERROR:    return "INIT_MPS_ERROR";
                        case AACENC_ENCODE_ERROR:      return "ENCODE_ERROR";
                        case AACENC_ENCODE_EOF:        return "ENCODE_EOF";
                        default:                        return "UNKNOWN";
                }
        }

        const char *aacDecErrName(AAC_DECODER_ERROR e) {
                if (e == AAC_DEC_OK) return "OK";
                if (IS_INIT_ERROR(e)) return "INIT_ERROR";
                if (IS_DECODE_ERROR(e)) return "DECODE_ERROR";
                if (IS_OUTPUT_VALID(e)) return "OUTPUT_VALID_WITH_WARNING";
                return "UNKNOWN";
        }

        // =============================================================================
        // FdkAacEncoder — AAC-LC default, raw output (TRANSMUX = TT_MP4_RAW).
        // =============================================================================

        class FdkAacEncoder : public AudioEncoder {
                public:
                        FdkAacEncoder() = default;

                        ~FdkAacEncoder() override {
                                if (_enc != nullptr) {
                                        aacEncClose(&_enc);
                                        _enc = nullptr;
                                }
                        }

                        void onConfigure(const MediaConfig &config) override {
                                int32_t kbps = config.getAs<int32_t>(MediaConfig::BitrateKbps);
                                if (kbps > 0) _bitrateBps = kbps * 1000;
                        }

                        Error submitFrame(const Frame &frame) override {
                                clearError();
                                PcmAudioPayload::Ptr payload = selectInputPayload(frame);
                                if (!payload.isValid() || !payload->isValid() || payload->planeCount() == 0) {
                                        promekiWarnThrottled(1000, "FdkAacEncoder::submitFrame: no PCM audio payload on frame");
                                        setError(Error::Invalid, "no PCM audio payload on frame");
                                        return _lastError;
                                }
                                _currentSource = frame;
                                _videoEchoDone = false;
                                if (!ensureEncoder(payload->desc())) return _lastError;
                                if (payload->desc().sampleRate() != _sampleRate ||
                                    payload->desc().channels() != _channels) {
                                        promekiWarn("FdkAacEncoder::submitFrame: payload format %s sr=%g ch=%u "
                                                    "does not match encoder configured sr=%g ch=%u",
                                                    payload->desc().format().toString().cstr(),
                                                    static_cast<double>(payload->desc().sampleRate()),
                                                    payload->desc().channels(),
                                                    static_cast<double>(_sampleRate), _channels);
                                        setError(Error::Invalid, "payload format does not match encoder configuration");
                                        return _lastError;
                                }

                                // fdk-aac wants interleaved 16-bit PCM for the
                                // basic AAC-LC encode path.  Convert if needed.
                                PcmAudioPayload::Ptr   converted;
                                const PcmAudioPayload *src = payload.ptr();
                                if (payload->desc().format().id() != AudioFormat::PCMI_S16LE) {
                                        converted = payload->convert(AudioFormat::PCMI_S16LE);
                                        if (!converted.isValid()) {
                                                promekiWarnThrottled(1000,
                                                        "FdkAacEncoder::submitFrame: failed to convert payload "
                                                        "format %s -> PCMI_S16LE",
                                                        payload->desc().format().toString().cstr());
                                                setError(Error::Invalid, "failed to convert input to PCMI_S16LE");
                                                return _lastError;
                                        }
                                        src = converted.ptr();
                                }

                                auto         view = src->plane(0);
                                const auto  *p    = reinterpret_cast<const int16_t *>(view.data());
                                size_t       n    = static_cast<size_t>(src->sampleCount()) * _channels;
                                size_t       oldSize = _pendingS16.size();
                                _pendingS16.resize(oldSize + n);
                                std::memcpy(_pendingS16.data() + oldSize, p, n * sizeof(int16_t));

                                if (!_havePts) {
                                        _basePts = payload->pts();
                                        _havePts = payload->pts().isValid();
                                }

                                flushFullFrames();

                                // No AAC packet fired for this submit (the
                                // encoder is buffering samples for a future
                                // packet boundary) — emit a video-only
                                // passthrough Frame so the input's video /
                                // ANC / metadata still reaches the
                                // downstream sink.  Without this the video
                                // for this submit would be stranded in the
                                // encoder's _currentSource until the next
                                // submit's first emit re-attaches it,
                                // dropping the original video frame.
                                if (!_videoEchoDone && _currentSource.isValid()
                                    && (!_currentSource.videoPayloads().isEmpty()
                                        || !_currentSource.ancPayloads().isEmpty())) {
                                        _outQueue.pushToBack(buildOutputFrame(_currentSource,
                                                                              CompressedAudioPayload::Ptr()));
                                        _videoEchoDone = true;
                                }
                                return Error::Ok;
                        }

                        Frame receiveFrame() override {
                                if (_outQueue.isEmpty()) {
                                        if (_flushed && !_eosEmitted) {
                                                _eosEmitted = true;
                                                AudioDesc eosDesc(AudioFormat(AudioFormat::AAC), _sampleRate,
                                                                  _channels);
                                                auto      eos = CompressedAudioPayload::Ptr::create(eosDesc);
                                                eos.modify()->markEndOfStream();
                                                Frame f;
                                                f.addPayload(eos);
                                                return f;
                                        }
                                        return Frame();
                                }
                                return _outQueue.popFromFront();
                        }

                        Error flush() override {
                                flushFullFrames();
                                if (_enc != nullptr) {
                                        // Pad the trailing partial frame to one full
                                        // frame's worth of samples so fdk-aac can emit
                                        // the final encoded packet.
                                        size_t needed = _frameSamples * _channels;
                                        if (_pendingS16.size() > 0 && _pendingS16.size() < needed) {
                                                _pendingS16.resize(needed, 0);
                                                flushOneFrame();
                                        }
                                        // Drain via EOF input (in_size = -1) — fdk-aac emits
                                        // any internally buffered packets, then signals EOF.
                                        for (int i = 0; i < 4; ++i) {
                                                if (!encodeOnce(/*eofMarker=*/true)) break;
                                        }
                                }
                                _flushed = true;
                                return Error::Ok;
                        }

                        Error reset() override {
                                _outQueue.clear();
                                _pendingS16.clear();
                                _flushed       = false;
                                _eosEmitted    = false;
                                _havePts       = false;
                                _samplesEmitted = 0;
                                _currentSource = Frame();
                                _videoEchoDone = false;
                                if (_enc != nullptr) {
                                        // fdk-aac has no in-place reset call; close + reopen.
                                        aacEncClose(&_enc);
                                        _enc = nullptr;
                                }
                                return Error::Ok;
                        }

                private:
                        bool ensureEncoder(const AudioDesc &desc) {
                                if (_enc != nullptr) return true;
                                CHANNEL_MODE mode = channelModeFor(desc.channels());
                                if (mode == MODE_INVALID) {
                                        promekiWarn("FdkAacEncoder::ensureEncoder: unsupported channel count %u",
                                                    desc.channels());
                                        setError(Error::Invalid,
                                                 String::sprintf("AAC: unsupported channel count %u", desc.channels()));
                                        return false;
                                }

                                AACENC_ERROR err = aacEncOpen(&_enc, /*encModules=*/0, /*maxChannels=*/desc.channels());
                                if (err != AACENC_OK || _enc == nullptr) {
                                        promekiWarn("FdkAacEncoder::ensureEncoder: aacEncOpen failed: %s (sr=%g ch=%u)",
                                                    aacEncErrName(err), static_cast<double>(desc.sampleRate()),
                                                    desc.channels());
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("aacEncOpen failed: %s", aacEncErrName(err)));
                                        _enc = nullptr;
                                        return false;
                                }

                                #define SET_PARAM(p, v)                                                      \
                                        do {                                                                 \
                                                AACENC_ERROR e = aacEncoder_SetParam(_enc, (p), (v));        \
                                                if (e != AACENC_OK) {                                        \
                                                        promekiWarn("FdkAacEncoder::ensureEncoder: "         \
                                                                    "aacEncoder_SetParam(%s) failed: %s",    \
                                                                    #p, aacEncErrName(e));                   \
                                                        setError(Error::LibraryFailure,                      \
                                                                 String::sprintf("aacEncoder_SetParam(%s) " \
                                                                                 "failed: %s",               \
                                                                                 #p, aacEncErrName(e)));     \
                                                        aacEncClose(&_enc);                                  \
                                                        _enc = nullptr;                                      \
                                                        return false;                                        \
                                                }                                                            \
                                        } while (0)

                                SET_PARAM(AACENC_AOT, _aot);
                                SET_PARAM(AACENC_SAMPLERATE, static_cast<uint32_t>(desc.sampleRate()));
                                SET_PARAM(AACENC_CHANNELMODE, mode);
                                SET_PARAM(AACENC_CHANNELORDER, 1);   // WAVE order
                                SET_PARAM(AACENC_BITRATE, _bitrateBps > 0 ? _bitrateBps : defaultBitrate(desc));
                                SET_PARAM(AACENC_TRANSMUX, TT_MP4_RAW);
                                SET_PARAM(AACENC_AFTERBURNER, 1);
                                #undef SET_PARAM

                                AACENC_ERROR initErr = aacEncEncode(_enc, nullptr, nullptr, nullptr, nullptr);
                                if (initErr != AACENC_OK) {
                                        promekiWarn("FdkAacEncoder::ensureEncoder: aacEncEncode init failed: %s",
                                                    aacEncErrName(initErr));
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("aacEncEncode init failed: %s", aacEncErrName(initErr)));
                                        aacEncClose(&_enc);
                                        _enc = nullptr;
                                        return false;
                                }

                                AACENC_InfoStruct info{};
                                AACENC_ERROR      iErr = aacEncInfo(_enc, &info);
                                if (iErr != AACENC_OK) {
                                        promekiWarn("FdkAacEncoder::ensureEncoder: aacEncInfo failed: %s",
                                                    aacEncErrName(iErr));
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("aacEncInfo failed: %s", aacEncErrName(iErr)));
                                        aacEncClose(&_enc);
                                        _enc = nullptr;
                                        return false;
                                }
                                _frameSamples  = info.frameLength;
                                _maxOutBytes   = info.maxOutBufBytes;
                                _sampleRate    = desc.sampleRate();
                                _channels      = desc.channels();
                                return true;
                        }

                        int32_t defaultBitrate(const AudioDesc &desc) const {
                                // Conservative-quality default — 64 kbps per channel
                                // at 48 kHz, scaled by sample rate.
                                float k = desc.sampleRate() / 48000.0f;
                                return static_cast<int32_t>(64000.0f * desc.channels() * k);
                        }

                        void flushFullFrames() {
                                // Encoder is initialised lazily on the first
                                // @ref submitPayload — flush()/close() against
                                // an encoder that never saw any samples leaves
                                // @c _enc nullptr and @c _channels = 0.  In
                                // that state @c needed would be zero, and the
                                // @c size_t comparison @c size() >= 0 is
                                // tautologically true, so the original loop
                                // spun forever calling @c encodeOnceWithInput
                                // against a null encoder handle (which builds
                                // an error string per iteration).  Bail out
                                // here so flush() on an unused encoder is a
                                // genuine no-op.
                                if (_enc == nullptr) return;
                                size_t needed = _frameSamples * _channels;
                                if (needed == 0) return;
                                while (_pendingS16.size() >= needed) flushOneFrame();
                        }

                        void flushOneFrame() {
                                size_t inSamples = _frameSamples * _channels;
                                encodeOnceWithInput(_pendingS16.data(), static_cast<int32_t>(inSamples));
                                _pendingS16.erase(_pendingS16.begin(), _pendingS16.begin() + inSamples);
                        }

                        // Submit one buffer of samples to fdk-aac.  Always
                        // requested-output-buffer-sized to capture whatever
                        // packet fdk-aac emits; on a short input, fdk-aac may
                        // accumulate and produce no output this call.
                        //
                        // fdk-aac's BufDesc takes @c INT* arrays for buffer
                        // identifiers / sizes / elem sizes.  We declare the
                        // backing locals as @c int (the underlying primitive)
                        // and pass them directly — fdk-aac's typedefs alias
                        // these widths exactly.
                        bool encodeOnceWithInput(int16_t *inputSamples, int32_t inputSamplesLen) {
                                AACENC_BufDesc inBuf{};
                                AACENC_BufDesc outBuf{};
                                AACENC_InArgs  inArgs{};
                                AACENC_OutArgs outArgs{};

                                int   inIdent  = IN_AUDIO_DATA;
                                int   inElSize = sizeof(int16_t);
                                int   inSize   = inputSamplesLen * static_cast<int32_t>(sizeof(int16_t));
                                void *inPtr    = inputSamples;
                                inBuf.numBufs            = 1;
                                inBuf.bufs               = &inPtr;
                                inBuf.bufferIdentifiers  = &inIdent;
                                inBuf.bufSizes           = &inSize;
                                inBuf.bufElSizes         = &inElSize;

                                Buffer out(_maxOutBytes);
                                int    outIdent  = OUT_BITSTREAM_DATA;
                                int    outElSize = 1;
                                int    outSize   = static_cast<int32_t>(_maxOutBytes);
                                void  *outPtr    = out.data();
                                outBuf.numBufs            = 1;
                                outBuf.bufs               = &outPtr;
                                outBuf.bufferIdentifiers  = &outIdent;
                                outBuf.bufSizes           = &outSize;
                                outBuf.bufElSizes         = &outElSize;

                                inArgs.numInSamples = inputSamplesLen;
                                AACENC_ERROR err    = aacEncEncode(_enc, &inBuf, &outBuf, &inArgs, &outArgs);
                                if (err == AACENC_ENCODE_EOF) return false;
                                if (err != AACENC_OK) {
                                        promekiWarnThrottled(1000, "FdkAacEncoder::encodeOnceWithInput: "
                                                                   "aacEncEncode failed: %s (inSamples=%d)",
                                                             aacEncErrName(err), (int)inputSamplesLen);
                                        setError(Error::EncodeFailed,
                                                 String::sprintf("aacEncEncode failed: %s", aacEncErrName(err)));
                                        return false;
                                }
                                if (outArgs.numOutBytes > 0) {
                                        out.setSize(static_cast<size_t>(outArgs.numOutBytes));
                                        emitPacket(out, _frameSamples);
                                }
                                return true;
                        }

                        // Drain pass — submit zero-length input with an EOF
                        // marker so fdk-aac flushes any internal state.
                        bool encodeOnce(bool eofMarker) {
                                AACENC_BufDesc inBuf{};
                                AACENC_BufDesc outBuf{};
                                AACENC_InArgs  inArgs{};
                                AACENC_OutArgs outArgs{};

                                Buffer out(_maxOutBytes);
                                int    outIdent  = OUT_BITSTREAM_DATA;
                                int    outElSize = 1;
                                int    outSize   = static_cast<int32_t>(_maxOutBytes);
                                void  *outPtr    = out.data();
                                outBuf.numBufs            = 1;
                                outBuf.bufs               = &outPtr;
                                outBuf.bufferIdentifiers  = &outIdent;
                                outBuf.bufSizes           = &outSize;
                                outBuf.bufElSizes         = &outElSize;

                                inArgs.numInSamples = eofMarker ? -1 : 0;
                                AACENC_ERROR err    = aacEncEncode(_enc, &inBuf, &outBuf, &inArgs, &outArgs);
                                if (err == AACENC_ENCODE_EOF) return false;
                                if (err != AACENC_OK) return false;
                                if (outArgs.numOutBytes > 0) {
                                        out.setSize(static_cast<size_t>(outArgs.numOutBytes));
                                        emitPacket(out, _frameSamples);
                                        return true;
                                }
                                return false;
                        }

                        MediaTimeStamp ptsForCurrentFrame() {
                                if (!_havePts) return MediaTimeStamp();
                                int64_t ns = static_cast<int64_t>(static_cast<double>(_samplesEmitted) * 1.0e9 /
                                                                          static_cast<double>(_sampleRate) +
                                                                  0.5);
                                return MediaTimeStamp(_basePts.timeStamp() + Duration::fromNanoseconds(ns),
                                                      _basePts.domain(), _basePts.offset());
                        }

                        void emitPacket(const Buffer &buf, size_t framePcmSamples) {
                                AudioDesc  desc(AudioFormat(AudioFormat::AAC), _sampleRate, _channels);
                                BufferView view(buf, 0, buf.size());
                                auto       cap = CompressedAudioPayload::Ptr::create(desc, view, framePcmSamples);
                                cap.modify()->setPts(ptsForCurrentFrame());
                                // The first emit per submitFrame echoes the
                                // input's video / ANC / metadata via
                                // buildOutputFrame so downstream sinks see
                                // the matching essence.  Subsequent emits
                                // for the same submit (the AAC frame size
                                // doesn't divide the input's per-frame PCM
                                // count, so 2 packets sometimes fire from
                                // one submit at common rates like 29.97 fps
                                // / 48 kHz) are audio-only — re-echoing the
                                // same video would deliver the matching
                                // compressed video twice over RTMP / RTP /
                                // MP4 and crash the decoder's reference
                                // chain.
                                if (_videoEchoDone) {
                                        Frame audioOnly;
                                        audioOnly.addPayload(cap);
                                        _outQueue.pushToBack(std::move(audioOnly));
                                } else {
                                        _outQueue.pushToBack(buildOutputFrame(_currentSource, std::move(cap)));
                                        _videoEchoDone = true;
                                }
                                _samplesEmitted += framePcmSamples;
                        }

                        HANDLE_AACENCODER  _enc          = nullptr;
                        float              _sampleRate   = 0.0f;
                        unsigned int       _channels     = 0;
                        uint32_t           _frameSamples = 1024;
                        uint32_t           _maxOutBytes  = 0;
                        int32_t            _bitrateBps   = 0;     // 0 = pick at ensureEncoder
                        int32_t            _aot          = 2;     // AOT_AAC_LC
                        List<int16_t> _pendingS16;
                        // Output queue holds pre-paired Frames (built via
                        // buildOutputFrame(_currentSource, pkt) at emission
                        // time) so the most-recent source Frame's video /
                        // ANC / metadata echo through downstream.
                        Deque<Frame> _outQueue;
                        Frame        _currentSource;
                        MediaTimeStamp     _basePts;
                        bool               _havePts     = false;
                        size_t             _samplesEmitted = 0;
                        bool               _flushed     = false;
                        bool               _eosEmitted  = false;
                        // Latches when the first AAC packet of the current
                        // submitFrame's input has been emitted with the
                        // input's video / ANC / metadata echoed onto its
                        // output Frame.  Reset to @c false at the top of
                        // every @c submitFrame.  See the rationale in
                        // @c emitPacket.
                        bool               _videoEchoDone = false;
        };

        // =============================================================================
        // FdkAacDecoder — raw input (TT_MP4_RAW), output PCMI_S16LE.
        // =============================================================================

        class FdkAacDecoder : public AudioDecoder {
                public:
                        FdkAacDecoder() = default;

                        ~FdkAacDecoder() override {
                                if (_dec != nullptr) {
                                        aacDecoder_Close(_dec);
                                        _dec = nullptr;
                                }
                        }

                        void onConfigure(const MediaConfig &cfg) override {
                                float sr = cfg.getAs<float>(MediaConfig::AudioRate);
                                if (sr > 0.0f) _outRate = sr;
                                int32_t ch = cfg.getAs<int32_t>(MediaConfig::AudioChannels);
                                if (ch > 0) _outChannels = static_cast<unsigned int>(ch);
                        }

                        Error submitFrame(const Frame &frame) override {
                                clearError();
                                CompressedAudioPayload::Ptr payload = selectInputPayload(frame);
                                if (!payload.isValid() || !payload->isValid()) {
                                        promekiWarnThrottled(1000, "FdkAacDecoder::submitFrame: no compressed audio payload on frame");
                                        setError(Error::Invalid, "no compressed audio payload on frame");
                                        return _lastError;
                                }
                                _currentSource = frame;
                                if (!ensureDecoder(payload->desc())) return _lastError;

                                if (payload->planeCount() == 0) return Error::Ok;
                                auto   view = payload->plane(0);
                                if (view.size() == 0) return Error::Ok;

                                // fdk-aac's Fill API takes @c UCHAR** / @c UINT*
                                // (== unsigned char** / unsigned int*) — alias
                                // our @c uint8_t / @c uint32_t locals at the
                                // call boundary.
                                uint8_t  *inPtr  = static_cast<uint8_t *>(view.data());
                                uint32_t  inSize = static_cast<uint32_t>(view.size());
                                uint32_t  valid  = inSize;
                                AAC_DECODER_ERROR fillErr = aacDecoder_Fill(
                                        _dec, reinterpret_cast<unsigned char **>(&inPtr),
                                        reinterpret_cast<unsigned int *>(&inSize),
                                        reinterpret_cast<unsigned int *>(&valid));
                                if (fillErr != AAC_DEC_OK) {
                                        promekiWarnThrottled(1000, "FdkAacDecoder::submitFrame: aacDecoder_Fill failed: %s (size=%u)",
                                                             aacDecErrName(fillErr), (unsigned)view.size());
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("aacDecoder_Fill failed: %s",
                                                                 aacDecErrName(fillErr)));
                                        return _lastError;
                                }

                                // One Fill may produce multiple frames; drain.
                                while (true) {
                                        constexpr int32_t kMaxPcmSamplesPerFrame = 2048 * 8; // 8ch worst case
                                        int16_t pcm[kMaxPcmSamplesPerFrame] = {0};
                                        AAC_DECODER_ERROR dErr =
                                            aacDecoder_DecodeFrame(_dec, pcm, kMaxPcmSamplesPerFrame, 0);
                                        if (dErr == AAC_DEC_NOT_ENOUGH_BITS) break;
                                        if (dErr != AAC_DEC_OK) {
                                                promekiWarnThrottled(1000, "FdkAacDecoder::submitFrame: aacDecoder_DecodeFrame failed: %s",
                                                                     aacDecErrName(dErr));
                                                setError(Error::DecodeFailed,
                                                         String::sprintf("aacDecoder_DecodeFrame failed: %s",
                                                                         aacDecErrName(dErr)));
                                                break;
                                        }
                                        CStreamInfo *info = aacDecoder_GetStreamInfo(_dec);
                                        if (info == nullptr) {
                                                promekiWarnThrottled(1000, "FdkAacDecoder::submitFrame: aacDecoder_GetStreamInfo returned null");
                                                setError(Error::LibraryFailure,
                                                         "aacDecoder_GetStreamInfo returned null");
                                                break;
                                        }
                                        emitPcm(pcm, info->frameSize, info->numChannels, info->sampleRate);
                                }
                                return Error::Ok;
                        }

                        Frame receiveFrame() override {
                                if (_outQueue.isEmpty()) return Frame();
                                return _outQueue.popFromFront();
                        }

                        Error flush() override {
                                _flushed = true;
                                return Error::Ok;
                        }

                        Error reset() override {
                                _outQueue.clear();
                                _flushed = false;
                                if (_dec != nullptr) {
                                        aacDecoder_Close(_dec);
                                        _dec = nullptr;
                                }
                                return Error::Ok;
                        }

                private:
                        bool ensureDecoder(const AudioDesc &desc) {
                                if (_dec != nullptr) return true;
                                _dec = aacDecoder_Open(TT_MP4_RAW, /*nrOfLayers=*/1);
                                if (_dec == nullptr) {
                                        promekiWarn("FdkAacDecoder::ensureDecoder: aacDecoder_Open(TT_MP4_RAW) failed");
                                        setError(Error::LibraryFailure, "aacDecoder_Open failed");
                                        return false;
                                }
                                // TT_MP4_RAW requires the AudioSpecificConfig
                                // to be set before the first DecodeFrame call.
                                // Derive a minimal LC AudioSpecificConfig from
                                // the payload's AudioDesc and push it.
                                AacDecoderConfig cfg = AacDecoderConfig::fromAudioDesc(desc);
                                Buffer           ascBytes;
                                Error            sErr = cfg.serialize(ascBytes);
                                if (sErr.isError()) {
                                        promekiWarn("FdkAacDecoder::ensureDecoder: failed to serialize "
                                                    "AudioSpecificConfig from desc sr=%g ch=%u: %s",
                                                    static_cast<double>(desc.sampleRate()), desc.channels(),
                                                    sErr.name().cstr());
                                        setError(sErr, "failed to serialize AudioSpecificConfig from desc");
                                        return false;
                                }
                                uint8_t  *ascPtr   = static_cast<uint8_t *>(ascBytes.data());
                                uint8_t  *ascArr[1] = {ascPtr};
                                uint32_t  ascLen   = static_cast<uint32_t>(ascBytes.size());
                                AAC_DECODER_ERROR err = aacDecoder_ConfigRaw(
                                        _dec, reinterpret_cast<unsigned char **>(ascArr),
                                        reinterpret_cast<unsigned int *>(&ascLen));
                                if (err != AAC_DEC_OK) {
                                        promekiWarn("FdkAacDecoder::ensureDecoder: aacDecoder_ConfigRaw failed: %s",
                                                    aacDecErrName(err));
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("aacDecoder_ConfigRaw failed: %s",
                                                                 aacDecErrName(err)));
                                        return false;
                                }
                                return true;
                        }

                        void emitPcm(const int16_t *samples, int32_t frameSize, int32_t channels,
                                     int32_t sampleRate) {
                                AudioDesc desc(AudioFormat::PCMI_S16LE, static_cast<float>(sampleRate),
                                               static_cast<unsigned int>(channels));
                                size_t    bytes = desc.bufferSize(frameSize);
                                Buffer    buf(bytes);
                                std::memcpy(buf.data(), samples, bytes);
                                buf.setSize(bytes);
                                BufferView view(buf, 0, buf.size());
                                auto       pcm = PcmAudioPayload::Ptr::create(desc, static_cast<size_t>(frameSize),
                                                                              view);
                                _outQueue.pushToBack(buildOutputFrame(_currentSource, std::move(pcm)));
                        }

                        HANDLE_AACDECODER _dec = nullptr;
                        float             _outRate = 48000.0f;
                        unsigned int      _outChannels = 2;
                        // Output queue holds pre-paired Frames (built via
                        // buildOutputFrame at emit time) so source-side
                        // video / ANC / metadata echo through.
                        Deque<Frame>      _outQueue;
                        Frame             _currentSource;
                        bool              _flushed = false;
        };

        // =============================================================================
        // Static registration
        // =============================================================================

        struct FdkAacRegistrar {
                        FdkAacRegistrar() {
                                auto bk = AudioCodec::registerBackend("FdkAac");
                                if (error(bk).isError()) return;
                                auto backend = value(bk);

                                AudioEncoder::registerBackend({
                                        .codecId = AudioCodec::AAC,
                                        .backend = backend,
                                        .weight  = BackendWeight::Vendored,
                                        .supportedInputs =
                                                {
                                                        static_cast<int>(AudioFormat::PCMI_S16LE),
                                                        static_cast<int>(AudioFormat::PCMI_Float32LE),
                                                },
                                        .factory = []() -> AudioEncoder * { return new FdkAacEncoder(); },
                                });

                                AudioDecoder::registerBackend({
                                        .codecId = AudioCodec::AAC,
                                        .backend = backend,
                                        .weight  = BackendWeight::Vendored,
                                        .supportedOutputs =
                                                {
                                                        static_cast<int>(AudioFormat::PCMI_S16LE),
                                                },
                                        .factory = []() -> AudioDecoder * { return new FdkAacDecoder(); },
                                });
                        }
        };

        static FdkAacRegistrar _fdkAacRegistrar;

} // namespace

PROMEKI_NAMESPACE_END
