/**
 * @file      v4l2codecparams.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_V4L2

#include <algorithm>
#include <cmath>
#include <cstring>
#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

#include <promeki/v4l2codecparams.h>
#include <promeki/ciepoint.h>

PROMEKI_NAMESPACE_BEGIN

V4l2Colorimetry v4l2ColorimetryFromH273(uint32_t primaries, uint32_t transfer, uint32_t matrix, bool fullRange) {
        V4l2Colorimetry c;

        // colour_primaries → V4L2_COLORSPACE_*
        switch (primaries) {
                case 1: c.colorspace = V4L2_COLORSPACE_REC709; break;          // BT.709
                case 4: c.colorspace = V4L2_COLORSPACE_470_SYSTEM_M; break;    // BT.470 System M
                case 5: c.colorspace = V4L2_COLORSPACE_470_SYSTEM_BG; break;   // BT.601 625
                case 6: c.colorspace = V4L2_COLORSPACE_SMPTE170M; break;       // BT.601 525
                case 7: c.colorspace = V4L2_COLORSPACE_SMPTE240M; break;       // SMPTE 240M
                case 9: c.colorspace = V4L2_COLORSPACE_BT2020; break;          // BT.2020
                default: c.colorspace = V4L2_COLORSPACE_DEFAULT; break;        // unspecified / unknown
        }

        // matrix_coefficients → V4L2_YCBCR_ENC_*
        switch (matrix) {
                case 1: c.ycbcrEnc = V4L2_YCBCR_ENC_709; break;                 // BT.709
                case 5:
                case 6: c.ycbcrEnc = V4L2_YCBCR_ENC_601; break;                 // BT.601
                case 7: c.ycbcrEnc = V4L2_YCBCR_ENC_SMPTE240M; break;           // SMPTE 240M
                case 9: c.ycbcrEnc = V4L2_YCBCR_ENC_BT2020; break;              // BT.2020 non-constant lum.
                case 10: c.ycbcrEnc = V4L2_YCBCR_ENC_BT2020_CONST_LUM; break;   // BT.2020 constant lum.
                default: c.ycbcrEnc = V4L2_YCBCR_ENC_DEFAULT; break;
        }

        // transfer_characteristics → V4L2_XFER_FUNC_*
        switch (transfer) {
                case 1:
                case 6:
                case 14:
                case 15: c.xferFunc = V4L2_XFER_FUNC_709; break;                // BT.709 / BT.2020 10/12-bit
                case 7: c.xferFunc = V4L2_XFER_FUNC_SMPTE240M; break;           // SMPTE 240M
                case 8: c.xferFunc = V4L2_XFER_FUNC_NONE; break;                // linear
                case 13: c.xferFunc = V4L2_XFER_FUNC_SRGB; break;               // IEC 61966-2-1 (sRGB)
                case 16: c.xferFunc = V4L2_XFER_FUNC_SMPTE2084; break;          // PQ (HDR10)
                // 18 (ARIB STD-B67 / HLG) has no V4L2 transfer function.
                default: c.xferFunc = V4L2_XFER_FUNC_DEFAULT; break;
        }

        c.quantization = fullRange ? V4L2_QUANTIZATION_FULL_RANGE : V4L2_QUANTIZATION_LIM_RANGE;
        return c;
}

int v4l2H264Profile(int h264ProfileValue) {
        // Mirrors H264Profile: 1=Baseline 2=Main 3=High 4=High10 5=High422
        // 6=High444 7=ProgressiveHigh; 0=Auto → unset.
        switch (h264ProfileValue) {
                case 1: return V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
                case 2: return V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
                case 3:
                case 7: return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH; // ProgressiveHigh → High
                case 4: return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10;
                case 5: return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422;
                case 6: return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE;
                default: return -1;
        }
}

int v4l2H264Level(int levelIdc) {
        switch (levelIdc) {
                case 10: return V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
                case 11: return V4L2_MPEG_VIDEO_H264_LEVEL_1_1;
                case 12: return V4L2_MPEG_VIDEO_H264_LEVEL_1_2;
                case 13: return V4L2_MPEG_VIDEO_H264_LEVEL_1_3;
                case 20: return V4L2_MPEG_VIDEO_H264_LEVEL_2_0;
                case 21: return V4L2_MPEG_VIDEO_H264_LEVEL_2_1;
                case 22: return V4L2_MPEG_VIDEO_H264_LEVEL_2_2;
                case 30: return V4L2_MPEG_VIDEO_H264_LEVEL_3_0;
                case 31: return V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
                case 32: return V4L2_MPEG_VIDEO_H264_LEVEL_3_2;
                case 40: return V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
                case 41: return V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
                case 42: return V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
                case 50: return V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
                case 51: return V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
                case 52: return V4L2_MPEG_VIDEO_H264_LEVEL_5_2;
                case 60: return V4L2_MPEG_VIDEO_H264_LEVEL_6_0;
                case 61: return V4L2_MPEG_VIDEO_H264_LEVEL_6_1;
                case 62: return V4L2_MPEG_VIDEO_H264_LEVEL_6_2;
                default: return -1;
        }
}

int v4l2HevcProfile(const String &wire) {
        const String w = wire.toLower();
        if (w == "main") return V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN;
        if (w == "main10") return V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10;
        if (w == "mainstillpicture" || w == "mainstill") {
                return V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE;
        }
        // Main 4:2:2 10 has no mainline V4L2 enum — leave unset.
        return -1;
}

int v4l2HevcLevel(int levelIdc) {
        switch (levelIdc) {
                case 10: return V4L2_MPEG_VIDEO_HEVC_LEVEL_1;
                case 20: return V4L2_MPEG_VIDEO_HEVC_LEVEL_2;
                case 21: return V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1;
                case 30: return V4L2_MPEG_VIDEO_HEVC_LEVEL_3;
                case 31: return V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1;
                case 40: return V4L2_MPEG_VIDEO_HEVC_LEVEL_4;
                case 41: return V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1;
                case 50: return V4L2_MPEG_VIDEO_HEVC_LEVEL_5;
                case 51: return V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1;
                case 52: return V4L2_MPEG_VIDEO_HEVC_LEVEL_5_2;
                case 60: return V4L2_MPEG_VIDEO_HEVC_LEVEL_6;
                case 61: return V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1;
                case 62: return V4L2_MPEG_VIDEO_HEVC_LEVEL_6_2;
                default: return -1;
        }
}

struct v4l2_ctrl_hdr10_mastering_display v4l2MakeMasteringDisplay(const MasteringDisplay &md) {
        struct v4l2_ctrl_hdr10_mastering_display m;
        std::memset(&m, 0, sizeof(m));
        // CIE xy → 0.00002 units (×50000); SEI primary order is green, blue, red.
        auto chroma = [](double c) -> uint16_t {
                double v = std::lround(c * 50000.0);
                if (v < 0) v = 0;
                if (v > 65535) v = 65535;
                return static_cast<uint16_t>(v);
        };
        m.display_primaries_x[0] = chroma(md.green().x());
        m.display_primaries_y[0] = chroma(md.green().y());
        m.display_primaries_x[1] = chroma(md.blue().x());
        m.display_primaries_y[1] = chroma(md.blue().y());
        m.display_primaries_x[2] = chroma(md.red().x());
        m.display_primaries_y[2] = chroma(md.red().y());
        m.white_point_x = chroma(md.whitePoint().x());
        m.white_point_y = chroma(md.whitePoint().y());
        // Luminance → 0.0001 cd/m² units (×10000); both fields are u32.
        m.max_display_mastering_luminance =
                static_cast<uint32_t>(std::lround(md.maxLuminance() * 10000.0));
        m.min_display_mastering_luminance =
                static_cast<uint32_t>(std::lround(md.minLuminance() * 10000.0));
        return m;
}

struct v4l2_ctrl_hdr10_cll_info v4l2MakeCllInfo(const ContentLightLevel &cll) {
        struct v4l2_ctrl_hdr10_cll_info c;
        std::memset(&c, 0, sizeof(c));
        c.max_content_light_level = static_cast<uint16_t>(std::min(cll.maxCLL(), uint32_t(65535)));
        c.max_pic_average_light_level = static_cast<uint16_t>(std::min(cll.maxFALL(), uint32_t(65535)));
        return c;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
