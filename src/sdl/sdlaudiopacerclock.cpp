/**
 * @file      sdlaudiopacerclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlaudiopacerclock.h>
#include <promeki/sdl/sdlaudiooutput.h>

#include <chrono>
#include <thread>

PROMEKI_NAMESPACE_BEGIN

SDLAudioPacerClock::SDLAudioPacerClock(SDLAudioOutput *output, double bytesPerSec)
        : _output(output),
          _bytesPerSec(bytesPerSec)
{
        // One sample period in nanoseconds.  bytesPerSec includes
        // all channels, so dividing by channels * sizeof(float)
        // recovers the sample rate, and 1e9/sampleRate gives the
        // per-sample resolution.  Simpler: one byte takes
        // 1/bytesPerSec seconds, and the smallest quantum the
        // audio device can advance is one sample across all
        // channels (channels * sizeof(float) bytes).
        double bytesPerSample = static_cast<double>(
                output->desc().channels()) * sizeof(float);
        _resolutionNs = static_cast<int64_t>(
                bytesPerSample / _bytesPerSec * 1e9);
        if(_resolutionNs < 1) _resolutionNs = 1;
}

const char *SDLAudioPacerClock::name() const {
        return "SDL Audio";
}

int64_t SDLAudioPacerClock::resolutionNs() const {
        return _resolutionNs;
}

int64_t SDLAudioPacerClock::nowNs() const {
        int64_t pushed = _output->totalBytesPushed();
        int64_t queued = static_cast<int64_t>(_output->queuedBytes());
        int64_t consumed = pushed - queued;
        if(consumed < 0) consumed = 0;
        return static_cast<int64_t>((double)consumed / _bytesPerSec * 1e9);
}

void SDLAudioPacerClock::sleepUntilNs(int64_t targetNs) {
        // Compute how many more bytes the device needs to consume
        // before the clock reaches targetNs.
        int64_t current = nowNs();
        if(current >= targetNs) return;

        int64_t remainingNs = targetNs - current;
        double remainingSec = (double)remainingNs / 1e9;

        // Sleep for 90% of the estimated drain time in one shot,
        // then poll the remainder.  This keeps wake-ups to 1–2
        // per frame while staying responsive to the actual audio
        // device rate.
        auto sleepUs = std::chrono::microseconds(
                static_cast<int64_t>(remainingSec * 0.9 * 1e6));
        if(sleepUs.count() > 0) {
                std::this_thread::sleep_for(sleepUs);
        }

        while(nowNs() < targetNs) {
                std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
}

PROMEKI_NAMESPACE_END
