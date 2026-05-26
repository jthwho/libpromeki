/**
 * @file      enums_color.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Color signalling and CSC pipeline enums.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/** @addtogroup wellknownenums */
/** @{ */

/**
 * @brief Well-known Enum type for chroma subsampling modes.
 *
 * Mirrors @c JpegVideoEncoder::Subsampling in value and order.  Used as the
 * value type for @ref MediaConfig::JpegSubsampling and anywhere else a
 * simple 4:4:4 / 4:2:2 / 4:2:0 selection is needed.
 */
class ChromaSubsampling : public TypedEnum<ChromaSubsampling> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("ChromaSubsampling", "Chroma Subsampling", 1,
                                                   {"YUV444", 0, "4:4:4 (No Subsampling)"},
                                                   {"YUV422", 1, "4:2:2"},
                                                   {"YUV420", 2, "4:2:0"}); // default: YUV422 (RFC 2435 JPEG-over-RTP compatible)

                using TypedEnum<ChromaSubsampling>::TypedEnum;

                static const ChromaSubsampling YUV444;
                static const ChromaSubsampling YUV422;
                static const ChromaSubsampling YUV420;
};

inline const ChromaSubsampling ChromaSubsampling::YUV444{0};
inline const ChromaSubsampling ChromaSubsampling::YUV422{1};
inline const ChromaSubsampling ChromaSubsampling::YUV420{2};

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
class CscPath : public TypedEnum<CscPath> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("CscPath", "Color Conversion Path", 0,
                                                   {"Optimized", 0, "Optimized (Fast Path)"},
                                                   {"Scalar", 1, "Scalar Reference (SIMD Off)"}); // default: Optimized

                using TypedEnum<CscPath>::TypedEnum;

                static const CscPath Optimized;
                static const CscPath Scalar;
};

inline const CscPath CscPath::Optimized{0};
inline const CscPath CscPath::Scalar{1};

/**
 * @brief Well-known Enum type for HDR tone-mapping policy on CSC pipelines.
 *
 * Used as the value type for the @ref MediaConfig::CscToneMapping
 * config key.  @c Auto enables ITU-R BT.2390 perceptual tone-mapping
 * automatically whenever the pipeline crosses an HDR boundary
 * (source is PQ / HLG, destination is SDR) — the default since
 * SDR clipping is rarely the desired behaviour for HDR-to-SDR
 * conversions.  @c Enabled forces tone-mapping on regardless of
 * the colorimetry of either end (callers who know they need
 * compression even between two HDR targets with mismatched peak
 * luminance).  @c Disabled bypasses tone-mapping entirely — the
 * pipeline lets the existing transfer / gamut chain produce
 * whatever clipping naturally falls out.
 */
class CscToneMapping : public TypedEnum<CscToneMapping> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("CscToneMapping", "HDR Tone Mapping", 0,
                                                   {"Auto", 0, "Auto (HDR-to-SDR)"},
                                                   {"Enabled", 1, "Always Enabled"},
                                                   {"Disabled", 2, "Disabled"});

                using TypedEnum<CscToneMapping>::TypedEnum;

                static const CscToneMapping Auto;
                static const CscToneMapping Enabled;
                static const CscToneMapping Disabled;
};

inline const CscToneMapping CscToneMapping::Auto{0};
inline const CscToneMapping CscToneMapping::Enabled{1};
inline const CscToneMapping CscToneMapping::Disabled{2};

/**
 * @brief Well-known Enum type for HDR tone-mapping operator selection.
 *
 * Used as the value type for the @ref MediaConfig::CscToneMapOperator
 * config key.  The enum reserves slots for all the operators the
 * library plans to support so callers can pin a choice today and the
 * runtime picks the matching kernel when it lands.  Until the kernel
 * for a given operator is registered, the pipeline falls back to
 * @ref Bt2390 with a one-shot warning.
 *
 * - @c Bt2390  — ITU-R BT.2390-9 Annex B.2.5 EETF.  Per-channel Hermite
 *               spline in PQ-encoded space.  Broadcast / display
 *               standard, simple and well-behaved.  Default.
 * - @c Reinhard — `L / (1 + L)` operator in linear scene-referred
 *               space.  Cheap, perceptually reasonable for typical
 *               content; popular in games.  Not yet implemented.
 * - @c Hable    — Uncharted 2 filmic curve in linear space (shoulder
 *               + toe).  Popular for game / cinematic content.  Not
 *               yet implemented.
 * - @c Aces     — Academy Color Encoding System RRT+ODT in linear
 *               space.  Film-industry standard; usually shipped via
 *               Stephen Hill's polynomial fit.  Not yet implemented.
 * - @c Bt2446a  — ITU-R BT.2446 Method A — broadcast HDR-to-HDR
 *               peak-luminance compression.  Not yet implemented.
 */
class CscToneMapOperator : public TypedEnum<CscToneMapOperator> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("CscToneMapOperator", "Tone Mapping Operator", 0,
                                                   {"Bt2390", 0, "ITU-R BT.2390 EETF"},
                                                   {"Reinhard", 1, "Reinhard"},
                                                   {"Hable", 2, "Hable (Uncharted 2 Filmic)"},
                                                   {"Aces", 3, "ACES"},
                                                   {"Bt2446a", 4, "ITU-R BT.2446 Method A"});

                using TypedEnum<CscToneMapOperator>::TypedEnum;

                static const CscToneMapOperator Bt2390;
                static const CscToneMapOperator Reinhard;
                static const CscToneMapOperator Hable;
                static const CscToneMapOperator Aces;
                static const CscToneMapOperator Bt2446a;
};

inline const CscToneMapOperator CscToneMapOperator::Bt2390{0};
inline const CscToneMapOperator CscToneMapOperator::Reinhard{1};
inline const CscToneMapOperator CscToneMapOperator::Hable{2};
inline const CscToneMapOperator CscToneMapOperator::Aces{3};
inline const CscToneMapOperator CscToneMapOperator::Bt2446a{4};

/**
 * @brief Well-known Enum type for video value range (aka quantization
 *        range / full-range flag).
 *
 * @c Limited means studio / broadcast / "video" range — 16..235 on 8-bit
 * Y'CbCr luma, 16..240 on the chroma channels, and the bit-depth
 * scaling of those values for 10/12/16-bit.  @c Full means the whole
 * digital range (0..255 on 8-bit, 0..2^N-1 in general).  @c Unknown is
 * the "auto-derive" / "not declared" default used by @ref PixelFormat
 * entries that pre-date the field being explicit, and by
 * @ref MediaConfig keys that want downstream code to infer the range
 * from the accompanying @ref PixelFormat.
 *
 * The numeric values are local to libpromeki and do @em not match any
 * codec-specific on-wire representation.  Encoders translate to
 * codec-native signalling (H.264/HEVC VUI @c videoFullRangeFlag, AV1
 * @c colorRange, etc.) at session init.
 */
class VideoRange : public TypedEnum<VideoRange> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("VideoRange", "Video Range", 0,
                                                   {"Unknown", 0, "Unknown (Auto-Derive)"},
                                                   {"Limited", 1, "Limited (Studio/Broadcast)"},
                                                   {"Full", 2, "Full (0..Max)"}); // default: Unknown

                using TypedEnum<VideoRange>::TypedEnum;

                static const VideoRange Unknown;
                static const VideoRange Limited;
                static const VideoRange Full;
};

inline const VideoRange VideoRange::Unknown{0};
inline const VideoRange VideoRange::Limited{1};
inline const VideoRange VideoRange::Full{2};

/**
 * @brief Well-known Enum type for VUI / container color primaries.
 *
 * Numeric values match ISO/IEC 23091-4 / ITU-T H.273 (and, by design,
 * the NV_ENC_VUI_COLOR_PRIMARIES / AV1 `color_primaries` enumerations
 * used in-bitstream by H.264 / HEVC / AV1).  Use this enum anywhere a
 * codec-agnostic color-primaries identifier is needed (VideoEncoder
 * VUI signalling, SDP `colorimetry=` parameters, etc.).
 *
 * Only the spec-registered values are exposed; reserved slots are
 * omitted.
 */
class ColorPrimaries : public TypedEnum<ColorPrimaries> {
        public:
                // The @c Auto numeric value (255) deliberately sits
                // outside the 0..22 H.273 value range so it can never
                // collide with a spec-registered primary.  Encoders
                // that see @c Auto resolve it by inspecting the input
                // PixelFormat's ColorModel at session init time.
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("ColorPrimaries", "Color Primaries", 255,
                                                   {"BT709", 1, "BT.709 (Rec.709 / sRGB)"},
                                                   {"Unspecified", 2, "Unspecified"},
                                                   {"BT470M", 4, "BT.470 System M (NTSC 1953)"},
                                                   {"BT470BG", 5, "BT.470 System B/G (PAL/SECAM)"},
                                                   {"SMPTE170M", 6, "SMPTE 170M (SMPTE-C / NTSC)"},
                                                   {"SMPTE240M", 7, "SMPTE 240M"},
                                                   {"Film", 8, "Generic Film (Illuminant C)"},
                                                   {"BT2020", 9, "BT.2020 (UHDTV / HDR)"},
                                                   {"SMPTE428", 10, "SMPTE 428 (CIE XYZ)"},
                                                   {"SMPTE431", 11, "SMPTE 431 (DCI-P3)"},
                                                   {"SMPTE432", 12, "SMPTE 432 (Display P3)"},
                                                   {"JEDEC_P22", 22, "JEDEC P22 (EBU Tech 3213)"},
                                                   {"Auto", 255, "Auto (Derive from Source)"}); // default: Auto

                using TypedEnum<ColorPrimaries>::TypedEnum;

                static const ColorPrimaries Auto;
                static const ColorPrimaries Unspecified;
                static const ColorPrimaries BT709;
                static const ColorPrimaries BT470M;
                static const ColorPrimaries BT470BG;
                static const ColorPrimaries SMPTE170M;
                static const ColorPrimaries SMPTE240M;
                static const ColorPrimaries Film;
                static const ColorPrimaries BT2020;
                static const ColorPrimaries SMPTE428;
                static const ColorPrimaries SMPTE431;
                static const ColorPrimaries SMPTE432;
                static const ColorPrimaries JEDEC_P22;
};

inline const ColorPrimaries ColorPrimaries::Auto{255};
inline const ColorPrimaries ColorPrimaries::Unspecified{2};
inline const ColorPrimaries ColorPrimaries::BT709{1};
inline const ColorPrimaries ColorPrimaries::BT470M{4};
inline const ColorPrimaries ColorPrimaries::BT470BG{5};
inline const ColorPrimaries ColorPrimaries::SMPTE170M{6};
inline const ColorPrimaries ColorPrimaries::SMPTE240M{7};
inline const ColorPrimaries ColorPrimaries::Film{8};
inline const ColorPrimaries ColorPrimaries::BT2020{9};
inline const ColorPrimaries ColorPrimaries::SMPTE428{10};
inline const ColorPrimaries ColorPrimaries::SMPTE431{11};
inline const ColorPrimaries ColorPrimaries::SMPTE432{12};
inline const ColorPrimaries ColorPrimaries::JEDEC_P22{22};

/**
 * @brief Well-known Enum type for VUI / container transfer characteristic
 *        (opto-electronic transfer function).
 *
 * Numeric values match ISO/IEC 23091-4 / ITU-T H.273.  This covers all
 * of the common SDR and HDR curves: @c BT709 (Rec.709 gamma),
 * @c SMPTE2084 (PQ, for HDR10 / HDR10+ / Dolby Vision base layer), and
 * @c ARIB_STD_B67 (HLG).
 */
class TransferCharacteristics : public TypedEnum<TransferCharacteristics> {
        public:
                // The @c Auto numeric value (255) sits outside the
                // 0..18 H.273 value range.  NB: auto-derivation
                // currently cannot pick between SDR gamma, PQ, and HLG
                // — the library's ColorModel doesn't distinguish HDR
                // transfer curves yet — so @c Auto resolves to the
                // SDR curve matching the primaries and callers must
                // set an explicit @c SMPTE2084 / @c ARIB_STD_B67 for
                // HDR content.
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("TransferCharacteristics", "Transfer Characteristics", 255,
                                                   {"BT709", 1, "BT.709 (Rec.709 Gamma)"},
                                                   {"Unspecified", 2, "Unspecified"},
                                                   {"Gamma22", 4, "Gamma 2.2"},
                                                   {"Gamma28", 5, "Gamma 2.8"},
                                                   {"SMPTE170M", 6, "SMPTE 170M"},
                                                   {"SMPTE240M", 7, "SMPTE 240M"},
                                                   {"Linear", 8, "Linear"},
                                                   {"Log", 9, "Logarithmic (100:1)"},
                                                   {"LogSqrt", 10, "Logarithmic (316:1 Sqrt)"},
                                                   {"IEC61966_2_4", 11, "xvYCC (IEC 61966-2-4)"},
                                                   {"BT1361", 12, "BT.1361 Extended Gamut"},
                                                   {"SRGB", 13, "sRGB / sYCC (IEC 61966-2-1)"},
                                                   {"BT2020_10", 14, "BT.2020 (10-bit)"},
                                                   {"BT2020_12", 15, "BT.2020 (12-bit)"},
                                                   {"SMPTE2084", 16, "PQ (SMPTE 2084 / HDR10)"},
                                                   {"SMPTE428", 17, "SMPTE 428 (DCI XYZ)"},
                                                   {"ARIB_STD_B67", 18, "HLG (ARIB STD-B67)"},
                                                   {"Auto", 255, "Auto (Derive from Source)"}); // default: Auto

                using TypedEnum<TransferCharacteristics>::TypedEnum;

                static const TransferCharacteristics Auto;
                static const TransferCharacteristics Unspecified;
                static const TransferCharacteristics BT709;
                static const TransferCharacteristics Gamma22;
                static const TransferCharacteristics Gamma28;
                static const TransferCharacteristics SMPTE170M;
                static const TransferCharacteristics SMPTE240M;
                static const TransferCharacteristics Linear;
                static const TransferCharacteristics Log;
                static const TransferCharacteristics LogSqrt;
                static const TransferCharacteristics IEC61966_2_4;
                static const TransferCharacteristics BT1361;
                static const TransferCharacteristics SRGB;
                static const TransferCharacteristics BT2020_10;
                static const TransferCharacteristics BT2020_12;
                static const TransferCharacteristics SMPTE2084; ///< PQ (HDR10).
                static const TransferCharacteristics SMPTE428;
                static const TransferCharacteristics ARIB_STD_B67; ///< HLG.
};

inline const TransferCharacteristics TransferCharacteristics::Auto{255};
inline const TransferCharacteristics TransferCharacteristics::Unspecified{2};
inline const TransferCharacteristics TransferCharacteristics::BT709{1};
inline const TransferCharacteristics TransferCharacteristics::Gamma22{4};
inline const TransferCharacteristics TransferCharacteristics::Gamma28{5};
inline const TransferCharacteristics TransferCharacteristics::SMPTE170M{6};
inline const TransferCharacteristics TransferCharacteristics::SMPTE240M{7};
inline const TransferCharacteristics TransferCharacteristics::Linear{8};
inline const TransferCharacteristics TransferCharacteristics::Log{9};
inline const TransferCharacteristics TransferCharacteristics::LogSqrt{10};
inline const TransferCharacteristics TransferCharacteristics::IEC61966_2_4{11};
inline const TransferCharacteristics TransferCharacteristics::BT1361{12};
inline const TransferCharacteristics TransferCharacteristics::SRGB{13};
inline const TransferCharacteristics TransferCharacteristics::BT2020_10{14};
inline const TransferCharacteristics TransferCharacteristics::BT2020_12{15};
inline const TransferCharacteristics TransferCharacteristics::SMPTE2084{16};
inline const TransferCharacteristics TransferCharacteristics::SMPTE428{17};
inline const TransferCharacteristics TransferCharacteristics::ARIB_STD_B67{18};

/**
 * @brief Well-known Enum type for VUI / container matrix coefficients
 *        (luma-chroma derivation from the RGB primaries).
 *
 * Numeric values match ISO/IEC 23091-4 / ITU-T H.273.  @c RGB is used
 * when the bitstream stores RGB natively (e.g. HEVC RGB 4:4:4, AV1
 * subsampling_x=subsampling_y=0 RGB).
 */
class MatrixCoefficients : public TypedEnum<MatrixCoefficients> {
        public:
                // @c Auto (numeric 255) sits outside the 0..11 H.273
                // range.  Encoders resolve @c Auto from the input
                // PixelFormat's ColorModel (RGB models → @c RGB,
                // YCbCr_Rec709 → @c BT709, YCbCr_Rec2020 → @c BT2020_NCL,
                // etc.) at session init time.
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("MatrixCoefficients", "Matrix Coefficients", 255,
                                                   {"RGB", 0, "RGB / GBR (Identity)"},
                                                   {"BT709", 1, "BT.709"},
                                                   {"Unspecified", 2, "Unspecified"},
                                                   {"FCC", 4, "FCC (US 47 CFR 73.682)"},
                                                   {"BT470BG", 5, "BT.470 System B/G (PAL)"},
                                                   {"SMPTE170M", 6, "SMPTE 170M (NTSC)"},
                                                   {"SMPTE240M", 7, "SMPTE 240M"},
                                                   {"YCgCo", 8, "YCgCo"},
                                                   {"BT2020_NCL", 9, "BT.2020 Non-Constant Luminance"},
                                                   {"BT2020_CL", 10, "BT.2020 Constant Luminance"},
                                                   {"SMPTE2085", 11, "SMPTE 2085 (Y'D'zD'x)"},
                                                   {"Auto", 255, "Auto (Derive from Source)"}); // default: Auto

                using TypedEnum<MatrixCoefficients>::TypedEnum;

                static const MatrixCoefficients Auto;
                static const MatrixCoefficients Unspecified;
                static const MatrixCoefficients RGB;
                static const MatrixCoefficients BT709;
                static const MatrixCoefficients FCC;
                static const MatrixCoefficients BT470BG;
                static const MatrixCoefficients SMPTE170M;
                static const MatrixCoefficients SMPTE240M;
                static const MatrixCoefficients YCgCo;
                static const MatrixCoefficients BT2020_NCL;
                static const MatrixCoefficients BT2020_CL;
                static const MatrixCoefficients SMPTE2085;
};

inline const MatrixCoefficients MatrixCoefficients::Auto{255};
inline const MatrixCoefficients MatrixCoefficients::Unspecified{2};
inline const MatrixCoefficients MatrixCoefficients::RGB{0};
inline const MatrixCoefficients MatrixCoefficients::BT709{1};
inline const MatrixCoefficients MatrixCoefficients::FCC{4};
inline const MatrixCoefficients MatrixCoefficients::BT470BG{5};
inline const MatrixCoefficients MatrixCoefficients::SMPTE170M{6};
inline const MatrixCoefficients MatrixCoefficients::SMPTE240M{7};
inline const MatrixCoefficients MatrixCoefficients::YCgCo{8};
inline const MatrixCoefficients MatrixCoefficients::BT2020_NCL{9};
inline const MatrixCoefficients MatrixCoefficients::BT2020_CL{10};
inline const MatrixCoefficients MatrixCoefficients::SMPTE2085{11};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
