/**
 * @file      audiodecodermediaio.h
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
#include <promeki/audiodecoder.h>
#include <promeki/audiocodec.h>
#include <promeki/audioformat.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend that decodes compressed audio packets into PCM Frames.
 * @ingroup proav
 *
 * @c AudioDecoderMediaIO is the symmetric counterpart of
 * @ref AudioEncoderMediaIO.  It accepts a Frame whose
 * @ref CompressedAudioPayload entries carry the encoded bitstream,
 * hands each payload to a concrete @ref AudioDecoder via
 * @c submitPayload, and drains @c receiveAudioPayload producing one
 * output Frame per decoded @ref PcmAudioPayload.  Video on the
 * input Frame is forwarded unchanged so it stays synchronised on
 * the same PTS.
 *
 * The registered backend name is @c "AudioDecoder"; callers select a
 * concrete codec in one of two ways:
 *
 *  -# **Explicit** — set @ref MediaConfig::AudioCodec in the config
 *     (e.g. @c "Opus", @c "AAC").  The decoder is created during
 *     @c open().
 *  -# **Auto-detect** — omit @ref MediaConfig::AudioCodec.  The task
 *     defers decoder creation until the first @c writeFrame() call,
 *     where it inspects the incoming @ref CompressedAudioPayload's
 *     descriptor AudioFormat and resolves the codec via
 *     @ref AudioFormat::audioCodec.
 *
 * @par Mode support
 *
 * Only @c MediaIO::Transform is supported.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::AudioCodec          | AudioCodec | (auto)  | Codec to use.  When omitted the codec is detected from the first payload's @ref AudioFormat. |
 * | @ref MediaConfig::OutputAudioDataType | Enum @ref AudioDataType | Invalid | Desired uncompressed output format.  Invalid means "use decoder's native". |
 * | @ref MediaConfig::Capacity            | int        | 8       | Output FIFO depth. |
 *
 * @par Example — explicit codec
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type,       "AudioDecoder");
 * cfg.set(MediaConfig::AudioCodec,  "Opus");
 * cfg.set(MediaConfig::OutputAudioDataType, AudioDataType::PCMI_S16LE);
 * MediaIO *dec = MediaIO::create(cfg);
 * dec->setExpectedDesc(compressedDesc);
 * dec->open(MediaIO::Transform);
 * dec->writeFrame(packetFrame);
 * Frame decoded;
 * dec->readFrame(decoded);
 * dec->close();
 * @endcode
 *
 * @par Example — auto-detect codec from packet
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "AudioDecoder");
 * MediaIO *dec = MediaIO::create(cfg);
 * dec->setExpectedDesc(compressedDesc);
 * dec->open(MediaIO::Transform);
 * dec->writeFrame(packetFrame);   // codec resolved here
 * Frame decoded;
 * dec->readFrame(decoded);
 * dec->close();
 * @endcode
 *
 * @par Thread Safety
 * Strand-affine — see @ref CommandMediaIO.
 */
class AudioDecoderMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(AudioDecoderMediaIO, SharedThreadMediaIO)
        public:
                /** @brief int64_t — total packets successfully decoded. */
                static inline const MediaIOStats::ID StatsPacketsDecoded{"PacketsDecoded"};

                /** @brief int64_t — total decoded PCM payloads emitted. */
                static inline const MediaIOStats::ID StatsFramesOut{"FramesOut"};

                AudioDecoderMediaIO(ObjectBase *parent = nullptr);
                ~AudioDecoderMediaIO() override;

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
                // Drains every currently-available output Frame out of
                // the underlying decoder and pushes them onto
                // @c _outputQueue.  The decoder is responsible for
                // echoing the source Frame's video / metadata through
                // onto each emitted Frame via the base
                // @ref AudioDecoder::buildOutputFrame helper.
                void  drainDecoderInto();
                Error createDecoder(const AudioCodec &codec);

                MediaConfig        _config;
                AudioCodec         _codec;
                AudioDecoder::UPtr _decoder;
                AudioFormat::ID    _outputAudioDataType = AudioFormat::Invalid;
                bool               _outputAudioDataTypeSet = false;
                int                _capacity = 8;
                Frame::List        _outputQueue;
                FrameCount         _frameCount{0};
                int64_t            _readCount = 0;
                int64_t            _packetsDecoded = 0;
                int64_t            _framesOut = 0;
                bool               _capacityWarned = false;
                bool               _closed = false;
};

/**
 * @brief @ref MediaIOFactory for the AudioDecoder backend.
 * @ingroup proav
 */
class AudioDecoderFactory : public MediaIOFactory {
        public:
                AudioDecoderFactory() = default;

                String name() const override { return String("AudioDecoder"); }
                String displayName() const override { return String("Audio Decoder"); }
                String description() const override {
                        return String("Decodes compressed audio bitstreams into PCM frames");
                }
                bool canBeTransform() const override { return true; }

                Config::SpecMap configSpecs() const override;
                bool            bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig,
                                       int *outCost) const override;
                MediaIO        *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
