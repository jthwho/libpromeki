/**
 * @file      imagedesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/imagedesc.h>
#include <promeki/sdpsession.h>
#include <promeki/colormodel.h>

PROMEKI_NAMESPACE_BEGIN

// ST 2110-20 colorimetry string from a ColorModel.
static const char *sdpColorimetry(const ColorModel &cm) {
        switch(cm.id()) {
                case ColorModel::YCbCr_Rec601:
                case ColorModel::Rec601_PAL:
                case ColorModel::Rec601_NTSC:
                        return "BT601-5";
                case ColorModel::YCbCr_Rec709:
                case ColorModel::Rec709:
                case ColorModel::sRGB:
                        return "BT709-2";
                case ColorModel::Rec2020:
                        return "BT2020";
                default:
                        return nullptr;
        }
}

// Determine quantization range from the PixelDesc's first component
// semantics.  Full range: min=0, limited: min>0 (typically 16).
static const char *sdpRange(const PixelDesc &pd) {
        const PixelDesc::CompSemantic &cs = pd.compSemantic(0);
        // RGB is always full range; for YCbCr, check the luma floor.
        if(pd.colorModel().type() == ColorModel::TypeRGB) return "FULL";
        return (cs.rangeMin > 0.0f) ? "NARROW" : "FULL";
}

// Map ST 2110-20 colorimetry + RANGE + subsampling to a JPEG PixelDesc.
// @p is420 selects 4:2:0 vs 4:2:2; @p isRgb overrides both to RGB.
// Returns PixelDesc::Invalid for unrecognised combinations.
static PixelDesc::ID jpegPixelDescFromColorimetry(
                const String &colorimetry,
                const String &range,
                bool is420,
                bool isRgb) {
        if(isRgb) return PixelDesc::JPEG_RGB8_sRGB;

        bool full = range.isEmpty() || range == "FULL";

        if(colorimetry.isEmpty() || colorimetry == "BT601-5") {
                if(is420) return full ? PixelDesc::JPEG_YUV8_420_Rec601_Full
                                     : PixelDesc::JPEG_YUV8_420_Rec601;
                return full ? PixelDesc::JPEG_YUV8_422_Rec601_Full
                            : PixelDesc::JPEG_YUV8_422_Rec601;
        }
        if(colorimetry == "BT709-2" || colorimetry == "BT709") {
                if(is420) return full ? PixelDesc::JPEG_YUV8_420_Rec709_Full
                                     : PixelDesc::JPEG_YUV8_420_Rec709;
                return full ? PixelDesc::JPEG_YUV8_422_Rec709_Full
                            : PixelDesc::JPEG_YUV8_422_Rec709;
        }
        // Unknown colorimetry — fall back to Rec.601 full (JFIF default).
        if(is420) return full ? PixelDesc::JPEG_YUV8_420_Rec601_Full
                             : PixelDesc::JPEG_YUV8_420_Rec601;
        return full ? PixelDesc::JPEG_YUV8_422_Rec601_Full
                    : PixelDesc::JPEG_YUV8_422_Rec601;
}

// Map an fmtp "sampling=YCbCr-4:2:2" + "depth=N" combo to a JPEG XS
// PixelDesc.  Returns PixelDesc::Invalid for any unrecognised
// combination so the caller can fall back to the library default.
static PixelDesc::ID jpegXsPixelDescFromFmtp(const String &sampling,
                                              const String &depth) {
        int d = depth.toInt();
        if(sampling == "YCbCr-4:2:2") {
                if(d == 8)  return PixelDesc::JPEG_XS_YUV8_422_Rec709;
                if(d == 10) return PixelDesc::JPEG_XS_YUV10_422_Rec709;
                if(d == 12) return PixelDesc::JPEG_XS_YUV12_422_Rec709;
        } else if(sampling == "YCbCr-4:2:0") {
                if(d == 8)  return PixelDesc::JPEG_XS_YUV8_420_Rec709;
                if(d == 10) return PixelDesc::JPEG_XS_YUV10_420_Rec709;
                if(d == 12) return PixelDesc::JPEG_XS_YUV12_420_Rec709;
        }
        return PixelDesc::Invalid;
}

ImageDesc ImageDesc::fromSdp(const SdpMediaDescription &md) {
        if(md.mediaType() != "video") return ImageDesc();

        SdpMediaDescription::RtpMap rm = md.rtpMap();
        if(!rm.valid) return ImageDesc();

        if(rm.encoding == "jxsv") {
                // RFC 9134 JPEG XS.  Geometry + sampling + depth
                // live in the fmtp line.  Fall back to the library
                // default PixelDesc (10-bit 4:2:2 Rec.709) when the
                // fmtp combo is incomplete or unrecognised — that is
                // the most common ST 2110-22 shape and gives a
                // useful answer even for terse SDPs.
                auto params = md.fmtpParameters();
                int w = params.value("width").toInt();
                int h = params.value("height").toInt();
                if(w <= 0 || h <= 0) return ImageDesc();

                PixelDesc::ID pdId = jpegXsPixelDescFromFmtp(
                        params.value("sampling"),
                        params.value("depth"));
                if(pdId == PixelDesc::Invalid) {
                        pdId = PixelDesc::JPEG_XS_YUV10_422_Rec709;
                }
                return ImageDesc(Size2Du32(static_cast<uint32_t>(w),
                                            static_cast<uint32_t>(h)),
                                  PixelDesc(pdId));
        }

        if(rm.encoding == "raw") {
                // RFC 4175 uncompressed video.  Geometry, sampling,
                // depth, colorimetry, and range all live in the fmtp
                // line, following the ST 2110-20 parameter set.
                auto params = md.fmtpParameters();
                int w = params.value("width").toInt();
                int h = params.value("height").toInt();
                if(w <= 0 || h <= 0) return ImageDesc();

                String sampling    = params.value("sampling");
                String depthStr    = params.value("depth");
                String colorimetry = params.value("colorimetry");
                String range       = params.value("RANGE");
                int depth = depthStr.toInt();
                if(depth <= 0) depth = 8;

                bool full = range.isEmpty() || range == "FULL";
                bool isRec709 = colorimetry == "BT709-2" || colorimetry == "BT709";

                PixelDesc::ID pdId = PixelDesc::Invalid;
                if(sampling == "RGBA") {
                        if(depth == 8) pdId = PixelDesc::RGBA8_sRGB;
                } else if(sampling == "RGB") {
                        if(depth == 8) pdId = PixelDesc::RGB8_sRGB;
                } else if(sampling == "YCbCr-4:2:2") {
                        // RFC 4175 wire format is Cb-Y-Cr-Y (UYVY)
                        // for all 4:2:2 depths.
                        if(depth == 8) {
                                if(isRec709) pdId = PixelDesc::YUV8_422_UYVY_Rec709;
                                else         pdId = PixelDesc::YUV8_422_UYVY_Rec601;
                        } else if(depth == 10) {
                                pdId = PixelDesc::YUV10_422_UYVY_LE_Rec709;
                        }
                } else if(sampling == "YCbCr-4:2:0") {
                        if(depth == 8) {
                                pdId = PixelDesc::YUV8_420_Planar_Rec709;
                        }
                }
                // Full-range YCbCr variants are not yet in the
                // uncompressed PixelDesc catalog, so the RANGE
                // parameter is noted but does not change the ID.
                // RGB is always full range regardless.
                (void)full;

                if(pdId == PixelDesc::Invalid) return ImageDesc();
                return ImageDesc(Size2Du32(static_cast<uint32_t>(w),
                                            static_cast<uint32_t>(h)),
                                  PixelDesc(pdId));
        }

        // "JPEG" (RFC 2435) carries geometry in the packet header,
        // not SDP.  Return invalid so the caller knows to look
        // elsewhere (deferred geometry in the RTP reader).
        return ImageDesc();
}

PixelDesc::ID ImageDesc::jpegPixelDescFromSdp(
                const String &colorimetry,
                const String &range,
                bool is420,
                bool isRgb) {
        return jpegPixelDescFromColorimetry(colorimetry, range, is420, isRgb);
}

SdpMediaDescription ImageDesc::toSdp(uint8_t payloadType) const {
        if(!isValid()) return SdpMediaDescription();

        const PixelDesc &pd = pixelDesc();
        const char *colorimetry = sdpColorimetry(pd.colorModel());
        const char *range       = sdpRange(pd);

        SdpMediaDescription md;
        md.setMediaType("video");
        md.setProtocol("RTP/AVP");

        int w = static_cast<int>(width());
        int h = static_cast<int>(height());

        if(pd.isCompressed() && pd.videoCodec().id() == VideoCodec::JPEG) {
                // RFC 2435 MJPEG.  Geometry is in-band (packet
                // header), so the rtpmap is just JPEG/90000.  We
                // emit colorimetry and RANGE as fmtp extensions
                // following the ST 2110-20 convention so receivers
                // that understand them can apply the correct
                // matrix and quantization range.
                uint8_t pt = payloadType;
                if(pt == 96) pt = 26; // static PT for JPEG
                md.addPayloadType(pt);
                md.setAttribute("rtpmap", String::number(pt) +
                                String(" JPEG/90000"));
                String fmtp;
                if(colorimetry != nullptr) {
                        fmtp += String("colorimetry=") + String(colorimetry);
                }
                if(range != nullptr) {
                        if(!fmtp.isEmpty()) fmtp += String(";");
                        fmtp += String("RANGE=") + String(range);
                }
                if(!fmtp.isEmpty()) {
                        md.setAttribute("fmtp", String::number(pt) +
                                        String(" ") + fmtp);
                }
        } else if(pd.isCompressed() && pd.videoCodec().id() == VideoCodec::JPEG_XS) {
                // RFC 9134 JPEG XS.
                md.addPayloadType(payloadType);
                String ptStr = String::number(payloadType);
                md.setAttribute("rtpmap", ptStr + String(" jxsv/90000"));

                const char *sampling = nullptr;
                int depth = 0;
                switch(pd.id()) {
                        case PixelDesc::JPEG_XS_YUV8_422_Rec709:
                                sampling = "YCbCr-4:2:2"; depth = 8;  break;
                        case PixelDesc::JPEG_XS_YUV10_422_Rec709:
                                sampling = "YCbCr-4:2:2"; depth = 10; break;
                        case PixelDesc::JPEG_XS_YUV12_422_Rec709:
                                sampling = "YCbCr-4:2:2"; depth = 12; break;
                        case PixelDesc::JPEG_XS_YUV8_420_Rec709:
                                sampling = "YCbCr-4:2:0"; depth = 8;  break;
                        case PixelDesc::JPEG_XS_YUV10_420_Rec709:
                                sampling = "YCbCr-4:2:0"; depth = 10; break;
                        case PixelDesc::JPEG_XS_YUV12_420_Rec709:
                                sampling = "YCbCr-4:2:0"; depth = 12; break;
                        default:
                                return SdpMediaDescription();
                }
                String fmtp = String("packetmode=0;rate=90000");
                fmtp += String(";sampling=")    + String(sampling);
                fmtp += String(";depth=")       + String::number(depth);
                fmtp += String(";width=")       + String::number(w);
                fmtp += String(";height=")      + String::number(h);
                if(colorimetry != nullptr) {
                        fmtp += String(";colorimetry=") + String(colorimetry);
                }
                if(range != nullptr) {
                        fmtp += String(";RANGE=") + String(range);
                }
                md.setAttribute("fmtp", ptStr + String(" ") + fmtp);
        } else if(!pd.isCompressed()) {
                // RFC 4175 raw uncompressed video.
                md.addPayloadType(payloadType);
                String ptStr = String::number(payloadType);
                md.setAttribute("rtpmap", ptStr + String(" raw/90000"));

                // Derive sampling from the pixel format's subsampling mode.
                const char *sampling = nullptr;
                const PixelFormat &pf = pd.pixelFormat();
                if(pd.colorModel().type() == ColorModel::TypeRGB) {
                        sampling = pd.hasAlpha() ? "RGBA" : "RGB";
                } else {
                        switch(pf.sampling()) {
                                case PixelFormat::Sampling422: sampling = "YCbCr-4:2:2"; break;
                                case PixelFormat::Sampling420: sampling = "YCbCr-4:2:0"; break;
                                case PixelFormat::Sampling411: sampling = "YCbCr-4:1:1"; break;
                                default:                       sampling = "YCbCr-4:4:4"; break;
                        }
                }

                // ST 2110-20 depth is bits per component, not bits
                // per pixel.  Read it from the first component
                // descriptor.
                int depth = (pf.compCount() > 0)
                        ? static_cast<int>(pf.compDesc(0).bits)
                        : 8;

                String fmtp;
                fmtp += String("sampling=")     + String(sampling);
                fmtp += String(";depth=")       + String::number(depth);
                fmtp += String(";width=")       + String::number(w);
                fmtp += String(";height=")      + String::number(h);
                if(colorimetry != nullptr) {
                        fmtp += String(";colorimetry=") + String(colorimetry);
                }
                if(range != nullptr) {
                        fmtp += String(";RANGE=") + String(range);
                }
                md.setAttribute("fmtp", ptStr + String(" ") + fmtp);
        } else {
                return SdpMediaDescription();
        }

        return md;
}

PROMEKI_NAMESPACE_END
