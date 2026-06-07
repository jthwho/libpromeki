/**
 * @file      ffmpegaudiocodec.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Generic FFmpeg (libavcodec) AudioEncoder / AudioDecoder backend.  One
 * backend (@c "FFmpeg") services several compressed audio codec families
 * that FFmpeg implements natively or via a wired-in external library:
 *
 *   - @ref AudioCodec::AC3  — Dolby Digital (native FFmpeg encoder + decoder)
 *   - @ref AudioCodec::MP3  — MPEG-1 Layer III (native decoder; libmp3lame
 *                             encoder)
 *   - @ref AudioCodec::FLAC — Free Lossless Audio Codec (native FFmpeg)
 *
 * AAC and Opus deliberately stay on their dedicated fdk-aac / libopus
 * backends (registered at equal weight), so FFmpeg only fills the gaps.
 *
 * Like the other backends, the sessions are codec-container-agnostic:
 * source @ref Frame objects carry a @ref PcmAudioPayload in / a
 * @ref CompressedAudioPayload out (and vice-versa).  Each emitted
 * compressed access unit is one raw codec frame — no container framing —
 * so the QuickTime muxer, RTP packetiser, or any other consumer can route
 * by codec identity alone.
 */

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <cstdint>
#include <cstring>
#include <promeki/audiocodec.h>
#include <promeki/audiodecoder.h>
#include <promeki/audiodesc.h>
#include <promeki/audioencoder.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/deque.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediatimestamp.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/ffmpegsupport.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // -------------------------------------------------------------------
        // Codec-family mapping
        // -------------------------------------------------------------------

        // Maps a promeki AudioCodec ID onto the libavcodec decoder ID.  Returns
        // AV_CODEC_ID_NONE for codecs this backend does not service.
        AVCodecID avDecodeIdFor(AudioCodec::ID id) {
                switch (id) {
                        case AudioCodec::AC3: return AV_CODEC_ID_AC3;
                        case AudioCodec::MP3: return AV_CODEC_ID_MP3;
                        case AudioCodec::FLAC: return AV_CODEC_ID_FLAC;
                        default: return AV_CODEC_ID_NONE;
                }
        }

        // Maps a promeki interleaved-PCM AudioFormat onto the matching
        // interleaved AVSampleFormat fed to the encoder's SwrContext.  The
        // encoder backend advertises exactly PCMI_S16LE / PCMI_Float32LE as
        // its inputs, so those are the cases that matter; anything else falls
        // back to S16 (the submit path converts to match before resampling).
        AVSampleFormat avInputSampleFormat(AudioFormat::ID id) {
                switch (id) {
                        case AudioFormat::PCMI_Float32LE: return AV_SAMPLE_FMT_FLT;
                        case AudioFormat::PCMI_S16LE: return AV_SAMPLE_FMT_S16;
                        default: return AV_SAMPLE_FMT_S16;
                }
        }

        // The promeki interleaved-PCM AudioFormat for a given interleaved
        // AVSampleFormat — the inverse of @ref avInputSampleFormat, used when
        // a stray payload must be converted to the encoder's configured input.
        AudioFormat::ID promekiInputFormat(AVSampleFormat fmt) {
                return fmt == AV_SAMPLE_FMT_FLT ? AudioFormat::PCMI_Float32LE : AudioFormat::PCMI_S16LE;
        }

        // Resolves the libavcodec encoder for a promeki AudioCodec ID.  MP3
        // encode goes through libmp3lame (the native MP3 encoder does not
        // exist); AC-3 and FLAC use FFmpeg's native encoders.
        const AVCodec *findEncoderFor(AudioCodec::ID id) {
                switch (id) {
                        case AudioCodec::AC3: return avcodec_find_encoder(AV_CODEC_ID_AC3);
                        case AudioCodec::FLAC: return avcodec_find_encoder(AV_CODEC_ID_FLAC);
                        case AudioCodec::MP3: {
                                const AVCodec *c = avcodec_find_encoder_by_name("libmp3lame");
                                return c != nullptr ? c : avcodec_find_encoder(AV_CODEC_ID_MP3);
                        }
                        default: return nullptr;
                }
        }

        // Default bitrate (bits/s) for lossy codecs when the caller did not
        // configure one.  FFmpeg's AC-3 encoder in particular refuses to open
        // without a bit rate, so a sane default keeps the planner-spliced
        // encoder from failing when no MediaConfig::BitrateKbps was supplied.
        int64_t defaultBitrateFor(AudioCodec::ID id) {
                switch (id) {
                        case AudioCodec::AC3: return 192000;
                        case AudioCodec::MP3: return 128000;
                        default: return 128000;
                }
        }

        // The compressed @ref AudioFormat that tags an emitted payload for a
        // given codec family, so downstream consumers read the right codec.
        AudioFormat::ID compressedFormatFor(AudioCodec::ID id) {
                switch (id) {
                        case AudioCodec::AC3: return AudioFormat::AC3;
                        case AudioCodec::MP3: return AudioFormat::MP3;
                        case AudioCodec::FLAC: return AudioFormat::FLAC;
                        default: return AudioFormat::Invalid;
                }
        }

        // Picks the first encoder-accepted sample format, preferring a planar
        // form.  Uses the modern avcodec_get_supported_config() query (the
        // AVCodec::sample_fmts field was deprecated in FFmpeg 7.0).  Falls
        // back to a sane per-codec default if the query yields nothing.
        AVSampleFormat pickEncoderSampleFormat(const AVCodecContext *ctx, const AVCodec *codec,
                                               AVSampleFormat fallback) {
                const AVSampleFormat *fmts = nullptr;
                int                   count = 0;
                int                   rc = avcodec_get_supported_config(
                        ctx, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                        reinterpret_cast<const void **>(&fmts), &count);
                if (rc < 0 || fmts == nullptr || count <= 0) return fallback;
                return fmts[0];
        }

        // -------------------------------------------------------------------
        // FfmpegAudioEncoder
        // -------------------------------------------------------------------

        class FfmpegAudioEncoder : public AudioEncoder {
                public:
                        FfmpegAudioEncoder() = default;

                        ~FfmpegAudioEncoder() override { teardown(); }

                        void onConfigure(const MediaConfig &config) override {
                                int32_t kbps = config.getAs<int32_t>(MediaConfig::BitrateKbps);
                                if (kbps > 0) _bitrateBps = static_cast<int64_t>(kbps) * 1000;
                        }

                        Error submitFrame(const Frame &frame) override {
                                clearError();
                                PcmAudioPayload::Ptr payload = selectInputPayload(frame);
                                if (!payload.isValid() || !payload->isValid() || payload->planeCount() == 0) {
                                        promekiWarnThrottled(1000,
                                                "FfmpegAudioEncoder::submitFrame: no PCM audio payload on frame");
                                        setError(Error::Invalid, "no PCM audio payload on frame");
                                        return _lastError;
                                }
                                _currentSource  = frame;
                                _videoEchoDone  = false;
                                if (!ensureEncoder(payload->desc())) return _lastError;
                                if (payload->desc().sampleRate() != _sampleRate ||
                                    payload->desc().channels() != _channels) {
                                        promekiWarn("FfmpegAudioEncoder::submitFrame: payload sr=%g ch=%u does not "
                                                    "match encoder sr=%g ch=%u",
                                                    static_cast<double>(payload->desc().sampleRate()),
                                                    payload->desc().channels(),
                                                    static_cast<double>(_sampleRate), _channels);
                                        setError(Error::Invalid, "payload format does not match encoder configuration");
                                        return _lastError;
                                }

                                // Feed the SwrContext from the payload's native sample
                                // format (configured at ensureEncoder).  This avoids a
                                // lossy S16 round-trip for Float32 input: swr converts
                                // straight to the encoder's (usually planar-float)
                                // format in one pass.  Only convert when a stray payload
                                // arrives in a different format than the encoder was
                                // configured for (rare — inputs are S16 / Float32).
                                PcmAudioPayload::Ptr   converted;
                                const PcmAudioPayload *src = payload.ptr();
                                if (avInputSampleFormat(payload->desc().format().id()) != _inputAvFmt) {
                                        converted = payload->convert(promekiInputFormat(_inputAvFmt));
                                        if (!converted.isValid()) {
                                                setError(Error::Invalid, "failed to convert input to encoder format");
                                                return _lastError;
                                        }
                                        src = converted.ptr();
                                }
                                if (!_havePts) {
                                        _basePts = payload->pts();
                                        _havePts = payload->pts().isValid();
                                }
                                if (!pushToFifo(*src)) return _lastError;

                                drainFullFrames(/*flushing=*/false);

                                // If nothing fired this submit, still echo the source
                                // video / ANC so it reaches the sink (see the same
                                // rationale in OpusAudioEncoder::submitFrame).
                                if (!_videoEchoDone && _currentSource.isValid() &&
                                    (!_currentSource.videoPayloads().isEmpty() ||
                                     !_currentSource.ancPayloads().isEmpty())) {
                                        _outQueue.pushToBack(
                                                buildOutputFrame(_currentSource, CompressedAudioPayload::Ptr()));
                                        _videoEchoDone = true;
                                }
                                return Error::Ok;
                        }

                        Frame receiveFrame() override {
                                if (_outQueue.isEmpty()) {
                                        if (_flushed && !_eosEmitted) {
                                                _eosEmitted = true;
                                                AudioDesc eosDesc(AudioFormat(compressedFormatFor(codec().id())),
                                                                  _sampleRate, _channels);
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
                                if (_ctx != nullptr) {
                                        drainFullFrames(/*flushing=*/true);
                                        // Send a NULL frame to flush the encoder's own
                                        // look-ahead, then drain any trailing packets.
                                        avcodec_send_frame(_ctx, nullptr);
                                        drainPackets();
                                }
                                _flushed = true;
                                return Error::Ok;
                        }

                        Error reset() override {
                                _outQueue.clear();
                                _flushed       = false;
                                _eosEmitted    = false;
                                _havePts       = false;
                                _samplesEmitted = 0;
                                _inputSamplesSent = 0;
                                _currentSource = Frame();
                                _videoEchoDone = false;
                                if (_fifo != nullptr) av_audio_fifo_reset(_fifo);
                                if (_ctx != nullptr) avcodec_flush_buffers(_ctx);
                                return Error::Ok;
                        }

                private:
                        bool ensureEncoder(const AudioDesc &desc) {
                                if (_ctx != nullptr) return true;
                                const AVCodec *codec = findEncoderFor(this->codec().id());
                                if (codec == nullptr) {
                                        setError(Error::NotSupported, "no FFmpeg encoder for this codec");
                                        return false;
                                }
                                _ctx = avcodec_alloc_context3(codec);
                                if (_ctx == nullptr) {
                                        setError(Error::LibraryFailure, "avcodec_alloc_context3 failed");
                                        return false;
                                }
                                _sampleRate = desc.sampleRate();
                                _channels   = desc.channels();
                                _inputAvFmt = avInputSampleFormat(desc.format().id());
                                _ctx->sample_rate = static_cast<int>(_sampleRate);
                                av_channel_layout_default(&_ctx->ch_layout, static_cast<int>(_channels));
                                // FLAC default fallback is S16; AC-3 / lossy default to
                                // planar float.
                                const bool     lossless = this->codec().isLossless();
                                AVSampleFormat fallback = lossless ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLTP;
                                _ctx->sample_fmt = pickEncoderSampleFormat(_ctx, codec, fallback);
                                if (!lossless) {
                                        _ctx->bit_rate = _bitrateBps > 0
                                                                 ? _bitrateBps
                                                                 : defaultBitrateFor(this->codec().id());
                                }

                                int rc = avcodec_open2(_ctx, codec, nullptr);
                                if (rc < 0) {
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("avcodec_open2 failed: %s", ffmpegErrorString(rc).cstr()));
                                        teardown();
                                        return false;
                                }

                                // Fixed-rate codecs report a frame size; variable-
                                // frame codecs (capability flag) accept any size.
                                _variableFrame = (codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) != 0;
                                _frameSize     = _ctx->frame_size > 0 ? _ctx->frame_size : 1024;
                                // Lossless codecs must not be padded with trailing
                                // silence (it would corrupt the round-trip length);
                                // FLAC accepts a short final block instead.
                                _padLastFrame = !lossless;

                                // SwrContext: interleaved input (the payload's native
                                // S16 / Float32) -> encoder sample_fmt out, same rate /
                                // channel count.
                                AVChannelLayout inLayout;
                                av_channel_layout_default(&inLayout, static_cast<int>(_channels));
                                rc = swr_alloc_set_opts2(&_swr, &_ctx->ch_layout, _ctx->sample_fmt,
                                                         _ctx->sample_rate, &inLayout, _inputAvFmt,
                                                         _ctx->sample_rate, 0, nullptr);
                                av_channel_layout_uninit(&inLayout);
                                if (rc < 0 || _swr == nullptr || swr_init(_swr) < 0) {
                                        setError(Error::LibraryFailure, "swr_init (encoder) failed");
                                        teardown();
                                        return false;
                                }

                                _fifo = av_audio_fifo_alloc(_ctx->sample_fmt, static_cast<int>(_channels), _frameSize);
                                if (_fifo == nullptr) {
                                        setError(Error::LibraryFailure, "av_audio_fifo_alloc failed");
                                        teardown();
                                        return false;
                                }

                                // Persistent resample / FIFO frames + output packet,
                                // reused across submits instead of per-call alloc/free.
                                _swrFrame = av_frame_alloc();
                                _fifoFrame = av_frame_alloc();
                                _pkt = av_packet_alloc();
                                if (_swrFrame == nullptr || _fifoFrame == nullptr || _pkt == nullptr) {
                                        setError(Error::LibraryFailure, "av_frame_alloc / av_packet_alloc failed");
                                        teardown();
                                        return false;
                                }
                                return true;
                        }

                        // Resamples one PCM payload (interleaved S16) into the
                        // encoder sample format and appends it to the FIFO.
                        bool pushToFifo(const PcmAudioPayload &pcm) {
                                if (pcm.planeCount() == 0) return true;
                                auto         view = pcm.plane(0);
                                const uint8_t *inData = static_cast<const uint8_t *>(view.data());
                                int            inSamples = static_cast<int>(pcm.sampleCount());
                                int            outCap = static_cast<int>(
                                        swr_get_out_samples(_swr, inSamples));
                                if (outCap <= 0) return true;

                                // Reuse the persistent resample frame; unref drops the
                                // previous call's buffer before we size this one.
                                AVFrame *tmp = _swrFrame;
                                av_frame_unref(tmp);
                                tmp->nb_samples = outCap;
                                tmp->format     = _ctx->sample_fmt;
                                av_channel_layout_copy(&tmp->ch_layout, &_ctx->ch_layout);
                                if (av_frame_get_buffer(tmp, 0) < 0) {
                                        setError(Error::LibraryFailure, "av_frame_get_buffer failed");
                                        return false;
                                }
                                int produced = swr_convert(_swr, tmp->data, outCap, &inData, inSamples);
                                if (produced < 0) {
                                        setError(Error::EncodeFailed, "swr_convert (encoder) failed");
                                        return false;
                                }
                                int wrote = av_audio_fifo_write(_fifo, reinterpret_cast<void **>(tmp->data),
                                                                produced);
                                if (wrote < produced) {
                                        setError(Error::LibraryFailure, "av_audio_fifo_write short write");
                                        return false;
                                }
                                return true;
                        }

                        // Pulls whole frames out of the FIFO and encodes them.  When
                        // @p flushing, also drains a final short frame (padded with
                        // silence for lossy codecs).
                        void drainFullFrames(bool flushing) {
                                while (av_audio_fifo_size(_fifo) >= _frameSize ||
                                       (flushing && av_audio_fifo_size(_fifo) > 0)) {
                                        int avail = av_audio_fifo_size(_fifo);
                                        int take  = (avail >= _frameSize) ? _frameSize : avail;
                                        int emit  = (_variableFrame || !_padLastFrame) ? take : _frameSize;

                                        // Reuse the persistent FIFO frame.
                                        AVFrame *fr = _fifoFrame;
                                        av_frame_unref(fr);
                                        fr->nb_samples = emit;
                                        fr->format     = _ctx->sample_fmt;
                                        av_channel_layout_copy(&fr->ch_layout, &_ctx->ch_layout);
                                        if (av_frame_get_buffer(fr, 0) < 0) {
                                                setError(Error::LibraryFailure, "av_frame_get_buffer failed");
                                                return;
                                        }
                                        if (emit > take) {
                                                // Pad the tail with silence (lossy last frame).
                                                av_samples_set_silence(fr->data, 0, emit, _ctx->ch_layout.nb_channels,
                                                                       _ctx->sample_fmt);
                                        }
                                        int got = av_audio_fifo_read(_fifo, reinterpret_cast<void **>(fr->data), take);
                                        (void)got;
                                        // PTS in input sample units, monotonically increasing
                                        // by the real (unpadded) sample count fed so far —
                                        // libmp3lame warns on non-monotonic input PTS.
                                        fr->pts = _inputSamplesSent;
                                        _inputSamplesSent += take;
                                        int rc = avcodec_send_frame(_ctx, fr);
                                        if (rc < 0) {
                                                setError(Error::EncodeFailed,
                                                         String::sprintf("avcodec_send_frame failed: %s",
                                                                         ffmpegErrorString(rc).cstr()));
                                                return;
                                        }
                                        // The encoded packet's sample span is the real
                                        // (unpadded) input count so timing stays exact.
                                        _pendingPacketSamples.pushToBack(static_cast<size_t>(take));
                                        drainPackets();
                                        if (!flushing && avail < _frameSize) break;
                                }
                        }

                        void drainPackets() {
                                while (true) {
                                        int rc = avcodec_receive_packet(_ctx, _pkt);
                                        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
                                        if (rc < 0) {
                                                setError(Error::EncodeFailed,
                                                         String::sprintf("avcodec_receive_packet failed: %s",
                                                                         ffmpegErrorString(rc).cstr()));
                                                break;
                                        }
                                        size_t samples = _frameSize;
                                        if (!_pendingPacketSamples.isEmpty())
                                                samples = _pendingPacketSamples.popFromFront();
                                        emitPacket(_pkt, samples);
                                        av_packet_unref(_pkt);
                                }
                        }

                        void emitPacket(AVPacket *avpkt, size_t framePcmSamples) {
                                AudioDesc  desc(AudioFormat(compressedFormatFor(codec().id())), _sampleRate, _channels);
                                // Zero-copy: wrap the encoder's packet buffer; copy only
                                // if it isn't refcountable.
                                BufferView view = ffmpegWrapPacket(avpkt);
                                if (view.count() != 1) {
                                        Buffer buf(static_cast<size_t>(avpkt->size));
                                        std::memcpy(buf.data(), avpkt->data, static_cast<size_t>(avpkt->size));
                                        buf.setSize(static_cast<size_t>(avpkt->size));
                                        view = BufferView(buf, 0, buf.size());
                                }
                                auto       cap = CompressedAudioPayload::Ptr::create(desc, view, framePcmSamples);
                                cap.modify()->setPts(ptsForOffset(_samplesEmitted));
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

                        MediaTimeStamp ptsForOffset(size_t sampleOffset) const {
                                if (!_havePts) return MediaTimeStamp();
                                int64_t ns = static_cast<int64_t>(static_cast<double>(sampleOffset) * 1.0e9 /
                                                                          static_cast<double>(_sampleRate) +
                                                                  0.5);
                                return MediaTimeStamp(_basePts.timeStamp() + Duration::fromNanoseconds(ns),
                                                      _basePts.domain(), _basePts.offset());
                        }

                        void teardown() {
                                if (_swrFrame != nullptr) av_frame_free(&_swrFrame);
                                if (_fifoFrame != nullptr) av_frame_free(&_fifoFrame);
                                if (_pkt != nullptr) av_packet_free(&_pkt);
                                if (_fifo != nullptr) {
                                        av_audio_fifo_free(_fifo);
                                        _fifo = nullptr;
                                }
                                if (_swr != nullptr) swr_free(&_swr);
                                if (_ctx != nullptr) avcodec_free_context(&_ctx);
                        }

                        AVCodecContext *_ctx  = nullptr;
                        SwrContext     *_swr  = nullptr;
                        AVAudioFifo    *_fifo = nullptr;
                        AVFrame        *_swrFrame = nullptr;
                        AVFrame        *_fifoFrame = nullptr;
                        AVPacket       *_pkt = nullptr;
                        AVSampleFormat  _inputAvFmt = AV_SAMPLE_FMT_S16;
                        float           _sampleRate = 0.0f;
                        unsigned int    _channels   = 0;
                        int             _frameSize   = 1024;
                        bool            _variableFrame = false;
                        bool            _padLastFrame  = true;
                        int64_t         _bitrateBps = 0;
                        Deque<size_t>   _pendingPacketSamples;
                        Deque<Frame>    _outQueue;
                        Frame           _currentSource;
                        MediaTimeStamp  _basePts;
                        bool            _havePts        = false;
                        size_t          _samplesEmitted = 0;
                        int64_t         _inputSamplesSent = 0;
                        bool            _flushed        = false;
                        bool            _eosEmitted     = false;
                        bool            _videoEchoDone  = false;
        };

        // -------------------------------------------------------------------
        // FfmpegAudioDecoder
        // -------------------------------------------------------------------

        class FfmpegAudioDecoder : public AudioDecoder {
                public:
                        FfmpegAudioDecoder() = default;

                        ~FfmpegAudioDecoder() override { teardown(); }

                        void onConfigure(const MediaConfig &cfg) override {
                                float sr = cfg.getAs<float>(MediaConfig::AudioRate);
                                if (sr > 0.0f) _hintRate = sr;
                                int32_t ch = cfg.getAs<int32_t>(MediaConfig::AudioChannels);
                                if (ch > 0) _hintChannels = static_cast<unsigned int>(ch);
                        }

                        Error submitFrame(const Frame &frame) override {
                                clearError();
                                // Collect every compressed AU on the frame — a
                                // QuickTime/MP4 reader hands us several access units
                                // per video frame to keep the audio timeline paced.
                                List<CompressedAudioPayload::Ptr> units;
                                for (const AudioPayload::Ptr &ap : frame.audioPayloads()) {
                                        if (!ap.isValid()) continue;
                                        auto cap = sharedPointerCast<CompressedAudioPayload>(ap);
                                        if (cap.isValid() && cap->isValid()) units.pushToBack(cap);
                                }
                                if (units.isEmpty()) {
                                        promekiWarnThrottled(1000,
                                                "FfmpegAudioDecoder::submitFrame: no compressed audio payload on frame");
                                        setError(Error::Invalid, "no compressed audio payload on frame");
                                        return _lastError;
                                }
                                _currentSource = frame;
                                if (!ensureDecoder()) return _lastError;

                                // Decode every AU into one payload — a transform stage
                                // must be 1-in-1-out so FrameSync sees the full per-frame
                                // audio span.  The decoded frames are held by reference
                                // (no PCM copy) and packed once, directly into the output
                                // Buffer, in emitPcm — so the resample is the only copy.
                                freeDecFrames();
                                int accChannels = 0;
                                int accRate     = 0;
                                int64_t accSamples = 0;
                                for (const CompressedAudioPayload::Ptr &payload : units) {
                                        if (payload->planeCount() == 0) continue;
                                        auto view = payload->plane(0);
                                        if (view.size() == 0) continue;
                                        if (!decodeAU(static_cast<const uint8_t *>(view.data()), view.size(),
                                                      accChannels, accRate, accSamples)) {
                                                freeDecFrames();
                                                return _lastError;
                                        }
                                }

                                if (accSamples > 0) {
                                        emitPcm(accChannels, accRate, static_cast<size_t>(accSamples));
                                } else if (!_currentSource.videoPayloads().isEmpty() ||
                                           !_currentSource.ancPayloads().isEmpty()) {
                                        _outQueue.pushToBack(buildOutputFrame(_currentSource, PcmAudioPayload::Ptr()));
                                }
                                return Error::Ok;
                        }

                        Frame receiveFrame() override {
                                if (_outQueue.isEmpty()) return Frame();
                                return _outQueue.popFromFront();
                        }

                        Error flush() override { return Error::Ok; }

                        Error reset() override {
                                _outQueue.clear();
                                freeDecFrames();
                                _havePtsBase    = false;
                                _decodedSamples = 0;
                                if (_ctx != nullptr) avcodec_flush_buffers(_ctx);
                                return Error::Ok;
                        }

                private:
                        bool ensureDecoder() {
                                if (_ctx != nullptr) return true;
                                AVCodecID id = avDecodeIdFor(codec().id());
                                const AVCodec *codec = avcodec_find_decoder(id);
                                if (codec == nullptr) {
                                        setError(Error::NotSupported, "no FFmpeg decoder for this codec");
                                        return false;
                                }
                                _ctx = avcodec_alloc_context3(codec);
                                if (_ctx == nullptr) {
                                        setError(Error::LibraryFailure, "avcodec_alloc_context3 failed");
                                        return false;
                                }
                                if (_hintRate > 0.0f) _ctx->sample_rate = static_cast<int>(_hintRate);
                                if (_hintChannels > 0)
                                        av_channel_layout_default(&_ctx->ch_layout,
                                                                  static_cast<int>(_hintChannels));
                                int rc = avcodec_open2(_ctx, codec, nullptr);
                                if (rc < 0) {
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("avcodec_open2 failed: %s", ffmpegErrorString(rc).cstr()));
                                        teardown();
                                        return false;
                                }
                                // Persistent packet / frame reused across every AU.
                                _pkt = av_packet_alloc();
                                _frame = av_frame_alloc();
                                if (_pkt == nullptr || _frame == nullptr) {
                                        setError(Error::LibraryFailure, "av_packet_alloc / av_frame_alloc failed");
                                        teardown();
                                        return false;
                                }
                                return true;
                        }

                        // Sends one access unit and holds every decoded frame by
                        // reference (av_frame_clone — a refcount bump, no PCM copy) for
                        // the later single-pass pack in emitPcm.  Accumulates the
                        // channel/rate/sample totals across the held frames.
                        bool decodeAU(const uint8_t *data, size_t size, int &accChannels, int &accRate,
                                      int64_t &accSamples) {
                                // The AU buffer is owned by the caller for the duration;
                                // _pkt borrows it (not refcounted), so unref is a cheap
                                // reset and we clear data/size after the send.
                                av_packet_unref(_pkt);
                                _pkt->data = const_cast<uint8_t *>(data);
                                _pkt->size = static_cast<int>(size);
                                int rc = avcodec_send_packet(_ctx, _pkt);
                                _pkt->data = nullptr;
                                _pkt->size = 0;
                                if (rc < 0) {
                                        setError(Error::DecodeFailed,
                                                 String::sprintf("avcodec_send_packet failed: %s", ffmpegErrorString(rc).cstr()));
                                        return false;
                                }

                                while (true) {
                                        rc = avcodec_receive_frame(_ctx, _frame);
                                        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
                                        if (rc < 0) {
                                                setError(Error::DecodeFailed,
                                                         String::sprintf("avcodec_receive_frame failed: %s",
                                                                         ffmpegErrorString(rc).cstr()));
                                                return false;
                                        }
                                        AVFrame *held = av_frame_clone(_frame);
                                        av_frame_unref(_frame);
                                        if (held == nullptr) {
                                                setError(Error::LibraryFailure, "av_frame_clone failed");
                                                return false;
                                        }
                                        accChannels = held->ch_layout.nb_channels;
                                        accRate     = held->sample_rate;
                                        accSamples += held->nb_samples;
                                        _decFrames.pushToBack(held);
                                }
                                return true;
                        }

                        // Lazily builds the decoder SwrContext (decoded layout/format ->
                        // interleaved S16, same rate) from the first held frame.
                        bool ensureSwr(int channels, int rate, const AVFrame *fr) {
                                if (_swr != nullptr) return true;
                                AVChannelLayout outLayout;
                                av_channel_layout_default(&outLayout, channels);
                                int rc = swr_alloc_set_opts2(
                                        &_swr, &outLayout, AV_SAMPLE_FMT_S16, rate, &fr->ch_layout,
                                        static_cast<AVSampleFormat>(fr->format), fr->sample_rate, 0, nullptr);
                                av_channel_layout_uninit(&outLayout);
                                if (rc < 0 || _swr == nullptr || swr_init(_swr) < 0) {
                                        setError(Error::LibraryFailure, "swr_init (decoder) failed");
                                        return false;
                                }
                                return true;
                        }

                        // Frees and clears any held decoded frames.
                        void freeDecFrames() {
                                for (AVFrame *f : _decFrames) {
                                        AVFrame *tmp = f;
                                        av_frame_free(&tmp);
                                }
                                _decFrames.clear();
                        }

                        void emitPcm(int channels, int rate, size_t sampleCount) {
                                AudioDesc desc(AudioFormat::PCMI_S16LE, static_cast<float>(rate),
                                               static_cast<unsigned int>(channels));
                                // Single allocation sized for the whole frame's PCM, then
                                // resample each held AVFrame straight into it at a running
                                // offset — no intermediate accumulation buffer / copy.
                                const size_t bytesPerSample = static_cast<size_t>(channels) * sizeof(int16_t);
                                Buffer       buf(sampleCount * bytesPerSample);
                                size_t       produced = 0;
                                bool         ok = true;
                                for (AVFrame *f : _decFrames) {
                                        if (!ensureSwr(channels, rate, f)) {
                                                ok = false;
                                                break;
                                        }
                                        uint8_t *outPtr = static_cast<uint8_t *>(buf.data()) + produced * bytesPerSample;
                                        int      n = swr_convert(_swr, &outPtr, static_cast<int>(sampleCount - produced),
                                                            const_cast<const uint8_t **>(f->data), f->nb_samples);
                                        if (n < 0) {
                                                setError(Error::DecodeFailed, "swr_convert (decoder) failed");
                                                ok = false;
                                                break;
                                        }
                                        produced += static_cast<size_t>(n);
                                }
                                freeDecFrames();
                                if (!ok) return;
                                buf.setSize(produced * bytesPerSample);
                                BufferView view(buf, 0, buf.size());
                                auto       pcm = PcmAudioPayload::Ptr::create(desc, produced, view);
                                // Stamp a sample-accurate, monotonic PTS computed as
                                // basePts + cumulativeSamples/rate.  Propagating each
                                // input AU's PTS directly would jitter by up to half an
                                // access unit when the muxer paces a variable number of
                                // AUs per video frame; a continuity-checking consumer
                                // (Inspector / FrameSync) needs PTS to advance by exactly
                                // the emitted sample count.
                                if (!_havePtsBase && !_currentSource.audioPayloads().isEmpty()) {
                                        _basePts = _currentSource.audioPayloads()[0]->pts();
                                        _havePtsBase = _basePts.isValid();
                                }
                                if (_havePtsBase && rate > 0) {
                                        int64_t ns = static_cast<int64_t>(static_cast<double>(_decodedSamples) *
                                                                                  1.0e9 / static_cast<double>(rate) +
                                                                          0.5);
                                        pcm.modify()->setPts(MediaTimeStamp(_basePts.timeStamp() +
                                                                                    Duration::fromNanoseconds(ns),
                                                                            _basePts.domain(), _basePts.offset()));
                                }
                                _decodedSamples += static_cast<int64_t>(sampleCount);
                                _outQueue.pushToBack(buildOutputFrame(_currentSource, std::move(pcm)));
                        }

                        void teardown() {
                                freeDecFrames();
                                if (_frame != nullptr) av_frame_free(&_frame);
                                if (_pkt != nullptr) av_packet_free(&_pkt);
                                if (_swr != nullptr) swr_free(&_swr);
                                if (_ctx != nullptr) avcodec_free_context(&_ctx);
                        }

                        AVCodecContext *_ctx = nullptr;
                        SwrContext     *_swr = nullptr;
                        AVPacket       *_pkt = nullptr;
                        AVFrame        *_frame = nullptr;
                        List<AVFrame *> _decFrames;
                        float           _hintRate     = 0.0f;
                        unsigned int    _hintChannels = 0;
                        Deque<Frame>    _outQueue;
                        Frame           _currentSource;
                        MediaTimeStamp  _basePts;
                        bool            _havePtsBase    = false;
                        int64_t         _decodedSamples = 0;
        };

        // -------------------------------------------------------------------
        // Static registration
        // -------------------------------------------------------------------

        struct FfmpegRegistrar {
                        FfmpegRegistrar() {
                                ffmpegInstallLogBridge();
                                auto bk = AudioCodec::registerBackend("FFmpeg");
                                if (error(bk).isError()) return;
                                auto backend = value(bk);

                                const AudioCodec::ID codecs[] = {
                                        AudioCodec::AC3,
                                        AudioCodec::MP3,
                                        AudioCodec::FLAC,
                                };
                                for (AudioCodec::ID id : codecs) {
                                        AudioEncoder::registerBackend({
                                                .codecId = id,
                                                .backend = backend,
                                                .weight  = BackendWeight::Vendored,
                                                .supportedInputs =
                                                        {
                                                                static_cast<int>(AudioFormat::PCMI_S16LE),
                                                                static_cast<int>(AudioFormat::PCMI_Float32LE),
                                                        },
                                                .factory = []() -> AudioEncoder * {
                                                        return new FfmpegAudioEncoder();
                                                },
                                        });
                                        AudioDecoder::registerBackend({
                                                .codecId = id,
                                                .backend = backend,
                                                .weight  = BackendWeight::Vendored,
                                                .supportedOutputs =
                                                        {
                                                                static_cast<int>(AudioFormat::PCMI_S16LE),
                                                        },
                                                .factory = []() -> AudioDecoder * {
                                                        return new FfmpegAudioDecoder();
                                                },
                                        });
                                }
                        }
        };

        static FfmpegRegistrar _ffmpegRegistrar;

} // namespace

PROMEKI_NAMESPACE_END
