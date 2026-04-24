/**
 * @file      compressedvideopayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/uniqueptr.h>
#include <promeki/videopayload.h>
#include <promeki/buffer.h>
#include <promeki/videocodec.h>
#include <promeki/enums.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Compressed video payload — one encoded access unit whose
 *        codec identity lives in the base @ref ImageDesc's compressed
 *        @ref PixelFormat.
 * @ingroup proav
 *
 * The access unit is carried in the base's plane list — usually one
 * plane for a single contiguous bitstream, though multiple planes
 * (SPS / PPS / IDR fragments sharing a single backing @ref Buffer
 * via non-overlapping @ref BufferView offsets) are supported for
 * encoders that emit concatenated NAL units in one locked buffer.
 *
 * @par Codec-specific state
 *
 * - @ref frameType classifies the access unit as @c I / @c P / @c B
 *   / @c IDR / @c BRef (or @ref FrameType::Unknown when the producer
 *   doesn't set it).  The accessor is @c virtual so codec-specific
 *   subclasses (future @c NALBitstreamPayload, @c ProResPayload, …)
 *   can map their internal slice / picture types onto this common
 *   vocabulary without requiring every consumer to dispatch on the
 *   concrete type.
 * - @ref inBandCodecData optionally carries codec-private bytes
 *   attached to this specific packet (an in-band SPS/PPS update,
 *   for example).  Stream-level codec-private data normally lives
 *   on the producer's @ref MediaConfig; this per-payload override
 *   is null in the common case.
 *
 * The @ref MediaPayload::ParameterSet concept from the older packet
 * type lives here as the @c ParameterSet flag — packets that carry
 * @em only parameter sets (no actual slice data) set it so muxers
 * know to treat them specially.
 *
 * CompressedVideoPayload is intentionally not @c final — codec-
 * specific subclasses are a supported extension point.
 *
 * @par Example
 * @code
 * ImageDesc desc(1920, 1080, PixelFormat(PixelFormat::H264));
 * auto pkt = CompressedVideoPayload::Ptr::create(desc, encodedBuffer);
 * pkt.modify()->setPts(pts);
 * pkt.modify()->addFlag(MediaPayload::Keyframe);
 * pkt.modify()->setFrameType(FrameType::IDR);
 * @endcode
 */
class CompressedVideoPayload : public VideoPayload {
        public:
                virtual CompressedVideoPayload *_promeki_clone() const override {
                        return new CompressedVideoPayload(*this);
                }

                /** @brief Shared-pointer alias for CompressedVideoPayload ownership. */
                using Ptr = SharedPtr<CompressedVideoPayload, /*CopyOnWrite=*/true, CompressedVideoPayload>;

                /** @brief List of shared pointers to CompressedVideoPayload instances. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Unique-ownership pointer to a CompressedVideoPayload. */
                using UPtr = UniquePtr<CompressedVideoPayload>;

                /**
                 * @brief Codec-agnostic flags that apply to every
                 *        compressed video payload.
                 *
                 * Stored in the high bits of the base
                 * @ref MediaPayload::Flag bitmask so the two enums can
                 * share the same 32-bit word.  Use @ref addFlag /
                 * @ref hasFlag (inherited from the base) to manipulate
                 * values.
                 */
                enum VideoFlag : uint32_t {
                        ParameterSet = 1u << 16, ///< Carries only parameter sets (SPS / PPS / VPS); no slice data.
                };

                /** @brief Constructs an empty compressed video payload. */
                CompressedVideoPayload() = default;

                /**
                 * @brief Constructs a compressed video payload with a
                 *        descriptor.  Plane list left empty.
                 */
                explicit CompressedVideoPayload(const ImageDesc &desc) :
                        VideoPayload(desc) { }

                /**
                 * @brief Constructs a compressed video payload with a
                 *        descriptor and plane list.
                 *
                 * The single-slice shape (one shared buffer) is
                 * constructed by passing @c BufferView(buf, offset,
                 * size) directly.
                 */
                CompressedVideoPayload(const ImageDesc &desc,
                                       const BufferView &data) :
                        VideoPayload(desc, data) { }

                /**
                 * @brief Constructs a compressed video payload that owns
                 *        a whole buffer as its single-plane payload.
                 */
                CompressedVideoPayload(const ImageDesc &desc, Buffer::Ptr buffer) :
                        VideoPayload(desc,
                                buffer ? BufferView(buffer, 0, buffer->size())
                                       : BufferView()) { }

                /**
                 * @brief Always returns @c true — this class only models
                 *        compressed payloads.
                 */
                bool isCompressed() const override { return true; }

                /**
                 * @brief Tightens validity to also require a compressed
                 *        pixel format on the descriptor.
                 */
                bool isValid() const override {
                        return MediaPayload::isValid()
                            && desc().pixelFormat().isValid()
                            && desc().pixelFormat().isCompressed();
                }

                /**
                 * @brief Returns the role of this access unit in its stream.
                 *
                 * Virtual so codec-specific subclasses can map their
                 * internal picture / slice type onto this common
                 * vocabulary.  The default reports @ref FrameType::IDR
                 * when the @ref MediaPayload::Keyframe flag is set
                 * (callers that want to distinguish non-IDR I-pictures
                 * should set the frame type explicitly) and
                 * @ref FrameType::Unknown otherwise.
                 */
                virtual const FrameType &frameType() const {
                        if(_frameType.isValid() && _frameType != FrameType::Unknown) {
                                return _frameType;
                        }
                        return hasFlag(Keyframe) ? FrameType::IDR : FrameType::Unknown;
                }

                /** @brief Records the frame type explicitly. */
                void setFrameType(const FrameType &t) { _frameType = t; }

                /**
                 * @brief Returns true when stopping the stream before
                 *        this access unit leaves the decoder in a
                 *        coherent state.
                 *
                 * - Intra-only streams (JPEG, JPEG XS, ProRes, DNxHD)
                 *   are always safe — every access unit is an
                 *   independent random-access point.
                 * - Temporal streams (H.264, HEVC, AV1) are safe only
                 *   when the @ref MediaPayload::Keyframe flag is set.
                 * - A compressed payload whose descriptor has no
                 *   codec identity is treated as unsafe — we have
                 *   no evidence the stream can be truncated here.
                 */
                bool isSafeCutPoint() const override {
                        if(!isValid()) return false;
                        const VideoCodec &codec = desc().pixelFormat().videoCodec();
                        if(!codec.isValid()) return false;
                        if(codec.codingType() != VideoCodec::CodingTemporal) return true;
                        return isKeyframe();
                }

                /** @brief Returns true when the @c ParameterSet flag is set. */
                bool isParameterSet() const {
                        return (flags() & ParameterSet) != 0;
                }

                /** @brief Sets the @c ParameterSet flag. */
                void markParameterSet(bool v = true) {
                        if(v) setFlags(flags() | ParameterSet);
                        else  setFlags(flags() & ~ParameterSet);
                }

                /**
                 * @brief Returns an optional per-payload codec-private
                 *        data buffer.
                 *
                 * Typically null — stream-level codec-private data lives
                 * on the producer's @ref MediaConfig.  When non-null,
                 * this buffer carries an in-band update that applies to
                 * the current access unit.
                 */
                const Buffer::Ptr &inBandCodecData() const { return _inBandCodecData; }

                /** @brief Replaces the per-payload codec-private data buffer. */
                void setInBandCodecData(Buffer::Ptr b) {
                        _inBandCodecData = std::move(b);
                }

        protected:
                /**
                 * @brief Reports in-band codec data as an extra
                 *        exclusive field.
                 *
                 * Reuses the base @ref MediaPayload::isExclusive
                 * plane-list walk and additionally checks
                 * @ref inBandCodecData: when the codec data buffer
                 * is shared, the payload as a whole is not
                 * exclusive.
                 */
                bool isExclusiveExtras() const override {
                        return !_inBandCodecData.isValid() ||
                                _inBandCodecData.referenceCount() <= 1;
                }

                /**
                 * @brief CoW-detaches the in-band codec data alongside
                 *        the plane buffers.
                 */
                void ensureExclusiveExtras() override {
                        if(_inBandCodecData.isValid() &&
                           _inBandCodecData.referenceCount() > 1) {
                                _inBandCodecData.modify();
                        }
                }

        public:
                /** @brief Stable FourCC for DataStream serialisation. */
                static constexpr FourCC kSubclassFourCC{'C','V','d','p'};

                uint32_t subclassFourCC() const override {
                        return kSubclassFourCC.value();
                }

                /** @copydoc MediaPayload::serialisePayload */
                void serialisePayload(DataStream &s) const override;

                /** @copydoc MediaPayload::deserialisePayload */
                void deserialisePayload(DataStream &s) override;

                CompressedVideoPayload(const CompressedVideoPayload &) = default;
                CompressedVideoPayload(CompressedVideoPayload &&) = default;
                CompressedVideoPayload &operator=(const CompressedVideoPayload &) = default;
                CompressedVideoPayload &operator=(CompressedVideoPayload &&) = default;

        private:
                FrameType   _frameType;
                Buffer::Ptr _inBandCodecData;
};

PROMEKI_NAMESPACE_END
