/**
 * @file      videoencoder.h
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
 * @brief Abstract base class for stateful video encoders.
 * @ingroup proav
 *
 * A VideoEncoder is a single push-frame / pull-packet codec session.
 * Concrete session classes hold codec state (reference frames,
 * rate-control context, internal allocators, …) across successive
 * @ref submitFrame calls and emit @ref CompressedVideoPayload access
 * units via @ref receivePacket as the codec decides they're ready.
 *
 * Encoders expose no codec-family metadata directly — the caller asks
 * the @ref codec wrapper returned by @ref codec() instead.  The codec
 * wrapper knows its own name, description, FourCC list, compressed
 * PixelFormat range, inter-/intra-frame coding, bit-depth coverage,
 * and all other spec-level facts; per-backend facts (the specific
 * uncompressed PixelFormats this particular backend accepts, a
 * bitrate ceiling, …) live on the registered @ref BackendRecord and
 * are reachable via @ref VideoCodec::encoderSupportedInputs.
 *
 * @par Backend registration
 *
 * Concrete backend source files register themselves against the
 * process-wide encoder registry at static-init time:
 *
 * @code
 * // 1. Name the backend once — returns a typed handle.
 * auto bk = VideoCodec::registerBackend("Nvidia");
 * if(error(bk).isError()) return;
 * auto backend = value(bk);
 *
 * // 2. Attach a concrete implementation to each (codec, backend) pair.
 * VideoEncoder::registerBackend({
 *         .codecId         = VideoCodec::H264,
 *         .backend         = backend,
 *         .weight          = BackendWeight::Vendored,
 *         .supportedInputs = NvencVideoEncoder::supportedInputList(),
 *         .factory         = []() -> VideoEncoder * {
 *                 return new NvencVideoEncoder(NvencVideoEncoder::Codec_H264);
 *         },
 * });
 * @endcode
 *
 * @ref VideoCodec::createEncoder resolves a backend (caller's pin →
 * @c MediaConfig::CodecBackend → highest-weight), invokes the factory,
 * stamps the codec wrapper onto the returned instance (so
 * @ref codec() reports the right backend) and — when a @ref MediaConfig
 * was supplied — calls @ref configure on the new session before
 * returning.
 *
 * @par Session lifecycle
 *
 *   1. Resolve a session via @ref VideoCodec::createEncoder.
 *   2. Call @ref configure with a @ref MediaConfig holding bitrate,
 *      GOP length, preset, and any other well-known knobs.  (Skip
 *      when a config was already supplied to @c createEncoder.)
 *   3. For each source frame, call @ref submitFrame.
 *   4. After each submit (and any time after), drain with
 *      @ref receivePacket until it returns a null Ptr.  Packets may
 *      arrive with PTS out of order (B-frames); the DTS on each
 *      packet reflects decode order.
 *   5. When the input stream is exhausted, call @ref flush and keep
 *      draining until @ref receivePacket returns a packet whose
 *      @c MediaPayload::Flags::EndOfStream flag is set.
 *   6. Destroy the encoder.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Each pipeline thread should own its own
 * encoder instance; concurrent access to a single instance is
 * unsupported.  Typical use is one encoder per stream.
 */
class VideoEncoder {
        public:
                /** @brief Unique-ownership pointer to a VideoEncoder. */
                using UPtr = UniquePtr<VideoEncoder>;

                /** @brief Factory signature used by the encoder registry. */
                using Factory = std::function<VideoEncoder *()>;

                /**
                 * @brief Registration record attaching a concrete encoder to a (codec, backend) pair.
                 *
                 * Populated by backend source files at static-init time
                 * and handed to @ref registerBackend.  The registry owns
                 * a copy of the record for the process lifetime.
                 */
                struct BackendRecord {
                                VideoCodec::ID codecId; ///< Codec family this record implements.
                                VideoCodec::Backend
                                        backend; ///< Typed backend handle (see @ref VideoCodec::registerBackend).
                                int     weight = BackendWeight::
                                        Vendored; ///< Selection weight when multiple backends share a codec.
                                /**
                         * @brief Uncompressed @ref PixelFormat IDs this
                         *        backend ingests without internal conversion.
                         *
                         * Stored as @c int for header-layering reasons.
                         * Empty means "this backend accepts any uncompressed
                         * input it can convert internally."
                         */
                                List<int> supportedInputs;
                                Factory   factory; ///< Creates a fresh session; never null.
                };

                /** @brief Virtual destructor. */
                virtual ~VideoEncoder();

                /**
                 * @brief Returns the codec this session encodes.
                 *
                 * The returned wrapper is pinned with the backend handle
                 * the factory was registered under, so downstream code
                 * can round-trip through @c codec().toString() to obtain
                 * the canonical @c "Name:Backend" form.  Stamped onto
                 * every outgoing @ref CompressedVideoPayload by the common session
                 * plumbing.
                 */
                VideoCodec codec() const { return _codec; }

                /**
                 * @brief Applies encoder parameters from a @ref MediaConfig.
                 *
                 * Each backend reads the well-known keys it understands
                 * (@c BitrateKbps, @c VideoRcMode, @c GopLength,
                 * @c VideoPreset, …) and silently ignores the rest.
                 * Calling @c configure() while a session is active is
                 * allowed; the backend applies what it can at runtime
                 * and defers the rest to the next IDR.  The default
                 * implementation is a no-op.
                 *
                 * @param config Caller-supplied configuration database.
                 */
                virtual void configure(const MediaConfig &config);

                /**
                 * @brief Submits one uncompressed payload for encoding.
                 *
                 * The payload already carries its own PTS as first-class
                 * state — callers stamp the payload's PTS before
                 * submitting.  A null Ptr is treated as @ref Error::Invalid.
                 *
                 * @param payload The source payload in one of the
                 *                backend's supported uncompressed pixel
                 *                formats (see
                 *                @ref VideoCodec::encoderSupportedInputs).
                 * @return @c Error::Ok on success, or an error code
                 *         describing why the payload was rejected.
                 */
                virtual Error submitPayload(const UncompressedVideoPayload::Ptr &payload) = 0;

                /**
                 * @brief Dequeues one encoded payload, or a null Ptr when none is ready.
                 *
                 * The returned @ref CompressedVideoPayload carries the
                 * access-unit bytes, PTS / DTS / duration, and keyframe /
                 * parameter-set / end-of-stream flags.  Packets may
                 * arrive with PTS out of order (B-frames); the DTS
                 * reflects decode order.
                 */
                virtual CompressedVideoPayload::Ptr receiveCompressedPayload() = 0;

                /** @brief Signals end-of-stream and asks the encoder to drain remaining packets. */
                virtual Error flush() = 0;

                /** @brief Discards any pending frames / packets and returns to a fresh state. */
                virtual Error reset() = 0;

                /**
                 * @brief Asks the encoder to mark the next packet as an
                 *        independently decodable random-access point.
                 *
                 * For temporal codecs this maps to the codec's IDR /
                 * keyframe mechanism.  For intra-only codecs (every
                 * packet is already a random-access point) the default
                 * implementation is a no-op.  Pipeline glue can call
                 * this unconditionally without checking the codec type.
                 */
                virtual void requestKeyframe();

                /** @brief Returns the last error produced by the encoder. */
                Error lastError() const { return _lastError; }

                /** @brief Returns a human-readable message for the last error. */
                const String &lastErrorMessage() const { return _lastErrorMessage; }

                // ---- Backend registry ----

                /**
                 * @brief Registers a concrete encoder backend for a (codec, backend) pair.
                 *
                 * Called at static-init time by concrete backend source
                 * files.  Re-registering the same (codec, backend) pair
                 * replaces the prior record (useful for app-level
                 * overrides).
                 *
                 * @param record The populated BackendRecord.  The @c codecId
                 *               must resolve via @ref VideoCodec::lookupData
                 *               and the @c backend handle must come from
                 *               @ref VideoCodec::registerBackend.
                 * @return @c Error::Ok on success; @c Error::Invalid when
                 *         the record is malformed (no factory, invalid
                 *         codec, invalid backend).
                 */
                static Error registerBackend(BackendRecord record);

                // ---- Internal bridge from VideoCodec ----
                //
                // These are the lookup surfaces VideoCodec::canEncode /
                // createEncoder / encoderSupportedInputs / availableEncoderBackends
                // call into.  Public only so the VideoCodec wrapper (which
                // lives in a different TU) can reach them without a friend
                // declaration; application code should not use them directly.

                /** @brief Returns every backend that has registered an encoder for @p codecId. */
                static VideoCodec::BackendList availableBackends(VideoCodec::ID codecId);

                /**
                 * @brief Returns the supportedInputs list of a specific (codec, backend) pair.
                 *
                 * When @p backend is an invalid handle, returns the union
                 * across every registered backend for @p codecId (the
                 * planner's "could any backend do it?" query).
                 */
                static List<int> supportedInputsFor(VideoCodec::ID codecId, VideoCodec::Backend backend);

                /**
                 * @brief Resolves a backend and invokes its factory.
                 *
                 * Backend resolution honors @p pinned first; when it is
                 * invalid, @p config->CodecBackend (as a backend name)
                 * is consulted; when that is empty or not found, the
                 * highest-weight registered backend is chosen.  The
                 * returned encoder has @ref codec() pre-populated and
                 * @ref configure() invoked when @p config is non-null.
                 */
                static Result<VideoEncoder *> create(VideoCodec::ID codecId, VideoCodec::Backend pinned,
                                                     const MediaConfig *config);

        protected:
                VideoEncoder() = default;

                /**
                 * @brief Records the codec + backend this session implements.
                 *
                 * Called by the registration machinery after a factory
                 * produces a new instance, so concrete session classes
                 * don't have to track their own identity.  Exposed as
                 * protected for backends that want to set it eagerly in
                 * their constructor (so @ref codec() works before the
                 * registration pipeline has a chance to stamp it).
                 */
                void setCodec(VideoCodec codec) { _codec = codec; }

                Error  _lastError;
                String _lastErrorMessage;

                /** @brief Records a new error state. */
                void setError(Error err, const String &msg = String());

                /** @brief Clears the error state. */
                void clearError();

        private:
                VideoCodec _codec;
};

PROMEKI_NAMESPACE_END
