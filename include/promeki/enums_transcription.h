/**
 * @file      enums_transcription.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Speech transcription mode enums.
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
 * @brief Operating mode a @ref TranscriptionEngine session runs in.
 *
 * Used as the @ref TranscriptionConfig::mode field to tell the engine
 * whether the caller expects interim hypotheses as audio arrives or
 * only the final transcript once the input stream has been fully
 * submitted.
 *
 * - @c Streaming — the engine may emit partial cues during
 *                  @c submitFrame as it accumulates audio.  Partial
 *                  cues carry @ref Subtitle::partial @c true; the
 *                  same span of audio typically re-emits as a
 *                  finalised cue (with @ref Subtitle::partial
 *                  @c false) once an endpoint heuristic fires
 *                  (silence gap, punctuation, VAD trailing edge).
 *                  Useful for live caption overlays and low-latency
 *                  UI feedback.
 * - @c Batch     — the engine accumulates audio silently and only
 *                  starts emitting cues after @c flush.  Cues are
 *                  always finalised (@ref Subtitle::partial
 *                  @c false).  Useful for offline transcription
 *                  where global rescoring / two-pass decoders
 *                  produce measurably better results than streaming.
 *
 * Engines that only support one mode reject the other at
 * @c configure with @c Error::NotSupported; the
 * @ref TranscriptionEngine::BackendRecord declares the supported
 * modes so callers can pick a compatible backend up front.
 */
class TranscriptionMode : public TypedEnum<TranscriptionMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("TranscriptionMode", "Transcription Mode", 0,
                                                   {"Streaming", 0, "Streaming (Interim Cues)"},
                                                   {"Batch", 1, "Batch (Final Only)"}); // default: Streaming

                using TypedEnum<TranscriptionMode>::TypedEnum;

                static const TranscriptionMode Streaming;
                static const TranscriptionMode Batch;
};

inline const TranscriptionMode TranscriptionMode::Streaming{0};
inline const TranscriptionMode TranscriptionMode::Batch{1};

/**
 * @brief How a @ref TranscriptionConfig selects audio for transcription.
 *
 * The engine receives multichannel PCM through
 * @ref TranscriptionEngine::submitFrame and must decide which sample
 * stream actually feeds the speech-to-text decoder.  This enum picks
 * the selection strategy; the @c TranscriptionConfig carries the
 * accompanying payload (an @ref AudioChannelMap for role-based
 * selection, an @c int for single-channel selection, or nothing for
 * full downmix).
 *
 * - @c ChannelMap   — use the engine-specific downmix of the channels
 *                     whose @ref ChannelRole appears in the configured
 *                     @ref AudioChannelMap.  Example: @c {FrontCenter}
 *                     to listen only to the dialog stem in a 5.1
 *                     bed; @c {Mono} to pick out a Commentary track
 *                     in a multi-stream buffer.
 * - @c ChannelIndex — pick exactly one channel by its zero-based
 *                     index in the source @ref PcmAudioPayload.  Use
 *                     when the caller already knows the physical
 *                     channel layout.
 * - @c DownmixAll   — the engine sums every channel to mono and
 *                     transcribes that.  Useful as a degraded
 *                     fallback when the caller has no role / index
 *                     information.
 */
class TranscriptionChannelMode : public TypedEnum<TranscriptionChannelMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("TranscriptionChannelMode", "Transcription Channel Mode", 0,
                                                   {"ChannelMap", 0, "Channel Map (By Role)"},
                                                   {"ChannelIndex", 1, "Channel Index (Single Channel)"},
                                                   {"DownmixAll", 2, "Downmix All to Mono"}); // default: ChannelMap

                using TypedEnum<TranscriptionChannelMode>::TypedEnum;

                static const TranscriptionChannelMode ChannelMap;
                static const TranscriptionChannelMode ChannelIndex;
                static const TranscriptionChannelMode DownmixAll;
};

inline const TranscriptionChannelMode TranscriptionChannelMode::ChannelMap{0};
inline const TranscriptionChannelMode TranscriptionChannelMode::ChannelIndex{1};
inline const TranscriptionChannelMode TranscriptionChannelMode::DownmixAll{2};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
