/**
 * @file      enums_st2110.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SMPTE ST 2110-20 video SDP fmtp parameter enums.
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
 * @brief Well-known Enum type for the ST 2110-20 @c sampling SDP fmtp parameter.
 *
 * Lists the colour-difference sub-sampling structures defined by
 * SMPTE ST 2110-20:2022 §7.4.1.  Identifiers and value names follow
 * the project's CamelCase convention; the @c YCbCr / @c CLYCbCr /
 * @c ICtCp letter casing already matches the spec's own mixed-case
 * spelling.  The wire form (e.g. @c YCbCr-4:2:2) lives one layer up
 * in the SDP fmtp builder / parser — same pattern used by
 * @ref RtpTsMode (@c Samp / @c New / @c Pres → wire @c SAMP / @c NEW /
 * @c PRES).
 */
class St2110Sampling : public TypedEnum<St2110Sampling> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("St2110Sampling", "ST 2110-20 Sampling", 0,
                                           {"Invalid", 0, "Invalid"},
                                           {"YCbCr444", 1, "YCbCr 4:4:4"}, {"YCbCr422", 2, "YCbCr 4:2:2"}, {"YCbCr420", 3, "YCbCr 4:2:0"},
                                           {"CLYCbCr444", 4, "Constant-Luminance YCbCr 4:4:4"}, {"CLYCbCr422", 5, "Constant-Luminance YCbCr 4:2:2"}, {"CLYCbCr420", 6, "Constant-Luminance YCbCr 4:2:0"},
                                           {"ICtCp444", 7, "ICtCp 4:4:4"}, {"ICtCp422", 8, "ICtCp 4:2:2"}, {"ICtCp420", 9, "ICtCp 4:2:0"},
                                           {"Rgb", 10, "RGB"}, {"Xyz", 11, "XYZ"},
                                           {"Key", 12, "Key (Alpha)"}); // default: Invalid

                using TypedEnum<St2110Sampling>::TypedEnum;

                static const St2110Sampling Invalid;
                static const St2110Sampling YCbCr444;
                static const St2110Sampling YCbCr422;
                static const St2110Sampling YCbCr420;
                static const St2110Sampling CLYCbCr444;
                static const St2110Sampling CLYCbCr422;
                static const St2110Sampling CLYCbCr420;
                static const St2110Sampling ICtCp444;
                static const St2110Sampling ICtCp422;
                static const St2110Sampling ICtCp420;
                static const St2110Sampling Rgb;
                static const St2110Sampling Xyz;
                static const St2110Sampling Key;
};

inline const St2110Sampling St2110Sampling::Invalid{0};
inline const St2110Sampling St2110Sampling::YCbCr444{1};
inline const St2110Sampling St2110Sampling::YCbCr422{2};
inline const St2110Sampling St2110Sampling::YCbCr420{3};
inline const St2110Sampling St2110Sampling::CLYCbCr444{4};
inline const St2110Sampling St2110Sampling::CLYCbCr422{5};
inline const St2110Sampling St2110Sampling::CLYCbCr420{6};
inline const St2110Sampling St2110Sampling::ICtCp444{7};
inline const St2110Sampling St2110Sampling::ICtCp422{8};
inline const St2110Sampling St2110Sampling::ICtCp420{9};
inline const St2110Sampling St2110Sampling::Rgb{10};
inline const St2110Sampling St2110Sampling::Xyz{11};
inline const St2110Sampling St2110Sampling::Key{12};

/**
 * @brief Well-known Enum type for the ST 2110-20 @c depth SDP fmtp parameter.
 *
 * Lists the per-sample bit depths defined by SMPTE ST 2110-20:2022
 * §7.4.2.  Wire form is @c "8" / @c "10" / @c "12" / @c "16" /
 * @c "16f" — emitted by the SDP layer.  The project-side
 * identifiers @c Bits8 / @c Bits10 / etc. avoid the leading-digit
 * problem.
 */
class St2110Depth : public TypedEnum<St2110Depth> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("St2110Depth", "ST 2110-20 Bit Depth", 0,
                                           {"Invalid", 0, "Invalid"},
                                           {"Bits8", 1, "8-bit"}, {"Bits10", 2, "10-bit"}, {"Bits12", 3, "12-bit"}, {"Bits16", 4, "16-bit"},
                                           {"Bits16f", 5, "16-bit Float"}); // default: Invalid

                using TypedEnum<St2110Depth>::TypedEnum;

                static const St2110Depth Invalid;
                static const St2110Depth Bits8;
                static const St2110Depth Bits10;
                static const St2110Depth Bits12;
                static const St2110Depth Bits16;
                static const St2110Depth Bits16f;
};

inline const St2110Depth St2110Depth::Invalid{0};
inline const St2110Depth St2110Depth::Bits8{1};
inline const St2110Depth St2110Depth::Bits10{2};
inline const St2110Depth St2110Depth::Bits12{3};
inline const St2110Depth St2110Depth::Bits16{4};
inline const St2110Depth St2110Depth::Bits16f{5};

/**
 * @brief Well-known Enum type for the ST 2110-20 @c colorimetry SDP fmtp parameter.
 *
 * Lists the colorimetric specifications defined by SMPTE ST 2110-20:2022
 * §7.5.  Wire form is all-uppercase (@c BT601, @c BT709, @c ST2065-1,
 * etc.) — emitted by the SDP layer.
 */
class St2110Colorimetry : public TypedEnum<St2110Colorimetry> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("St2110Colorimetry", "ST 2110-20 Colorimetry", 0,
                                           {"Invalid", 0, "Invalid"},
                                           {"Bt601", 1, "ITU-R BT.601"}, {"Bt709", 2, "ITU-R BT.709"}, {"Bt2020", 3, "ITU-R BT.2020"}, {"Bt2100", 4, "ITU-R BT.2100"},
                                           {"St2065_1", 5, "SMPTE ST 2065-1 (ACES)"}, {"St2065_3", 6, "SMPTE ST 2065-3 (ADX)"}, {"Unspecified", 7, "Unspecified"},
                                           {"Xyz", 8, "CIE XYZ"}, {"Alpha", 9, "Alpha (Key)"}); // default: Invalid

                using TypedEnum<St2110Colorimetry>::TypedEnum;

                static const St2110Colorimetry Invalid;
                static const St2110Colorimetry Bt601;
                static const St2110Colorimetry Bt709;
                static const St2110Colorimetry Bt2020;
                static const St2110Colorimetry Bt2100;
                static const St2110Colorimetry St2065_1;
                static const St2110Colorimetry St2065_3;
                static const St2110Colorimetry Unspecified;
                static const St2110Colorimetry Xyz;
                static const St2110Colorimetry Alpha;
};

inline const St2110Colorimetry St2110Colorimetry::Invalid{0};
inline const St2110Colorimetry St2110Colorimetry::Bt601{1};
inline const St2110Colorimetry St2110Colorimetry::Bt709{2};
inline const St2110Colorimetry St2110Colorimetry::Bt2020{3};
inline const St2110Colorimetry St2110Colorimetry::Bt2100{4};
inline const St2110Colorimetry St2110Colorimetry::St2065_1{5};
inline const St2110Colorimetry St2110Colorimetry::St2065_3{6};
inline const St2110Colorimetry St2110Colorimetry::Unspecified{7};
inline const St2110Colorimetry St2110Colorimetry::Xyz{8};
inline const St2110Colorimetry St2110Colorimetry::Alpha{9};

/**
 * @brief Well-known Enum type for the ST 2110-20 @c TCS SDP fmtp parameter.
 *
 * Lists the Transfer Characteristic System values defined by SMPTE
 * ST 2110-20:2022 §7.6.  Default value on the wire is @c SDR
 * (§7.6: "If the @c TCS value is not specified, receivers shall
 * assume the value @c SDR").  Wire form is all-uppercase
 * (@c SDR, @c BT2100LINPQ, @c ST2115LOGS3, etc.) — emitted by the
 * SDP layer.
 */
class St2110Tcs : public TypedEnum<St2110Tcs> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("St2110Tcs", "ST 2110-20 Transfer Characteristic System (TCS)", 1,
                                           {"Invalid", 0, "Invalid"},
                                           {"Sdr", 1, "Standard Dynamic Range (SDR)"}, {"Pq", 2, "Perceptual Quantizer (PQ)"}, {"Hlg", 3, "Hybrid Log-Gamma (HLG)"}, {"Linear", 4, "Linear"},
                                           {"Bt2100LinPq", 5, "BT.2100 Linear (PQ Reference)"}, {"Bt2100LinHlg", 6, "BT.2100 Linear (HLG Reference)"},
                                           {"St2065_1", 7, "SMPTE ST 2065-1 (ACES)"}, {"St428_1", 8, "SMPTE ST 428-1 (DCDM)"},
                                           {"Density", 9, "Density"}, {"St2115LogS3", 10, "SMPTE ST 2115 Log S3"},
                                           {"Unspecified", 11, "Unspecified"}); // default: Sdr

                using TypedEnum<St2110Tcs>::TypedEnum;

                static const St2110Tcs Invalid;
                static const St2110Tcs Sdr;
                static const St2110Tcs Pq;
                static const St2110Tcs Hlg;
                static const St2110Tcs Linear;
                static const St2110Tcs Bt2100LinPq;
                static const St2110Tcs Bt2100LinHlg;
                static const St2110Tcs St2065_1;
                static const St2110Tcs St428_1;
                static const St2110Tcs Density;
                static const St2110Tcs St2115LogS3;
                static const St2110Tcs Unspecified;
};

inline const St2110Tcs St2110Tcs::Invalid{0};
inline const St2110Tcs St2110Tcs::Sdr{1};
inline const St2110Tcs St2110Tcs::Pq{2};
inline const St2110Tcs St2110Tcs::Hlg{3};
inline const St2110Tcs St2110Tcs::Linear{4};
inline const St2110Tcs St2110Tcs::Bt2100LinPq{5};
inline const St2110Tcs St2110Tcs::Bt2100LinHlg{6};
inline const St2110Tcs St2110Tcs::St2065_1{7};
inline const St2110Tcs St2110Tcs::St428_1{8};
inline const St2110Tcs St2110Tcs::Density{9};
inline const St2110Tcs St2110Tcs::St2115LogS3{10};
inline const St2110Tcs St2110Tcs::Unspecified{11};

/**
 * @brief Well-known Enum type for the ST 2110-20 @c RANGE SDP fmtp parameter.
 *
 * §7.3: @c NARROW or @c FULL when paired with BT.2100 colorimetry;
 * @c NARROW, @c FULL, or @c FULLPROTECT in any other context.
 * Default @c NARROW (§7.3: "In the absence of this parameter,
 * @c NARROW shall be the assumed value in either case").  Wire form
 * is all-uppercase — emitted by the SDP layer.
 */
class St2110Range : public TypedEnum<St2110Range> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("St2110Range", "ST 2110-20 Quantization Range", 1,
                                           {"Invalid", 0, "Invalid"},
                                           {"Narrow", 1, "Narrow Range"}, {"Full", 2, "Full Range"},
                                           {"FullProtect", 3, "Full Range (Protected)"}); // default: Narrow

                using TypedEnum<St2110Range>::TypedEnum;

                static const St2110Range Invalid;
                static const St2110Range Narrow;
                static const St2110Range Full;
                static const St2110Range FullProtect;
};

inline const St2110Range St2110Range::Invalid{0};
inline const St2110Range St2110Range::Narrow{1};
inline const St2110Range St2110Range::Full{2};
inline const St2110Range St2110Range::FullProtect{3};

/**
 * @brief Well-known Enum type for the ST 2110-20 @c PM (Packing Mode) SDP fmtp parameter.
 *
 * §6.3: General Packing Mode (wire @c 2110GPM) is the default and
 * allows any pgroup-aligned packetization; Block Packing Mode
 * (wire @c 2110BPM) constrains every packet to a multiple-of-180-
 * octet payload.  The wire form starts with a leading digit, so the
 * project-side identifiers are @c Gpm / @c Bpm.
 */
class St2110PackingMode : public TypedEnum<St2110PackingMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("St2110PackingMode", "ST 2110-20 Packing Mode", 1,
                                           {"Invalid", 0, "Invalid"},
                                           {"Gpm", 1, "General Packing Mode (GPM)"},
                                           {"Bpm", 2, "Block Packing Mode (BPM)"}); // default: Gpm

                using TypedEnum<St2110PackingMode>::TypedEnum;

                static const St2110PackingMode Invalid;
                static const St2110PackingMode Gpm;
                static const St2110PackingMode Bpm;
};

inline const St2110PackingMode St2110PackingMode::Invalid{0};
inline const St2110PackingMode St2110PackingMode::Gpm{1};
inline const St2110PackingMode St2110PackingMode::Bpm{2};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
