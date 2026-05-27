/**
 * @file      audioencodermediaio.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/sharedthreadmediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaconfig.h>
#include <promeki/audioencoder.h>
#include <promeki/audiocodec.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend that encodes PCM Frames into compressed audio packets.
 * @ingroup proav
 *
 * @c AudioEncoderMediaIO is the audio-side counterpart of
 * @ref VideoEncoderMediaIO.  It wires an @ref AudioEncoder session
 * into the MediaIO command/signal model.  It accepts a Frame on
 * @c writeFrame(), feeds the first @ref PcmAudioPayload into the
 * encoder via @ref AudioEncoder::submitPayload, and drains
 * @ref AudioEncoder::receiveCompressedPayload producing one output
 * Frame per emitted @ref CompressedAudioPayload.  Video payloads on
 * the source Frame are forwarded alongside each output packet so
 * downstream stages still see the picture and the sound on the same
 * PTS.
 *
 * The registered backend name is @c "AudioEncoder"; callers pick a
 * concrete codec via the @ref MediaConfig::AudioCodec key (e.g.
 * @c "Opus", @c "AAC") — the task looks up the matching factory
 * through @ref AudioCodec::createEncoder.
 *
 * @par Mode support
 *
 * Only @c MediaIO::Transform is supported — the backend has no
 * independent source or sink of its own.
 *
 * @par First-cut limitations
 *
 * - Only the first audio payload in a Frame is encoded; additional
 *   audio tracks are dropped with a one-shot warning.  Multi-track
 *   audio encoding is a follow-up.
 * - The concrete @ref AudioEncoder must operate in a 1-in / N-out
 *   regime: every @c writeFrame may produce zero, one, or many
 *   compressed packets depending on the codec's frame size and the
 *   sample count of the input chunk.  The encoder is responsible
 *   for accumulating sub-frame inputs and emitting whole packets.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::AudioCodec  | AudioCodec | (required) | Codec factory name (@c "Opus", @c "AAC"). |
 * | @ref MediaConfig::BitrateKbps | int        | 128        | Target / average bitrate. |
 * | @ref MediaConfig::Capacity    | int        | 8          | Output FIFO depth. |
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type,       "AudioEncoder");
 * cfg.set(MediaConfig::AudioCodec,  "Opus");
 * cfg.set(MediaConfig::BitrateKbps, 96);
 * MediaIO *enc = MediaIO::create(cfg);
 * enc->setExpectedDesc(upstreamDesc);
 * enc->open(MediaIO::Transform);
 * enc->writeFrame(pcmFrame);
 * Frame encoded;
 * enc->readFrame(encoded);
 * // encoded.audioPayloads()[0] is a CompressedAudioPayload
 * // carrying the codec's access unit.
 * enc->close();
 * @endcode
 *
 * @par Thread Safety
 * Strand-affine — see @ref CommandMediaIO.
 */
class AudioEncoderMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(AudioEncoderMediaIO, SharedThreadMediaIO)
        public:
                /** @brief int64_t — total frames successfully encoded. */
                static inline const MediaIOStats::ID StatsFramesEncoded{"FramesEncoded"};

                /** @brief int64_t — total compressed packets emitted. */
                static inline const MediaIOStats::ID StatsPacketsOut{"PacketsOut"};

                AudioEncoderMediaIO(ObjectBase *parent = nullptr);
                ~AudioEncoderMediaIO() override;

                Error describe(MediaIODescription *out) const override;
                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;
                Error proposeOutput(const MediaDesc &requested, MediaDesc *achievable,
                                    MediaConfig *configDelta = nullptr) const override;
                int   pendingInternalWrites() const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                void  configChanged(const MediaConfig &delta) override;

        private:
                // Drains the underlying encoder's ready Frames into
                // @c _outputQueue.  The encoder is responsible for
                // echoing the source Frame's video / ANC / metadata
                // through onto each emitted Frame via the base
                // @ref AudioEncoder::buildOutputFrame helper.
                void drainEncoderInto();

                MediaConfig        _config;
                AudioCodec         _codec;
                AudioEncoder::UPtr _encoder;
                int                _capacity = 8;
                Frame::List        _outputQueue;
                FrameCount         _frameCount{0};
                int64_t            _readCount = 0;
                FrameCount         _framesEncoded{0};
                int64_t            _packetsOut = 0;
                bool               _capacityWarned = false;
                bool               _closed = false;
};

/**
 * @brief @ref MediaIOFactory for the AudioEncoder backend.
 * @ingroup proav
 */
class AudioEncoderFactory : public MediaIOFactory {
        public:
                AudioEncoderFactory() = default;

                String name() const override { return String("AudioEncoder"); }
                String displayName() const override { return String("Audio Encoder"); }
                String description() const override {
                        return String("Encodes PCM audio frames into a registered AudioCodec bitstream");
                }
                bool canBeTransform() const override { return true; }

                Config::SpecMap configSpecs() const override;
                bool            bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig,
                                       int *outCost) const override;
                MediaIO        *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
