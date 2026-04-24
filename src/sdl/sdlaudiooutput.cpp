/**
 * @file      sdlaudiooutput.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlaudioclock.h>
#include <promeki/string.h>
#include <promeki/logger.h>
#include <promeki/pcmaudiopayload.h>

#include <SDL3/SDL.h>

PROMEKI_NAMESPACE_BEGIN

SDLAudioOutput::SDLAudioOutput(ObjectBase *parent) : ObjectBase(parent) {
}

SDLAudioOutput::~SDLAudioOutput() {
        close();
}

bool SDLAudioOutput::configure(const AudioDesc &desc) {
        if(_open) {
                promekiErr("SDLAudioOutput: cannot configure while open");
                return false;
        }
        _desc = desc;
        _configured = true;
        return true;
}

bool SDLAudioOutput::open() {
        if(!_configured) {
                promekiErr("SDLAudioOutput: not configured");
                return false;
        }
        if(_open) return true;

        // We always push float32 to SDL — the
        // PcmAudioPayload::convert() path handles conversion
        // from any promeki format to native float.
        SDL_AudioSpec spec = {};
        spec.format = SDL_AUDIO_F32;
        spec.channels = static_cast<int>(_desc.channels());
        spec.freq = static_cast<int>(_desc.sampleRate());

        _stream = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                &spec, nullptr, nullptr
        );

        if(_stream == nullptr) {
                promekiErr("SDLAudioOutput: SDL_OpenAudioDeviceStream failed: %s",
                           SDL_GetError());
                return false;
        }

        // Register (or reuse) a ClockDomain keyed on the actual SDL
        // device name.  PerStream epoch reflects that SDL audio
        // devices are independent time sources, not comparable across
        // hardware.  Multiple SDLAudioOutputs targeting the same
        // device share this domain by construction, since
        // ClockDomain::registerDomain dedupes by name.
        SDL_AudioDeviceID devid = SDL_GetAudioStreamDevice(_stream);
        const char *sdlName = (devid != 0) ? SDL_GetAudioDeviceName(devid) : nullptr;
        String domainName = "sdl.audio";
        if(sdlName != nullptr && sdlName[0] != '\0') {
                domainName += ":";
                domainName += sdlName;
        }
        _clockDomain = ClockDomain(ClockDomain::registerDomain(
                domainName,
                "SDL audio device consumption-rate clock",
                ClockEpoch::PerStream));

        // Start playback
        SDL_ResumeAudioStreamDevice(_stream);

        _open = true;
        return true;
}

void SDLAudioOutput::close() {
        if(_stream != nullptr) {
                SDL_DestroyAudioStream(_stream);
                _stream = nullptr;
        }
        _open = false;
        _totalBytesPushed = 0;
}

bool SDLAudioOutput::pushAudio(const PcmAudioPayload &payload) {
        if(!_open || _stream == nullptr) return false;
        if(!payload.isValid()) return false;

        // Convert to native float if needed.  The conversion uses the
        // payload's own convert entry; on the fast path (already
        // native float) we just keep a view of the original.
        PcmAudioPayload::Ptr converted;
        const PcmAudioPayload *src = &payload;
        if(payload.desc().format().id() != AudioFormat::NativeFloat) {
                converted = payload.convert(AudioFormat(AudioFormat::NativeFloat));
                if(!converted.isValid()) {
                        promekiErr("SDLAudioOutput: audio format conversion failed");
                        return false;
                }
                src = converted.ptr();
        }

        // Native-float PCM lands in a single interleaved plane.
        if(src->planeCount() == 0) return false;
        auto view = src->plane(0);
        const size_t bytes = src->sampleCount() * src->desc().channels() * sizeof(float);
        if(view.size() < bytes) {
                promekiErr("SDLAudioOutput: payload plane smaller than expected (%zu < %zu)",
                           view.size(), bytes);
                return false;
        }
        if(!SDL_PutAudioStreamData(_stream, view.data(), static_cast<int>(bytes))) {
                promekiErr("SDLAudioOutput: SDL_PutAudioStreamData failed: %s",
                           SDL_GetError());
                return false;
        }

        _totalBytesPushed += static_cast<int64_t>(bytes);
        return true;
}

int SDLAudioOutput::queuedBytes() const {
        if(!_open || _stream == nullptr) return 0;
        return SDL_GetAudioStreamQueued(_stream);
}

Error SDLAudioOutput::setPaused(bool paused) {
        if(!_open || _stream == nullptr) return Error::NotOpen;
        bool ok = paused
                ? SDL_PauseAudioStreamDevice(_stream)
                : SDL_ResumeAudioStreamDevice(_stream);
        if(!ok) {
                promekiErr("SDLAudioOutput: SDL_%sAudioStreamDevice failed: %s",
                           paused ? "Pause" : "Resume", SDL_GetError());
                return Error::DeviceError;
        }
        return {};
}

bool SDLAudioOutput::isPaused() const {
        if(!_open || _stream == nullptr) return false;
        return SDL_AudioStreamDevicePaused(_stream);
}

Clock *SDLAudioOutput::createClock() {
        if(!_open) {
                promekiErr("SDLAudioOutput::createClock called before open");
                return nullptr;
        }
        return new SDLAudioClock(this);
}

PROMEKI_NAMESPACE_END
