/**
 * @file      uncompressedvideopayload.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/uncompressedvideopayload.h>
#include <promeki/cscpipeline.h>
#include <promeki/mediaconfig.h>
#include <promeki/paintengine.h>
#include <promeki/variantlookup.h>
#include <promeki/variantdatabase.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

UncompressedVideoPayload::Ptr UncompressedVideoPayload::convert(
        const PixelFormat &dstPd,
        const Metadata &metadata) const
{
        return convert(dstPd, metadata, MediaConfig());
}

UncompressedVideoPayload::Ptr UncompressedVideoPayload::convert(
        const PixelFormat &dstPd,
        const Metadata &metadata,
        const MediaConfig &config) const
{
        if(!isValid() || !dstPd.isValid()) return Ptr();
        if(dstPd.isCompressed()) {
                promekiErr("UncompressedVideoPayload::convert: target "
                           "pixel format '%s' is compressed — use a "
                           "video encoder instead",
                           dstPd.name().cstr());
                return Ptr();
        }
        CSCPipeline::Ptr pipeline = CSCPipeline::cached(
                desc().pixelFormat(), dstPd, config);
        if(!pipeline || !pipeline->isValid()) return Ptr();

        ImageDesc dstDesc(desc().size(), dstPd);
        dstDesc.metadata() = metadata;
        Ptr dst = UncompressedVideoPayload::allocate(dstDesc);
        if(!dst.isValid()) return Ptr();
        Error err = pipeline->execute(*this, *dst.modify());
        if(err.isError()) return Ptr();
        return dst;
}

PaintEngine UncompressedVideoPayload::createPaintEngine() const {
        return desc().pixelFormat().createPaintEngine(*this);
}

UncompressedVideoPayload::Ptr UncompressedVideoPayload::allocate(
        const ImageDesc &desc)
{
        const PixelFormat &pd = desc.pixelFormat();
        if(!pd.isValid() || !desc.size().isValid()) return Ptr();
        const int planeCount = pd.planeCount();
        if(planeCount <= 0) return Ptr();
        BufferView planes;
        for(int i = 0; i < planeCount; ++i) {
                const size_t bytes = pd.planeSize(static_cast<size_t>(i), desc);
                if(bytes == 0) return Ptr();
                auto buf = Buffer::Ptr::create(bytes);
                buf.modify()->setSize(bytes);
                planes.pushToBack(buf, 0, bytes);
        }
        return Ptr::create(desc, planes);
}

// ============================================================================
// VariantLookup registration
// ============================================================================

PROMEKI_LOOKUP_REGISTER(VideoPayload)
        .scalar("Width",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint32_t>(p.desc().width()));
                })
        .scalar("Height",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint32_t>(p.desc().height()));
                })
        .scalar("Size",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(p.desc().size());
                })
        .scalar("PixelFormat",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(p.desc().pixelFormat());
                })
        .scalar("PixelMemLayout",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(p.desc().memLayout());
                })
        .scalar("ColorModel",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(p.desc().colorModel());
                })
        .scalar("LinePad",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(p.desc().linePad()));
                })
        .scalar("LineAlign",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(p.desc().lineAlign()));
                })
        .scalar("ScanMode",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(String(p.desc().videoScanMode().valueName()));
                })
        .scalar("PlaneCount",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(p.planeCount()));
                })
        .scalar("IsValid",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(p.isValid());
                })
        .scalar("IsCompressed",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(p.isCompressed());
                })
        .scalar("IsExclusive",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(p.isExclusive());
                })
        .database<"Metadata">("Meta",
                [](const VideoPayload &p) -> const VariantDatabase<"Metadata"> * {
                        return &p.desc().metadata();
                },
                [](VideoPayload &p) -> VariantDatabase<"Metadata"> * {
                        return &p.desc().metadata();
                });

PROMEKI_NAMESPACE_END
