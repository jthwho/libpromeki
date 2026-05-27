/**
 * @file      audioencoder.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <functional>
#include <promeki/function.h>
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
 * @brief Abstract base class for stateful audio encoders.
 * @ingroup proav
 *
 * AudioEncoder is a single push-Frame / pull-Frame codec session:
 * source @ref Frame objects (carrying a @ref PcmAudioPayload, plus
 * optional video / metadata) are pushed via @ref submitFrame and
 * fully-assembled output @ref Frame objects (carrying a
 * @ref CompressedAudioPayload and the echoed-through video /
 * metadata of the matching submitted source) come back out of
 * @ref receiveFrame zero, one, or many submits later depending on
 * the codec's frame size and look-ahead.
 *
 * Encoders expose no codec-family metadata directly — the caller asks
 * the @ref codec wrapper returned by @ref codec() instead.  The codec
 * wrapper knows its own name, description, FourCC list, compression
 * type, bitrate mode, and all other spec-level facts; per-backend
 * facts (specific uncompressed input formats, bitrate ceilings, …)
 * live on the registered @ref BackendRecord and are reachable via
 * @ref AudioCodec::encoderSupportedInputs.
 *
 * The session contract is intentionally identical to @ref VideoEncoder
 * (configure, submit/receive, flush, reset) so pipeline glue
 * (MediaIO, QuickTime muxer, RTP packetiser, …) can treat audio
 * and video encoders symmetrically.
 *
 * @par Backend registration
 *
 * @code
 * auto bk = AudioCodec::registerBackend("Native");
 * if(error(bk).isError()) return;
 * AudioEncoder::registerBackend({
 *         .codecId         = AudioCodec::Opus,
 *         .backend         = value(bk),
 *         .weight          = BackendWeight::Vendored,
 *         .supportedInputs = { AudioFormat::PCMI_S16LE, AudioFormat::PCMI_Float32LE },
 *         .factory         = []() -> AudioEncoder * { return new OpusAudioEncoder; },
 * });
 * @endcode
 *
 * @par Session lifecycle
 *
 *   1. Resolve a session via @ref AudioCodec::createEncoder.
 *   2. Call @ref configure with a @ref MediaConfig.  (Skip when a
 *      config was already supplied to @c createEncoder.)
 *   3. For each source Frame containing a @ref PcmAudioPayload, call
 *      @ref submitFrame.
 *   4. After each submit, drain with @ref receiveFrame until it
 *      returns an invalid Frame.  Each emitted output Frame carries
 *      this codec's @ref codec() value on the compressed payload so
 *      downstream code can route it without consulting the encoder
 *      again.
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
 * concrete backends override.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Each pipeline thread should own its own
 * encoder instance; concurrent access to a single instance is
 * unsupported.
 */
class AudioEncoder {
        public:
                /** @brief Unique-ownership pointer to an AudioEncoder. */
                using UPtr = UniquePtr<AudioEncoder>;

                /** @brief Factory signature used by the encoder registry. */
                using Factory = Function<AudioEncoder *()>;

                /**
                 * @brief Registration record attaching a concrete encoder to a (codec, backend) pair.
                 */
                struct BackendRecord {
                                AudioCodec::ID      codecId;
                                AudioCodec::Backend backend;
                                int                 weight = BackendWeight::Vendored;
                                /**
                         * @brief Uncompressed @ref AudioFormat IDs this backend ingests.
                         *
                         * Stored as @c int.  Empty means "backend accepts
                         * any sample format it can convert internally."
                         */
                                List<int> supportedInputs;
                                Factory   factory;
                };

                /** @brief Virtual destructor. */
                virtual ~AudioEncoder();

                /**
                 * @brief Returns the codec this session encodes, with backend pinned.
                 *
                 * Stamped onto every outgoing @ref CompressedAudioPayload so
                 * downstream code can dispatch on codec identity
                 * without consulting the encoder.
                 */
                AudioCodec codec() const { return _codec; }

                /**
                 * @brief Applies encoder parameters from a @ref MediaConfig.
                 *
                 * Non-virtual.  Stores @p config on the base (reachable
                 * via @ref config()) and then dispatches to the virtual
                 * @ref onConfigure hook.  Backends override
                 * @ref onConfigure rather than this method.
                 */
                void configure(const MediaConfig &config);

                /**
                 * @brief Submits one source @ref Frame for encoding.
                 *
                 * The encoder extracts the @ref PcmAudioPayload it
                 * wants from @p frame (typically via the
                 * @ref selectInputPayload helper).  A submitted Frame
                 * with no matching audio payload is treated as
                 * @ref Error::Invalid.
                 */
                virtual Error submitFrame(const Frame &frame) = 0;

                /**
                 * @brief Dequeues one encoded output @ref Frame, or an
                 *        invalid Frame when none is ready.
                 */
                virtual Frame receiveFrame() = 0;

                /** @brief Signals end-of-stream and asks the encoder to drain remaining frames. */
                virtual Error flush() = 0;

                /** @brief Discards pending samples / packets and returns the encoder to a fresh state. */
                virtual Error reset() = 0;

                /**
                 * @brief Asks the encoder to start a fresh independently
                 *        decodable run at the next packet.
                 *
                 * Mirrors @ref VideoEncoder::requestKeyframe.  Most audio
                 * codecs are packet-independent and don't need anything
                 * here; codecs with internal state (FLAC sync points,
                 * Opus DTX boundaries, MP3 bit reservoir) override to
                 * force the relevant reset on the next submit.  Default
                 * implementation is a no-op so pipeline glue can call
                 * this unconditionally.
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
                 * @return @c Error::Ok on success; @c Error::Invalid when
                 *         the record is malformed.
                 */
                static Error registerBackend(BackendRecord record);

                // ---- Internal bridge from AudioCodec (see VideoEncoder's equivalents) ----

                static AudioCodec::BackendList availableBackends(AudioCodec::ID codecId);
                static List<int>               supportedInputsFor(AudioCodec::ID codecId, AudioCodec::Backend backend);
                static Result<AudioEncoder *>  create(AudioCodec::ID codecId, AudioCodec::Backend pinned,
                                                      const MediaConfig *config);

        protected:
                AudioEncoder() = default;

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
                 * @brief Looks up the first @ref PcmAudioPayload on
                 *        @p frame matching @p streamIndex.
                 *
                 * Walks @c frame.audioPayloads() once.  When
                 * @p streamIndex is @c -1 (the default), returns the
                 * first PCM audio payload found regardless of its
                 * @ref MediaPayload::streamIndex.  When
                 * @p streamIndex is non-negative, returns the first
                 * PCM audio payload whose @c streamIndex() matches.
                 *
                 * Returns a null @ref PcmAudioPayload::Ptr when no
                 * matching payload exists or when the candidate
                 * payload is compressed.
                 */
                static PcmAudioPayload::Ptr selectInputPayload(const Frame &frame, int streamIndex = -1);

                /**
                 * @brief Constructs an output @ref Frame paired with an
                 *        emitted compressed audio payload.
                 *
                 * Copies metadata from @p source, adds @p emitted to
                 * the output's payload list, and echoes every video
                 * and ANC payload from @p source onto the output so
                 * downstream pipeline stages still see them.  Audio
                 * payloads from @p source are intentionally not
                 * forwarded — the compressed @p emitted replaces
                 * them.
                 *
                 * Returns an empty (but valid) @ref Frame when
                 * @p emitted is null.
                 */
                static Frame buildOutputFrame(const Frame &source, CompressedAudioPayload::Ptr emitted);

        private:
                UniquePtr<MediaConfig> _stashedConfig;
                AudioCodec             _codec;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
