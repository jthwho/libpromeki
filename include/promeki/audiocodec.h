/**
 * @file      audiocodec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringregistry.h>
#include <promeki/list.h>
#include <promeki/fourcc.h>
#include <promeki/enums.h>
#include <promeki/result.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class AudioEncoder;
class AudioDecoder;
class AudioFormat;
class MediaConfig;

/**
 * @brief Compile-time tag for the AudioCodec backend StringRegistry.
 * @ingroup proav
 *
 * Chosen separately from the @c VideoCodec backend tag so the two
 * namespaces never alias — an @c "Native" backend on the audio side is
 * a different typed value from a @c "Native" video backend.
 */
using AudioCodecBackendRegistry = StringRegistry<"AudioCodecBackend">;

/**
 * @brief First-class identifier for an audio codec family.
 * @ingroup proav
 *
 * Symmetric counterpart to @ref VideoCodec — same TypeRegistry
 * wrapper-around-Data pattern, same typed multi-backend support, same
 * colon-separated string form (@c "Opus" or @c "Opus:Native").  The
 * data flowing through an audio codec is samples rather than frames.
 *
 * Well-known IDs cover the codec families libpromeki's pipelines will
 * realistically see: AAC, Opus, FLAC, MP3, AC-3.  Uncompressed PCM is
 * handled by @ref AudioFormat directly, not here — AudioCodec concerns
 * itself strictly with real (compressed) codec families.
 *
 * @par Multi-backend support
 *
 * Every codec can have multiple concrete backends (vendored libopus,
 * system FFmpeg, application overrides, …).  Backends are identified
 * by a typed @ref Backend handle rather than naked strings, registered
 * via @ref registerBackend / @ref lookupBackend, and attached to
 * specific (codec, backend) pairs by the concrete @ref AudioEncoder /
 * @ref AudioDecoder factory registration.
 *
 * @par String form
 *
 * Serialized codec values travel as @c "<CodecName>" or
 * @c "<CodecName>:<BackendName>" — e.g. @c "Opus" for unpinned, or
 * @c "Opus:Native" to pin the vendored libopus backend.
 * @ref fromString parses both forms.  @ref toString emits the
 * canonical form.
 *
 * @par Capability metadata
 *
 * @ref Data exposes codec-family facts (compression type, bitrate
 * mode, packet independence, sample-rate / channel / format spec
 * limits, frame-size constraints, …) so planners can reason about
 * suitability before instantiating a session.  Per-backend limits
 * live on the backend record and are reachable via
 * @ref supportedSampleFormats / @ref encoderSupportedInputs.
 *
 * @par Thread Safety
 * Fully thread-safe — same contract as @ref VideoCodec.  The codec
 * handle wraps an integer ID and is safe to share by value across
 * threads.  Registrations are expected at static-init time; thereafter
 * @c lookup is lock-free.
 */
class AudioCodec {
        public:
                /**
                 * @brief Identifies an audio codec family.
                 *
                 * Well-known codecs have named enumerators.  User-defined
                 * codecs obtain IDs from @ref registerType, starting at
                 * @c UserDefined.
                 */
                enum ID {
                        Invalid         = 0,    ///< Invalid or uninitialised.
                        AAC             = 1,    ///< Advanced Audio Coding (ISO/IEC 14496-3).
                        Opus            = 2,    ///< Opus (RFC 6716).
                        FLAC            = 3,    ///< Free Lossless Audio Codec.
                        MP3             = 4,    ///< MPEG-1 Audio Layer III.
                        AC3             = 5,    ///< Dolby Digital (AC-3).
                        UserDefined     = 1024  ///< First ID available for user-registered codecs.
                };

                /** @brief List of AudioCodec IDs. */
                using IDList = List<ID>;

                /**
                 * @brief Classifies the codec's lossy-vs-lossless behaviour.
                 */
                enum CompressionType {
                        CompressionInvalid  = 0,    ///< Unknown / not classified.
                        CompressionLossless = 1,    ///< Bit-exact reconstruction (FLAC, ALAC).
                        CompressionLossy    = 2     ///< Psychoacoustic compression (AAC, Opus, MP3, AC-3).
                };

                /**
                 * @brief Describes how an individual access unit depends on prior packets.
                 *
                 * Drives planner decisions about seeking, packet loss
                 * tolerance, and streaming friendliness.
                 */
                enum PacketIndependence {
                        PacketIndependenceInvalid = 0,  ///< Unknown / not classified.
                        PacketIndependenceEvery   = 1,  ///< Every packet decodes standalone (Opus, PCM-in-container).
                        PacketIndependenceKeyframe = 2, ///< Only keyframe packets decode standalone (uncommon for audio).
                        PacketIndependenceInter   = 3   ///< Packets require prior-packet state (MP3 bit reservoir, AAC-LTP).
                };

                /**
                 * @brief Typed handle for a concrete codec backend.
                 *
                 * Instances are obtained via @ref registerBackend or
                 * @ref lookupBackend.  The handle is a cheap value
                 * type (one @c uint64_t under the hood) backed by a
                 * process-wide @ref AudioCodecBackendRegistry.
                 */
                class Backend {
                        public:
                                constexpr Backend() : _id(AudioCodecBackendRegistry::InvalidID) {}

                                static constexpr Backend fromId(uint64_t id) {
                                        Backend b;
                                        b._id = id;
                                        return b;
                                }

                                constexpr uint64_t id() const { return _id; }

                                String name() const {
                                        return AudioCodecBackendRegistry::instance().name(_id);
                                }

                                constexpr bool isValid() const {
                                        return _id != AudioCodecBackendRegistry::InvalidID;
                                }

                                constexpr bool operator==(const Backend &o) const { return _id == o._id; }
                                constexpr bool operator!=(const Backend &o) const { return _id != o._id; }
                                constexpr bool operator<(const Backend &o) const { return _id < o._id; }

                        private:
                                uint64_t _id;
                };

                /** @brief List of codec backend handles. */
                using BackendList = List<Backend>;

                /**
                 * @brief Immutable descriptor for an audio codec.
                 *
                 * Populated by the library for well-known codecs, or by
                 * applications via @ref registerData for custom codecs.
                 * Captures codec-family facts that do not depend on any
                 * specific backend implementation — per-backend
                 * capabilities live on the @ref AudioEncoder /
                 * @ref AudioDecoder backend records.
                 */
                struct Data {
                        ID                 id = Invalid;                                        ///< Unique codec identifier.
                        String             name;                                                ///< Short name; must be a valid C identifier (e.g. @c "Opus").
                        String             desc;                                                ///< Human-readable description.
                        FourCC::List         fourccList;                                          ///< Associated FourCC codes.
                        CompressionType    compressionType      = CompressionInvalid;           ///< Lossless vs lossy.
                        /**
                         * @brief Rate-control modes the codec can be driven in.
                         *
                         * Same shape as @ref VideoCodec::Data::rateControlModes.
                         * Empty means "constant-quality / fixed codec"
                         * (PCM-in-container, FLAC, etc.).
                         */
                        List<RateControlMode> rateControlModes;
                        PacketIndependence packetIndependence   = PacketIndependenceInvalid;    ///< Inter-packet dependencies.
                        bool               isStreamable         = false;                        ///< Friendly to streaming (packet-independent / low startup latency).
                        bool               supportsDRC          = false;                        ///< Bitstream natively carries dynamic-range control metadata (AC-3 yes).
                        bool               hasBuiltInSilence    = false;                        ///< Codec has first-class silence / DTX frames (Opus yes).
                        /**
                         * @brief Uncompressed @ref AudioFormat IDs the codec accepts / produces.
                         *
                         * Stored as @c int for header-layering reasons
                         * (audioformat.h pulls in audiocodec.h itself,
                         * so the dependency can't go the other way).
                         * Callers wrap each entry as @c AudioFormat(id)
                         * at use time.  Empty means "no spec-level
                         * constraint on sample format."
                         */
                        List<int>          supportedSampleFormats;
                        /**
                         * @brief Sample rates the codec spec permits, in Hz.
                         *
                         * Populated only when the codec spec actually
                         * restricts its input (Opus: 8000/12000/16000/
                         * 24000/48000).  Empty means "any rate".
                         */
                        List<float>        supportedSampleRates;
                        /**
                         * @brief Channel counts the codec spec permits.
                         *
                         * Populated only when the codec spec restricts
                         * channel count.  Empty means "any count".
                         */
                        List<int>          supportedChannelCounts;
                        int                maxChannels          = 0;                            ///< 0 = unlimited.
                        /**
                         * @brief Frame sizes the codec accepts, in samples.
                         *
                         * Populated when the codec has discrete frame
                         * size restrictions (Opus at 48 kHz:
                         * 120/240/480/960/1920/2880).  Empty means
                         * "any size the backend can accumulate".
                         */
                        List<int>          frameSizeSamples;
                };

                /** @brief Allocates and returns a unique ID for a user-defined codec. */
                static ID registerType();

                /**
                 * @brief Registers a Data record in the registry.
                 *
                 * @c data.name must be a valid C identifier; malformed
                 * names are rejected with a warning and the registration
                 * is dropped.
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns the list of every registered codec's ID.
                 * @return IDs of every registered codec, excluding @ref Invalid.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Looks up a codec by its registered name (unpinned).
                 *
                 * Returns @ref Error::IdNotFound when the name is not in
                 * the registry.  For parsing user input that may carry a
                 * @c ":<BackendName>" suffix, use @ref fromString instead.
                 */
                static Result<AudioCodec> lookup(const String &name);

                /**
                 * @brief Parses a string of the form @c "Codec" or @c "Codec:Backend".
                 *
                 * Returns @ref Error::IdNotFound when the codec name or
                 * backend name is unknown, and @ref Error::Invalid when
                 * the string shape is malformed.
                 */
                static Result<AudioCodec> fromString(const String &spec);

                /**
                 * @brief Registers a backend name and returns its typed handle.
                 *
                 * Idempotent; rejects names that are not valid C
                 * identifiers with @ref Error::Invalid.
                 */
                static Result<Backend> registerBackend(const String &name);

                /**
                 * @brief Looks up a previously registered backend by name.
                 *
                 * Returns @ref Error::IdNotFound when the name is not
                 * present in the registry.
                 */
                static Result<Backend> lookupBackend(const String &name);

                /**
                 * @brief Returns every backend with at least one registered encoder or decoder implementation.
                 */
                static BackendList registeredBackends();

                /**
                 * @brief Constructs an AudioCodec from an ID and optional pinned backend.
                 */
                inline AudioCodec(ID id = Invalid, Backend backend = Backend());

                /** @brief Returns true when this wrapper references a registered codec. */
                bool isValid() const { return d != nullptr && d->id != Invalid; }

                /** @brief Returns the unique ID. */
                ID id() const { return d->id; }

                /** @brief Returns the codec's short registered name. */
                const String &name() const { return d->name; }

                /** @brief Returns the codec's human-readable description. */
                const String &description() const { return d->desc; }

                /** @brief Returns the list of FourCCs associated with this codec. */
                const FourCC::List &fourccList() const { return d->fourccList; }

                /** @brief Returns the codec's compression classification. */
                CompressionType compressionType() const { return d->compressionType; }

                /** @brief Shorthand for @c compressionType()==CompressionLossless. */
                bool isLossless() const { return d->compressionType == CompressionLossless; }

                /** @brief Returns the rate-control modes the codec can be driven in. */
                const List<RateControlMode> &rateControlModes() const { return d->rateControlModes; }

                /** @brief Returns the codec's packet-independence classification. */
                PacketIndependence packetIndependence() const { return d->packetIndependence; }

                /** @brief Returns true when the codec is streaming-friendly. */
                bool isStreamable() const { return d->isStreamable; }

                /** @brief Returns true when the bitstream carries DRC metadata natively. */
                bool supportsDRC() const { return d->supportsDRC; }

                /** @brief Returns true when the codec has first-class silence / DTX frames. */
                bool hasBuiltInSilence() const { return d->hasBuiltInSilence; }

                /** @brief Returns the codec's spec-level accepted sample formats. */
                List<AudioFormat> supportedSampleFormats() const;

                /** @brief Returns the codec's spec-level accepted sample rates. */
                const List<float> &supportedSampleRates() const { return d->supportedSampleRates; }

                /** @brief Returns the codec's spec-level accepted channel counts. */
                const List<int> &supportedChannelCounts() const { return d->supportedChannelCounts; }

                /** @brief Returns the codec's max channel count (0 = unlimited). */
                int maxChannels() const { return d->maxChannels; }

                /** @brief Returns the codec's allowed frame sizes in samples. */
                const List<int> &frameSizeSamples() const { return d->frameSizeSamples; }

                /** @brief Returns the pinned backend handle, or an invalid handle when unpinned. */
                Backend backend() const { return _backend; }

                /** @brief Returns every backend with a registered encoder for this codec. */
                BackendList availableEncoderBackends() const;

                /** @brief Returns every backend with a registered decoder for this codec. */
                BackendList availableDecoderBackends() const;

                /** @brief Returns true when at least one encoder backend matches this wrapper. */
                bool canEncode() const;

                /** @brief Returns true when at least one decoder backend matches this wrapper. */
                bool canDecode() const;

                /**
                 * @brief Returns the uncompressed @c AudioFormat values the
                 *        encoder accepts (pinned backend, or union otherwise).
                 */
                List<AudioFormat> encoderSupportedInputs() const;

                /**
                 * @brief Returns the uncompressed @c AudioFormat values the
                 *        decoder can emit (pinned backend, or union otherwise).
                 */
                List<AudioFormat> decoderSupportedOutputs() const;

                /**
                 * @brief Creates a new @ref AudioEncoder session for this codec.
                 *
                 * Backend resolution order:
                 *   1. The wrapper's pinned backend (@ref backend), when valid.
                 *   2. @c MediaConfig::CodecBackend on @p config, when set.
                 *   3. The highest-weight registered encoder backend.
                 */
                Result<AudioEncoder *> createEncoder(const MediaConfig *config = nullptr) const;

                /**
                 * @brief Creates a new @ref AudioDecoder session for this codec.
                 *
                 * Mirrors @ref createEncoder.
                 */
                Result<AudioDecoder *> createDecoder(const MediaConfig *config = nullptr) const;

                /** @brief Returns the canonical string form: @c "Name" or @c "Name:Backend". */
                String toString() const;

                /** @brief Equality by (underlying Data, pinned backend). */
                bool operator==(const AudioCodec &o) const {
                        return d == o.d && _backend == o._backend;
                }

                bool operator!=(const AudioCodec &o) const { return !(*this == o); }

                /** @brief Returns the underlying Data pointer. */
                const Data *data() const { return d; }

        private:
                const Data *d        = nullptr;
                Backend     _backend;
                static const Data *lookupData(ID id);
};

inline AudioCodec::AudioCodec(ID id, Backend backend)
        : d(lookupData(id)), _backend(backend) {}

PROMEKI_NAMESPACE_END
