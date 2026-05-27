/**
 * @file      enums_ndi.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * NDI receiver bandwidth / color-format enums.
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
 * @brief Well-known Enum type for the NDI receiver bandwidth tier.
 *
 * Selects how much bandwidth an `NdiMediaIO` source asks the SDK
 * for at @c NDIlib_recv_create_v3 time.  Drives the
 * @ref MediaConfig::NdiBandwidth config key.  The integer values
 * are libpromeki-internal — the backend translates them to the
 * matching @c NDIlib_recv_bandwidth_e value when calling the SDK.
 *
 * - @c Highest        — full-quality video + audio + metadata.
 * - @c Lowest         — preview-quality video, full audio + metadata.
 * - @c AudioOnly      — audio + metadata, no video frames.
 * - @c MetadataOnly   — metadata only (PTZ / tally / KVM control).
 */
class NdiBandwidth : public TypedEnum<NdiBandwidth> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("NdiBandwidth", "NDI Bandwidth", 0,
                                                   {"Highest", 0, "Highest (Full Quality)"},
                                                   {"Lowest", 1, "Lowest (Preview Quality)"},
                                                   {"AudioOnly", 2, "Audio Only"},
                                                   {"MetadataOnly", 3, "Metadata Only"}); // default: Highest

                using TypedEnum<NdiBandwidth>::TypedEnum;

                static const NdiBandwidth Highest;
                static const NdiBandwidth Lowest;
                static const NdiBandwidth AudioOnly;
                static const NdiBandwidth MetadataOnly;
};

inline const NdiBandwidth NdiBandwidth::Highest{0};
inline const NdiBandwidth NdiBandwidth::Lowest{1};
inline const NdiBandwidth NdiBandwidth::AudioOnly{2};
inline const NdiBandwidth NdiBandwidth::MetadataOnly{3};

/**
 * @brief Well-known Enum type for the NDI receiver color-format hint.
 *
 * Maps to `NDIlib_recv_color_format_e`.  Drives the
 * @ref MediaConfig::NdiColorFormat config key, controlling what
 * FourCC family the SDK delivers for a given source.
 *
 * - @c Best       — keep the source's native FourCC where possible.
 *                   Right choice for high-bit-depth pipelines (P216
 *                   stays P216, etc.) — the SDK won't quietly
 *                   down-convert.  Note: the Advanced SDK delivers
 *                   PA16 (4:2:2:4 16-bit planar+alpha) under this
 *                   mode which libpromeki does not yet decode.
 * - @c Fastest    — (**default**) minimize the SDK's per-frame work.
 *                   Returns the format on the wire (UYVY for 8-bit,
 *                   P216 for 10/12/16-bit) — both are handled by
 *                   the capture loop.  Avoids the PA16 delivery
 *                   that @c Best produces with the Advanced SDK.
 * - @c UyvyBgra   — 8-bit YUV 4:2:2 (UYVY) for opaque frames,
 *                   BGRA for sources that carry alpha.
 * - @c UyvyRgba   — same as above with RGBA instead of BGRA.
 * - @c BgrxBgra   — BGRX for opaque, BGRA for alpha.
 * - @c RgbxRgba   — RGBX for opaque, RGBA for alpha.
 */
class NdiColorFormat : public TypedEnum<NdiColorFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("NdiColorFormat", "NDI Color Format", 0,
                                                   {"Best", 0, "Best (Native FourCC)"},
                                                   {"Fastest", 1, "Fastest (On-Wire Format)"},
                                                   {"UyvyBgra", 2, "UYVY / BGRA"}, {"UyvyRgba", 3, "UYVY / RGBA"},
                                                   {"BgrxBgra", 4, "BGRX / BGRA"},
                                                   {"RgbxRgba", 5, "RGBX / RGBA"}); // default: Fastest (see MediaConfig::NdiColorFormat)

                using TypedEnum<NdiColorFormat>::TypedEnum;

                static const NdiColorFormat Best;
                static const NdiColorFormat Fastest;
                static const NdiColorFormat UyvyBgra;
                static const NdiColorFormat UyvyRgba;
                static const NdiColorFormat BgrxBgra;
                static const NdiColorFormat RgbxRgba;
};

inline const NdiColorFormat NdiColorFormat::Best{0};
inline const NdiColorFormat NdiColorFormat::Fastest{1};
inline const NdiColorFormat NdiColorFormat::UyvyBgra{2};
inline const NdiColorFormat NdiColorFormat::UyvyRgba{3};
inline const NdiColorFormat NdiColorFormat::BgrxBgra{4};
inline const NdiColorFormat NdiColorFormat::RgbxRgba{5};

/**
 * @brief Well-known Enum type for the NDI receive-side bit-depth tag.
 *
 * NDI's P216 wire format always carries 16-bit-container 4:2:2 data,
 * but the actual semantic precision (10 / 12 / 16) is *not*
 * signalled by the FourCC.  This enum lets a caller who knows the
 * upstream source's true precision ask the receiver to tag emitted
 * frames with a narrower promeki PixelFormat::ID, avoiding a
 * downstream 16→10 conversion.  Drives the
 * @ref MediaConfig::NdiReceiveBitDepth config key.
 *
 * - @c Auto    — receiver tags frames as 16-bit (precision-honest).
 *                Default.
 * - @c Bits10  — tag P216 frames as
 *                @c YUV10_422_SemiPlanar_LE_Rec709.  Caller-side
 *                promise: the upstream is producing 10-bit content.
 * - @c Bits12  — same, for 12-bit content
 *                (@c YUV12_422_SemiPlanar_LE_Rec709).
 * - @c Bits16  — explicit form of @c Auto for callers that want to
 *                document the choice.
 */
class NdiReceiveBitDepth : public TypedEnum<NdiReceiveBitDepth> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("NdiReceiveBitDepth", "NDI Receive Bit Depth", 0,
                                                   {"Auto", 0, "Automatic (16-bit)"}, {"Bits10", 10, "10-bit"},
                                                   {"Bits12", 12, "12-bit"}, {"Bits16", 16, "16-bit"}); // default: Auto

                using TypedEnum<NdiReceiveBitDepth>::TypedEnum;

                static const NdiReceiveBitDepth Auto;
                static const NdiReceiveBitDepth Bits10;
                static const NdiReceiveBitDepth Bits12;
                static const NdiReceiveBitDepth Bits16;
};

inline const NdiReceiveBitDepth NdiReceiveBitDepth::Auto{0};
inline const NdiReceiveBitDepth NdiReceiveBitDepth::Bits10{10};
inline const NdiReceiveBitDepth NdiReceiveBitDepth::Bits12{12};
inline const NdiReceiveBitDepth NdiReceiveBitDepth::Bits16{16};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
