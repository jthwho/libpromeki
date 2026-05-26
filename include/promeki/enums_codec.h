/**
 * @file      enums_codec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Video encoder rate-control / preset enums.
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
 * @brief Well-known Enum type for codec rate-control modes (audio + video).
 *
 * Selects the rate-control strategy a codec uses when producing a
 * bitstream.  Single shared enum so audio and video sides of the codec
 * API can describe and configure rate-control with the same vocabulary.
 *
 * - @c CBR — constant bitrate.  The encoder targets
 *   @ref MediaConfig::BitrateKbps with as little short-term variation
 *   as possible.  Use for live streaming, broadcast contribution, and
 *   any transport where bandwidth must be tightly bounded.
 * - @c VBR — variable bitrate.  The encoder targets an average
 *   bitrate but allows short-term variation to preserve quality on
 *   complex content.  Use for file storage where the average
 *   matters but instantaneous peaks do not.
 * - @c ABR — average bitrate.  Long-term average is held to the
 *   target while short-term rate is allowed to drift more freely than
 *   under VBR.  Common on audio codecs (MP3, AAC).
 * - @c CQP — constant quantization parameter.  The encoder ignores
 *   the bitrate target and instead holds quality constant; the
 *   resulting bitrate varies with content complexity.  Use for
 *   testing / quality analysis where reproducible quality matters
 *   more than bitrate.  Common on video codecs.
 */
class RateControlMode : public TypedEnum<RateControlMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("RateControlMode", "Rate Control Mode", 1,
                                                   {"CBR", 0, "Constant Bitrate (CBR)"},
                                                   {"VBR", 1, "Variable Bitrate (VBR)"},
                                                   {"ABR", 2, "Average Bitrate (ABR)"},
                                                   {"CQP", 3, "Constant Quantizer (CQP)"}); // default: VBR

                using TypedEnum<RateControlMode>::TypedEnum;

                static const RateControlMode CBR;
                static const RateControlMode VBR;
                static const RateControlMode ABR;
                static const RateControlMode CQP;
};

inline const RateControlMode RateControlMode::CBR{0};
inline const RateControlMode RateControlMode::VBR{1};
inline const RateControlMode RateControlMode::ABR{2};
inline const RateControlMode RateControlMode::CQP{3};

/**
 * @brief Well-known Enum type for video-encoder speed / quality presets.
 *
 * Presets are neutral names for points along the
 * encode-speed-vs-quality curve.  Each concrete backend maps the
 * generic preset onto its own native preset enum (NVENC's P1–P7,
 * x264's @c ultrafast…@c placebo, QSV's target usage, etc.).
 *
 * - @c UltraLowLatency — absolute minimum encode latency.  Typically
 *   disables B-frames, look-ahead, and multi-pass.  Use for live
 *   conferencing / interactive capture where every frame of latency
 *   costs.
 * - @c LowLatency      — low-latency tuning with some coding tools
 *   enabled.  Suitable for live streaming contribution.
 * - @c Balanced        — default midpoint.  Reasonable latency and
 *   reasonable quality at a sensible CPU / GPU cost.
 * - @c HighQuality     — prioritise quality: multi-pass / look-ahead /
 *   slower motion search.  Use for file-based encoding where
 *   latency is unconstrained.
 */
class VideoEncoderPreset : public TypedEnum<VideoEncoderPreset> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("VideoEncoderPreset", "Video Encoder Preset", 2,
                                                   {"UltraLowLatency", 0, "Ultra-Low Latency"},
                                                   {"LowLatency", 1, "Low Latency"}, {"Balanced", 2, "Balanced"},
                                                   {"HighQuality", 3, "High Quality"},
                                                   {"Lossless", 4, "Lossless"}); // default: Balanced

                using TypedEnum<VideoEncoderPreset>::TypedEnum;

                static const VideoEncoderPreset UltraLowLatency;
                static const VideoEncoderPreset LowLatency;
                static const VideoEncoderPreset Balanced;
                static const VideoEncoderPreset HighQuality;
                static const VideoEncoderPreset Lossless;
};

inline const VideoEncoderPreset VideoEncoderPreset::UltraLowLatency{0};
inline const VideoEncoderPreset VideoEncoderPreset::LowLatency{1};
inline const VideoEncoderPreset VideoEncoderPreset::Balanced{2};
inline const VideoEncoderPreset VideoEncoderPreset::HighQuality{3};
inline const VideoEncoderPreset VideoEncoderPreset::Lossless{4};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
