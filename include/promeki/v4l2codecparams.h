/**
 * @file      v4l2codecparams.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_V4L2
#include <cstdint>
#include <linux/v4l2-controls.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Pure mappings from promeki codec parameters to V4L2 values.
 * @ingroup proav
 *
 * Keeps the H.273 → V4L2 colorimetry table and the profile/level enum
 * tables out of the encoder backend so they can be unit-tested on any
 * host (the controls themselves only take effect on a real H.264 / HEVC
 * codec node).
 */

/** @brief The four V4L2 colorimetry fields carried on a @c v4l2_format. */
struct V4l2Colorimetry {
                uint32_t colorspace = 0;   ///< @c V4L2_COLORSPACE_* (0 = DEFAULT, leave to driver).
                uint32_t ycbcrEnc = 0;     ///< @c V4L2_YCBCR_ENC_*.
                uint32_t xferFunc = 0;     ///< @c V4L2_XFER_FUNC_*.
                uint32_t quantization = 0; ///< @c V4L2_QUANTIZATION_*.
};

/**
 * @brief Maps resolved H.273 colour codepoints to V4L2 colorimetry.
 *
 * @param primaries H.273 @c colour_primaries (2 = unspecified → DEFAULT).
 * @param transfer  H.273 @c transfer_characteristics (2 = unspecified → DEFAULT).
 * @param matrix    H.273 @c matrix_coefficients (2 = unspecified → DEFAULT).
 * @param fullRange True for full-range quantization.
 *
 * @note V4L2 has no @c xfer_func for HLG (ARIB STD-B67, H.273 code 18); it
 *       maps to DEFAULT.  HDR PQ (code 16) maps to @c SMPTE2084.
 */
V4l2Colorimetry v4l2ColorimetryFromH273(uint32_t primaries, uint32_t transfer, uint32_t matrix, bool fullRange);

/**
 * @brief Maps an @ref H264Profile value to @c V4L2_CID_MPEG_VIDEO_H264_PROFILE.
 * @param h264ProfileValue @c H264Profile::value() (0 = Auto).
 * @return The V4L2 profile enum, or -1 when the control should be left unset.
 */
int v4l2H264Profile(int h264ProfileValue);

/**
 * @brief Maps a @c level_idc (level × 10, e.g. 41 for 4.1) to the V4L2 H.264 level enum.
 * @return The V4L2 level enum, or -1 when @p levelIdc is 0 / unmappable.
 */
int v4l2H264Level(int levelIdc);

/**
 * @brief Maps a HEVC profile wire token ("main", "main10", …) to
 *        @c V4L2_CID_MPEG_VIDEO_HEVC_PROFILE.
 * @return The V4L2 profile enum, or -1 when unknown.
 */
int v4l2HevcProfile(const String &wire);

/**
 * @brief Maps a @c level_idc (level × 10) to the V4L2 HEVC level enum.
 * @return The V4L2 level enum, or -1 when @p levelIdc is 0 / unmappable.
 */
int v4l2HevcLevel(int levelIdc);

/**
 * @brief Builds the @c V4L2_CID_COLORIMETRY_HDR10_MASTERING_DISPLAY payload.
 *
 * Converts CIE xy chromaticities to 0.00002 units (×50000) and luminance
 * to 0.0001 cd/m² units (×10000), in the SMPTE ST 2086 / HEVC SEI green,
 * blue, red primary order — matching @ref VideoEncoderSei::masteringDisplay.
 */
struct v4l2_ctrl_hdr10_mastering_display v4l2MakeMasteringDisplay(const MasteringDisplay &md);

/**
 * @brief Builds the @c V4L2_CID_COLORIMETRY_HDR10_CLL_INFO payload.
 *
 * MaxCLL / MaxFALL in 1 cd/m² units, clamped to the 16-bit field range.
 */
struct v4l2_ctrl_hdr10_cll_info v4l2MakeCllInfo(const ContentLightLevel &cll);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
