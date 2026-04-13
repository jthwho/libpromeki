/**
 * @file      sdlaudiooutput.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/logger.h>

#include <SDL3/SDL.h>

PROMEKI_NAMESPACE_BEGIN

SDLAudioOutput::SDLAudioOutput() {
        return;
}

SDLAudioOutput::~SDLAudioOutput() {
        close();
        return;
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

        // We always push float32 to SDL — the Audio::convertTo() path
        // handles conversion from any promeki format to native float.
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
        return;
}

bool SDLAudioOutput::pushAudio(const Audio &audio) {
        if(!_open || _stream == nullptr) return false;

        // Convert to native float if needed
        const Audio *src = &audio;
        Audio converted;
        if(!audio.isNative()) {
                converted = audio.convertTo(AudioDesc::NativeType);
                if(!converted.isValid()) {
                        promekiErr("SDLAudioOutput: audio format conversion failed");
                        return false;
                }
                src = &converted;
        }

        size_t bytes = src->samples() * src->desc().channels() * sizeof(float);
        if(!SDL_PutAudioStreamData(_stream, src->data<float>(), static_cast<int>(bytes))) {
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

PROMEKI_NAMESPACE_END
