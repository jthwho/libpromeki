/**
 * @file      captionencoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/cea708cdp.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/namespace.h>
#include <promeki/subtitle.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract base for closed-caption encoders.
 * @ingroup proav
 *
 * Codec-agnostic interface that consumes a @ref SubtitleList
 * timeline and yields per-frame @ref Cea708Cdp::CcDataList payloads.
 * Concrete implementations:
 *
 *  - @ref Cea608Encoder — emits line-21 byte pairs (@c cc_type 0 / 1)
 *    matching the configured channel.
 *  - @ref Cea708Encoder — emits DTVCC packet triples
 *    (@c cc_type 2 / 3) targeting the configured service number.
 *
 * Both shapes can ride in the same CDP's @c cc_data list, so a
 * producer (e.g. @ref TpgMediaIO) can hold a
 * @c List<UniquePtr<CaptionEncoder>> and merge each encoder's output
 * per frame to carry simultaneous 608 + 708.
 *
 * @par Storage and copy semantics
 *
 * Stateful worker.  Copy / move are deleted — instantiate one per
 * encode session via @ref create.
 *
 * @par Thread Safety
 *
 * Not thread-safe.  Each encoder instance is single-threaded.
 *
 * @see CaptionCodec, Cea608Encoder, Cea708Encoder, Cea708Cdp, SubtitleList
 */
class CaptionEncoder {
        public:
                virtual ~CaptionEncoder() = default;

                CaptionEncoder(const CaptionEncoder &) = delete;
                CaptionEncoder &operator=(const CaptionEncoder &) = delete;
                CaptionEncoder(CaptionEncoder &&) = delete;
                CaptionEncoder &operator=(CaptionEncoder &&) = delete;

                /** @brief Common factory config.
                 *
                 * Per-codec fields are ignored by codecs that don't use them
                 * (e.g. @c serviceNumber is meaningless for CEA-608 and
                 * silently passed through). */
                struct Config {
                                /// @brief Required.  Drives ms → frame conversion
                                ///        and pre-roll budgeting.
                                FrameRate frameRate;
                                /// @brief DTVCC service number (1..63).
                                ///        Ignored by CEA-608.  Default 1.
                                uint8_t serviceNumber = 1;
                                /// @brief Visible columns advertised by the
                                ///        708 DefineWindow command.  Ignored
                                ///        by CEA-608.  Default 32.
                                int32_t windowCols = 32;
                };

                /** @brief Wire codec this encoder produces. */
                virtual CaptionCodec codec() const = 0;

                /** @brief Frame rate the per-frame schedule is anchored to. */
                virtual FrameRate frameRate() const = 0;

                /**
                 * @brief Loads the cue timeline into the encoder's
                 *        per-frame schedule.  Replaces any previous
                 *        schedule.
                 *
                 * @return @c Error::Ok on success.  Codec-specific
                 *         errors otherwise (see the concrete encoder's
                 *         documentation).
                 */
                virtual Error setSubtitles(const SubtitleList &subs) = 0;

                /**
                 * @brief Returns the subset of @p in the encoder can
                 *        accept without failing @ref setSubtitles.
                 *
                 * Default implementation: returns all cues unchanged
                 * (the codec accepts any well-formed timeline).
                 * Concrete encoders (notably @ref Cea608Encoder) may
                 * override to apply pre-roll / back-to-back / wire-
                 * density filtering.
                 *
                 * @param in          Input cue list, sorted by start.
                 * @param outDropped  When non-null, dropped cues are
                 *                    appended for diagnostics.
                 * @return The subset that survives the filter.
                 */
                virtual SubtitleList encodableSubset(const SubtitleList &in,
                                                     SubtitleList      *outDropped = nullptr) const {
                        if (outDropped != nullptr) *outDropped = SubtitleList();
                        return in;
                }

                /**
                 * @brief Returns the @c CcData triples scheduled for
                 *        @p frame.  Empty list when no payload is
                 *        scheduled.
                 *
                 * Implementations must return triples whose @c cc_type
                 * matches the codec's wire shape (0/1 for CEA-608,
                 * 2/3 for CEA-708).  The caller is free to merge
                 * lists from multiple encoders into the same CDP.
                 */
                virtual Cea708Cdp::CcDataList nextFrame(FrameNumber frame) const = 0;

                /** @brief Clears the schedule. */
                virtual void reset() = 0;

                /**
                 * @brief Constructs a concrete encoder for @p codec.
                 *
                 * Returns a null @ref UniquePtr when @p codec is
                 * unrecognised or carries no per-codec wire encoder
                 * (e.g. @ref CaptionCodec::Both — callers wanting
                 * dual carriage construct one encoder per codec).
                 */
                static UniquePtr<CaptionEncoder> create(CaptionCodec codec, const Config &cfg);

        protected:
                CaptionEncoder() = default;
};

PROMEKI_NAMESPACE_END
