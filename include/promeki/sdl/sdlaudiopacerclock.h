/**
 * @file      sdl/sdlaudiopacerclock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/framepacer.h>

PROMEKI_NAMESPACE_BEGIN

class SDLAudioOutput;

/**
 * @brief FramePacerClock driven by an SDL audio device's consumption rate.
 * @ingroup sdl_core
 *
 * SDLAudioPacerClock derives its time from how many bytes the
 * SDL audio device has consumed since the stream was opened.
 * Video frames paced through this clock track the audio
 * device's actual hardware rate rather than the OS wall clock,
 * keeping A/V sync locked to the audio device.
 *
 * @par Time model
 *
 * The clock's epoch is the moment the audio stream started
 * (i.e. when @c totalBytesPushed was 0 and the device had
 * consumed nothing).  The current time in nanoseconds is:
 *
 * @code
 * consumed = output->totalBytesPushed() - output->queuedBytes()
 * nowNs    = consumed * 1e9 / bytesPerSec
 * @endcode
 *
 * @par Sleep model
 *
 * @c sleepUntilNs() computes how many bytes need to drain to
 * reach the target time, sleeps for 90% of the estimated drain
 * duration, then polls the remainder at 250 us granularity.
 *
 * @par Ownership
 *
 * The clock does not own the SDLAudioOutput.  The caller must
 * keep the output alive for the clock's lifetime.
 */
class SDLAudioPacerClock : public FramePacerClock {
        public:
                /**
                 * @brief Constructs an audio pacer clock.
                 *
                 * @param output     The SDL audio output to derive time from.
                 * @param bytesPerSec Audio drain rate in float32 bytes per
                 *                    second (sampleRate * channels * sizeof(float)).
                 */
                SDLAudioPacerClock(SDLAudioOutput *output, double bytesPerSec);

                const char *name() const override;
                int64_t resolutionNs() const override;
                int64_t nowNs() const override;
                void sleepUntilNs(int64_t targetNs) override;

        private:
                SDLAudioOutput *_output;
                double          _bytesPerSec;
                int64_t         _resolutionNs;
};

PROMEKI_NAMESPACE_END
