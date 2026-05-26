/**
 * @file      transcriptionengine.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/audioformat.h>
#include <promeki/backendweight.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/function.h>
#include <promeki/list.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/transcript.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class Frame;
class MediaConfig;

/**
 * @brief Abstract base class for speech-to-text sessions.
 * @ingroup proav
 *
 * A @c TranscriptionEngine is a single push-Frame / pull-Frame session
 * that ingests PCM audio (delivered on the source @ref Frame as a
 * @ref PcmAudioPayload) and emits @ref Frame values carrying the
 * resulting @ref Transcript utterance via the @c Metadata::Transcript
 * key.  Transcription is intentionally separated from subtitle
 * shaping — the engine emits raw transcript data (words with timing
 * and confidence, speaker, language), and a downstream
 * @c SubtitleCueBuilder consumes that stream to produce @ref Subtitle
 * cues per a configurable layout / merge / partial-gating policy.
 *
 * Concrete backends — vendored Whisper, Vosk, cloud Speech-to-Text,
 * application-supplied engines — derive from this class and register a
 * @ref BackendRecord so callers can resolve them by name.
 *
 * The session contract is intentionally symmetric with
 * @ref AudioEncoder / @ref VideoEncoder so pipeline glue can drive
 * transcription stages alongside other codec stages:
 *
 *   1. Resolve a session via @ref create(name, config).
 *   2. Optionally call @ref configure with a fresh @ref MediaConfig.
 *      (Skip when a config was already supplied to @c create.)
 *   3. For each source @ref Frame containing a @ref PcmAudioPayload,
 *      call @ref submitFrame.
 *   4. After each submit, drain with @ref receiveFrame until it
 *      returns an invalid Frame.  Each emitted Frame echoes the
 *      source's video / audio / ANC payloads through unchanged and
 *      stamps @c Metadata::Transcript on the metadata.  Streaming
 *      interim hypotheses set @ref Transcript::partial @c true on
 *      the utterance before stamping; finalised utterances leave it
 *      @c false.
 *   5. When the input stream is exhausted, call @ref flush and keep
 *      draining @ref receiveFrame until the engine returns an invalid
 *      Frame.  Batch engines emit every cue here; streaming engines
 *      finalise any in-flight partial.
 *   6. Destroy the engine (let the @ref UPtr go out of scope).
 *
 * @par Configure stash
 *
 * @ref configure is non-virtual.  It stores the most-recently-passed
 * @ref MediaConfig on the base (reachable via @ref config()) and then
 * dispatches to the virtual @ref onConfigure hook that concrete
 * backends override.
 *
 * @par Channel selection
 *
 * Per-session channel selection is described via three @ref MediaConfig
 * keys: @c TranscriptionChannelMode picks the strategy
 * (@ref TranscriptionChannelMode::ChannelMap,
 *  @ref TranscriptionChannelMode::ChannelIndex,
 *  @ref TranscriptionChannelMode::DownmixAll); @c TranscriptionChannelMap
 * carries the @ref AudioChannelMap when the mode is @c ChannelMap; and
 * @c TranscriptionChannelIndex carries the single channel index when
 * the mode is @c ChannelIndex.  Backends downmix or extract the
 * selected channels internally before feeding their decoder.
 *
 * @par Backend registration
 *
 * Each backend is announced at static-init time:
 *
 * @code
 * static int gWhisperBackend = TranscriptionEngine::registerBackend({
 *         .name            = "WhisperCpp",
 *         .description     = "Vendored whisper.cpp transcription engine.",
 *         .weight          = BackendWeight::Vendored,
 *         .supportedInputs = { AudioFormat::PCMI_Float32LE, AudioFormat::PCMI_S16LE },
 *         .supportedModes  = { TranscriptionMode::Streaming, TranscriptionMode::Batch },
 *         .factory         = []() -> TranscriptionEngine::UPtr {
 *                 return UniquePtr<TranscriptionEngine>::takeOwnership(new WhisperEngine);
 *         },
 * });
 * @endcode
 *
 * @par Thread Safety
 *
 * Conditionally thread-safe.  Each pipeline thread should own its own
 * engine instance; concurrent access to a single instance is
 * unsupported.  The static registry is internally synchronized.
 *
 * @see Subtitle, MediaConfig, AudioEncoder
 */
class TranscriptionEngine {
        public:
                /** @brief Unique-ownership pointer to a @c TranscriptionEngine. */
                using UPtr = UniquePtr<TranscriptionEngine>;

                /** @brief Factory signature used by the engine registry. */
                using Factory = Function<UPtr()>;

                /**
                 * @brief Registration record describing a concrete
                 *        transcription engine implementation.
                 *
                 * Holds everything the registry needs to advertise the
                 * engine to callers and construct fresh session
                 * instances on demand.  @c supportedInputs and
                 * @c supportedModes let callers filter the registry by
                 * capability before paying the construction cost of an
                 * engine that won't actually accept their audio.
                 */
                struct BackendRecord {
                                /** @brief Short, unique engine name (must be a valid C identifier). */
                                String name;

                                /** @brief Human-readable description, used by introspection / logging. */
                                String description;

                                /**
                                 * @brief Resolution weight when multiple
                                 *        engines could plausibly satisfy
                                 *        a capability query.
                                 *
                                 * Follows the @ref BackendWeight scale —
                                 * vendored backends register at
                                 * @c BackendWeight::Vendored, application
                                 * overrides at @c BackendWeight::User.
                                 */
                                int weight = BackendWeight::Vendored;

                                /**
                                 * @brief Uncompressed @ref AudioFormat IDs
                                 *        this engine can ingest.
                                 *
                                 * Stored as @c int (rather than the typed
                                 * wrapper) so this header doesn't need a
                                 * full @c audioformat.h dependency chain.
                                 * Callers wrap each entry as
                                 * @c AudioFormat(static_cast<AudioFormat::ID>(id))
                                 * at use time.  Empty means "engine
                                 * accepts any sample format it can
                                 * convert internally."
                                 */
                                List<int> supportedInputs;

                                /**
                                 * @brief @ref TranscriptionMode values
                                 *        this engine can run in.
                                 *
                                 * Stored as @c int for the same
                                 * header-dependency reason as
                                 * @ref supportedInputs.  Empty means
                                 * "engine supports every mode" — the
                                 * conservative answer is to populate
                                 * this explicitly.
                                 */
                                List<int> supportedModes;

                                /**
                                 * @brief Factory returning a freshly
                                 *        allocated, unconfigured engine
                                 *        instance.
                                 *
                                 * The returned @c UPtr is single-owner.
                                 * Callers configure it via
                                 * @ref configure (or supply a config to
                                 * @ref create) before pushing any
                                 * frames.
                                 */
                                Factory factory;
                };

                /** @brief Virtual destructor. */
                virtual ~TranscriptionEngine();

                /** @brief Returns the registered backend name pinned to this session. */
                const String &name() const { return _name; }

                /**
                 * @brief Applies engine parameters from a @ref MediaConfig.
                 *
                 * Non-virtual.  Stores @p config on the base (reachable
                 * via @ref config()) and then dispatches to the
                 * virtual @ref onConfigure hook.  Backends override
                 * @ref onConfigure rather than this method.
                 */
                void configure(const MediaConfig &config);

                /**
                 * @brief Submits one source @ref Frame for transcription.
                 *
                 * The engine extracts the @ref PcmAudioPayload it wants
                 * from @p frame (typically via @ref selectInputPayload).
                 * A submitted Frame with no matching audio payload is
                 * treated as @ref Error::Invalid.
                 *
                 * @return @c Error::Ok on success; an error code
                 *         describing the failure otherwise (also
                 *         retrievable via @ref lastError).
                 */
                virtual Error submitFrame(const Frame &frame) = 0;

                /**
                 * @brief Dequeues one transcription-output @ref Frame,
                 *        or an invalid Frame when none is ready.
                 *
                 * Emitted Frames echo the source's video / audio / ANC
                 * payloads through unchanged and carry the produced
                 * @ref Transcript on @c Metadata::Transcript.
                 * Streaming engines set @ref Transcript::partial
                 * @c true on interim hypotheses; finalised
                 * utterances leave it @c false.
                 */
                virtual Frame receiveFrame() = 0;

                /**
                 * @brief Signals end-of-stream and asks the engine to
                 *        emit every remaining cue.
                 *
                 * In @c Batch mode this is where every cue is produced;
                 * in @c Streaming mode this finalises any in-flight
                 * partial.  After @ref flush, @ref receiveFrame
                 * eventually returns an invalid Frame once the engine
                 * has nothing more to emit.
                 */
                virtual Error flush() = 0;

                /**
                 * @brief Discards pending audio / cues and returns the
                 *        engine to a fresh, pre-configure state.
                 *
                 * Configuration is preserved — only the in-flight
                 * decoder state is dropped.  Useful when seeking or
                 * cutting to a fresh source.
                 */
                virtual Error reset() = 0;

                /** @brief Returns the last error produced by the engine. */
                Error lastError() const { return _lastError; }

                /** @brief Returns a human-readable message for the last error. */
                const String &lastErrorMessage() const { return _lastErrorMessage; }

                // ---- Backend registry ----

                /**
                 * @brief Registers a concrete engine backend.
                 *
                 * @return @c Error::Ok on success; @c Error::Invalid when
                 *         the record is malformed (empty name, null
                 *         factory) or a duplicate name has already been
                 *         registered with the same or higher weight.
                 */
                static Error registerBackend(BackendRecord record);

                /**
                 * @brief Returns every registered engine backend.
                 *
                 * Sorted by descending @c weight so the first entry is
                 * the highest-priority match.
                 */
                static List<BackendRecord> registeredBackends();

                /**
                 * @brief Looks up a registered engine by name.
                 * @return The matching record, or @c Error::IdNotFound.
                 */
                static Result<BackendRecord> lookupBackend(const String &name);

                /**
                 * @brief Creates a new transcription session.
                 *
                 * @param name    Registered backend name.
                 * @param config  Optional initial configuration.  When
                 *                non-null, the engine's @ref configure
                 *                hook is invoked before the @c UPtr is
                 *                returned.
                 * @return A configured @c UPtr on success; an error
                 *         (@c Error::IdNotFound, @c Error::LibraryFailure,
                 *         @c Error::NotSupported) otherwise.
                 */
                static Result<UPtr> create(const String &name, const MediaConfig *config = nullptr);

                // ---- Frame-shaped helpers (public so backends can use them) ----

                /**
                 * @brief Looks up the first @ref PcmAudioPayload on
                 *        @p frame matching @p streamIndex.
                 *
                 * Mirrors @ref AudioEncoder::selectInputPayload.
                 * @p streamIndex @c -1 returns the first PCM audio
                 * payload found regardless of its
                 * @ref MediaPayload::streamIndex.  Non-negative values
                 * require a matching @c streamIndex().  Returns a null
                 * pointer on miss.
                 */
                static PcmAudioPayload::Ptr selectInputPayload(const Frame &frame, int streamIndex = -1);

                /**
                 * @brief Constructs an output @ref Frame echoing
                 *        @p source's payloads and stamping
                 *        @p transcript.
                 *
                 * Copies @p source's metadata, capture time, and every
                 * payload (video, audio, ANC) through unchanged —
                 * transcription is additive: the engine does not
                 * replace the audio it transcribed, it just adds the
                 * recognised words.  Then writes the
                 * @c Metadata::Transcript key to @p transcript.
                 * Partial vs. finalised state rides on the
                 * transcript itself via @ref Transcript::partial, so
                 * downstream consumers (subtitle builder, search
                 * indexer) read the same field whether the
                 * transcript arrived on a Frame or out of a
                 * @ref TranscriptList — callers set
                 * @c transcript.setPartial(true) before passing an
                 * interim hypothesis.
                 *
                 * @param source     Source Frame whose payloads /
                 *                   metadata are echoed through.
                 * @param transcript Utterance to stamp on the output.
                 * @return The assembled output Frame.
                 */
                static Frame buildOutputFrame(const Frame &source, const Transcript &transcript);

                /**
                 * @brief Returns the absolute media-time stamp for the
                 *        sample at @p sampleOffset within @p payload.
                 *
                 * Anchors transcript word timestamps to the source
                 * @ref PcmAudioPayload's @c pts so streaming
                 * sessions that accumulate audio across multiple
                 * submit chunks ride on a single coherent timeline
                 * without each engine bookkeeping a separate session
                 * epoch.  Computes
                 * @code
                 * payload.pts().timeStamp() + sampleOffset / sampleRate
                 * @endcode
                 * with nanosecond precision.  Returns
                 * @c TimeStamp::Invalid when the payload has no
                 * @c pts or its descriptor has a zero sample rate.
                 *
                 * Backends are encouraged to derive every word's
                 * @ref TranscriptWord::start / @ref TranscriptWord::end
                 * through this helper so all transcript timestamps
                 * are anchored identically across the library.
                 *
                 * @param payload      Source PCM payload that drove
                 *                     the recognition.
                 * @param sampleOffset Sample offset within @p payload
                 *                     (per-channel, not byte offset).
                 */
                static TimeStamp wordTimestamp(const PcmAudioPayload &payload, size_t sampleOffset);

        protected:
                TranscriptionEngine() = default;

                /** @brief Records the registered backend name on this session. */
                void setName(String name) { _name = std::move(name); }

                /**
                 * @brief Concrete-backend hook invoked from
                 *        @ref configure after the base stashes @p config.
                 *
                 * Default implementation is a no-op.
                 */
                virtual void onConfigure(const MediaConfig &config);

                /**
                 * @brief Returns the most-recent @ref MediaConfig passed
                 *        to @ref configure, or a default-constructed
                 *        instance when @ref configure has never been
                 *        called.
                 */
                const MediaConfig &config() const;

                Error  _lastError;
                String _lastErrorMessage;

                /** @brief Records a new error state. */
                void setError(Error err, const String &msg = String());

                /** @brief Clears the error state. */
                void clearError();

        private:
                UniquePtr<MediaConfig> _stashedConfig;
                String                 _name;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
