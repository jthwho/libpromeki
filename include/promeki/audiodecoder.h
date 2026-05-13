/**
 * @file      audiodecoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/uniqueptr.h>
#include <promeki/audiocodec.h>
#include <promeki/backendweight.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/compressedaudiopayload.h>

PROMEKI_NAMESPACE_BEGIN

class MediaConfig;
class Frame;

/**
 * @brief Abstract base class for stateful audio decoders.
 * @ingroup proav
 *
 * Inverse of @ref AudioEncoder &mdash; source @ref Frame objects
 * carrying a @ref CompressedAudioPayload pushed via @ref submitFrame
 * feed an internal pipeline and output @ref Frame objects (carrying a
 * @ref PcmAudioPayload, plus the echoed-through video / metadata of
 * the matching submitted source) come back out of @ref receiveFrame.
 * The decoder may buffer several packets before producing its first
 * output (codec start-up state, silence priming, reordering where
 * present).
 *
 * Codec-family metadata lives on the @ref AudioCodec wrapper returned
 * by @ref codec(); per-backend facts (supported output formats, …)
 * live on the registered @ref BackendRecord and are reachable via
 * @ref AudioCodec::decoderSupportedOutputs.
 *
 * @par Backend registration
 *
 * Same idiom as @ref AudioEncoder::registerBackend.
 *
 * @par Session lifecycle
 *
 *   1. Resolve a session via @ref AudioCodec::createDecoder.
 *   2. Optionally call @ref configure.
 *   3. For each source Frame carrying a @ref CompressedAudioPayload,
 *      call @ref submitFrame.
 *   4. After each submit, drain with @ref receiveFrame until it
 *      returns an invalid Frame.
 *   5. Call @ref flush when the input stream ends, then drain again.
 *   6. Destroy the decoder.
 *
 * @par Configure stash
 *
 * @ref configure is non-virtual.  Its body stores the most-recently-
 * passed @ref MediaConfig on the base (reachable via @ref config())
 * and then dispatches to the virtual @ref onConfigure hook that
 * concrete backends override.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Each pipeline thread should own its own
 * decoder instance; concurrent access to a single instance is
 * unsupported.
 */
class AudioDecoder {
        public:
                /** @brief Unique-ownership pointer to an AudioDecoder. */
                using UPtr = UniquePtr<AudioDecoder>;

                /** @brief Factory signature used by the decoder registry. */
                using Factory = std::function<AudioDecoder *()>;

                /**
                 * @brief Registration record attaching a concrete decoder to a (codec, backend) pair.
                 */
                struct BackendRecord {
                                AudioCodec::ID      codecId;
                                AudioCodec::Backend backend;
                                int                 weight = BackendWeight::Vendored;
                                /**
                         * @brief Uncompressed @ref AudioFormat IDs this backend emits.
                         *
                         * Stored as @c int.  Empty means "any format the
                         * rest of the pipeline can accept."
                         */
                                List<int> supportedOutputs;
                                Factory   factory;
                };

                /** @brief Virtual destructor. */
                virtual ~AudioDecoder();

                /** @brief Returns the codec this session decodes, with backend pinned. */
                AudioCodec codec() const { return _codec; }

                /**
                 * @brief Applies decoder parameters from a @ref MediaConfig.
                 *
                 * Non-virtual.  Stores @p config on the base (reachable
                 * via @ref config()) and then dispatches to the virtual
                 * @ref onConfigure hook.  Backends override
                 * @ref onConfigure rather than this method.
                 */
                void configure(const MediaConfig &config);

                /**
                 * @brief Submits one source @ref Frame for decoding.
                 *
                 * The decoder extracts the @ref CompressedAudioPayload
                 * it wants from @p frame (typically via the
                 * @ref selectInputPayload helper).  An invalid Frame
                 * is treated as @ref Error::Invalid.
                 */
                virtual Error submitFrame(const Frame &frame) = 0;

                /**
                 * @brief Dequeues one decoded output @ref Frame, or an
                 *        invalid Frame when none is ready.
                 */
                virtual Frame receiveFrame() = 0;

                /** @brief Signals end-of-stream; remaining frames can be drained with @ref receiveFrame. */
                virtual Error flush() = 0;

                /** @brief Discards any pending packets / frames. */
                virtual Error reset() = 0;

                /** @brief Returns the last error produced by the decoder. */
                Error lastError() const { return _lastError; }

                /** @brief Returns a human-readable message for the last error. */
                const String &lastErrorMessage() const { return _lastErrorMessage; }

                // ---- Backend registry ----

                /** @brief Registers a concrete decoder backend for a (codec, backend) pair. */
                static Error registerBackend(BackendRecord record);

                // ---- Internal bridge from AudioCodec ----

                static AudioCodec::BackendList availableBackends(AudioCodec::ID codecId);
                static List<int>               supportedOutputsFor(AudioCodec::ID codecId, AudioCodec::Backend backend);
                static Result<AudioDecoder *>  create(AudioCodec::ID codecId, AudioCodec::Backend pinned,
                                                      const MediaConfig *config);

        protected:
                AudioDecoder() = default;

                /** @brief Records the codec + backend this session implements. */
                void setCodec(AudioCodec codec) { _codec = codec; }

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

        public:
                // ---- Frame-shaped helpers (public so nested pImpls in concrete backends can use them) ----

                /**
                 * @brief Looks up the first @ref CompressedAudioPayload
                 *        on @p frame matching @p streamIndex.
                 *
                 * Walks @c frame.audioPayloads() once.  When
                 * @p streamIndex is @c -1 (the default), returns the
                 * first compressed audio payload found regardless of
                 * its @ref MediaPayload::streamIndex.  When
                 * @p streamIndex is non-negative, returns the first
                 * compressed audio payload whose @c streamIndex()
                 * matches.
                 *
                 * Returns a null @ref CompressedAudioPayload::Ptr when
                 * no matching payload exists or when the candidate
                 * payload is uncompressed.
                 */
                static CompressedAudioPayload::Ptr selectInputPayload(const Frame &frame, int streamIndex = -1);

                /**
                 * @brief Constructs an output @ref Frame paired with an
                 *        emitted PCM audio payload.
                 *
                 * Copies metadata from @p source, adds @p emitted to
                 * the output's payload list, and echoes every video
                 * and ANC payload from @p source onto the output so
                 * downstream pipeline stages still see them.  Audio
                 * payloads from @p source are intentionally not
                 * forwarded — the PCM @p emitted replaces them.
                 *
                 * Returns an empty (but valid) @ref Frame when
                 * @p emitted is null.
                 */
                static Frame buildOutputFrame(const Frame &source, PcmAudioPayload::Ptr emitted);

        private:
                UniquePtr<MediaConfig> _stashedConfig;
                AudioCodec             _codec;
};

PROMEKI_NAMESPACE_END
