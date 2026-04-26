/**
 * @file      videocodec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringregistry.h>
#include <promeki/list.h>
#include <promeki/fourcc.h>
#include <promeki/framerate.h>
#include <promeki/enums.h>
#include <promeki/result.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class PixelFormat;
class VideoEncoder;
class VideoDecoder;
class MediaConfig;

/**
 * @brief Compile-time tag for the VideoCodec backend StringRegistry.
 * @ingroup proav
 *
 * Chosen separately from the @c AudioCodec backend tag so the two
 * namespaces never alias — a @c Backend handle for @c "Nvidia" on the
 * video side is a different typed value from a (hypothetical) audio
 * @c "Nvidia" backend.
 */
using VideoCodecBackendRegistry = StringRegistry<"VideoCodecBackend">;

/**
 * @brief First-class identifier for a video codec family.
 * @ingroup proav
 *
 * Uses the @ref typeregistry "TypeRegistry pattern": a lightweight
 * inline wrapper around an immutable @ref Data record, identified by
 * an integer ID.  Well-known codecs are provided as named enum
 * constants; applications and third-party backends can register
 * additional codecs at runtime via @ref registerType and
 * @ref registerData.
 *
 * @par Multi-backend support
 *
 * Every codec family can have any number of concrete backends
 * (NVIDIA NVENC/NVDEC, libjpeg-turbo, SVT-JPEG-XS, vendored native
 * implementations, application-supplied overrides, …).  Backends are
 * identified by a typed @ref Backend handle rather than naked strings,
 * registered in a process-wide @ref VideoCodecBackendRegistry via
 * @ref registerBackend / @ref lookupBackend, and attached to specific
 * (codec, backend) pairs by the concrete @ref VideoEncoder /
 * @ref VideoDecoder factory registration.
 *
 * The wrapper can optionally carry a pinned @c Backend:
 *
 *  - @c VideoCodec(VideoCodec::H264)                      — unpinned;
 *    @ref createEncoder picks the highest-weight available backend
 *    (further overridden by @c MediaConfig::CodecBackend when that key
 *    is present).
 *  - @c VideoCodec(VideoCodec::H264, nvidiaBackend)        — pinned
 *    specifically to the NVIDIA backend; @ref createEncoder fails with
 *    @c Error::IdNotFound if the NVIDIA backend is not registered for
 *    @c H264.
 *
 * @par String form
 *
 * Serialized codec values travel as @c "<CodecName>" or
 * @c "<CodecName>:<BackendName>" — e.g. @c "H264" for unpinned, or
 * @c "H264:Nvidia" to pin the NVIDIA backend.  @ref fromString parses
 * both forms into a fully resolved wrapper and returns
 * @c Error::IdNotFound when the codec or backend is unknown.
 * @ref toString emits the canonical form.
 *
 * @par Capability introspection
 *
 * @ref Data exposes rich codec-family metadata — inter- vs intra-frame
 * coding, lossless capability, bit-depth support, HDR metadata support,
 * rate-control modes, and more — to let pipeline planners reason about
 * codec suitability before instantiating any session.  Per-backend
 * capabilities (specific input PixelFormats, bitrate ceilings, …) live
 * on the backend record and are queried via
 * @ref encoderSupportedInputs / @ref decoderSupportedOutputs.
 *
 * @par Example
 * @code
 * // Look up by ID and create a session:
 * VideoCodec codec(VideoCodec::H264);
 * auto result = codec.createEncoder();
 * if(isOk(result)) { VideoEncoder *enc = value(result); … }
 *
 * // Look up by string name, with optional backend pin:
 * auto parsed = VideoCodec::fromString("H264:Nvidia");
 * if(isOk(parsed)) { VideoCodec pinned = value(parsed); … }
 *
 * // Enumerate every registered codec:
 * for(auto id : VideoCodec::registeredIDs()) {
 *         VideoCodec vc(id);
 *         printf("%s — %s\n", vc.name().cstr(), vc.description().cstr());
 * }
 * @endcode
 *
 * @par Thread Safety
 * Fully thread-safe.  The @c VideoCodec handle wraps an integer ID and
 * is safe to share by value across threads.  Registrations are expected
 * at static-init time and the registry is internally synchronized;
 * thereafter @c lookup is lock-free.
 */
class VideoCodec {
        public:
                /**
                 * @brief Identifies a video codec family.
                 *
                 * Well-known codecs have named enumerators.  User-defined
                 * codecs obtain IDs from @ref registerType, starting at
                 * @c UserDefined.
                 */
                enum ID {
                        Invalid = 0,          ///< Invalid or uninitialised.
                        H264 = 1,             ///< H.264 / MPEG-4 AVC.
                        HEVC = 2,             ///< H.265 / HEVC.
                        AV1 = 3,              ///< AV1 (AOMedia Video 1).
                        VP9 = 4,              ///< VP9.
                        JPEG = 5,             ///< JPEG (ISO/IEC 10918-1 / JFIF).
                        JPEG_XS = 6,          ///< JPEG XS (ISO/IEC 21122).
                        ProRes_422_Proxy = 7, ///< Apple ProRes 422 Proxy (apco).
                        ProRes_422_LT = 8,    ///< Apple ProRes 422 LT (apcs).
                        ProRes_422 = 9,       ///< Apple ProRes 422 (apcn).
                        ProRes_422_HQ = 10,   ///< Apple ProRes 422 HQ (apch).
                        ProRes_4444 = 11,     ///< Apple ProRes 4444 (ap4h).
                        ProRes_4444_XQ = 12,  ///< Apple ProRes 4444 XQ (ap4x).
                        UserDefined = 1024    ///< First ID available for user-registered codecs.
                };

                /** @brief List of VideoCodec IDs. */
                using IDList = List<ID>;

                /**
                 * @brief Describes a codec's inter-frame coding behaviour.
                 *
                 * Drives planner decisions about random access, seek
                 * latency, error-recovery strategy, and whether a pipe
                 * may safely drop packets mid-stream.
                 */
                enum CodingType {
                        CodingInvalid = 0, ///< Unknown / not classified.
                        CodingIntraOnly =
                                1, ///< Every frame is independently decodable (JPEG, JPEG XS, ProRes, Motion JPEG).
                        CodingTemporal = 2 ///< Frames may reference other frames (H.264, HEVC, AV1, VP9).
                };

                /**
                 * @brief Granularity at which a stream can be randomly accessed.
                 *
                 * Intra-only codecs are random-access on every frame.
                 * Temporal codecs support random access only at GOP
                 * boundaries (IDR / keyframe).
                 */
                enum RandomAccessGranularity {
                        AccessInvalid = 0, ///< Unknown / not classified.
                        AccessFrame = 1,   ///< Every coded frame is a random-access point.
                        AccessGOP = 2      ///< Random access at GOP / keyframe boundaries only.
                };

                /**
                 * @brief Typed handle for a concrete codec backend.
                 *
                 * Instances are obtained via @ref VideoCodec::registerBackend
                 * or @ref VideoCodec::lookupBackend — callers never hold the
                 * raw backend name as a @ref String after registration.
                 * The handle is a cheap value type (one @c uint64_t under
                 * the hood) backed by a process-wide
                 * @ref VideoCodecBackendRegistry.
                 */
                class Backend {
                        public:
                                /** @brief Constructs an invalid backend handle. */
                                constexpr Backend() : _id(VideoCodecBackendRegistry::InvalidID) {}

                                /**
                                 * @brief Constructs a backend handle from a raw registry ID.
                                 *
                                 * Primarily used internally; callers should
                                 * prefer @ref VideoCodec::lookupBackend.
                                 * No validation is performed.
                                 */
                                static constexpr Backend fromId(uint64_t id) {
                                        Backend b;
                                        b._id = id;
                                        return b;
                                }

                                /** @brief Returns the underlying registry ID. */
                                constexpr uint64_t id() const { return _id; }

                                /** @brief Returns the registered backend name (e.g. @c "Nvidia"). */
                                String name() const { return VideoCodecBackendRegistry::instance().name(_id); }

                                /** @brief Returns true when this handle refers to a registered backend. */
                                constexpr bool isValid() const { return _id != VideoCodecBackendRegistry::InvalidID; }

                                /** @brief Equality by ID. */
                                constexpr bool operator==(const Backend &o) const { return _id == o._id; }
                                /** @brief Inequality by ID. */
                                constexpr bool operator!=(const Backend &o) const { return _id != o._id; }
                                /** @brief Ordered by ID (for use in @c Map / @c Set). */
                                constexpr bool operator<(const Backend &o) const { return _id < o._id; }

                        private:
                                uint64_t _id;
                };

                /** @brief List of codec backend handles. */
                using BackendList = List<Backend>;

                /**
                 * @brief Immutable descriptor for a video codec.
                 *
                 * Populated by the library for well-known codecs, or by
                 * applications via @ref registerData for custom codecs.
                 * Captures codec-family facts that do not depend on any
                 * specific backend implementation — per-backend
                 * capabilities live on the @ref VideoEncoder /
                 * @ref VideoDecoder backend records.
                 */
                struct Data {
                                ID           id = Invalid; ///< Unique codec identifier.
                                String       name;       ///< Short name, must be a valid C identifier (e.g. @c "H264").
                                String       desc;       ///< Human-readable description.
                                FourCC::List fourccList; ///< Associated FourCC codes.
                                /**
                         * @brief Compressed @ref PixelFormat IDs this codec produces / consumes.
                         *
                         * Stored as @c int for header-layering reasons
                         * (PixelFormat pulls in colormodel/pixelmemlayout).
                         * Callers wrap each as @ref PixelFormat(id) at use time.
                         */
                                List<int>               compressedPixelFormats;
                                CodingType              codingType = CodingInvalid; ///< Intra-only vs temporal.
                                RandomAccessGranularity randomAccessGranularity = AccessInvalid; ///< Seek granularity.
                                bool supportsBFrames = false; ///< Codec spec allows B-frame reordering.
                                bool supportsLossless =
                                        false; ///< Codec has a lossless mode (H.264 Hi444, ProRes 4444 XQ near-lossless, …).
                                bool supportsAlpha =
                                        false; ///< Codec encodes an alpha channel (ProRes 4444 yes, H.264 no).
                                bool supportsVariableFrameSize =
                                        false; ///< Each frame may declare new dimensions (JPEG yes, H.264 only at IDR).
                                bool supportsHDRMetadata =
                                        false; ///< Bitstream carries mastering display / content-light metadata natively.
                                bool supportsInterlaced = false; ///< Codec signals / encodes interlaced scan natively.
                                /**
                         * @brief Bit depths the codec spec permits, in bits per component.
                         *
                         * Empty means "no spec-level restriction" (unusual
                         * — most modern codecs enumerate 8 / 10 / 12).
                         */
                                List<int> supportedBitDepths;
                                /**
                         * @brief Rate-control modes the codec can be driven in.
                         *
                         * Same shape as @ref AudioCodec::Data::rateControlModes.
                         * Empty means "constant-quality / fixed codec"
                         * (lossless-only, JPEG at fixed quality, etc.).
                         */
                                List<RateControlMode> rateControlModes;
                                /**
                         * @brief Frame rates the codec spec permits.
                         *
                         * Populated only when the codec spec actually
                         * restricts its frame rate.  Empty means "any
                         * rate".  Mirror of
                         * @ref AudioCodec::Data::supportedSampleRates.
                         */
                                List<FrameRate> supportedFrameRates;
                                int             maxWidth = 0;  ///< 0 = spec-unlimited.
                                int             maxHeight = 0; ///< 0 = spec-unlimited.
                                int             maxChannels =
                                        0; ///< 0 = unlimited.  (Not meaningful for most video codecs; present for symmetry with audio.)
                };

                /**
                 * @brief Allocates and returns a unique ID for a user-defined codec.
                 *
                 * Thread-safe via an atomic counter that starts at @c UserDefined.
                 */
                static ID registerType();

                /**
                 * @brief Registers a Data record in the codec-family registry.
                 *
                 * After this call, constructing a @ref VideoCodec from
                 * @c data.id resolves to the registered data.  @c data.name
                 * must be a valid C identifier (@ref String::isIdentifier);
                 * registrations with malformed names abort in debug builds
                 * and are dropped (with a warning) in release builds.
                 *
                 * @param data The populated Data struct with id set to a value from @ref registerType
                 *             (or one of the well-known IDs — backends commonly re-register the
                 *             well-known record to adjust metadata).
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns the list of every registered codec's ID.
                 *
                 * Excludes @ref Invalid.  Includes well-known and
                 * user-registered codecs alike.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Looks up a codec by its registered name.
                 * @param name The name to search for (e.g. @c "H264").
                 * @return The matching codec on success, or
                 *         @ref Error::IdNotFound if no registered codec
                 *         carries that name.  The returned wrapper is
                 *         unpinned (no specific backend).  Use
                 *         @ref fromString when parsing user input that
                 *         may contain a @c ":<BackendName>" suffix.
                 */
                static Result<VideoCodec> lookup(const String &name);

                /**
                 * @brief Finds the codec whose @ref compressedPixelFormats list
                 *        contains @p pd.
                 * @param pd A compressed PixelFormat (e.g. @c PixelFormat::H264).
                 * @return The matching codec, or an invalid codec if no
                 *         registered codec claims @p pd.
                 */
                static VideoCodec fromPixelFormat(const PixelFormat &pd);

                /**
                 * @brief Parses a string of the form @c "Codec" or @c "Codec:Backend".
                 *
                 * When the backend suffix is present, the backend must
                 * already be registered (via a call to
                 * @ref registerBackend at static-init time by the
                 * relevant backend provider).  Returns
                 * @ref Error::IdNotFound when the codec name or backend
                 * name is unknown, and @ref Error::Invalid when the
                 * overall string shape is malformed.
                 *
                 * @param spec The string to parse.
                 * @return A resolved @ref VideoCodec on success, or an
                 *         error Result.
                 */
                static Result<VideoCodec> fromString(const String &spec);

                /**
                 * @brief Registers a backend name and returns its typed handle.
                 *
                 * Idempotent — re-registering the same name returns the
                 * existing handle.  Rejects names that are not valid C
                 * identifiers (see @ref String::isIdentifier) with
                 * @ref Error::Invalid.  Hash collisions with an already-
                 * registered backend are handled transparently via the
                 * underlying @ref StringRegistry's probing registration.
                 *
                 * @param name The backend name to register (e.g. @c "Nvidia", @c "Turbo").
                 * @return The typed @ref Backend handle on success.
                 */
                static Result<Backend> registerBackend(const String &name);

                /**
                 * @brief Looks up an already-registered backend by name.
                 *
                 * Does not register the name.  Returns
                 * @ref Error::IdNotFound if the name is not present in
                 * the registry.
                 *
                 * @param name The backend name to look up.
                 * @return The typed @ref Backend handle on success.
                 */
                static Result<Backend> lookupBackend(const String &name);

                /**
                 * @brief Returns every backend that has a registered encoder or decoder implementation.
                 *
                 * The list is deduplicated across encoder and decoder
                 * registrations and ordered arbitrarily — callers that
                 * care about determinism should sort by @c name().
                 */
                static BackendList registeredBackends();

                /**
                 * @brief Constructs a VideoCodec from an ID and optional pinned backend.
                 * @param id      The codec ID (default: @ref Invalid).
                 * @param backend Optional pinned backend handle; default
                 *                is an invalid handle ("unpinned").
                 */
                inline VideoCodec(ID id = Invalid, Backend backend = Backend());

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

                /** @brief Returns the list of compressed @ref PixelFormat values this codec covers. */
                List<PixelFormat> compressedPixelFormats() const;

                /** @brief Returns the codec's inter-frame coding classification. */
                CodingType codingType() const { return d->codingType; }

                /** @brief Shorthand for <tt>codingType() == CodingIntraOnly</tt>. */
                bool isIntraOnly() const { return d->codingType == CodingIntraOnly; }

                /** @brief Returns the codec's random-access granularity. */
                RandomAccessGranularity randomAccessGranularity() const { return d->randomAccessGranularity; }

                /** @brief Returns true when the codec spec permits B-frames. */
                bool supportsBFrames() const { return d->supportsBFrames; }

                /** @brief Returns true when the codec has a lossless mode. */
                bool supportsLossless() const { return d->supportsLossless; }

                /** @brief Returns true when the codec encodes alpha. */
                bool supportsAlpha() const { return d->supportsAlpha; }

                /** @brief Returns true when each frame may declare new dimensions. */
                bool supportsVariableFrameSize() const { return d->supportsVariableFrameSize; }

                /** @brief Returns true when the bitstream natively carries HDR mastering / CLL metadata. */
                bool supportsHDRMetadata() const { return d->supportsHDRMetadata; }

                /** @brief Returns true when the codec natively signals interlaced scan. */
                bool supportsInterlaced() const { return d->supportsInterlaced; }

                /** @brief Returns the bit depths permitted by the codec spec. */
                const List<int> &supportedBitDepths() const { return d->supportedBitDepths; }

                /** @brief Returns the rate-control modes the codec can be driven in. */
                const List<RateControlMode> &rateControlModes() const { return d->rateControlModes; }

                /** @brief Returns the frame rates the codec spec permits (empty = any). */
                const List<FrameRate> &supportedFrameRates() const { return d->supportedFrameRates; }

                /** @brief Returns the spec's maximum supported width (0 = unlimited). */
                int maxWidth() const { return d->maxWidth; }

                /** @brief Returns the spec's maximum supported height (0 = unlimited). */
                int maxHeight() const { return d->maxHeight; }

                /**
                 * @brief Returns the pinned backend handle, or an invalid handle when unpinned.
                 */
                Backend backend() const { return _backend; }

                /**
                 * @brief Returns every backend with an encoder implementation registered for this codec.
                 */
                BackendList availableEncoderBackends() const;

                /**
                 * @brief Returns every backend with a decoder implementation registered for this codec.
                 */
                BackendList availableDecoderBackends() const;

                /**
                 * @brief Returns true when at least one encoder backend is registered for this codec.
                 */
                bool canEncode() const;

                /**
                 * @brief Returns true when at least one decoder backend is registered for this codec.
                 */
                bool canDecode() const;

                /**
                 * @brief Returns the uncompressed @ref PixelFormat values the
                 *        encoder accepts.
                 *
                 * When this wrapper pins a specific backend, returns the
                 * pinned backend's list.  Otherwise returns the union
                 * across every registered encoder backend.  Empty means
                 * "the relevant backend accepts any uncompressed input".
                 */
                List<PixelFormat> encoderSupportedInputs() const;

                /**
                 * @brief Returns the uncompressed @ref PixelFormat values the
                 *        decoder can emit.
                 *
                 * Same semantics as @ref encoderSupportedInputs — pinned
                 * backend wins when set, otherwise union across registered
                 * decoder backends.
                 */
                List<PixelFormat> decoderSupportedOutputs() const;

                /**
                 * @brief Creates a new @ref VideoEncoder session for this codec.
                 *
                 * Backend resolution order:
                 *   1. The wrapper's pinned backend (@ref backend), when valid.
                 *   2. @c MediaConfig::CodecBackend on @p config, when set and parses as a registered backend.
                 *   3. The highest-weight registered encoder backend for this codec.
                 *
                 * @param config Optional caller-supplied configuration.
                 *               Read-only here — the returned encoder has
                 *               had @ref VideoEncoder::configure called on
                 *               it automatically when @p config is non-null.
                 * @return The session on success.  Error values:
                 *         @c Error::Invalid when the codec is invalid,
                 *         @c Error::IdNotFound when no backend can satisfy
                 *         the pinned selection.  The encoder is owned by
                 *         the caller.
                 */
                Result<VideoEncoder *> createEncoder(const MediaConfig *config = nullptr) const;

                /**
                 * @brief Creates a new @ref VideoDecoder session for this codec.
                 *
                 * Mirrors @ref createEncoder.
                 *
                 * @param config Optional caller-supplied configuration.
                 * @return The session, owned by the caller, or an error Result.
                 */
                Result<VideoDecoder *> createDecoder(const MediaConfig *config = nullptr) const;

                /**
                 * @brief Returns the canonical string form: @c "Name" or @c "Name:Backend".
                 */
                String toString() const;

                /** @brief Equality compares (underlying Data, pinned backend). */
                bool operator==(const VideoCodec &o) const { return d == o.d && _backend == o._backend; }

                /** @brief Inequality. */
                bool operator!=(const VideoCodec &o) const { return !(*this == o); }

                /** @brief Returns the underlying Data pointer. */
                const Data *data() const { return d; }

        private:
                const Data        *d = nullptr;
                Backend            _backend;
                static const Data *lookupData(ID id);
};

inline VideoCodec::VideoCodec(ID id, Backend backend) : d(lookupData(id)), _backend(backend) {}

PROMEKI_NAMESPACE_END
