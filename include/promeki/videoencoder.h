/**
 * @file      videoencoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/function.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/videocodec.h>
#include <promeki/backendweight.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class MediaConfig;
class Frame;

/**
 * @brief Abstract base class for stateful video encoders.
 * @ingroup proav
 *
 * A VideoEncoder is a single push-Frame / pull-Frame codec session.
 * Concrete session classes hold codec state (reference frames,
 * rate-control context, internal allocators, …) across successive
 * @ref submitFrame calls and emit fully-assembled output @ref Frame
 * objects (carrying a @ref CompressedVideoPayload plus echoed-through
 * audio / ANC / metadata) via @ref receiveFrame as the codec decides
 * they're ready.
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
 *   3. For each source Frame, call @ref submitFrame.  The encoder
 *      extracts the uncompressed video payload it wants via
 *      @ref selectInputPayload, reads any per-frame ANC the encoder
 *      cares about (e.g. CEA-708 packets earmarked for SEI injection)
 *      via @ref selectAncForSei, and holds the source Frame's audio /
 *      metadata so the matching output Frame can echo them through.
 *   4. After each submit (and any time after), drain with
 *      @ref receiveFrame until it returns an invalid Frame.  Output
 *      Frames may arrive in PTS-out-of-order (B-frames); the DTS on
 *      the contained payload reflects decode order.
 *   5. When the input stream is exhausted, call @ref flush and keep
 *      draining until @ref receiveFrame returns a Frame whose
 *      compressed payload carries the @c MediaPayload::Flags::EndOfStream
 *      flag.
 *   6. Destroy the encoder.
 *
 * @par Configure stash
 *
 * @ref configure is non-virtual.  Its body stores the most-recently-
 * passed @ref MediaConfig on the base (reachable via @ref config())
 * and then dispatches to the virtual @ref onConfigure hook that
 * concrete backends override.  This guarantees the stashed config
 * is up to date for the base-class helpers (@ref selectInputPayload,
 * @ref selectAncForSei) regardless of what a backend's
 * @ref onConfigure body does.
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref VideoEncoder.
 */
class VideoEncoder {
        public:
                /** @brief Unique-ownership pointer to a VideoEncoder. */
                using UPtr = UniquePtr<VideoEncoder>;

                /** @brief Factory signature used by the encoder registry. */
                using Factory = Function<VideoEncoder *()>;

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
                 * Non-virtual.  Stores @p config on the base (reachable
                 * via @ref config()) and then dispatches to the virtual
                 * @ref onConfigure hook.  Backends override
                 * @ref onConfigure rather than this method.
                 *
                 * @param config Caller-supplied configuration database.
                 */
                void configure(const MediaConfig &config);

                /**
                 * @brief Submits one source @ref Frame for encoding.
                 *
                 * The encoder extracts the uncompressed video payload it
                 * wants from @p frame (typically via the
                 * @ref selectInputPayload helper), reads any ANC the
                 * encoder is configured to inject (typically via
                 * @ref selectAncForSei), and holds the source Frame's
                 * audio / metadata so the matching emitted output
                 * @ref Frame can echo them through.  The submitted
                 * payload's @c pts threads from the input through to
                 * the emitted compressed payload — callers stamp it on
                 * the payload before adding it to the Frame.
                 *
                 * @param frame The source Frame containing the
                 *              uncompressed video payload (and
                 *              optionally audio, ANC, metadata) to
                 *              encode.  An invalid (default-
                 *              constructed) Frame is treated as
                 *              @ref Error::Invalid.
                 * @return @c Error::Ok on success, or an error code
                 *         describing why the frame was rejected.
                 */
                virtual Error submitFrame(const Frame &frame) = 0;

                /**
                 * @brief Dequeues one encoded output @ref Frame, or an
                 *        invalid Frame when none is ready.
                 *
                 * The returned Frame carries the emitted
                 * @ref CompressedVideoPayload plus whatever audio / ANC
                 * / metadata the corresponding submitted source Frame
                 * carried.  Frames may arrive with their compressed
                 * payload's PTS out of order (B-frames); the DTS
                 * reflects decode order.
                 */
                virtual Frame receiveFrame() = 0;

                /** @brief Signals end-of-stream and asks the encoder to drain remaining frames. */
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

                // ---- Frame-shaped helpers (public so nested pImpls in concrete backends can use them) ----

                /**
                 * @brief Looks up the first @ref UncompressedVideoPayload
                 *        on @p frame matching @p streamIndex.
                 *
                 * Walks @c frame.videoPayloads() once.  When
                 * @p streamIndex is @c -1 (the default), returns the
                 * first uncompressed video payload found regardless of
                 * its @ref MediaPayload::streamIndex.  When
                 * @p streamIndex is non-negative, returns the first
                 * uncompressed video payload whose @c streamIndex()
                 * matches.
                 *
                 * Returns a null @ref UncompressedVideoPayload::Ptr
                 * when no matching payload exists, when the candidate
                 * payload is compressed, or when @p frame is invalid.
                 */
                static UncompressedVideoPayload::Ptr selectInputPayload(const Frame &frame, int streamIndex = -1);

                /**
                 * @brief Collects @ref AncPacket entries from @p frame
                 *        that this encoder should translate to SEI and
                 *        inject on the emitted compressed payload.
                 *
                 * Walks @c frame.ancPayloads().  For each payload, the
                 * payload's @ref AncDesc::pairedVideoStreamIndex must
                 * either match @p pairedVideoStreamIndex or be @c -1
                 * (unbound — accepted as a global stream paired with
                 * any encoder).  For each packet in an admitted
                 * payload, the packet's @c format().id() must appear
                 * in @p allowedFormats.
                 *
                 * Returns an empty list when @p allowedFormats is
                 * empty (disabled — the common default).
                 */
                static AncPacket::List selectAncForSei(const Frame &frame, int pairedVideoStreamIndex,
                                                      const AncFormat::IDList &allowedFormats);

                /**
                 * @brief Constructs an output @ref Frame paired with an
                 *        emitted compressed payload.
                 *
                 * Copies metadata from @p source, adds @p emitted to
                 * the output's payload list, and echoes every audio
                 * and ANC payload from @p source onto the output so
                 * downstream pipeline stages still see them.  Video
                 * payloads from @p source are intentionally not
                 * forwarded — the compressed @p emitted is the
                 * encoder's replacement for the source's uncompressed
                 * video.
                 *
                 * Returns an empty (but valid) @ref Frame when
                 * @p emitted is null.
                 */
                static Frame buildOutputFrame(const Frame &source, CompressedVideoPayload::Ptr emitted);

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
                 *               must resolve via @c VideoCodec::lookupData
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
                 * @ref configure invoked when @p config is non-null.
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

                /**
                 * @brief Concrete-backend hook invoked from
                 *        @ref configure after the base stashes @p config.
                 *
                 * Default implementation is a no-op.  Backends override
                 * this (not @ref configure) to read their own
                 * @ref MediaConfig keys.
                 */
                virtual void onConfigure(const MediaConfig &config);

                /**
                 * @brief Returns the most-recent @ref MediaConfig passed
                 *        to @ref configure.
                 *
                 * Returns a reference to a stable, library-owned config
                 * instance — the reference remains valid for the
                 * lifetime of the encoder.  Returns a default-constructed
                 * @ref MediaConfig when @ref configure has never been
                 * called.
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
                VideoCodec             _codec;
};

PROMEKI_NAMESPACE_END
