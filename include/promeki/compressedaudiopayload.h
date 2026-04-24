/**
 * @file      compressedaudiopayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/uniqueptr.h>
#include <promeki/audiopayload.h>
#include <promeki/buffer.h>
#include <promeki/audiocodec.h>
#include <promeki/variantlookup.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Compressed audio payload — one encoded access unit whose
 *        codec identity lives in the base @ref AudioDesc's
 *        compressed @ref AudioFormat.
 * @ingroup proav
 *
 * Block-based audio codecs (Opus, AAC, MP3, FLAC) emit exactly one
 * encoded audio frame per payload; its decoded length lives in the
 * base as @ref AudioPayload::sampleCount, from which
 * @ref MediaPayload::duration is derived using the descriptor's
 * sample rate.  The encoded bytes live in the base's plane list as
 * a single @ref BufferView; multiple packets may share one backing
 * @ref Buffer when the encoder emits them concatenated.
 *
 * Whether a given packet is a self-contained decode entry point is
 * codec-dependent.  Most compressed audio codecs of interest here
 * (Opus, MP3) have @c PacketIndependenceEvery — every packet
 * stands alone — so the base @ref MediaPayload::Keyframe flag is
 * expected to be set by the producer.  Codecs with stricter
 * dependencies (AAC LATM, some Vorbis configurations) leave the
 * flag clear on non-entry packets.
 *
 * @par Codec-private data
 *
 * - @ref inBandCodecData optionally carries codec-private bytes
 *   attached to this packet (an in-band magic-cookie refresh,
 *   say).  Stream-level codec-private data normally lives on the
 *   producer's @ref MediaConfig; this per-payload override is
 *   null in the common case.
 *
 * CompressedAudioPayload is intentionally not @c final — codec-
 * specific subclasses are a supported extension point.
 *
 * @par Example
 * @code
 * AudioDesc desc(AudioFormat(AudioFormat::Opus), 48000, 2);
 * auto pkt = CompressedAudioPayload::Ptr::create(
 *         desc, encodedBuffer, /\*sampleCount=*\/960);
 * pkt.modify()->setPts(pts);
 * // Duration is derived: 960 / 48000 = 20 ms.
 * @endcode
 */
class CompressedAudioPayload : public AudioPayload {
        public:
                PROMEKI_MEDIAPAYLOAD_LOOKUP_DISPATCH(CompressedAudioPayload)

                virtual CompressedAudioPayload *_promeki_clone() const override {
                        return new CompressedAudioPayload(*this);
                }

                /** @brief Shared-pointer alias for CompressedAudioPayload ownership. */
                using Ptr = SharedPtr<CompressedAudioPayload, /*CopyOnWrite=*/true, CompressedAudioPayload>;

                /** @brief List of shared pointers to CompressedAudioPayload instances. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Unique-ownership pointer to a CompressedAudioPayload. */
                using UPtr = UniquePtr<CompressedAudioPayload>;

                /** @brief Constructs an empty compressed audio payload. */
                CompressedAudioPayload() = default;

                /**
                 * @brief Constructs a compressed audio payload with the
                 *        given descriptor and (optional) sample count.
                 *        Plane list left empty.
                 *
                 * @p sampleCount is the number of decoded samples per
                 * channel that the encoded access unit represents.
                 * Block audio codecs (Opus, AAC, MP3, …) always know
                 * this value at encode time.
                 */
                explicit CompressedAudioPayload(const AudioDesc &desc,
                                                size_t sampleCount = 0) :
                        AudioPayload(desc, sampleCount) { }

                /**
                 * @brief Constructs a compressed audio payload with a
                 *        descriptor, (optional) sample count, and plane
                 *        list.
                 *
                 * The single-slice shape (one shared buffer) is
                 * constructed by passing @c BufferView(buf, offset,
                 * size) directly.
                 */
                CompressedAudioPayload(const AudioDesc &desc,
                                       const BufferView &data,
                                       size_t sampleCount = 0) :
                        AudioPayload(desc, sampleCount, data) { }

                /**
                 * @brief Constructs a compressed audio payload that owns
                 *        a whole buffer as its single-plane payload.
                 */
                CompressedAudioPayload(const AudioDesc &desc, Buffer::Ptr buffer,
                                       size_t sampleCount = 0) :
                        AudioPayload(desc, sampleCount,
                                buffer ? BufferView(buffer, 0, buffer->size())
                                       : BufferView()) { }

                /**
                 * @brief Always returns @c true — this class only models
                 *        compressed audio payloads.
                 */
                bool isCompressed() const override { return true; }

                /**
                 * @brief Tightens validity to also require a compressed
                 *        audio format on the descriptor.
                 */
                bool isValid() const override {
                        return MediaPayload::isValid()
                            && desc().format().isValid()
                            && desc().format().isCompressed();
                }

                /**
                 * @brief Returns true when stopping the stream before
                 *        this access unit leaves the decoder in a
                 *        coherent state.
                 *
                 * - Codecs whose @ref AudioCodec::PacketIndependence is
                 *   @c Every (Opus, PCM-in-container, FLAC frames) are
                 *   safe at every packet.
                 * - Codecs whose independence is @c Keyframe are safe
                 *   only when the @ref MediaPayload::Keyframe flag is
                 *   set on this payload.  (Unlike the pre-payload
                 *   audio API, this cut-point predicate has a real
                 *   per-packet keyframe flag to consult.)
                 * - Codecs with @c Inter dependencies (MP3 bit
                 *   reservoir, AAC-LTP) are never safe to cut at the
                 *   payload level.
                 * - A compressed payload whose descriptor has no
                 *   codec identity is treated as unsafe.
                 */
                bool isSafeCutPoint() const override {
                        if(!isValid()) return false;
                        const AudioCodec &codec = desc().format().audioCodec();
                        if(!codec.isValid()) return false;
                        switch(codec.packetIndependence()) {
                                case AudioCodec::PacketIndependenceEvery:
                                        return true;
                                case AudioCodec::PacketIndependenceKeyframe:
                                        return isKeyframe();
                                case AudioCodec::PacketIndependenceInter:
                                case AudioCodec::PacketIndependenceInvalid:
                                        return false;
                        }
                        return false;
                }

                /**
                 * @brief Returns an optional per-payload codec-private
                 *        data buffer.  Typically null.
                 */
                const Buffer::Ptr &inBandCodecData() const { return _inBandCodecData; }

                /** @brief Replaces the per-payload codec-private data buffer. */
                void setInBandCodecData(Buffer::Ptr b) {
                        _inBandCodecData = std::move(b);
                }

        protected:
                /**
                 * @brief Reports in-band codec data as an extra
                 *        exclusive field.  @sa @ref MediaPayload::isExclusive
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
                static constexpr FourCC kSubclassFourCC{'C','A','d','p'};

                uint32_t subclassFourCC() const override {
                        return kSubclassFourCC.value();
                }

                /** @copydoc MediaPayload::serialisePayload */
                void serialisePayload(DataStream &s) const override;

                /** @copydoc MediaPayload::deserialisePayload */
                void deserialisePayload(DataStream &s) override;

                CompressedAudioPayload(const CompressedAudioPayload &) = default;
                CompressedAudioPayload(CompressedAudioPayload &&) = default;
                CompressedAudioPayload &operator=(const CompressedAudioPayload &) = default;
                CompressedAudioPayload &operator=(CompressedAudioPayload &&) = default;

        private:
                Buffer::Ptr _inBandCodecData;
};

PROMEKI_NAMESPACE_END
