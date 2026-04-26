/**
 * @file      videodecoder.h
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
#include <promeki/videocodec.h>
#include <promeki/backendweight.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class MediaConfig;

/**
 * @brief Abstract base class for stateful video decoders.
 * @ingroup proav
 *
 * A VideoDecoder is the inverse of @ref VideoEncoder: packets submitted
 * via @ref submitPacket feed an internal decode pipeline and
 * uncompressed frames come back out of @ref receiveFrame.  The decoder
 * may buffer several packets before producing its first frame (B-frame
 * reordering, reference-frame dependencies).
 *
 * Codec-family metadata lives on the @ref VideoCodec wrapper returned
 * by @ref codec(); per-backend facts (supported output PixelFormats,
 * bitrate limits, …) live on the registered @ref BackendRecord and
 * are reachable via @ref VideoCodec::decoderSupportedOutputs.
 *
 * @par Backend registration
 *
 * Same idiom as @ref VideoEncoder::registerBackend, but using this
 * class's @ref BackendRecord — see that method's documentation for the
 * full registration example.
 *
 * @par Session lifecycle
 *
 *   1. Resolve a session via @ref VideoCodec::createDecoder.
 *   2. Optionally call @ref configure (most decoders infer parameters
 *      from the bitstream and need no explicit configuration).
 *   3. For each encoded packet, call @ref submitPacket.
 *   4. After each submit, drain with @ref receiveFrame until it
 *      returns an invalid @ref Image.
 *   5. Call @ref flush when the input stream ends, then drain again.
 *   6. Destroy the decoder.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Each pipeline thread should own its own
 * decoder instance; concurrent access to a single instance is
 * unsupported.
 */
class VideoDecoder {
        public:
                /** @brief Unique-ownership pointer to a VideoDecoder. */
                using UPtr = UniquePtr<VideoDecoder>;

                /** @brief Factory signature used by the decoder registry. */
                using Factory = std::function<VideoDecoder *()>;

                /**
                 * @brief Registration record attaching a concrete decoder to a (codec, backend) pair.
                 *
                 * Mirrors @ref VideoEncoder::BackendRecord.
                 */
                struct BackendRecord {
                        VideoCodec::ID       codecId;                         ///< Codec family this record implements.
                        VideoCodec::Backend  backend;                         ///< Typed backend handle.
                        int                  weight = BackendWeight::Vendored; ///< Selection weight.
                        /**
                         * @brief Uncompressed @ref PixelFormat IDs this backend can emit.
                         *
                         * Stored as @c int.  Empty means "any format that
                         * can be converted from the codec's native output."
                         */
                        List<int>            supportedOutputs;
                        Factory              factory;                        ///< Creates a fresh session.
                };

                /** @brief Virtual destructor. */
                virtual ~VideoDecoder();

                /** @brief Returns the codec this session decodes, with backend pinned. */
                VideoCodec codec() const { return _codec; }

                /**
                 * @brief Applies decoder parameters from a @ref MediaConfig.
                 *
                 * Same semantics as @ref VideoEncoder::configure — reads
                 * known keys, ignores the rest.  The default implementation
                 * is a no-op.
                 */
                virtual void configure(const MediaConfig &config);

                /**
                 * @brief Submits one encoded payload for decoding.
                 *
                 * @param payload Compressed access unit to decode.  Its
                 *                @ref ImageDesc::pixelFormat should fall
                 *                within this codec's compressed
                 *                PixelFormat set (see
                 *                @ref VideoCodec::compressedPixelFormats).
                 *                A null Ptr is treated as @ref Error::Invalid.
                 * @return @c Error::Ok on success.
                 */
                virtual Error submitPayload(const CompressedVideoPayload::Ptr &payload) = 0;

                /**
                 * @brief Dequeues one decoded payload.
                 * @return A valid @ref UncompressedVideoPayload::Ptr, or
                 *         a null Ptr when no frame is ready.
                 */
                virtual UncompressedVideoPayload::Ptr receiveVideoPayload() = 0;

                /** @brief Signals end-of-stream; remaining frames can be drained with @ref receiveVideoPayload. */
                virtual Error flush() = 0;

                /** @brief Discards any pending packets / frames. */
                virtual Error reset() = 0;

                /** @brief Returns the last error produced by the decoder. */
                Error lastError() const { return _lastError; }

                /** @brief Returns a human-readable message for the last error. */
                const String &lastErrorMessage() const { return _lastErrorMessage; }

                // ---- Backend registry ----

                /**
                 * @brief Registers a concrete decoder backend for a (codec, backend) pair.
                 * See @ref VideoEncoder::registerBackend for the symmetric contract.
                 */
                static Error registerBackend(BackendRecord record);

                // ---- Internal bridge from VideoCodec (see VideoEncoder's equivalents) ----

                static VideoCodec::BackendList availableBackends(VideoCodec::ID codecId);
                static List<int> supportedOutputsFor(VideoCodec::ID codecId,
                                                     VideoCodec::Backend backend);
                static Result<VideoDecoder *> create(VideoCodec::ID codecId,
                                                    VideoCodec::Backend pinned,
                                                    const MediaConfig *config);

        protected:
                VideoDecoder() = default;

                /** @brief Records the codec + backend this session implements. */
                void setCodec(VideoCodec codec) { _codec = codec; }

                Error   _lastError;
                String  _lastErrorMessage;

                /** @brief Records a new error state. */
                void setError(Error err, const String &msg = String());

                /** @brief Clears the error state. */
                void clearError();

        private:
                VideoCodec _codec;
};

PROMEKI_NAMESPACE_END
