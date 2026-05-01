/**
 * @file      audioencoder.h
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
#include <promeki/pcmaudiopayload.h>
#include <promeki/compressedaudiopayload.h>

PROMEKI_NAMESPACE_BEGIN

class MediaConfig;

/**
 * @brief Abstract base class for stateful audio encoders.
 * @ingroup proav
 *
 * AudioEncoder is a single push-frame / pull-packet codec session:
 * @ref PcmAudioPayload frames pushed via @ref submitFrame
 * feed an internal pipeline, and encoded
 * @ref CompressedAudioPayload access units come back out of
 * @ref receivePacket zero, one, or many submits later depending on the
 * codec's frame size and look-ahead.
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
 *   3. For each @ref PcmAudioPayload frame, call @ref submitFrame.
 *   4. After each submit, drain with @ref receivePacket until it
 *      returns a null Ptr.  Each emitted packet carries this codec's
 *      @ref codec() value so downstream code can route it without
 *      consulting the encoder again.
 *   5. When the input stream is exhausted, call @ref flush and keep
 *      draining until @ref receivePacket returns a packet whose
 *      @c MediaPayload::Flags::EndOfStream flag is set.
 *   6. Destroy the encoder.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Each pipeline thread should own its own
 * encoder instance; concurrent access to a single instance is
 * unsupported.
 */
class AudioEncoder {
        public:
                /** @brief Factory signature used by the encoder registry. */
                using Factory = std::function<AudioEncoder *()>;

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
                 * Same semantics as @ref VideoEncoder::configure: reads
                 * known keys, ignores the rest.  Default is a no-op.
                 */
                virtual void configure(const MediaConfig &config);

                /**
                 * @brief Submits one uncompressed audio payload for encoding.
                 *
                 * The payload carries its own PTS — callers stamp it
                 * before submitting.  A null Ptr is treated as
                 * @ref Error::Invalid.
                 */
                virtual Error submitPayload(const PcmAudioPayload::Ptr &payload) = 0;

                /** @brief Dequeues one encoded payload, or a null Ptr when none is ready. */
                virtual CompressedAudioPayload::Ptr receiveCompressedPayload() = 0;

                /** @brief Signals end-of-stream and asks the encoder to drain remaining packets. */
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

                Error  _lastError;
                String _lastErrorMessage;

                /** @brief Records a new error state. */
                void setError(Error err, const String &msg = String());

                /** @brief Clears the error state. */
                void clearError();

        private:
                AudioCodec _codec;
};

PROMEKI_NAMESPACE_END
