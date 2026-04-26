/**
 * @file      opusaudiocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Native libopus AudioEncoder / AudioDecoder backend for the
 * @ref AudioCodec::Opus codec ID.  Registers the @c "Native" backend
 * and both encoder and decoder factory records against the Opus codec
 * ID at process startup with a Vendored weight.  Emits raw Opus
 * packets (one access unit per coded frame) — no Ogg / Matroska
 * container — so it can feed RTP (RFC 7587), MediaPipeline streaming,
 * and any other MediaPayload consumer without going through libsndfile.
 */

#include <opus/opus.h>
#include <cstdint>
#include <cstring>
#include <promeki/deque.h>
#include <promeki/list.h>
#include <promeki/audiocodec.h>
#include <promeki/audiodecoder.h>
#include <promeki/audiodesc.h>
#include <promeki/audioencoder.h>
#include <promeki/buffer.h>
#include <promeki/duration.h>
#include <promeki/enum.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediatimestamp.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/compressedaudiopayload.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Opus accepts these PCM rates; the encoder rejects anything else.
        bool isSupportedRate(float sr) {
                return sr == 8000.0f || sr == 12000.0f || sr == 16000.0f || sr == 24000.0f || sr == 48000.0f;
        }

        // Frame sizes supported by libopus (in milliseconds).  Other values
        // return OPUS_BAD_ARG out of opus_encode().
        bool isSupportedFrameSizeMs(float ms) {
                return ms == 2.5f || ms == 5.0f || ms == 10.0f || ms == 20.0f || ms == 40.0f || ms == 60.0f;
        }

        // Maps the OpusApplication enum onto libopus's OPUS_APPLICATION_*.
        int applicationFromEnum(const Enum &app) {
                if (app == promeki::OpusApplication::Voip) return OPUS_APPLICATION_VOIP;
                if (app == promeki::OpusApplication::LowDelay) return OPUS_APPLICATION_RESTRICTED_LOWDELAY;
                return OPUS_APPLICATION_AUDIO; // default
        }

        // Maximum packet size we'll ever emit.  libopus recommends 4000 bytes
        // per the project documentation; we round to a power of two and use
        // it as the upper bound for opus_encode().
        constexpr opus_int32 kMaxPacketBytes = 4000;

        // =============================================================================
        // OpusAudioEncoder
        // =============================================================================

        class OpusAudioEncoder : public AudioEncoder {
                public:
                        OpusAudioEncoder() = default;

                        ~OpusAudioEncoder() override {
                                if (_enc != nullptr) {
                                        opus_encoder_destroy(_enc);
                                        _enc = nullptr;
                                }
                        }

                        void configure(const MediaConfig &config) override {
                                // Bitrate is in kilobits/s; libopus wants bits/s.
                                int32_t kbps = config.getAs<int32_t>(MediaConfig::BitrateKbps);
                                if (kbps > 0) _bitrateBps = kbps * 1000;
                                _application = config.getAs<Enum>(MediaConfig::OpusApplication);
                                float ms = config.getAs<float>(MediaConfig::OpusFrameSizeMs);
                                if (ms > 0.0f) _frameSizeMs = ms;
                                if (_enc != nullptr) {
                                        applyEncoderControls();
                                }
                        }

                        Error submitPayload(const PcmAudioPayload::Ptr &payload) override {
                                clearError();
                                if (!payload.isValid() || !payload->isValid() || payload->planeCount() == 0) {
                                        setError(Error::Invalid, "invalid audio payload");
                                        return _lastError;
                                }
                                if (!ensureEncoder(payload->desc())) return _lastError;
                                if (payload->desc().sampleRate() != _sampleRate ||
                                    payload->desc().channels() != _channels) {
                                        setError(Error::Invalid, "payload format does not match encoder configuration");
                                        return _lastError;
                                }

                                PcmAudioPayload::Ptr   converted;
                                const PcmAudioPayload *src = payload.ptr();
                                if (_useFloat) {
                                        if (payload->desc().format().id() != AudioFormat::PCMI_Float32LE) {
                                                converted = payload->convert(AudioFormat::PCMI_Float32LE);
                                                if (!converted.isValid()) {
                                                        setError(Error::Invalid,
                                                                 "failed to convert input to PCMI_Float32LE");
                                                        return _lastError;
                                                }
                                                src = converted.ptr();
                                        }
                                        appendFloat(*src);
                                } else {
                                        if (payload->desc().format().id() != AudioFormat::PCMI_S16LE) {
                                                converted = payload->convert(AudioFormat::PCMI_S16LE);
                                                if (!converted.isValid()) {
                                                        setError(Error::Invalid,
                                                                 "failed to convert input to PCMI_S16LE");
                                                        return _lastError;
                                                }
                                                src = converted.ptr();
                                        }
                                        appendS16(*src);
                                }

                                if (!_havePts) {
                                        _basePts = payload->pts();
                                        _havePts = payload->pts().isValid();
                                }

                                flushFullFrames();
                                return Error::Ok;
                        }

                        CompressedAudioPayload::Ptr receiveCompressedPayload() override {
                                if (_outQueue.isEmpty()) {
                                        if (_flushed && !_eosEmitted) {
                                                _eosEmitted = true;
                                                AudioDesc eosDesc(AudioFormat(AudioFormat::Opus), _sampleRate,
                                                                  _channels);
                                                auto      eos = CompressedAudioPayload::Ptr::create(eosDesc);
                                                eos.modify()->markEndOfStream();
                                                return eos;
                                        }
                                        return CompressedAudioPayload::Ptr();
                                }
                                return _outQueue.popFromFront();
                        }

                        Error flush() override {
                                // Drain any remaining whole frames; pad the
                                // final partial frame with silence so libopus
                                // can encode it.  Without padding, libopus
                                // would reject the final partial frame and the
                                // tail of the stream would be dropped.
                                flushFullFrames();
                                if (_enc != nullptr) {
                                        size_t needed = framePcmSamples();
                                        if (_useFloat) {
                                                if (_pendingFloat.size() > 0 && _pendingFloat.size() < needed) {
                                                        _pendingFloat.resize(needed, 0.0f);
                                                        flushOneFrameFloat();
                                                }
                                        } else {
                                                if (_pendingS16.size() > 0 && _pendingS16.size() < needed) {
                                                        _pendingS16.resize(needed, 0);
                                                        flushOneFrameS16();
                                                }
                                        }
                                }
                                _flushed = true;
                                return Error::Ok;
                        }

                        Error reset() override {
                                _outQueue.clear();
                                _pendingS16.clear();
                                _pendingFloat.clear();
                                _flushed = false;
                                _eosEmitted = false;
                                _havePts = false;
                                _samplesEmitted = 0;
                                if (_enc != nullptr) opus_encoder_ctl(_enc, OPUS_RESET_STATE);
                                return Error::Ok;
                        }

                private:
                        bool ensureEncoder(const AudioDesc &desc) {
                                if (_enc != nullptr) return true;
                                if (!isSupportedRate(desc.sampleRate())) {
                                        setError(Error::Invalid, "Opus does not support this sample rate "
                                                                 "(supported: 8/12/16/24/48 kHz)");
                                        return false;
                                }
                                unsigned int ch = desc.channels();
                                if (ch != 1 && ch != 2) {
                                        setError(Error::Invalid, "Opus encoder supports 1 or 2 channels only");
                                        return false;
                                }
                                if (!isSupportedFrameSizeMs(_frameSizeMs)) {
                                        setError(Error::Invalid,
                                                 "Opus frame size must be 2.5, 5, 10, 20, 40, or 60 ms");
                                        return false;
                                }
                                _sampleRate = desc.sampleRate();
                                _channels = ch;
                                _useFloat = (desc.format().id() == AudioFormat::PCMI_Float32LE);

                                int err = OPUS_OK;
                                _enc = opus_encoder_create(static_cast<opus_int32>(_sampleRate),
                                                           static_cast<int>(_channels),
                                                           applicationFromEnum(_application), &err);
                                if (err != OPUS_OK || _enc == nullptr) {
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("opus_encoder_create failed: %s", opus_strerror(err)));
                                        _enc = nullptr;
                                        return false;
                                }
                                applyEncoderControls();
                                return true;
                        }

                        void applyEncoderControls() {
                                if (_enc == nullptr) return;
                                opus_encoder_ctl(_enc, OPUS_SET_BITRATE(_bitrateBps));
                        }

                        size_t framePcmSamples() const {
                                return static_cast<size_t>(_frameSizeMs * _sampleRate / 1000.0f + 0.5f);
                        }

                        void appendS16(const PcmAudioPayload &src) {
                                if (src.planeCount() == 0) return;
                                auto        view = src.plane(0);
                                const auto *p = reinterpret_cast<const int16_t *>(view.data());
                                size_t      n = src.sampleCount() * _channels;
                                size_t      oldSize = _pendingS16.size();
                                _pendingS16.resize(oldSize + n);
                                std::memcpy(_pendingS16.data() + oldSize, p, n * sizeof(int16_t));
                        }

                        void appendFloat(const PcmAudioPayload &src) {
                                if (src.planeCount() == 0) return;
                                auto        view = src.plane(0);
                                const auto *p = reinterpret_cast<const float *>(view.data());
                                size_t      n = src.sampleCount() * _channels;
                                size_t      oldSize = _pendingFloat.size();
                                _pendingFloat.resize(oldSize + n);
                                std::memcpy(_pendingFloat.data() + oldSize, p, n * sizeof(float));
                        }

                        void flushFullFrames() {
                                size_t needed = framePcmSamples();
                                if (needed == 0) return;
                                if (_useFloat) {
                                        while (_pendingFloat.size() >= needed * _channels) {
                                                flushOneFrameFloat();
                                        }
                                } else {
                                        while (_pendingS16.size() >= needed * _channels) {
                                                flushOneFrameS16();
                                        }
                                }
                        }

                        MediaTimeStamp ptsForCurrentFrame() {
                                if (!_havePts) return MediaTimeStamp();
                                // Compute PTS for the current opus packet from
                                // the base PTS plus the cumulative samples
                                // already emitted; libopus access units cover
                                // a fixed sample count so the offset is exact.
                                int64_t ns = static_cast<int64_t>(static_cast<double>(_samplesEmitted) * 1.0e9 /
                                                                          static_cast<double>(_sampleRate) +
                                                                  0.5);
                                return MediaTimeStamp(_basePts.timeStamp() + Duration::fromNanoseconds(ns),
                                                      _basePts.domain(), _basePts.offset());
                        }

                        void flushOneFrameS16() {
                                size_t     n = framePcmSamples();
                                auto       buf = Buffer::Ptr::create(kMaxPacketBytes);
                                opus_int32 wrote = opus_encode(_enc, _pendingS16.data(), static_cast<int>(n),
                                                               static_cast<unsigned char *>(buf.modify()->data()),
                                                               kMaxPacketBytes);
                                size_t     consumed = n * _channels;
                                if (wrote < 0) {
                                        setError(Error::EncodeFailed,
                                                 String::sprintf("opus_encode failed: %s", opus_strerror(wrote)));
                                        _pendingS16.erase(_pendingS16.begin(), _pendingS16.begin() + consumed);
                                        return;
                                }
                                buf.modify()->setSize(static_cast<size_t>(wrote));
                                emitPacket(buf, n);
                                _pendingS16.erase(_pendingS16.begin(), _pendingS16.begin() + consumed);
                        }

                        void flushOneFrameFloat() {
                                size_t     n = framePcmSamples();
                                auto       buf = Buffer::Ptr::create(kMaxPacketBytes);
                                opus_int32 wrote = opus_encode_float(_enc, _pendingFloat.data(), static_cast<int>(n),
                                                                     static_cast<unsigned char *>(buf.modify()->data()),
                                                                     kMaxPacketBytes);
                                size_t     consumed = n * _channels;
                                if (wrote < 0) {
                                        setError(Error::EncodeFailed,
                                                 String::sprintf("opus_encode_float failed: %s", opus_strerror(wrote)));
                                        _pendingFloat.erase(_pendingFloat.begin(), _pendingFloat.begin() + consumed);
                                        return;
                                }
                                buf.modify()->setSize(static_cast<size_t>(wrote));
                                emitPacket(buf, n);
                                _pendingFloat.erase(_pendingFloat.begin(), _pendingFloat.begin() + consumed);
                        }

                        void emitPacket(const Buffer::Ptr &buf, size_t framePcmSamplesValue) {
                                // Build a compressed audio payload whose single
                                // plane covers the whole encoded Opus frame
                                // buffer.  The descriptor identifies the
                                // compressed codec (Opus) so downstream
                                // consumers of this @ref CompressedAudioPayload
                                // see the correct @ref AudioCodec.
                                AudioDesc  desc(AudioFormat(AudioFormat::Opus), _sampleRate, _channels);
                                BufferView view(buf, 0, buf->size());
                                auto       cvp = CompressedAudioPayload::Ptr::create(desc, view, framePcmSamplesValue);
                                cvp.modify()->setPts(ptsForCurrentFrame());
                                _outQueue.pushToBack(cvp);
                                _samplesEmitted += framePcmSamplesValue;
                        }

                        OpusEncoder                       *_enc = nullptr;
                        float                              _sampleRate = 0.0f;
                        unsigned int                       _channels = 0;
                        bool                               _useFloat = false;
                        int32_t                            _bitrateBps = 64000;
                        Enum                               _application = promeki::OpusApplication::Audio;
                        float                              _frameSizeMs = 20.0f;
                        List<int16_t>                      _pendingS16;
                        List<float>                        _pendingFloat;
                        Deque<CompressedAudioPayload::Ptr> _outQueue;
                        MediaTimeStamp                     _basePts;
                        bool                               _havePts = false;
                        size_t                             _samplesEmitted = 0;
                        bool                               _flushed = false;
                        bool                               _eosEmitted = false;
        };

        // =============================================================================
        // OpusAudioDecoder
        // =============================================================================

        class OpusAudioDecoder : public AudioDecoder {
                public:
                        OpusAudioDecoder() = default;

                        ~OpusAudioDecoder() override {
                                if (_dec != nullptr) {
                                        opus_decoder_destroy(_dec);
                                        _dec = nullptr;
                                }
                        }

                        void configure(const MediaConfig &cfg) override {
                                float sr = cfg.getAs<float>(MediaConfig::AudioRate);
                                if (sr > 0.0f) _outRate = sr;
                                int32_t ch = cfg.getAs<int32_t>(MediaConfig::AudioChannels);
                                if (ch > 0) _outChannels = static_cast<unsigned int>(ch);
                        }

                        Error submitPayload(const CompressedAudioPayload::Ptr &payload) override {
                                clearError();
                                if (!payload.isValid() || !payload->isValid() || payload->planeCount() == 0) {
                                        setError(Error::Invalid, "invalid payload");
                                        return _lastError;
                                }
                                if (!ensureDecoder()) return _lastError;
                                auto view = payload->plane(0);
                                if (view.size() == 0) {
                                        setError(Error::Invalid, "empty opus payload");
                                        return _lastError;
                                }

                                // Decode into a worst-case-sized temporary;
                                // 120 ms is the longest opus frame, so the
                                // maximum PCM samples per channel for any
                                // valid sample rate (48 kHz) is 5760.  Round
                                // up.  Decoding goes straight into PCMI_S16LE.
                                constexpr int kMaxPcmSamplesPerChannel = 5760;
                                AudioDesc     outDesc(AudioFormat::PCMI_S16LE, _outRate, _outChannels);
                                size_t        pcmCap = outDesc.bufferSize(kMaxPcmSamplesPerChannel);
                                auto          pcmBuf = Buffer::Ptr::create(pcmCap);
                                int decoded = opus_decode(_dec, static_cast<const unsigned char *>(view.data()),
                                                          static_cast<opus_int32>(view.size()),
                                                          static_cast<opus_int16 *>(pcmBuf.modify()->data()),
                                                          kMaxPcmSamplesPerChannel,
                                                          /* decode_fec = */ 0);
                                if (decoded < 0) {
                                        setError(Error::DecodeFailed,
                                                 String::sprintf("opus_decode failed: %s", opus_strerror(decoded)));
                                        return _lastError;
                                }
                                size_t actualBytes = static_cast<size_t>(decoded) * _outChannels * sizeof(int16_t);
                                pcmBuf.modify()->setSize(actualBytes);
                                BufferView planes;
                                planes.pushToBack(pcmBuf, 0, pcmBuf->size());
                                auto uap = PcmAudioPayload::Ptr::create(outDesc, static_cast<size_t>(decoded), planes);
                                uap.modify()->setPts(payload->pts());
                                _frames.pushToBack(std::move(uap));
                                return Error::Ok;
                        }

                        PcmAudioPayload::Ptr receiveAudioPayload() override {
                                if (_frames.isEmpty()) return PcmAudioPayload::Ptr();
                                return _frames.popFromFront();
                        }

                        Error flush() override { return Error::Ok; }

                        Error reset() override {
                                _frames.clear();
                                if (_dec != nullptr) opus_decoder_ctl(_dec, OPUS_RESET_STATE);
                                return Error::Ok;
                        }

                private:
                        bool ensureDecoder() {
                                if (_dec != nullptr) return true;
                                if (_outChannels != 1 && _outChannels != 2) {
                                        setError(Error::Invalid, "Opus decoder supports 1 or 2 channels only");
                                        return false;
                                }
                                if (!isSupportedRate(_outRate)) {
                                        setError(Error::Invalid,
                                                 "Opus decoder output sample rate must be 8/12/16/24/48 kHz");
                                        return false;
                                }
                                int err = OPUS_OK;
                                _dec = opus_decoder_create(static_cast<opus_int32>(_outRate),
                                                           static_cast<int>(_outChannels), &err);
                                if (err != OPUS_OK || _dec == nullptr) {
                                        setError(Error::LibraryFailure,
                                                 String::sprintf("opus_decoder_create failed: %s", opus_strerror(err)));
                                        _dec = nullptr;
                                        return false;
                                }
                                return true;
                        }

                        OpusDecoder                *_dec = nullptr;
                        float                       _outRate = 48000.0f;
                        unsigned int                _outChannels = 2;
                        Deque<PcmAudioPayload::Ptr> _frames;
        };

        // =============================================================================
        // Static registration
        // =============================================================================

        struct OpusRegistrar {
                        OpusRegistrar() {
                                auto bk = AudioCodec::registerBackend("Native");
                                if (error(bk).isError()) return;
                                auto backend = value(bk);

                                AudioEncoder::registerBackend({
                                        .codecId = AudioCodec::Opus,
                                        .backend = backend,
                                        .weight = BackendWeight::Vendored,
                                        .supportedInputs =
                                                {
                                                        static_cast<int>(AudioFormat::PCMI_S16LE),
                                                        static_cast<int>(AudioFormat::PCMI_Float32LE),
                                                },
                                        .factory = []() -> AudioEncoder * { return new OpusAudioEncoder(); },
                                });

                                AudioDecoder::registerBackend({
                                        .codecId = AudioCodec::Opus,
                                        .backend = backend,
                                        .weight = BackendWeight::Vendored,
                                        .supportedOutputs =
                                                {
                                                        static_cast<int>(AudioFormat::PCMI_S16LE),
                                                },
                                        .factory = []() -> AudioDecoder * { return new OpusAudioDecoder(); },
                                });
                        }
        };

        static OpusRegistrar _opusRegistrar;

} // namespace

PROMEKI_NAMESPACE_END
