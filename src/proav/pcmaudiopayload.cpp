/**
 * @file      pcmaudiopayload.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <cstring>
#include <promeki/pcmaudiopayload.h>
#include <promeki/variantlookup.h>
#include <promeki/variantdatabase.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PcmAudioPayload::Ptr PcmAudioPayload::convert(
        const AudioFormat &dstFormat) const
{
        if(!isValid() || !dstFormat.isValid()) return Ptr();
        if(dstFormat.isCompressed()) {
                promekiErr("PcmAudioPayload::convert: target "
                           "audio format '%s' is compressed — use an "
                           "audio encoder instead",
                           dstFormat.name().cstr());
                return Ptr();
        }
        if(dstFormat.bytesPerSample() == 0) return Ptr();

        const AudioDesc &srcDesc = desc();
        if(srcDesc.format() == dstFormat) return Ptr::create(*this);

        // Only single-plane (interleaved) PCM is supported by the sample
        // kernels right now — planar layouts would need per-channel
        // conversion which the legacy converter also did not provide.
        if(planeCount() != 1) {
                promekiErr("PcmAudioPayload::convert: planar "
                           "PCM conversion not supported (planeCount=%zu)",
                           planeCount());
                return Ptr();
        }
        auto srcView = plane(0);
        if(!srcView.isValid()) return Ptr();

        AudioDesc dstDesc(dstFormat, srcDesc.sampleRate(), srcDesc.channels());
        if(!dstDesc.isValid()) return Ptr();

        const size_t samples = sampleCount();
        const size_t totalSamples = samples * srcDesc.channels();
        const size_t dstBytes = dstDesc.bufferSize(samples);

        auto dstBuf = Buffer::Ptr::create(dstBytes);
        dstBuf.modify()->setSize(dstBytes);

        const uint8_t *srcBytes = static_cast<const uint8_t *>(srcView.data());
        uint8_t *dstBytesPtr = static_cast<uint8_t *>(dstBuf.modify()->data());

        // Fast path: source is native float — direct floatToSamples.
        if(srcDesc.format().id() == AudioFormat::NativeFloat) {
                const float *floatData = reinterpret_cast<const float *>(srcBytes);
                dstFormat.floatToSamples(dstBytesPtr, floatData, totalSamples);
        }
        // Fast path: target is native float — direct samplesToFloat.
        else if(dstFormat.id() == AudioFormat::NativeFloat) {
                float *floatData = reinterpret_cast<float *>(dstBytesPtr);
                srcDesc.format().samplesToFloat(floatData, srcBytes, totalSamples);
        }
        // General case: source → float → target (two passes).
        else {
                auto tmpBuf = Buffer::Ptr::create(totalSamples * sizeof(float));
                float *tmp = static_cast<float *>(tmpBuf.modify()->data());
                srcDesc.format().samplesToFloat(tmp, srcBytes, totalSamples);
                dstFormat.floatToSamples(dstBytesPtr, tmp, totalSamples);
        }

        BufferView dstViews;
        dstViews.pushToBack(dstBuf, 0, dstBytes);
        auto result = Ptr::create(dstDesc, samples, dstViews);
        result.modify()->metadata() = metadata();
        result.modify()->setPts(pts());
        return result;
}

// ============================================================================
// VariantLookup registration
//
// AudioPayload surfaces every @ref AudioDesc field directly as a
// first-class scalar on the payload — no @c Desc.* composition, no
// second lookup hop.  In the payload context the descriptor is an
// implementation detail, so queries like @c Audio[0].SampleRate stay
// flat.  @c SampleCount also lives here because every audio payload
// (PCM or compressed block codec) carries a per-channel decoded
// sample count.
//
// @c Meta.* is @b not re-registered here.  @ref MediaPayload's Meta
// binding lives on the base and resolves through the virtual
// @ref MediaPayload::metadata, which @ref AudioPayload overrides to
// return @c desc().metadata() — so the cascade already hands back
// descriptor metadata on audio payloads without a second binding.
// ============================================================================

PROMEKI_LOOKUP_REGISTER(AudioPayload)
        .inheritsFrom<MediaPayload>()
        .scalar("SampleRate",
                [](const AudioPayload &p) -> std::optional<Variant> {
                        return Variant(p.desc().sampleRate());
                })
        .scalar("Channels",
                [](const AudioPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint32_t>(p.desc().channels()));
                })
        .scalar("Format",
                [](const AudioPayload &p) -> std::optional<Variant> {
                        return Variant(p.desc().format());
                })
        .scalar("BytesPerSample",
                [](const AudioPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(p.desc().bytesPerSample()));
                })
        .scalar("IsNative",
                [](const AudioPayload &p) -> std::optional<Variant> {
                        return Variant(p.desc().isNative());
                })
        .scalar("SampleCount",
                [](const AudioPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(p.sampleCount()));
                });

PROMEKI_LOOKUP_REGISTER(PcmAudioPayload)
        .inheritsFrom<AudioPayload>();

PROMEKI_NAMESPACE_END
