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
//
// VideoPayload surfaces every @ref ImageDesc field directly as a
// first-class scalar on the payload — no @c Desc.* composition, no
// second lookup hop.  In the payload context the descriptor is an
// implementation detail, so queries like @c Video[0].Width stay flat.
//
// @c Meta.* is @b not re-registered here.  @ref MediaPayload's Meta
// binding lives on the base and resolves through the virtual
// @ref MediaPayload::metadata, which @ref VideoPayload overrides to
// return @c desc().metadata() — so the cascade already hands back
// descriptor metadata on video payloads without a second binding.
// ============================================================================

PROMEKI_LOOKUP_REGISTER(VideoPayload)
        .inheritsFrom<MediaPayload>()
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
        // ImageDesc's "format declares N planes" vs MediaPayload's
        // "payload carries M buffer slices" are genuinely different —
        // the former may say 3 for YUV-planar while the latter says 1
        // for a single-buffer packed read.  Expose the format-side
        // count under a distinct name so the two don't shadow.
        .scalar("FormatPlaneCount",
                [](const VideoPayload &p) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(p.desc().planeCount()));
                });

// No concrete-leaf fields to add on top of VideoPayload — the leaf
// exists purely to anchor the @c variantLookupResolve dispatch on the
// uncompressed side.  inheritsFrom<VideoPayload>() gives it the
// complete set of inherited keys.
PROMEKI_LOOKUP_REGISTER(UncompressedVideoPayload)
        .inheritsFrom<VideoPayload>();

PROMEKI_NAMESPACE_END
