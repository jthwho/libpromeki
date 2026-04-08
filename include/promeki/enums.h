/**
 * @file      enums.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @defgroup wellknownenums Well-known Enums
 * @ingroup util
 *
 * Library-provided @ref Enum types that are commonly used as config values
 * throughout the ProAV pipeline.  Each struct follows the pattern documented
 * on @ref Enum:
 *
 * - A @c Type member holds the registered @ref Enum::Type handle.
 * - A set of @c static @c inline @c const @ref Enum constants, one per
 *   registered value, for ergonomic use at call sites:
 *   @code
 *   cfg.set(MediaIOTask_TPG::ConfigVideoPattern, VideoPattern::ColorBars);
 *   @endcode
 *
 * The integer values are stable and match the corresponding C++ `enum`
 * inside the subsystem that originally defined them (e.g.
 * VideoTestPattern::Pattern), so code that still holds the C++ `enum`
 * internally can convert in either direction via a plain @c static_cast
 * on @c Enum::value().
 *
 * The registered string names use the same CamelCase spelling as the C++
 * constants on the wrapper struct (e.g. @c "ColorBars", @c "BottomCenter",
 * @c "LTC") rather than the legacy all-lowercase config strings.  Call
 * sites that previously wrote @c "colorbars" / @c "bottomcenter" /
 * @c "tone" / @c "422" need to be updated to the CamelCase form or, better,
 * to use the @ref Enum constants directly.
 * @{
 */

/**
 * @brief Well-known Enum type for video test pattern generator modes.
 *
 * Mirrors @c VideoTestPattern::Pattern in value and order.  Used as the
 * value type for the @c MediaIOTask_TPG @c ConfigVideoPattern config key.
 *
 * @par Example
 * @code
 * cfg.set(MediaIOTask_TPG::ConfigVideoPattern, VideoPattern::ZonePlate);
 * Enum e = cfg.getAs<Enum>(MediaIOTask_TPG::ConfigVideoPattern,
 *                          VideoPattern::ColorBars);
 * VideoTestPattern::Pattern pat =
 *         static_cast<VideoTestPattern::Pattern>(e.value());
 * @endcode
 */
struct VideoPattern {
        static inline const Enum::Type Type = Enum::registerType("VideoPattern",
                {
                        { "ColorBars",    0  },
                        { "ColorBars75",  1  },
                        { "Ramp",         2  },
                        { "Grid",         3  },
                        { "Crosshatch",   4  },
                        { "Checkerboard", 5  },
                        { "SolidColor",   6  },
                        { "White",        7  },
                        { "Black",        8  },
                        { "Noise",        9  },
                        { "ZonePlate",    10 },
                        { "AvSync",       11 }
                },
                0);  // default: ColorBars

        static inline const Enum ColorBars    { Type, 0  };
        static inline const Enum ColorBars75  { Type, 1  };
        static inline const Enum Ramp         { Type, 2  };
        static inline const Enum Grid         { Type, 3  };
        static inline const Enum Crosshatch   { Type, 4  };
        static inline const Enum Checkerboard { Type, 5  };
        static inline const Enum SolidColor   { Type, 6  };
        static inline const Enum White        { Type, 7  };
        static inline const Enum Black        { Type, 8  };
        static inline const Enum Noise        { Type, 9  };
        static inline const Enum ZonePlate    { Type, 10 };
        static inline const Enum AvSync       { Type, 11 };
};

/**
 * @brief Well-known Enum type for on-screen burn-in position presets.
 *
 * Mirrors @c VideoTestPattern::BurnPosition in value and order.  Used as
 * the value type for the @c MediaIOTask_TPG @c ConfigVideoBurnPosition
 * config key.
 */
struct BurnPosition {
        static inline const Enum::Type Type = Enum::registerType("BurnPosition",
                {
                        { "TopLeft",      0 },
                        { "TopCenter",    1 },
                        { "TopRight",     2 },
                        { "BottomLeft",   3 },
                        { "BottomCenter", 4 },
                        { "BottomRight",  5 },
                        { "Center",       6 }
                },
                4);  // default: BottomCenter

        static inline const Enum TopLeft      { Type, 0 };
        static inline const Enum TopCenter    { Type, 1 };
        static inline const Enum TopRight     { Type, 2 };
        static inline const Enum BottomLeft   { Type, 3 };
        static inline const Enum BottomCenter { Type, 4 };
        static inline const Enum BottomRight  { Type, 5 };
        static inline const Enum Center       { Type, 6 };
};

/**
 * @brief Well-known Enum type for audio test pattern generator modes.
 *
 * Mirrors @c AudioTestPattern::Mode in value and order.  Used as the value
 * type for the @c MediaIOTask_TPG @c ConfigAudioMode config key.
 *
 * Note that the config key is historically named @c ConfigAudioMode, but
 * the corresponding Enum type is called @c AudioPattern for consistency
 * with @ref VideoPattern.
 */
struct AudioPattern {
        static inline const Enum::Type Type = Enum::registerType("AudioPattern",
                {
                        { "Tone",    0 },
                        { "Silence", 1 },
                        { "LTC",     2 },
                        { "AvSync",  3 }
                },
                0);  // default: Tone

        static inline const Enum Tone    { Type, 0 };
        static inline const Enum Silence { Type, 1 };
        static inline const Enum LTC     { Type, 2 };
        static inline const Enum AvSync  { Type, 3 };
};

/**
 * @brief Well-known Enum type for chroma subsampling modes.
 *
 * Mirrors @c JpegImageCodec::Subsampling in value and order.  Used as the
 * value type for the @c JpegEncoderNode "Subsampling" config key and
 * anywhere else a simple 4:4:4 / 4:2:2 / 4:2:0 selection is needed.
 */
struct ChromaSubsampling {
        static inline const Enum::Type Type = Enum::registerType("ChromaSubsampling",
                {
                        { "YUV444", 0 },
                        { "YUV422", 1 },
                        { "YUV420", 2 }
                },
                1);  // default: YUV422 (RFC 2435 JPEG-over-RTP compatible)

        static inline const Enum YUV444 { Type, 0 };
        static inline const Enum YUV422 { Type, 1 };
        static inline const Enum YUV420 { Type, 2 };
};

/** @} */

PROMEKI_NAMESPACE_END
