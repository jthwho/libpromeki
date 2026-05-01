/**
 * @file      srcmediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedthreadmediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/audiodesc.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend that performs audio sample format conversion.
 * @ingroup proav
 *
 * ReadWrite MediaIO that accepts a frame on @c writeFrame(), converts
 * each audio track to the configured output @ref AudioFormat via
 * @ref PcmAudioPayload::convert, and emits the result on
 * @c readFrame().  Video payloads and metadata are forwarded unchanged.
 *
 * If no @ref MediaConfig::OutputAudioDataType is set (or it is
 * @c Invalid), audio tracks pass through unchanged.
 *
 * @par Mode support
 *
 * Only @c MediaIO::Transform is supported.
 *
 * @par Back-pressure
 *
 * The task maintains an internal output FIFO.  Writes always succeed —
 * frames are never silently dropped.  When the FIFO exceeds the
 * configured capacity a one-shot warning is logged.  When the FIFO is
 * empty, @c readFrame() returns @c Error::TryAgain.  FIFO capacity is
 * configurable via @ref MediaConfig::Capacity (default 4).
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::OutputAudioDataType | Enum @ref AudioDataType | Invalid (pass-through) | Target audio sample format. |
 * | @ref MediaConfig::Capacity            | int                     | 4                      | Maximum output FIFO depth. |
 *
 * @par Stats keys
 *
 * | Key | Type | Description |
 * |-----|------|-------------|
 * | FramesConverted | int64_t | Total frames successfully converted. |
 * | QueueDepth      | int64_t | Current FIFO depth. |
 * | QueueCapacity   | int64_t | Maximum FIFO depth. |
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "SRC");
 * cfg.set(MediaConfig::OutputAudioDataType,
 *         AudioDataType::PCMI_S16LE);
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::Transform);
 * io->writeFrame(inputFrame);
 * Frame::Ptr outputFrame;
 * io->readFrame(outputFrame);
 * io->close();
 * delete io;
 * @endcode
 *
 * @par Thread Safety
 * Strand-affine — see @ref CommandMediaIO.
 */
class SrcMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(SrcMediaIO, SharedThreadMediaIO)
        public:
                /** @brief int64_t — total frames successfully converted. */
                static inline const MediaIOStats::ID StatsFramesConverted{"FramesConverted"};

                SrcMediaIO(ObjectBase *parent = nullptr);
                ~SrcMediaIO() override;

                Error describe(MediaIODescription *out) const override;
                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;
                Error proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const override;
                int   pendingInternalWrites() const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

        private:
                Error convertFrame(const Frame::Ptr &input, Frame::Ptr &output);

                AudioFormat::ID _outputAudioDataType = AudioFormat::Invalid;
                bool            _outputAudioDataTypeSet = false;
                int             _capacity = 4;

                List<Frame::Ptr> _outputQueue;
                FrameCount       _frameCount{0};
                int64_t          _readCount = 0;
                FrameCount       _framesConverted{0};
                bool             _capacityWarned = false;
};

/**
 * @brief @ref MediaIOFactory for the SRC (audio sample-rate / format converter) backend.
 * @ingroup proav
 */
class SrcFactory : public MediaIOFactory {
        public:
                SrcFactory() = default;

                String name() const override { return String("SRC"); }
                String displayName() const override { return String("Sample Rate Converter"); }
                String description() const override { return String("Audio sample format converter"); }
                bool   canBeTransform() const override { return true; }

                Config::SpecMap configSpecs() const override;
                bool            bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig,
                                       int *outCost) const override;
                MediaIO        *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END
