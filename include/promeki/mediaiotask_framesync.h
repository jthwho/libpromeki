/**
 * @file      mediaiotask_framesync.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/framesync.h>
#include <promeki/clock.h>
#include <promeki/syntheticclock.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

class Clock;

/**
 * @brief MediaIOTask backend that resyncs media to a target clock cadence.
 * @ingroup proav
 *
 * ReadWrite MediaIO that wraps a @ref FrameSync instance.  The write
 * side pushes source frames into the FrameSync; the read side calls
 * @ref FrameSync::pullFrame, which blocks on the configured clock
 * until the next deadline and synthesises one output frame.
 *
 * By default a @ref SyntheticClock is used, making the task a
 * non-blocking frame-rate converter suitable for offline / file-based
 * pipelines.  For real-time playback, call @ref setClock before
 * opening to substitute a @ref WallClock or device-paced clock
 * (e.g. @c SDLAudioClock).  Callers using an external clock will
 * typically construct the task directly and hand it to
 * @ref MediaIO::adoptTask.
 *
 * @par Mode support
 *
 * Only @c MediaIO::Transform is supported.
 *
 * @par Threading
 *
 * @ref FrameSync::pullFrame blocks the MediaIO read strand until the
 * clock deadline.  This is intentional — the FrameSync task is the
 * pacing stage that governs the pipeline's temporal cadence.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::OutputFrameRate      | FrameRate               | Invalid (inherit source) | Output frame rate. |
 * | @ref MediaConfig::OutputAudioRate      | float                   | 0 (inherit source)       | Output audio sample rate in Hz. |
 * | @ref MediaConfig::OutputAudioChannels  | int                     | 0 (inherit source)       | Output audio channel count. |
 * | @ref MediaConfig::OutputAudioDataType  | Enum @ref AudioDataType | Invalid (inherit source) | Output audio sample format. |
 * | @ref MediaConfig::InputQueueCapacity   | int                     | 8                        | Maximum input queue depth. |
 *
 * All output format keys default to "inherit from source" — set only
 * the components you want to override.  The overflow policy defaults
 * to @c DropOldest; callers that need
 * @c Block should construct the task directly, call
 * @ref frameSync().setInputOverflowPolicy, and use
 * @ref MediaIO::adoptTask.
 *
 * @par Stats keys
 *
 * | Key | Type | Description |
 * |-----|------|-------------|
 * | FramesPushed   | int64_t | Total frames pushed by the write side. |
 * | FramesPulled   | int64_t | Total frames pulled by the read side. |
 * | FramesRepeated | int64_t | Total source frames repeated. |
 * | FramesDropped  | int64_t | Total source frames dropped. |
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "FrameSync");
 * MediaIO *io = MediaIO::create(cfg);
 * io->setExpectedDesc(upstreamDesc);
 * io->open(MediaIO::Transform);
 * io->writeFrame(sourceFrame);
 * Frame::Ptr outputFrame;
 * io->readFrame(outputFrame);
 * io->close();
 * delete io;
 * @endcode
 */
class MediaIOTask_FrameSync : public MediaIOTask {
        public:
                /** @brief int64_t — total frames pushed by the write side. */
                static inline const MediaIOStats::ID StatsFramesPushed{"FramesPushed"};

                /** @brief int64_t — total frames pulled by the read side. */
                static inline const MediaIOStats::ID StatsFramesPulled{"FramesPulled"};

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc describing the FrameSync backend.
                 */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs a MediaIOTask_FrameSync with a default SyntheticClock. */
                MediaIOTask_FrameSync() = default;

                /** @brief Destructor. */
                ~MediaIOTask_FrameSync() override;

                /**
                 * @brief Sets the clock used for frame pacing.
                 *
                 * Must be called before @c open().  Ownership is shared
                 * via the @ref Clock::Ptr.  Passing a null Ptr reverts
                 * to the built-in @ref SyntheticClock.
                 *
                 * @param clock External clock, or null for synthetic.
                 */
                void setClock(const Clock::Ptr &clock);

                /**
                 * @brief Returns the underlying FrameSync instance.
                 *
                 * Exposed so callers that construct the task directly
                 * (via @ref MediaIO::adoptTask) can fine-tune FrameSync
                 * parameters before opening.
                 */
                FrameSync &frameSync() { return _sync; }

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

                Error describe(MediaIODescription *out) const override;
                Error proposeInput(const MediaDesc &offered,
                                   MediaDesc *preferred) const override;
                Error proposeOutput(const MediaDesc &requested,
                                    MediaDesc *achievable) const override;

                FrameSync               _sync;
                Clock::Ptr              _ownedClock;
                Clock::Ptr              _externalClock;
                FrameCount              _framesPushed{0};
                FrameCount              _framesPulled{0};
};

PROMEKI_NAMESPACE_END
