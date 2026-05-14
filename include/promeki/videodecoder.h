/**
 * @file      videodecoder.h
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
#include <promeki/ancpacket.h>
#include <promeki/mediaioallocator.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

class MediaConfig;
class Frame;

/**
 * @brief Abstract base class for stateful video decoders.
 * @ingroup proav
 *
 * A VideoDecoder is the inverse of @ref VideoEncoder.  Frames submitted
 * via @ref submitFrame feed an internal decode pipeline and emitted
 * Frames (carrying an @ref UncompressedVideoPayload, plus any ANC the
 * decoder extracted from the bitstream and the echoed-through audio /
 * metadata of the submitted Frame) come back out of @ref receiveFrame.
 * The decoder may buffer several frames before producing its first
 * output (B-frame reordering, reference-frame dependencies).
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
 *   3. For each source Frame carrying a compressed payload, call
 *      @ref submitFrame.
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
class VideoDecoder {
        public:
                /** @brief Unique-ownership pointer to a VideoDecoder. */
                using UPtr = UniquePtr<VideoDecoder>;

                /** @brief Factory signature used by the decoder registry. */
                using Factory = Function<VideoDecoder *()>;

                /**
                 * @brief Registration record attaching a concrete decoder to a (codec, backend) pair.
                 *
                 * Mirrors @ref VideoEncoder::BackendRecord.
                 */
                struct BackendRecord {
                                VideoCodec::ID      codecId; ///< Codec family this record implements.
                                VideoCodec::Backend backend; ///< Typed backend handle.
                                int                 weight = BackendWeight::Vendored; ///< Selection weight.
                                /**
                         * @brief Uncompressed @ref PixelFormat IDs this backend can emit.
                         *
                         * Stored as @c int.  Empty means "any format that
                         * can be converted from the codec's native output."
                         */
                                List<int> supportedOutputs;
                                Factory   factory; ///< Creates a fresh session.
                };

                /** @brief Virtual destructor. */
                virtual ~VideoDecoder();

                /** @brief Returns the codec this session decodes, with backend pinned. */
                VideoCodec codec() const { return _codec; }

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
                 * The decoder extracts the compressed video payload it
                 * wants from @p frame (typically via the
                 * @ref selectInputPayload helper), and holds the source
                 * Frame's audio / metadata so the matching emitted
                 * output @ref Frame can echo them through.  Decoders
                 * may additionally extract ANC riding inside the
                 * compressed bitstream (e.g. CEA-708 SEI captions) and
                 * attach the recovered @ref AncPacket entries to the
                 * output Frame via @ref attachExtractedAnc.
                 *
                 * @param frame Compressed access unit to decode.  An
                 *              invalid Frame is treated as
                 *              @ref Error::Invalid.
                 * @return @c Error::Ok on success.
                 */
                virtual Error submitFrame(const Frame &frame) = 0;

                /**
                 * @brief Dequeues one decoded output @ref Frame, or an
                 *        invalid Frame when none is ready.
                 *
                 * The returned Frame carries the decoded
                 * @ref UncompressedVideoPayload plus whatever audio /
                 * metadata the corresponding submitted source Frame
                 * carried, plus any ANC the decoder extracted from the
                 * compressed bitstream.
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

                /**
                 * @brief Returns the buffer allocator this decoder uses
                 *        for its emitted uncompressed payloads.
                 *
                 * Backends that support placement control route every
                 * payload allocation through this accessor.  When no
                 * override has been installed via @ref setAllocator,
                 * returns @ref MediaIOAllocator::defaultAllocator (a
                 * stateless singleton that defers to
                 * @c BufferAllocator::defaultAllocator's heap-backed
                 * @ref MemSpace::Default allocator) — existing call
                 * sites that don't know about the allocator framework
                 * see no behaviour change.
                 *
                 * Never returns null.
                 *
                 * @par Thread safety
                 * Safe to call from any thread once the decoder has
                 * been constructed.  The pointer is updated only by
                 * @ref setAllocator on the user thread before the
                 * first @ref submitFrame — readers on the decode
                 * strand observe the most-recently-installed value.
                 */
                MediaIOAllocator::Ptr allocator() const;

                /**
                 * @brief Installs a per-decoder allocator override.
                 *
                 * Pass null to clear and revert to
                 * @ref MediaIOAllocator::defaultAllocator.  Typically
                 * called by the pipeline / caller once it knows what
                 * placement policy fits the downstream consumer.  An
                 * NVDEC backend, for example, hands out
                 * @ref MemSpace::CudaDevice planes through its own
                 * allocator subclass so decoded frames stay device-
                 * resident; the install line is the documented
                 * rollback point if that policy ever needs reverting.
                 *
                 * Default no-op implementation on the base stores the
                 * pointer.  Backends that need to thread the allocator
                 * into a private worker / Impl override and forward.
                 */
                virtual void setAllocator(MediaIOAllocator::Ptr a);

                // ---- Backend registry ----

                /**
                 * @brief Registers a concrete decoder backend for a (codec, backend) pair.
                 * See @ref VideoEncoder::registerBackend for the symmetric contract.
                 */
                static Error registerBackend(BackendRecord record);

                // ---- Internal bridge from VideoCodec (see VideoEncoder's equivalents) ----

                static VideoCodec::BackendList availableBackends(VideoCodec::ID codecId);
                static List<int>               supportedOutputsFor(VideoCodec::ID codecId, VideoCodec::Backend backend);
                static Result<VideoDecoder *>  create(VideoCodec::ID codecId, VideoCodec::Backend pinned,
                                                      const MediaConfig *config);

        protected:
                VideoDecoder() = default;

                /** @brief Records the codec + backend this session implements. */
                void setCodec(VideoCodec codec) { _codec = codec; }

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

                /**
                 * @brief Per-decoder allocator override.
                 *
                 * Cleared (invalid) by default; @ref allocator falls
                 * back to @ref MediaIOAllocator::defaultAllocator when
                 * unset.  Subclasses may read this directly when they
                 * dispatch allocation onto a private worker that can't
                 * conveniently call back through the public accessor.
                 */
                MediaIOAllocator::Ptr _allocator;

        public:
                // ---- Frame-shaped helpers (public so nested pImpls in concrete backends can use them) ----

                /**
                 * @brief Looks up the first @ref CompressedVideoPayload
                 *        on @p frame matching @p streamIndex.
                 *
                 * Walks @c frame.videoPayloads() once.  When
                 * @p streamIndex is @c -1 (the default), returns the
                 * first compressed video payload found regardless of
                 * its @ref MediaPayload::streamIndex.  When
                 * @p streamIndex is non-negative, returns the first
                 * compressed video payload whose @c streamIndex()
                 * matches.
                 *
                 * Returns a null @ref CompressedVideoPayload::Ptr when
                 * no matching payload exists or when the candidate
                 * payload is uncompressed.
                 */
                static CompressedVideoPayload::Ptr selectInputPayload(const Frame &frame, int streamIndex = -1);

                /**
                 * @brief Attaches a decoder-extracted @ref AncPacket to
                 *        the output @p frame.
                 *
                 * Finds (or creates) an @ref AncPayload on @p frame
                 * whose @ref AncDesc::pairedVideoStreamIndex matches
                 * @p pairedVideoStreamIndex and appends @p pkt.
                 * Convenience used by decoders that extract caption /
                 * HDR / AFD ANC from the compressed bitstream and want
                 * to surface it on the emitted output Frame for the
                 * rest of the pipeline.
                 */
                static void attachExtractedAnc(Frame &frame, AncPacket pkt, int pairedVideoStreamIndex);

                /**
                 * @brief Constructs an output @ref Frame paired with an
                 *        emitted uncompressed payload.
                 *
                 * Copies metadata from @p source, adds @p emitted to
                 * the output's payload list, and echoes every audio
                 * payload from @p source onto the output.  ANC
                 * payloads from @p source are intentionally not
                 * forwarded — the decoder may have extracted in-band
                 * ANC from the bitstream and should attach that
                 * (typically via @ref attachExtractedAnc) instead.
                 *
                 * Returns an empty (but valid) @ref Frame when
                 * @p emitted is null.
                 */
                static Frame buildOutputFrame(const Frame &source, UncompressedVideoPayload::Ptr emitted);

        private:
                UniquePtr<MediaConfig> _stashedConfig;
                VideoCodec             _codec;
};

PROMEKI_NAMESPACE_END
