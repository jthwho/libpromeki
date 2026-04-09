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
 *   cfg.set(MediaConfig::VideoPattern, VideoPattern::ColorBars);
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
 * cfg.set(MediaConfig::VideoPattern, VideoPattern::ZonePlate);
 * Enum e = cfg.getAs<Enum>(MediaConfig::VideoPattern,
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
 * value type for @ref MediaConfig::JpegSubsampling and anywhere else a
 * simple 4:4:4 / 4:2:2 / 4:2:0 selection is needed.
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

/**
 * @brief Well-known Enum type for audio sample formats.
 *
 * Mirrors @c AudioDesc::DataType in value and order.  Used as the value
 * type for any config key that selects an audio sample format (e.g.
 * @c MediaConfig::OutputAudioDataType).
 *
 * The integer values match the @c AudioDesc::DataType enumeration so
 * callers can convert in either direction via a plain @c static_cast
 * on @c Enum::value().  The string names also match — e.g.
 * @c "PCMI_S16LE", @c "PCMI_Float32LE" — so legacy code that still
 * passes strings through @c AudioDesc::stringToDataType keeps working
 * when the same string is fed through the Enum lookup path.
 */
struct AudioDataType {
        static inline const Enum::Type Type = Enum::registerType("AudioDataType",
                {
                        { "Invalid",        0  },
                        { "PCMI_Float32LE", 1  },
                        { "PCMI_Float32BE", 2  },
                        { "PCMI_S8",        3  },
                        { "PCMI_U8",        4  },
                        { "PCMI_S16LE",     5  },
                        { "PCMI_U16LE",     6  },
                        { "PCMI_S16BE",     7  },
                        { "PCMI_U16BE",     8  },
                        { "PCMI_S24LE",     9  },
                        { "PCMI_U24LE",     10 },
                        { "PCMI_S24BE",     11 },
                        { "PCMI_U24BE",     12 },
                        { "PCMI_S32LE",     13 },
                        { "PCMI_U32LE",     14 },
                        { "PCMI_S32BE",     15 },
                        { "PCMI_U32BE",     16 }
                },
                1);  // default: PCMI_Float32LE

        static inline const Enum Invalid        { Type, 0  };
        static inline const Enum PCMI_Float32LE { Type, 1  };
        static inline const Enum PCMI_Float32BE { Type, 2  };
        static inline const Enum PCMI_S8        { Type, 3  };
        static inline const Enum PCMI_U8        { Type, 4  };
        static inline const Enum PCMI_S16LE     { Type, 5  };
        static inline const Enum PCMI_U16LE     { Type, 6  };
        static inline const Enum PCMI_S16BE     { Type, 7  };
        static inline const Enum PCMI_U16BE     { Type, 8  };
        static inline const Enum PCMI_S24LE     { Type, 9  };
        static inline const Enum PCMI_U24LE     { Type, 10 };
        static inline const Enum PCMI_S24BE     { Type, 11 };
        static inline const Enum PCMI_U24BE     { Type, 12 };
        static inline const Enum PCMI_S32LE     { Type, 13 };
        static inline const Enum PCMI_U32LE     { Type, 14 };
        static inline const Enum PCMI_S32BE     { Type, 15 };
        static inline const Enum PCMI_U32BE     { Type, 16 };
};

/**
 * @brief Well-known Enum type for @ref CSCPipeline processing-path selection.
 *
 * Used as the value type for the @ref MediaConfig::CscPath config key.
 * @c Optimized lets the pipeline pick the best registered fast-path
 * kernel (or fall back to the SIMD-accelerated generic pipeline).
 * @c Scalar forces the generic float pipeline with SIMD disabled —
 * useful for debugging and as a reference for accuracy comparisons
 * against @ref Color::convert.
 */
struct CscPath {
        static inline const Enum::Type Type = Enum::registerType("CscPath",
                {
                        { "Optimized", 0 },
                        { "Scalar",    1 }
                },
                0);  // default: Optimized

        static inline const Enum Optimized { Type, 0 };
        static inline const Enum Scalar    { Type, 1 };
};

/**
 * @brief Well-known Enum type for QuickTime / ISO-BMFF container layout.
 *
 * Used as the value type for the @ref MediaConfig::QuickTimeLayout config
 * key.  @c Classic writes a traditional movie atom ending in a single
 * moov atom; @c Fragmented writes an initial moov followed by a series
 * of moof / mdat fragment pairs, which is what streaming and live
 * ingest pipelines need.
 */
struct QuickTimeLayout {
        static inline const Enum::Type Type = Enum::registerType("QuickTimeLayout",
                {
                        { "Classic",    0 },
                        { "Fragmented", 1 }
                },
                1);  // default: Fragmented

        static inline const Enum Classic    { Type, 0 };
        static inline const Enum Fragmented { Type, 1 };
};

/** @} */

PROMEKI_NAMESPACE_END
