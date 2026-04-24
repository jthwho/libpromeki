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
#include <promeki/audiocodec.h>
#include <promeki/backendweight.h>
#include <promeki/uncompressedaudiopayload.h>
#include <promeki/compressedaudiopayload.h>

PROMEKI_NAMESPACE_BEGIN

class MediaConfig;

/**
 * @brief Abstract base class for stateful audio decoders.
 * @ingroup proav
 *
 * Inverse of @ref AudioEncoder: encoded @ref CompressedAudioPayload
 * access units pushed via @ref submitPacket feed an internal pipeline
 * and @ref UncompressedAudioPayload frames come back out of
 * @ref receiveFrame.  The decoder
 * may buffer several packets before producing its first frame (codec
 * start-up state, silence priming, reordering where present).
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
 *   3. For each encoded packet, call @ref submitPacket.
 *   4. After each submit, drain with @ref receiveFrame until it
 *      returns an invalid @ref Audio.
 *   5. Call @ref flush when the input stream ends, then drain again.
 *   6. Destroy the decoder.
 *
 * Implementations are not required to be thread-safe.
 */
class AudioDecoder {
        public:
                /** @brief Factory signature used by the decoder registry. */
                using Factory = std::function<AudioDecoder *()>;

                /**
                 * @brief Registration record attaching a concrete decoder to a (codec, backend) pair.
                 */
                struct BackendRecord {
                        AudioCodec::ID       codecId;
                        AudioCodec::Backend  backend;
                        int                  weight = BackendWeight::Vendored;
                        /**
                         * @brief Uncompressed @ref AudioFormat IDs this backend emits.
                         *
                         * Stored as @c int.  Empty means "any format the
                         * rest of the pipeline can accept."
                         */
                        List<int>            supportedOutputs;
                        Factory              factory;
                };

                /** @brief Virtual destructor. */
                virtual ~AudioDecoder();

                /** @brief Returns the codec this session decodes, with backend pinned. */
                AudioCodec codec() const { return _codec; }

                /**
                 * @brief Applies decoder parameters from a @ref MediaConfig.
                 *
                 * Same semantics as @ref AudioEncoder::configure.
                 * Default is a no-op.
                 */
                virtual void configure(const MediaConfig &config);

                /**
                 * @brief Submits one encoded audio payload for decoding.
                 *
                 * A null Ptr is treated as @ref Error::Invalid.
                 */
                virtual Error submitPayload(const CompressedAudioPayload::Ptr &payload) = 0;

                /**
                 * @brief Dequeues one decoded PCM payload.
                 * @return A valid @ref UncompressedAudioPayload::Ptr, or
                 *         a null Ptr when no frame is ready.
                 */
                virtual UncompressedAudioPayload::Ptr receiveAudioPayload() = 0;

                /** @brief Signals end-of-stream; remaining frames can be drained with @ref receiveAudioPayload. */
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
                static List<int> supportedOutputsFor(AudioCodec::ID codecId,
                                                     AudioCodec::Backend backend);
                static Result<AudioDecoder *> create(AudioCodec::ID codecId,
                                                     AudioCodec::Backend pinned,
                                                     const MediaConfig *config);

        protected:
                AudioDecoder() = default;

                /** @brief Records the codec + backend this session implements. */
                void setCodec(AudioCodec codec) { _codec = codec; }

                Error   _lastError;
                String  _lastErrorMessage;

                /** @brief Records a new error state. */
                void setError(Error err, const String &msg = String());

                /** @brief Clears the error state. */
                void clearError();

        private:
                AudioCodec _codec;
};

PROMEKI_NAMESPACE_END
