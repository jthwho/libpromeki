/**
 * @file      ndiformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <promeki/ndiformat.h>

#include <promeki/enums_color.h>
#include <promeki/xml.h>

#include <Processing.NDI.Lib.h>

PROMEKI_NAMESPACE_BEGIN

PixelFormat::ID NdiFormat::fourccToPixelFormat(uint32_t fourcc, BitDepth bitDepth) {
        switch (fourcc) {
                case NDIlib_FourCC_video_type_UYVY:
                        return PixelFormat::YUV8_422_UYVY_Rec709;
                case NDIlib_FourCC_video_type_UYVA:
                        // NDI lays UYVA out as a UYVY plane followed by
                        // an 8-bit alpha plane in the same buffer.  We
                        // alias to the alpha-less UYVY ID so the
                        // capture path can copy the UYVY plane verbatim
                        // and drop the alpha — none of the existing
                        // sinks consume an NDI alpha channel.  Adding
                        // a dedicated YUV+A pixel format would be the
                        // right next step if a sink ever needs it.
                        return PixelFormat::YUV8_422_UYVY_Rec709;
                case NDIlib_FourCC_video_type_NV12:
                        return PixelFormat::YUV8_420_SemiPlanar_Rec709;
                case NDIlib_FourCC_video_type_I420:
                        return PixelFormat::YUV8_420_Planar_Rec709;
                case NDIlib_FourCC_video_type_BGRA:
                        return PixelFormat::BGRA8_sRGB;
                case NDIlib_FourCC_video_type_RGBA:
                        return PixelFormat::RGBA8_sRGB;
                case NDIlib_FourCC_video_type_BGRX:
                        // No dedicated BGRX8_sRGB ID exists — alias to BGRA8
                        // and treat the X byte as opaque alpha (0xFF).  The
                        // sender / receiver code is responsible for honoring
                        // the X-vs-A semantics on the wire.
                        return PixelFormat::BGRA8_sRGB;
                case NDIlib_FourCC_video_type_P216:
                        // P216 is a 16-bit container with implicit precision.
                        // The bit-depth selector picks the matching promeki
                        // format that shares its wire layout.
                        switch (bitDepth) {
                                case BitDepth10:
                                        return PixelFormat::YUV10_422_SemiPlanar_LE_Rec709;
                                case BitDepth12:
                                        return PixelFormat::YUV12_422_SemiPlanar_LE_Rec709;
                                case BitDepth16:
                                case BitDepthAuto:
                                default:
                                        return PixelFormat::YUV16_422_SemiPlanar_LE_Rec709;
                        }
                default:
                        // YV12, UYVA, PA16 are intentionally not in this table —
                        // see docs/ndi.md "deferred" list.  Return Invalid so
                        // callers can fall through to a clear error message
                        // naming the unsupported FourCC.
                        return PixelFormat::Invalid;
        }
}

uint32_t NdiFormat::pixelFormatToFourcc(PixelFormat::ID id) {
        switch (id) {
                case PixelFormat::YUV8_422_UYVY_Rec709:
                        return NDIlib_FourCC_video_type_UYVY;
                case PixelFormat::YUV8_420_SemiPlanar_Rec709:
                        return NDIlib_FourCC_video_type_NV12;
                case PixelFormat::YUV8_420_Planar_Rec709:
                        return NDIlib_FourCC_video_type_I420;
                case PixelFormat::BGRA8_sRGB:
                        return NDIlib_FourCC_video_type_BGRA;
                case PixelFormat::RGBA8_sRGB:
                        return NDIlib_FourCC_video_type_RGBA;
                // All three high-bit-depth semi-planar 4:2:2 entries map to
                // P216 — the wire layout is identical (16-bit container, LE),
                // and the precision of the meaningful bits is a per-stream
                // convention NDI does not signal in-band.
                case PixelFormat::YUV10_422_SemiPlanar_LE_Rec709:
                case PixelFormat::YUV12_422_SemiPlanar_LE_Rec709:
                case PixelFormat::YUV16_422_SemiPlanar_LE_Rec709:
                        return NDIlib_FourCC_video_type_P216;
                // HDR variants share the SDR wire layout — the bound
                // HDR @ref ColorModel travels with the buffer.  NDI
                // signals BT.2020 / PQ / HLG out-of-band via the
                // @c FrameSyncV2 colour gamut field; the actual byte
                // layout on the wire is identical to the SDR sibling
                // so reuse the same FourCC.
                case PixelFormat::YUV16_422_SemiPlanar_LE_Rec2020_PQ:
                case PixelFormat::YUV16_422_SemiPlanar_LE_Rec2020_HLG:
                        return NDIlib_FourCC_video_type_P216;
                default:
                        return 0;
        }
}

PixelFormat::ID NdiFormat::upgradeForHdrMetadata(PixelFormat::ID sdrId, const String &xmlMetadata) {
        // Fast-out paths.  Empty / missing metadata means the sender
        // did not signal a colour description — leave the SDR id
        // alone, matching pollSourceVpid's "no opinion" semantics.
        if (xmlMetadata.isEmpty()) return sdrId;
        if (!xmlMetadata.contains("ndi_color_info")) return sdrId;

        // Only the P216 family carries enough bit depth for HDR over
        // NDI today.  Other receive IDs (UYVY 8-bit, NV12, I420,
        // BGRA / RGBA) are SDR-only on the wire so skip the parse
        // overhead.
        if (sdrId != PixelFormat::YUV10_422_SemiPlanar_LE_Rec709 &&
            sdrId != PixelFormat::YUV12_422_SemiPlanar_LE_Rec709 &&
            sdrId != PixelFormat::YUV16_422_SemiPlanar_LE_Rec709) {
                return sdrId;
        }

        // NDI per-frame metadata is allowed to contain multiple
        // top-level tags (XML fragment, not a strict document); the
        // colour-info tag may be a sibling of other tags such as
        // <ndi_capture_info ...>.  Wrap in a synthetic root so
        // XmlElement::parse accepts the fragment, then walk children
        // looking for the colour-info tag.
        const String wrapped = String("<ndi>") + xmlMetadata + String("</ndi>");
        XmlElement   root    = XmlElement::parse(wrapped);
        if (!root.isValid()) return sdrId;

        // Walk top-level children for the colour-info tag.  Use the
        // child() lookup directly since the tag name is fixed and
        // we do not care about ordering or duplicates (last write
        // wins on conflict, which mirrors the SDK convention).
        XmlElement info = root.child("ndi_color_info");
        if (!info.isValid()) return sdrId;

        Error      err;
        const String transferStr = info.attribute("transfer_function", &err);
        if (err.isError() || transferStr.isEmpty()) return sdrId;
        const int transferCode = transferStr.toInt(&err);
        if (err.isError()) return sdrId;

        const bool isPq  = (transferCode == TransferCharacteristics::SMPTE2084.value());
        const bool isHlg = (transferCode == TransferCharacteristics::ARIB_STD_B67.value());
        if (!isPq && !isHlg) return sdrId;

        // Upgrade to the matching HDR P216 sibling.  Bit depth
        // selection is preserved (10 / 12 / 16) — only the bound
        // ColorModel changes.  We have HDR variants of the 16-bit
        // entry today; the 10/12-bit HDR siblings would be added
        // to the catalog when bandwidth budgets matter on the wire.
        // For now any P216 bit-depth claim upgrades to YUV16 HDR —
        // the bytes on the wire are identical (16-bit container),
        // only the conceptual precision metadata differs.
        return isPq ? PixelFormat::YUV16_422_SemiPlanar_LE_Rec2020_PQ
                    : PixelFormat::YUV16_422_SemiPlanar_LE_Rec2020_HLG;
}

String NdiFormat::fourccToString(uint32_t fourcc) {
        // FourCCs are ASCII byte tags packed little-endian into a uint32.
        // NDI_LIB_FOURCC('U','Y','V','Y') = 'U' | ('Y' << 8) | ('V' << 16) | ('Y' << 24).
        char ascii[5] = {static_cast<char>(fourcc & 0xff),
                         static_cast<char>((fourcc >> 8) & 0xff),
                         static_cast<char>((fourcc >> 16) & 0xff),
                         static_cast<char>((fourcc >> 24) & 0xff),
                         '\0'};

        // Sanity check the bytes — if anything looks non-printable
        // fall back to a hex dump so log lines stay readable.
        for (int i = 0; i < 4; ++i) {
                unsigned char b = static_cast<unsigned char>(ascii[i]);
                if (b < 0x20 || b > 0x7e) {
                        return String::sprintf("0x%08x", fourcc);
                }
        }
        return String(ascii);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NDI
