/**
 * @file      videoformat.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/size2d.h>
#include <promeki/framerate.h>
#include <promeki/enums.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief X-macro defining the well-known video raster sizes.
 * @ingroup video
 *
 * Each entry has the form @c X(EnumID, CanonicalName, Width, Height).
 * The macro is expanded in both @ref VideoFormat::WellKnownRaster
 * (enum generation) and in @c videoformat.cpp (registry data).
 *
 * Only canonical names appear here.  Additional parsing aliases
 * ("NTSC", "PAL", "1080", "2160", "4KUHD", etc.) are listed in the
 * source file.
 */
#define PROMEKI_WELL_KNOWN_RASTERS                                                                                     \
        X(Raster_Invalid, "INV", 0, 0)                                                                                 \
        X(Raster_SD525, "SD525", 720, 486)                                                                             \
        X(Raster_SD625, "SD625", 720, 576)                                                                             \
        X(Raster_HD720, "HD720", 1280, 720)                                                                            \
        X(Raster_HD, "HD", 1920, 1080)                                                                                 \
        X(Raster_2K, "2K", 2048, 1080)                                                                                 \
        X(Raster_QHD, "QHD", 2560, 1440)                                                                               \
        X(Raster_UHD, "UHD", 3840, 2160)                                                                               \
        X(Raster_4K, "4K", 4096, 2160)                                                                                 \
        X(Raster_UHD8K, "UHD8K", 7680, 4320)                                                                           \
        X(Raster_8K, "8K", 8192, 4320)

/**
 * @brief X-macro defining well-known video formats.
 * @ingroup video
 *
 * Each entry has the form
 * @c X(EnumID, RasterID, FrameRateID, ScanMode, Flags).
 *
 * - @c EnumID — unqualified @ref VideoFormat::WellKnownFormat identifier
 *   (e.g. @c Smpte1080p29_97).
 * - @c RasterID — unqualified @ref VideoFormat::WellKnownRaster id
 *   (e.g. @c Raster_HD).
 * - @c FrameRateID — unqualified @ref FrameRate::WellKnownRate id
 *   (e.g. @c FPS_29_97).
 * - @c ScanMode — unqualified @ref VideoScanMode static member
 *   (e.g. @c Progressive, @c Interlaced, @c PsF).
 * - @c Flags — bitwise-OR of @ref VideoFormat::WellKnownFormatFlag
 *   values describing the format family.
 *
 * Expanded in both @ref VideoFormat::WellKnownFormat (enum generation)
 * and in @c videoformat.cpp (registry data).
 */
#define PROMEKI_WELL_KNOWN_VIDEO_FORMATS                                                                               \
        X(Smpte486i59_94, Raster_SD525, FPS_29_97, Interlaced, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Sd)     \
        X(Smpte576i50, Raster_SD625, FPS_25, Interlaced, FormatFlag_Smpte | FormatFlag_Pal | FormatFlag_Sd)            \
        X(Smpte720p50, Raster_HD720, FPS_50, Progressive, FormatFlag_Smpte | FormatFlag_Pal | FormatFlag_Hd)           \
        X(Smpte720p59_94, Raster_HD720, FPS_59_94, Progressive, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Hd)    \
        X(Smpte720p60, Raster_HD720, FPS_60, Progressive, FormatFlag_Smpte | FormatFlag_Hd)                            \
        X(Smpte1080i50, Raster_HD, FPS_25, Interlaced, FormatFlag_Smpte | FormatFlag_Pal | FormatFlag_Hd)              \
        X(Smpte1080i59_94, Raster_HD, FPS_29_97, Interlaced, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Hd)       \
        X(Smpte1080i60, Raster_HD, FPS_30, Interlaced, FormatFlag_Smpte | FormatFlag_Hd)                               \
        X(Smpte1080p23_98, Raster_HD, FPS_23_98, Progressive, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Hd)      \
        X(Smpte1080p24, Raster_HD, FPS_24, Progressive, FormatFlag_Smpte | FormatFlag_Hd)                              \
        X(Smpte1080p25, Raster_HD, FPS_25, Progressive, FormatFlag_Smpte | FormatFlag_Pal | FormatFlag_Hd)             \
        X(Smpte1080p29_97, Raster_HD, FPS_29_97, Progressive, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Hd)      \
        X(Smpte1080p30, Raster_HD, FPS_30, Progressive, FormatFlag_Smpte | FormatFlag_Hd)                              \
        X(Smpte1080p50, Raster_HD, FPS_50, Progressive, FormatFlag_Smpte | FormatFlag_Pal | FormatFlag_Hd)             \
        X(Smpte1080p59_94, Raster_HD, FPS_59_94, Progressive, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Hd)      \
        X(Smpte1080p60, Raster_HD, FPS_60, Progressive, FormatFlag_Smpte | FormatFlag_Hd)                              \
        X(Smpte1080psf23_98, Raster_HD, FPS_23_98, PsF, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Hd)            \
        X(Smpte1080psf24, Raster_HD, FPS_24, PsF, FormatFlag_Smpte | FormatFlag_Hd)                                    \
        X(Smpte1080psf25, Raster_HD, FPS_25, PsF, FormatFlag_Smpte | FormatFlag_Pal | FormatFlag_Hd)                   \
        X(Smpte1080psf29_97, Raster_HD, FPS_29_97, PsF, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Hd)            \
        X(Smpte1080psf30, Raster_HD, FPS_30, PsF, FormatFlag_Smpte | FormatFlag_Hd)                                    \
        X(Smpte2160p23_98, Raster_UHD, FPS_23_98, Progressive, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Uhd)    \
        X(Smpte2160p24, Raster_UHD, FPS_24, Progressive, FormatFlag_Smpte | FormatFlag_Uhd)                            \
        X(Smpte2160p25, Raster_UHD, FPS_25, Progressive, FormatFlag_Smpte | FormatFlag_Pal | FormatFlag_Uhd)           \
        X(Smpte2160p29_97, Raster_UHD, FPS_29_97, Progressive, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Uhd)    \
        X(Smpte2160p30, Raster_UHD, FPS_30, Progressive, FormatFlag_Smpte | FormatFlag_Uhd)                            \
        X(Smpte2160p50, Raster_UHD, FPS_50, Progressive, FormatFlag_Smpte | FormatFlag_Pal | FormatFlag_Uhd)           \
        X(Smpte2160p59_94, Raster_UHD, FPS_59_94, Progressive, FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Uhd)    \
        X(Smpte2160p60, Raster_UHD, FPS_60, Progressive, FormatFlag_Smpte | FormatFlag_Uhd)                            \
        X(Smpte4320p23_98, Raster_UHD8K, FPS_23_98, Progressive,                                                       \
          FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Uhd8k)                                                       \
        X(Smpte4320p24, Raster_UHD8K, FPS_24, Progressive, FormatFlag_Smpte | FormatFlag_Uhd8k)                        \
        X(Smpte4320p25, Raster_UHD8K, FPS_25, Progressive, FormatFlag_Smpte | FormatFlag_Pal | FormatFlag_Uhd8k)       \
        X(Smpte4320p29_97, Raster_UHD8K, FPS_29_97, Progressive,                                                       \
          FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Uhd8k)                                                       \
        X(Smpte4320p30, Raster_UHD8K, FPS_30, Progressive, FormatFlag_Smpte | FormatFlag_Uhd8k)                        \
        X(Smpte4320p50, Raster_UHD8K, FPS_50, Progressive, FormatFlag_Smpte | FormatFlag_Pal | FormatFlag_Uhd8k)       \
        X(Smpte4320p59_94, Raster_UHD8K, FPS_59_94, Progressive,                                                       \
          FormatFlag_Smpte | FormatFlag_Ntsc | FormatFlag_Uhd8k)                                                       \
        X(Smpte4320p60, Raster_UHD8K, FPS_60, Progressive, FormatFlag_Smpte | FormatFlag_Uhd8k)                        \
        X(Dci2Kp24, Raster_2K, FPS_24, Progressive, FormatFlag_Dci)                                                    \
        X(Dci2Kp25, Raster_2K, FPS_25, Progressive, FormatFlag_Dci)                                                    \
        X(Dci2Kp30, Raster_2K, FPS_30, Progressive, FormatFlag_Dci)                                                    \
        X(Dci2Kp48, Raster_2K, FPS_48, Progressive, FormatFlag_Dci)                                                    \
        X(Dci2Kp50, Raster_2K, FPS_50, Progressive, FormatFlag_Dci)                                                    \
        X(Dci2Kp60, Raster_2K, FPS_60, Progressive, FormatFlag_Dci)                                                    \
        X(Dci4Kp24, Raster_4K, FPS_24, Progressive, FormatFlag_Dci)                                                    \
        X(Dci4Kp25, Raster_4K, FPS_25, Progressive, FormatFlag_Dci)                                                    \
        X(Dci4Kp30, Raster_4K, FPS_30, Progressive, FormatFlag_Dci)                                                    \
        X(Dci4Kp48, Raster_4K, FPS_48, Progressive, FormatFlag_Dci)                                                    \
        X(Dci4Kp50, Raster_4K, FPS_50, Progressive, FormatFlag_Dci)                                                    \
        X(Dci4Kp60, Raster_4K, FPS_60, Progressive, FormatFlag_Dci)

/**
 * @brief Combined video raster, frame rate, and scan mode.
 * @ingroup video
 *
 * VideoFormat identifies a video signal by the triple
 * @c (active_raster, frame_rate, scan_mode).  It is a first-class
 * Variant type and can round-trip to and from the industry-standard
 * SMPTE-style short form ("1080i59.94", "720p59.94", "1080psf23.98").
 *
 * @par String format
 *
 * @c toString() emits the form @c "<raster><scan><rate>":
 *
 * - @c &lt;raster&gt; is the SMPTE height-only prefix
 *   ("486", "576", "720", "1080", "2160", "4320") when the full
 *   raster matches the standard broadcast width at that height
 *   (720×486, 720×576, 1280×720, 1920×1080, 3840×2160, 7680×4320).
 *   Otherwise the explicit @c "WxH" form is emitted ("2048x1080",
 *   "4096x2160", "405x314").
 * - @c &lt;scan&gt; is @c "p", @c "i", or @c "psf".
 * - @c &lt;rate&gt; is the frame rate as formatted by the FrameRate
 *   registry ("23.98", "29.97", "59.94", "24", "25", "30", "60",
 *   ...) for progressive and PsF.  For interlaced modes
 *   @c &lt;rate&gt; is the SMPTE @em field rate (twice the frame
 *   rate), matching industry convention:
 *
 * | Frame rate | Progressive | Interlaced | PsF         |
 * |------------|-------------|------------|-------------|
 * | 23.98      | 1080p23.98  | —          | 1080psf23.98|
 * | 24         | 1080p24     | —          | 1080psf24   |
 * | 25         | 720p25      | 1080i50    | 1080psf25   |
 * | 29.97      | 1080p29.97  | 1080i59.94 | 1080psf29.97|
 * | 30         | 1080p30     | 1080i60    | 1080psf30   |
 * | 59.94      | 720p59.94   | —          | —           |
 *
 * Set @ref StringOptions::useNamedRaster to prefer well-known raster
 * names ("HDp29.97", "UHDp60", "2Kp24") in place of the SMPTE height
 * prefix.  Rasters with no registered name fall back to explicit
 * @c "WxH" in both modes.
 *
 * @par String parsing
 *
 * @c fromString() is case-insensitive and accepts:
 *  - SMPTE height prefixes — @c "1080i59.94", @c "720p60".
 *  - Well-known raster names — @c "HDp29.97", @c "UHDp60",
 *    @c "2Kp24", @c "4Kp24", @c "NTSCi59.94", @c "PALi50".
 *  - Explicit rasters — @c "1920x1080p29.97", @c "405x314p30".
 *  - @c "psf" suffix — @c "1080psf23.98".
 *  - All frame-rate strings that FrameRate::fromString accepts,
 *    including @c "23.976" (alias for @c 24000/1001) and explicit
 *    rationals like @c "30000/1001".
 *  - Whitespace and cosmetic separators (@c '@', @c ',', @c ';')
 *    between the raster, scan, and rate tokens —
 *    @c "1920x1080 @ 29.97", @c "HD, 29.97", @c "1080 i 59.94"
 *    all parse.
 *  - Scan marker positioned before or after the rate —
 *    @c "1080 29.97p", @c "1080 59.94i", @c "1080 24psf",
 *    @c "1080 29.97 p" all work in addition to the canonical
 *    @c "1080p29.97" inline placement.  If no scan character is
 *    present anywhere, the scan mode defaults to @c Progressive.
 *
 * By default (ParseOptions::strictInterlacedFieldRate = @c true) the
 * rate suffix for an interlaced form is interpreted as the field
 * rate, so @c "1080i59.94" yields a frame rate of @c 30000/1001.
 * With @c strictInterlacedFieldRate set to @c false, the parser
 * falls back to interpreting the suffix as a direct frame rate when
 * halving it does not yield a well-known frame rate — this tolerates
 * non-SMPTE notation like @c "1080i29.97".
 *
 * A default-constructed VideoFormat is invalid.
 */
class VideoFormat {
        public:
                /** @brief Identifiers for well-known raster sizes. */
                enum WellKnownRaster {
                        Raster_NotWellKnown = 0,
#define X(id, name, w, h) id,
                        PROMEKI_WELL_KNOWN_RASTERS
#undef X
                };

                /**
                 * @brief Flags describing which families a well-known
                 *        video format belongs to.
                 *
                 * Returned bitwise-OR'd from @ref wellKnownFormatFlags.
                 * Individual flags can also be tested via the @c isXxxFormat()
                 * accessors.
                 */
                enum WellKnownFormatFlag : uint32_t {
                        FormatFlag_Smpte = 1u << 0, ///< Standardised by SMPTE.
                        FormatFlag_Dci = 1u << 1,   ///< Digital Cinema Initiative format.
                        FormatFlag_Ntsc = 1u << 2,  ///< NTSC family (525 lines or 1000/1001 rate).
                        FormatFlag_Pal = 1u << 3,   ///< PAL family (625 lines or 50 Hz cadence).
                        FormatFlag_Sd = 1u << 4,    ///< Standard Definition raster.
                        FormatFlag_Hd = 1u << 5,    ///< High Definition raster (720 or 1080 lines).
                        FormatFlag_Uhd = 1u << 6,   ///< Ultra HD raster (3840×2160).
                        FormatFlag_Uhd8k = 1u << 7, ///< 8K UHD raster (7680×4320).
                };

                /**
                 * @brief Identifiers for well-known video formats.
                 *
                 * The two sentinel values precede the generated list:
                 *
                 *  - @c Invalid — the underlying @ref VideoFormat is
                 *    not valid at all (invalid raster or rate).
                 *  - @c NotWellKnown — the @ref VideoFormat is valid
                 *    but its raster / rate / scan combination does not
                 *    match any entry in @ref PROMEKI_WELL_KNOWN_VIDEO_FORMATS.
                 *
                 * Every other value names a specific well-known format
                 * and can be passed to the @ref VideoFormat(WellKnownFormat)
                 * constructor to instantiate it.
                 */
                enum WellKnownFormat {
                        Invalid = 0,
                        NotWellKnown,
#define X(id, raster, rate, scan, flags) id,
                        PROMEKI_WELL_KNOWN_VIDEO_FORMATS
#undef X
                };

                /** @brief Plain-value list of @ref WellKnownFormat ids. */
                using WellKnownFormatList = List<WellKnownFormat>;

                /** @brief Controls how toString() formats the result. */
                struct StringOptions {
                                /**
                         * @brief Prefer a well-known raster name over the SMPTE height prefix.
                         *
                         * When @c true, toString() emits e.g.
                         * @c "HDp29.97" instead of @c "1080p29.97".
                         * Rasters that have no registered well-known
                         * name fall back to the explicit @c "WxH"
                         * form (the SMPTE height prefix is only used
                         * when this flag is @c false).
                         */
                                bool useNamedRaster = false;
                };

                /** @brief Controls how fromString() interprets the rate suffix. */
                struct ParseOptions {
                                /**
                         * @brief Strictly interpret the rate suffix of an interlaced form
                         * as the SMPTE field rate.
                         *
                         * When @c true (the default), the rate
                         * suffix for a form ending in @c 'i' is
                         * always halved to produce the frame rate
                         * (SMPTE convention — @c "1080i59.94"
                         * becomes a frame rate of @c 30000/1001).
                         *
                         * When @c false, the halved rate is used
                         * only if it is a well-known frame rate;
                         * otherwise the un-halved rate is used
                         * directly as the frame rate.  This
                         * tolerates non-SMPTE notation such as
                         * @c "1080i29.97" (interpreted as 1080i at
                         * 29.97 frames/second).
                         */
                                bool strictInterlacedFieldRate = true;
                };

                /** @brief Default-constructs an invalid VideoFormat. */
                VideoFormat() = default;

                /**
                 * @brief Constructs a VideoFormat from explicit components.
                 * @param raster   Active pixel raster (width × height).
                 * @param rate     Frame rate.
                 * @param scanMode Video scan mode (defaults to Progressive).
                 */
                VideoFormat(const Size2Du32 &raster, const FrameRate &rate,
                            VideoScanMode scanMode = VideoScanMode::Progressive);

                /**
                 * @brief Constructs a VideoFormat using a well-known raster identifier.
                 * @param raster   Well-known raster id.
                 * @param rate     Frame rate.
                 * @param scanMode Video scan mode (defaults to Progressive).
                 *
                 * Passing @c Raster_Invalid or @c Raster_NotWellKnown
                 * produces an invalid VideoFormat.
                 */
                VideoFormat(WellKnownRaster raster, const FrameRate &rate,
                            VideoScanMode scanMode = VideoScanMode::Progressive);

                /**
                 * @brief Constructs a VideoFormat from a well-known format id.
                 *
                 * Passing @c Invalid or @c NotWellKnown produces an
                 * invalid VideoFormat.  Any other value yields the
                 * raster / rate / scan triple associated with that
                 * entry in @ref PROMEKI_WELL_KNOWN_VIDEO_FORMATS.
                 *
                 * @param fmt Well-known format id.
                 */
                VideoFormat(WellKnownFormat fmt);

                /** @brief Returns true if the raster and frame rate are both valid. */
                bool isValid() const { return _raster.isValid() && _rate.isValid(); }

                /** @brief Returns the active pixel raster. */
                const Size2Du32 &raster() const { return _raster; }

                /** @brief Returns the frame rate (always the frame rate, never the field rate). */
                const FrameRate &frameRate() const { return _rate; }

                /** @brief Returns the scan mode. */
                VideoScanMode videoScanMode() const { return _scanMode; }

                /**
                 * @brief Returns the WellKnownRaster id matching this raster, or
                 *        @c Raster_NotWellKnown if none applies.
                 */
                WellKnownRaster wellKnownRaster() const;

                /** @brief Returns true if this raster matches a well-known entry. */
                bool isWellKnownRaster() const { return wellKnownRaster() != Raster_NotWellKnown; }

                /**
                 * @brief Returns the WellKnownFormat id matching this format.
                 *
                 * Returns @c Invalid for a default-constructed or
                 * otherwise invalid VideoFormat, @c NotWellKnown for a
                 * valid VideoFormat that does not match any entry in
                 * @ref PROMEKI_WELL_KNOWN_VIDEO_FORMATS, or the
                 * specific WellKnownFormat id otherwise.
                 */
                WellKnownFormat wellKnownFormat() const;

                /** @brief Returns true if this format matches a well-known entry. */
                bool isWellKnownFormat() const {
                        const WellKnownFormat id = wellKnownFormat();
                        return id != Invalid && id != NotWellKnown;
                }

                /**
                 * @brief Returns the bitwise-OR of WellKnownFormatFlag
                 *        values describing this format.
                 *
                 * Returns 0 if the format is not a well-known entry.
                 */
                uint32_t wellKnownFormatFlags() const;

                /** @brief Returns true if this format is SMPTE-standardised. */
                bool isSmpteFormat() const { return (wellKnownFormatFlags() & FormatFlag_Smpte) != 0; }

                /** @brief Returns true if this format is a Digital Cinema Initiative format. */
                bool isDciFormat() const { return (wellKnownFormatFlags() & FormatFlag_Dci) != 0; }

                /** @brief Returns true if this format belongs to the NTSC family. */
                bool isNtscFormat() const { return (wellKnownFormatFlags() & FormatFlag_Ntsc) != 0; }

                /** @brief Returns true if this format belongs to the PAL family. */
                bool isPalFormat() const { return (wellKnownFormatFlags() & FormatFlag_Pal) != 0; }

                /** @brief Returns true if this format uses a Standard Definition raster. */
                bool isSdFormat() const { return (wellKnownFormatFlags() & FormatFlag_Sd) != 0; }

                /** @brief Returns true if this format uses a High Definition raster. */
                bool isHdFormat() const { return (wellKnownFormatFlags() & FormatFlag_Hd) != 0; }

                /** @brief Returns true if this format uses a 4K Ultra HD raster (3840×2160). */
                bool isUhdFormat() const { return (wellKnownFormatFlags() & FormatFlag_Uhd) != 0; }

                /** @brief Returns true if this format uses an 8K Ultra HD raster (7680×4320). */
                bool isUhd8kFormat() const { return (wellKnownFormatFlags() & FormatFlag_Uhd8k) != 0; }

                /**
                 * @brief Returns true if this format is a traditional
                 *        broadcast television format.
                 *
                 * Equivalent to @c Smpte flag set and not a DCI cinema
                 * format.  Covers SD, HD, UHD and 8K broadcast formats
                 * (NTSC / PAL / ATSC / DVB families).
                 */
                bool isBroadcastFormat() const {
                        const uint32_t f = wellKnownFormatFlags();
                        return (f & FormatFlag_Smpte) != 0 && (f & FormatFlag_Dci) == 0;
                }

                /**
                 * @brief Returns true if this format is a digital cinema format.
                 *
                 * Currently equivalent to @ref isDciFormat; kept as a
                 * separate query so callers can express "cinema content"
                 * semantically without relying on the underlying flag
                 * taxonomy.
                 */
                bool isCinemaFormat() const { return isDciFormat(); }

                /**
                 * @brief Returns true if this format has an integer frame-rate cadence.
                 *
                 * True for rates whose exact rational has denominator 1
                 * (24, 25, 30, 48, 50, 60, 100, 120), false for the
                 * NTSC-derived 1000/1001 family (23.98, 29.97, 59.94,
                 * …) and for invalid frame rates.  Works on any
                 * VideoFormat, not just well-known ones.
                 */
                bool isIntegerCadence() const { return _rate.isValid() && _rate.denominator() == 1; }

                /**
                 * @brief Returns the flags for a WellKnownFormat id.
                 *
                 * @param fmt Well-known format id.
                 * @return Flags for that format, or 0 for
                 *         @c Invalid / @c NotWellKnown.
                 */
                static uint32_t formatFlags(WellKnownFormat fmt);

                /**
                 * @brief Returns the list of every WellKnownFormat id.
                 *
                 * The returned list is in the order the entries appear
                 * in @ref PROMEKI_WELL_KNOWN_VIDEO_FORMATS and does
                 * @em not include the @c Invalid / @c NotWellKnown
                 * sentinels.  Intended for populating selection UIs
                 * and for parametrised tests.
                 *
                 * @param requiredFlags Optional bitmask; only formats
                 *                      with every bit in @p requiredFlags
                 *                      set are returned.  Pass @c 0
                 *                      (the default) to include every
                 *                      well-known format.
                 * @return              List of matching WellKnownFormat ids.
                 */
                static WellKnownFormatList allWellKnownFormats(uint32_t requiredFlags = 0);

                /**
                 * @brief Returns the raster + scan portion of the formatted string.
                 *
                 * Example returns: @c "1080p", @c "1080i", @c "1080psf",
                 * @c "2048x1080p", @c "HDp" (with @c useNamedRaster).
                 *
                 * Returns an empty String when the raster is invalid.
                 */
                String rasterString() const { return rasterString(StringOptions()); }
                /**
                 * @brief Returns the raster + scan portion of the formatted string.
                 * @param opts Formatting options.
                 */
                String rasterString(const StringOptions &opts) const;

                /**
                 * @brief Returns the frame-rate portion of the formatted string.
                 *
                 * For interlaced modes this is the SMPTE field rate
                 * (twice the frame rate).  For progressive and PsF
                 * modes it is the frame rate itself.  Well-known
                 * rates are emitted as their common-name form
                 * ("29.97", "59.94", "24", "60"); arbitrary rates
                 * are emitted in exact-rational form ("30000/1001").
                 *
                 * Returns an empty String when the frame rate is
                 * invalid.
                 */
                String frameRateString() const;

                /**
                 * @brief Returns the combined SMPTE-style format string.
                 *
                 * Returns an empty String if the format is invalid.
                 */
                String toString() const { return toString(StringOptions()); }
                /**
                 * @brief Returns the combined SMPTE-style format string.
                 * @param opts Formatting options.
                 */
                String toString(const StringOptions &opts) const;

                /**
                 * @brief Parses a VideoFormat from a string.
                 *
                 * @param str  Input string (case-insensitive).
                 * @return     Parsed VideoFormat on success, or @c Error::Invalid.
                 */
                static Result<VideoFormat> fromString(const String &str) { return fromString(str, ParseOptions()); }
                /**
                 * @brief Parses a VideoFormat from a string with explicit parse options.
                 *
                 * @param str  Input string (case-insensitive).
                 * @param opts Parsing options.
                 * @return     Parsed VideoFormat on success, or @c Error::Invalid.
                 */
                static Result<VideoFormat> fromString(const String &str, const ParseOptions &opts);

                /** @brief Returns true if both formats are equal in all three components. */
                bool operator==(const VideoFormat &o) const {
                        return _raster == o._raster && _rate == o._rate && _scanMode == o._scanMode;
                }

                /** @brief Returns true if the formats differ. */
                bool operator!=(const VideoFormat &o) const { return !(*this == o); }

                /**
                 * @brief Returns true if this VideoFormat matches the
                 *        given WellKnownFormat id.
                 *
                 * Equivalent to @c wellKnownFormat() @c == @c fmt,
                 * which means @c Invalid matches an invalid VideoFormat
                 * and @c NotWellKnown matches a valid VideoFormat that
                 * is not in the well-known list.
                 */
                bool operator==(WellKnownFormat fmt) const { return wellKnownFormat() == fmt; }

                /** @brief Returns true if this VideoFormat does not match @p fmt. */
                bool operator!=(WellKnownFormat fmt) const { return wellKnownFormat() != fmt; }

                /** @brief Enum-on-the-left overload for @c ==. */
                friend bool operator==(WellKnownFormat fmt, const VideoFormat &vf) { return vf == fmt; }

                /** @brief Enum-on-the-left overload for @c !=. */
                friend bool operator!=(WellKnownFormat fmt, const VideoFormat &vf) { return vf != fmt; }

        private:
                Size2Du32     _raster;
                FrameRate     _rate;
                VideoScanMode _scanMode{VideoScanMode::Progressive};
};

PROMEKI_NAMESPACE_END

/**
 * @brief @c std::formatter specialisation for @ref promeki::VideoFormat.
 * @ingroup video
 *
 * Accepts an optional style keyword that drives
 * @ref promeki::VideoFormat::StringOptions, followed by an optional
 * standard string format spec for width / fill / alignment.  The two
 * sections are separated by a colon, matching the Timecode formatter
 * convention:
 *
 * | Format spec            | Result                                   |
 * |------------------------|------------------------------------------|
 * | @c {}                  | default toString() — "1080p29.97"        |
 * | @c {:smpte}            | explicit SMPTE height form — "1080p29.97"|
 * | @c {:named}            | useNamedRaster=true — "HDp29.97"         |
 * | @c {:>16}              | default style, right-justified width 16  |
 * | @c {:named:>16}        | named raster, right-justified width 16   |
 * | @c {:smpte:*<16}       | SMPTE form, left-justified, '*' fill, 16 |
 *
 * Unrecognised style hints fall through to the standard string format
 * parser, so a stray spec like @c {:>16} still works.
 */
template <> struct std::formatter<promeki::VideoFormat> {
                enum class Style {
                        Smpte, ///< Default SMPTE height form ("1080p29.97").
                        Named, ///< Well-known raster names ("HDp29.97").
                };

                Style                            _style = Style::Smpte;
                std::formatter<std::string_view> _base;

                constexpr auto parse(std::format_parse_context &ctx) {
                        auto it = ctx.begin();
                        auto end = ctx.end();

                        // Match a style keyword at the start of the spec.  Each
                        // candidate stops at end-of-spec or a ':' separator so
                        // trailing std-spec forwarding still works.
                        auto tryKeyword = [&](const char *kw, Style s) {
                                auto p = it;
                                while (*kw && p != end && *p == *kw) {
                                        ++p;
                                        ++kw;
                                }
                                if (*kw == 0 && (p == end || *p == '}' || *p == ':')) {
                                        it = p;
                                        _style = s;
                                        return true;
                                }
                                return false;
                        };

                        if (!tryKeyword("smpte", Style::Smpte) && !tryKeyword("named", Style::Named)) {
                                // No keyword — leave _style at default and let
                                // the base parser consume the entire remaining spec.
                        }

                        // Separating ':' between the style hint and a standard
                        // string format spec is consumed here so the base parser
                        // sees a bare ">16" rather than ":>16".
                        if (it != end && *it == ':') ++it;

                        ctx.advance_to(it);
                        return _base.parse(ctx);
                }

                template <typename FormatContext>
                auto format(const promeki::VideoFormat &vf, FormatContext &ctx) const {
                        promeki::VideoFormat::StringOptions opts;
                        opts.useNamedRaster = (_style == Style::Named);
                        const promeki::String s = vf.toString(opts);
                        return _base.format(std::string_view(s.cstr(), s.byteCount()), ctx);
                }
};
