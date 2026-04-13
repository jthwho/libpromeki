/**
 * @file      sdl/sdlaudiooutput.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/audiodesc.h>
#include <promeki/audio.h>

struct SDL_AudioStream;

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Manages an SDL3 audio output device for playback.
 * @ingroup sdl_core
 *
 * SDLAudioOutput opens an SDL audio device and stream, accepting
 * promeki Audio objects for playback.  Audio data is converted to
 * the platform's native float format if necessary and pushed to
 * an SDL audio stream, which handles sample rate conversion and
 * buffering automatically.
 *
 * pushAudio() is thread-safe — it can be called from any thread
 * (e.g. a MediaIO Strand worker) while the SDL audio device
 * plays on its own thread.
 *
 * @par Example
 * @code
 * SDLAudioOutput output;
 * output.configure(audioDesc);
 * output.open();
 *
 * // From any thread:
 * output.pushAudio(audioPtr);
 * @endcode
 */
class SDLAudioOutput {
        public:
                SDLAudioOutput();
                ~SDLAudioOutput();

                SDLAudioOutput(const SDLAudioOutput &) = delete;
                SDLAudioOutput &operator=(const SDLAudioOutput &) = delete;

                /**
                 * @brief Configures the audio output format.
                 *
                 * Must be called before open(). The audio stream will
                 * accept data in this format and convert to the device's
                 * native format as needed.
                 *
                 * @param desc The audio format descriptor.
                 * @return true on success.
                 */
                bool configure(const AudioDesc &desc);

                /**
                 * @brief Opens the SDL audio device and starts playback.
                 * @return true on success.
                 */
                bool open();

                /**
                 * @brief Closes the audio device and frees resources.
                 */
                void close();

                /**
                 * @brief Returns true if the device is open and playing.
                 */
                bool isOpen() const { return _open; }

                /**
                 * @brief Pushes audio samples to the output stream.
                 *
                 * Thread-safe.  The audio data is converted to float32
                 * if necessary before being pushed to the SDL stream.
                 *
                 * @param audio The audio data to play.
                 * @return true on success.
                 */
                bool pushAudio(const Audio &audio);

                /**
                 * @brief Returns the number of bytes currently queued.
                 *
                 * Useful for monitoring buffer health and A/V sync.
                 *
                 * @return Queued byte count, or 0 if not open.
                 */
                int queuedBytes() const;

                /**
                 * @brief Returns the configured audio descriptor.
                 */
                const AudioDesc &desc() const { return _desc; }

                /**
                 * @brief Returns total float32 bytes pushed since open.
                 *
                 * Combined with @ref queuedBytes(), this lets callers
                 * derive how many bytes the device has consumed:
                 * <tt>consumed = totalBytesPushed() - queuedBytes()</tt>.
                 *
                 * @return Cumulative byte count.
                 */
                int64_t totalBytesPushed() const { return _totalBytesPushed; }

        private:
                SDL_AudioStream *_stream = nullptr;
                AudioDesc        _desc;
                bool             _configured = false;
                bool             _open = false;
                int64_t          _totalBytesPushed = 0;
};

PROMEKI_NAMESPACE_END
