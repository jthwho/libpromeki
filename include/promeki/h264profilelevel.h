/**
 * @file      h264profilelevel.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/enums_codec.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Serialization layer between H.264 config strings and @ref H264Profile.
 * @ingroup proav
 *
 * The H.264 profile vocabulary is a first-class @ref H264Profile
 * @c TypedEnum (CamelCase identifiers, validation / display / toString
 * for free).  The on-the-wire / config form, however, is the lowercase
 * x264 / ffmpeg token (@c "high422", @c "main", …) carried in the
 * codec-agnostic String key @ref MediaConfig::VideoProfile.  This class
 * is the single place that maps between the two — keeping the wire-form
 * mapping out of the enum itself — plus the geometry-based profile
 * auto-selection and the @c VideoLevel string → @c level_idc parser.
 *
 * @par Level numbering
 *
 * @ref levelIdc returns the H.264 @c level_idc (level number × 10, e.g.
 * @c "4.1" → @c 41).  Level @c "1b" returns @c 9 — the x264 sentinel for
 * 1b, and also the value of @c NV_ENC_LEVEL_H264_1b — so the single
 * number feeds both @c x264_param_t::i_level_idc and a cast to
 * @c NV_ENC_LEVEL_H264_*.  An empty or unrecognised string returns @c 0,
 * which both back-ends treat as "auto-select".
 *
 * @par Thread Safety
 * Stateless; all methods are pure.
 */
class H264ProfileLevel {
        public:
                /**
                 * @brief Maps a @c VideoProfile wire token to an @ref H264Profile.
                 * @param wire The lowercase profile token (@c "baseline",
                 *             @c "main", @c "high", @c "high10",
                 *             @c "high422", @c "high444", @c "progressive").
                 * @return The matching @ref H264Profile; @ref H264Profile::Auto
                 *         for an empty string or an unrecognised token (the
                 *         backend then derives a profile from input geometry
                 *         via @ref autoProfile).
                 */
                static H264Profile profileFromWire(const String &wire);

                /**
                 * @brief Maps an @ref H264Profile back to its wire token.
                 * @param profile The profile to serialize.
                 * @return The lowercase token (suitable for
                 *         @c x264_param_apply_profile).
                 *         @ref H264Profile::ProgressiveHigh maps to
                 *         @c "high" (x264 has no distinct token);
                 *         @ref H264Profile::Auto and invalid profiles map to
                 *         an empty string ("no profile constraint").
                 */
                static String profileToWire(H264Profile profile);

                /**
                 * @brief Derives a concrete profile from input geometry.
                 *
                 * For use when the caller left @c VideoProfile empty
                 * (@ref H264Profile::Auto): 4:4:4 → @ref H264Profile::High444,
                 * 4:2:2 → @ref H264Profile::High422, 10-bit →
                 * @ref H264Profile::High10, otherwise @ref H264Profile::High.
                 *
                 * @param chromaFormatIDC 1 = 4:2:0, 2 = 4:2:2, 3 = 4:4:4.
                 * @param bitDepth        Bits per component (8 / 10 / …).
                 */
                static H264Profile autoProfile(int chromaFormatIDC, int bitDepth);

                /**
                 * @brief Parses a @c VideoLevel string into an H.264 @c level_idc.
                 * @param level The level string (@c "4.1", @c "1b", @c "5",
                 *              …); empty or unrecognised → @c 0 (auto).
                 * @return The @c level_idc (level × 10; @c "1b" → @c 9), or
                 *         @c 0 to mean auto-select.
                 */
                static int levelIdc(const String &level);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
