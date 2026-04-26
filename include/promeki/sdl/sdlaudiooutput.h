/**
 * @file      sdl/sdlaudiooutput.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/clock.h>
#include <promeki/clockdomain.h>
#include <promeki/audiodesc.h>
#include <promeki/pcmaudiopayload.h>

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
 * @par Clock integration
 *
 * Each open device registers its own @ref ClockDomain (derived from
 * the SDL device name) and can hand out a @ref Clock via
 * @ref createClock.  The returned clock is a subclass of
 * @ref Clock bound to this output — pausing it pauses the SDL
 * device, and it propagates @ref Error::ObjectGone via the
 * underlying @ref ObjectBasePtr when the output is destroyed.
 *
 * @par Example
 * @code
 * SDLAudioOutput output;
 * output.configure(audioDesc);
 * output.open();
 *
 * Clock::Ptr clock = Clock::Ptr::takeOwnership(output.createClock());
 *
 * // From any thread:
 * output.pushAudio(audioPtr);
 * @endcode
 *
 * @par Thread Safety
 * Mixed.  @ref pushAudio() is fully thread-safe and may be called from
 * any thread (e.g. a MediaIO Strand worker) — it pushes into the SDL
 * audio stream which is internally synchronised.  Lifecycle calls
 * (@ref configure / @ref open / @ref close, signal/slot connections,
 * destruction) are thread-affine via @ref ObjectBase and must run on
 * the thread that owns the bound @ref SdlSubsystem.
 */
class SDLAudioOutput : public ObjectBase {
        PROMEKI_OBJECT(SDLAudioOutput, ObjectBase)
        public:
                /** @brief Constructs an SDLAudioOutput. */
                explicit SDLAudioOutput(ObjectBase *parent = nullptr);

                ~SDLAudioOutput() override;

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
                 *
                 * Also registers the per-device @ref ClockDomain derived
                 * from the underlying SDL device identity.  The domain
                 * is accessible via @ref clockDomain after a successful
                 * open.
                 *
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
                 * @brief Pushes uncompressed audio samples to the output stream.
                 *
                 * Thread-safe.  The audio data is converted to float32
                 * if necessary before being pushed to the SDL stream.
                 *
                 * @param payload The payload's PCM bytes to play.
                 * @return true on success.
                 */
                bool pushAudio(const PcmAudioPayload &payload);

                /**
                 * @brief Returns the number of bytes currently queued.
                 *
                 * Useful for monitoring buffer health and A/V sync.
                 *
                 * Virtual so tests can supply a controllable stand-in
                 * without opening a real SDL audio device.
                 *
                 * @return Queued byte count, or 0 if not open.
                 */
                virtual int queuedBytes() const;

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
                 * Virtual so tests can supply a controllable stand-in
                 * without opening a real SDL audio device.
                 *
                 * @return Cumulative byte count.
                 */
                virtual int64_t totalBytesPushed() const { return _totalBytesPushed; }

                /**
                 * @brief Returns the @ref ClockDomain registered for the
                 *        open SDL device.
                 *
                 * Invalid (default-constructed) until @ref open
                 * succeeds.  Multiple @ref Clock instances constructed
                 * from the same device share this domain.  Virtual so
                 * tests can supply a controllable stand-in without
                 * opening a real SDL audio device.
                 */
                virtual ClockDomain clockDomain() const { return _clockDomain; }

                /**
                 * @brief Pauses or resumes the underlying SDL audio
                 *        device.
                 *
                 * The SDL-level pause freezes consumption of queued
                 * audio — @ref queuedBytes and @ref totalBytesPushed
                 * stop advancing relative to each other until the
                 * device is resumed.  Virtual so tests can override
                 * without holding a real SDL stream.
                 *
                 * @param paused The target state.
                 * @return Error::Ok on success, or a descriptive error
                 *         when the SDL call fails.
                 */
                virtual Error setPaused(bool paused);

                /**
                 * @brief Returns the current pause state.
                 *
                 * Virtual so tests can supply a controllable stand-in.
                 */
                virtual bool isPaused() const;

                /**
                 * @brief Returns a @ref Clock whose time is driven by
                 *        this output's consumed-byte counter.
                 *
                 * The clock's domain is this output's
                 * @ref clockDomain, its pause mode is
                 * @ref ClockPauseMode::PausesRawStops, and pausing it
                 * pauses the SDL device.  Returned pointer is heap
                 * allocated — the caller adopts it into a
                 * @ref Clock::Ptr.
                 *
                 * @return A newly-allocated Clock, or nullptr if the
                 *         output has not been opened yet.
                 */
                Clock *createClock();

        private:
                SDL_AudioStream *_stream = nullptr;
                AudioDesc        _desc;
                ClockDomain      _clockDomain;
                bool             _configured = false;
                bool             _open = false;
                int64_t          _totalBytesPushed = 0;
};

PROMEKI_NAMESPACE_END
