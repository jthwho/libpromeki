/**
 * @file      videocodec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/fourcc.h>

PROMEKI_NAMESPACE_BEGIN

class VideoEncoder;
class VideoDecoder;

/**
 * @brief First-class identifier for a video / still-image codec family.
 * @ingroup proav
 *
 * Uses the @ref typeregistry "TypeRegistry pattern": a lightweight
 * inline wrapper around an immutable @ref Data record, identified by
 * an integer ID.  Well-known codecs are provided as named enum
 * constants; applications and third-party backends can register
 * additional codecs at runtime via @ref registerType and
 * @ref registerData.
 *
 * A @ref VideoCodec entry is the central place where everything the
 * library needs to know about a codec lives:
 *
 *  - Human-readable name + description (what the CLI, logs, and
 *    stats reports display).
 *  - FourCC list (identifiers for sample-entry / ISO-BMFF boxes,
 *    RTP payloads, etc.).
 *  - Compressed @ref PixelDesc IDs the codec produces / consumes
 *    (so introspection can answer "what PixelDescs does the h264
 *    codec cover?" without hard-coding the list at each callsite).
 *  - Factory pointers that create new @ref VideoEncoder / @ref
 *    VideoDecoder session objects — either can be null for decode-
 *    or encode-only codecs.
 *
 * The factory fields are the replacement for the string-keyed
 * @c VideoEncoder::registerEncoder / @c VideoDecoder::registerDecoder
 * registries.  Instead of looking up a codec by string, callers
 * construct a @ref VideoCodec (either from a well-known ID or
 * wrapping a user-registered ID) and call
 * @ref createEncoder / @ref createDecoder on it.
 *
 * Temporal codecs (H.264, HEVC, AV1, VP9, ProRes) and I-frame codecs
 * (JPEG, JPEG XS) share the same interface: every codec produces a
 * stateful session via its encoder / decoder factory, even when that
 * session's state is just a reusable allocation context for the
 * next frame.  Unifying them through @ref VideoEncoder /
 * @ref VideoDecoder (instead of the old @c ImageCodec stateless
 * contract) lets I-frame codecs amortise libjpeg-turbo / SVT-JPEG-XS
 * allocation across frames and keeps exactly one compression
 * entry point in the pipeline.
 *
 * @par Example
 * @code
 * // Look up by ID and create a session:
 * VideoCodec codec(VideoCodec::H264);
 * VideoEncoder *enc = codec.createEncoder();
 *
 * // Look up by string name (CLI flow):
 * VideoCodec fromCli = VideoCodec::lookup("H264");
 * assert(fromCli == codec);
 *
 * // Enumerate every registered codec (--cc VideoCodec:list):
 * for(auto id : VideoCodec::registeredIDs()) {
 *         VideoCodec vc(id);
 *         printf("%s — %s\n", vc.name().cstr(), vc.description().cstr());
 * }
 * @endcode
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
                        Invalid     = 0,    ///< Invalid or uninitialised.
                        H264        = 1,    ///< H.264 / MPEG-4 AVC.
                        HEVC        = 2,    ///< H.265 / HEVC.
                        AV1         = 3,    ///< AV1 (AOMedia Video 1).
                        VP9         = 4,    ///< VP9.
                        JPEG        = 5,    ///< JPEG (ISO/IEC 10918-1 / JFIF).
                        JPEG_XS     = 6,    ///< JPEG XS (ISO/IEC 21122).
                        ProRes_422_Proxy = 7,  ///< Apple ProRes 422 Proxy (apco).
                        ProRes_422_LT    = 8,  ///< Apple ProRes 422 LT (apcs).
                        ProRes_422       = 9,  ///< Apple ProRes 422 (apcn).
                        ProRes_422_HQ    = 10, ///< Apple ProRes 422 HQ (apch).
                        ProRes_4444      = 11, ///< Apple ProRes 4444 (ap4h).
                        ProRes_4444_XQ   = 12, ///< Apple ProRes 4444 XQ (ap4x).
                        UserDefined = 1024  ///< First ID available for user-registered codecs.
                };

                /** @brief List of VideoCodec IDs. */
                using IDList = List<ID>;

                /** @brief Factory for a new VideoEncoder session (may be null). */
                using EncoderFactory = std::function<VideoEncoder *()>;

                /** @brief Factory for a new VideoDecoder session (may be null). */
                using DecoderFactory = std::function<VideoDecoder *()>;

                /**
                 * @brief Immutable descriptor for a video codec.
                 *
                 * Populated by the library for well-known codecs, or by
                 * applications via @ref registerData for custom codecs.
                 */
                struct Data {
                        ID              id = Invalid;       ///< Unique codec identifier.
                        String          name;               ///< Short name (e.g. @c "H264").
                        String          desc;               ///< Human-readable description.
                        FourCCList      fourccList;         ///< Associated FourCC codes.
                        /**
                         * @brief Compressed @ref PixelDesc IDs this codec produces / consumes.
                         *
                         * Stored as @c int for header-layering reasons
                         * (PixelDesc pulls in colormodel/pixelformat).
                         * Callers wrap each as @ref PixelDesc(id) at use time.
                         */
                        List<int>       compressedPixelDescs;
                        EncoderFactory  createEncoder;      ///< Factory for VideoEncoder sessions; null = encode not supported.
                        DecoderFactory  createDecoder;      ///< Factory for VideoDecoder sessions; null = decode not supported.
                };

                /**
                 * @brief Allocates and returns a unique ID for a user-defined codec.
                 *
                 * Thread-safe via an atomic counter that starts at @c UserDefined.
                 */
                static ID registerType();

                /**
                 * @brief Registers a Data record in the registry.
                 *
                 * After this call, constructing a @ref VideoCodec from
                 * @c data.id resolves to the registered data.
                 *
                 * @param data The populated Data struct with id set to a value from @ref registerType
                 *             (or one of the well-known IDs — backends commonly register
                 *             factories against those directly).
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns the list of every registered codec's ID.
                 *
                 * Excludes @ref Invalid.  Includes well-known and
                 * user-registered codecs alike.  Useful for
                 * @c --cc VideoCodec:list CLI enumeration.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Looks up a codec by its registered name.
                 * @param name The name to search for (e.g. @c "H264").
                 * @return The matching codec, or an invalid codec if not found.
                 */
                static VideoCodec lookup(const String &name);

                /**
                 * @brief Constructs a VideoCodec from an ID.
                 * @param id The codec ID (default: @ref Invalid).
                 */
                inline VideoCodec(ID id = Invalid);

                /** @brief Returns true when this wrapper references a registered codec. */
                bool isValid() const { return d != nullptr && d->id != Invalid; }

                /** @brief Returns the unique ID. */
                ID id() const { return d->id; }

                /** @brief Returns the codec's short registered name. */
                const String &name() const { return d->name; }

                /** @brief Returns the codec's human-readable description. */
                const String &description() const { return d->desc; }

                /** @brief Returns the list of FourCCs associated with this codec. */
                const FourCCList &fourccList() const { return d->fourccList; }

                /**
                 * @brief Returns the list of compressed @ref PixelDesc IDs this codec covers.
                 *
                 * For H.264 / HEVC this is a single entry matching the
                 * same-named @ref PixelDesc.  For codecs that represent
                 * multiple compressed variants (the ProRes family, JPEG
                 * XS Rec.709 / Rec.2020, …) every covered PixelDesc
                 * appears in the list.
                 */
                const List<int> &compressedPixelDescs() const { return d->compressedPixelDescs; }

                /** @brief Returns true when this codec has a registered encoder factory. */
                bool canEncode() const { return d->createEncoder != nullptr; }

                /** @brief Returns true when this codec has a registered decoder factory. */
                bool canDecode() const { return d->createDecoder != nullptr; }

                /**
                 * @brief Creates a new @ref VideoEncoder session for this codec.
                 * @return A freshly-allocated encoder (caller owns), or nullptr when encode
                 *         is not supported by this codec.
                 */
                VideoEncoder *createEncoder() const {
                        return d->createEncoder ? d->createEncoder() : nullptr;
                }

                /**
                 * @brief Creates a new @ref VideoDecoder session for this codec.
                 * @return A freshly-allocated decoder (caller owns), or nullptr when decode
                 *         is not supported by this codec.
                 */
                VideoDecoder *createDecoder() const {
                        return d->createDecoder ? d->createDecoder() : nullptr;
                }

                /** @brief Equality compares the underlying Data pointer. */
                bool operator==(const VideoCodec &o) const { return d == o.d; }

                /** @brief Inequality. */
                bool operator!=(const VideoCodec &o) const { return d != o.d; }

                /** @brief Returns the underlying Data pointer. */
                const Data *data() const { return d; }

        private:
                const Data *d = nullptr;
                static const Data *lookupData(ID id);
};

inline VideoCodec::VideoCodec(ID id) : d(lookupData(id)) {}

PROMEKI_NAMESPACE_END
