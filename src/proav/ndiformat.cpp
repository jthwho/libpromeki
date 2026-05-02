/**
 * @file      ndiformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <promeki/ndiformat.h>

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
                default:
                        return 0;
        }
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
