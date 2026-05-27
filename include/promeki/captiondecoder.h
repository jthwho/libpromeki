/**
 * @file      captiondecoder.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/cea708cdp.h>
#include <promeki/enums_subtitle.h>
#include <promeki/framenumber.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract base for closed-caption decoders.
 * @ingroup proav
 *
 * Codec-agnostic interface that consumes a per-frame sequence of
 * @ref Cea708Cdp::CcDataList payloads (typically pulled from an
 * @ref AncPayload that arrived via RTP-40, HLS SEI, or any other
 * CDP-bearing transport) and reconstructs the @ref SubtitleList
 * that produced them.  Concrete implementations:
 *
 *  - @ref Cea608Decoder — consumes line-21 byte pairs
 *    (@c cc_type 0 / 1) for the configured channel.
 *  - @ref Cea708Decoder — consumes DTVCC packet triples
 *    (@c cc_type 2 / 3) for the configured service number.
 *
 * Both shapes ride in the same CDP's @c cc_data list, so a consumer
 * (e.g. @ref SubtitleBurnMediaIO) can hold a
 * @c List<UniquePtr<CaptionDecoder>> and feed each frame's
 * @c CcDataList to every decoder — each filters to its own wire
 * shape and accumulates an independent @ref SubtitleList.
 *
 * @par Storage and copy semantics
 *
 * Stateful worker.  Copy / move are deleted — instantiate one per
 * decode session via @ref create.
 *
 * @par Thread Safety
 *
 * Not thread-safe.  @ref pushFrame must be called serially on a
 * given decoder instance.
 *
 * @see CaptionCodec, Cea608Decoder, Cea708Decoder, Cea708Cdp,
 *      SubtitleList, CaptionEncoder
 */
class CaptionDecoder {
        public:
                /** @brief Owning list-of-decoders alias.
                 *
                 * Concrete decoders are stateful and non-copyable, so
                 * the natural container is a @ref List of owning
                 * @ref UniquePtr.  Consumers that mix codecs (e.g.
                 * @ref SubtitleBurnMediaIO listening to both
                 * @ref SubtitleSource::Cea608Anc and
                 * @ref SubtitleSource::Cea708Anc on the same stream)
                 * hold one of these and dispatch by @ref codec.
                 *
                 * Named @c PtrList (not @c List) per the project
                 * convention — @c ::List is reserved for value-type
                 * containers, which @ref CaptionDecoder cannot provide
                 * since it is abstract and non-copyable.
                 */
                using PtrList = promeki::List<UniquePtr<CaptionDecoder>>;

                virtual ~CaptionDecoder() = default;

                CaptionDecoder(const CaptionDecoder &) = delete;
                CaptionDecoder &operator=(const CaptionDecoder &) = delete;
                CaptionDecoder(CaptionDecoder &&) = delete;
                CaptionDecoder &operator=(CaptionDecoder &&) = delete;

                /** @brief Common factory config.
                 *
                 * Per-codec fields are ignored by codecs that don't use them
                 * (e.g. @c serviceNumber is meaningless for CEA-608 and
                 * silently passed through). */
                struct Config {
                                /// @brief DTVCC service number (1..63).
                                ///        Ignored by CEA-608.  Default 1.
                                uint8_t serviceNumber = 1;
                };

                /** @brief Wire codec this decoder consumes. */
                virtual CaptionCodec codec() const = 0;

                /**
                 * @brief Feeds one frame's worth of @c CcData triples.
                 *
                 * Implementations filter @p data to the triples whose
                 * @c cc_type matches the codec's wire shape (0/1 for
                 * CEA-608, 2/3 for CEA-708) and update internal cue
                 * state.  Triples for other codecs are ignored, so the
                 * same @c CcDataList may be fanned out to multiple
                 * decoders.
                 *
                 * @param frame  Frame number (advisory; the decoder
                 *               uses @p ts for cue timestamping).
                 * @param ts     Media-relative @ref TimeStamp (epoch =
                 *               media t=0) at this frame.  Used to
                 *               record cue @c start / @c end.
                 * @param data   The frame's @c CcData list — typically
                 *               @ref Cea708Cdp::ccData for the CDP
                 *               attached to the frame.
                 */
                virtual void pushFrame(FrameNumber frame, TimeStamp ts,
                                       const Cea708Cdp::CcDataList &data) = 0;

                /**
                 * @brief Returns the currently displayed plain text.
                 *
                 * Empty when no cue is on screen (either nothing has
                 * been pushed yet or the last clear / end event
                 * cleared the displayed memory).  Useful for live
                 * renderers that want to query state between
                 * @ref pushFrame calls.
                 *
                 * Returned by value so that codecs whose live text is
                 * composed on-the-fly (e.g. CEA-708 window flattening)
                 * can satisfy the contract without caching a
                 * referenceable copy.  @ref String is a CoW handle,
                 * so returning by value is cheap.
                 *
                 * For the *styled* cue (spans + anchor recovered from
                 * codec-specific style codes) use @ref displayedCue —
                 * this accessor is the flat-text fast path for
                 * callers that don't need the attributes.
                 */
                virtual String displayedText() const = 0;

                /**
                 * @brief Returns the currently displayed cue as a
                 *        @ref Subtitle, carrying the styled spans and
                 *        anchor recovered from codec-specific style
                 *        codes.
                 *
                 * Empty (@ref Subtitle::isEmpty returns @c true) when
                 * no cue is displayed.  The cue's @c start is the
                 * @ref TimeStamp at which the cue became visible;
                 * @c end is the most recent @ref pushFrame timestamp
                 * (the cue is still live, so the end is tentative —
                 * finalised when the cue clears).
                 */
                virtual Subtitle displayedCue() const = 0;

                /**
                 * @brief Emits the accumulated @ref SubtitleList.
                 *
                 * If a cue is still displayed at finalize time (the
                 * stream ended before a clear event fired), the cue
                 * is emitted with @c end set to the @ref TimeStamp of
                 * the most recent @ref pushFrame call.
                 *
                 * After finalize, the decoder is reset to its
                 * initial state.
                 */
                virtual SubtitleList finalize() = 0;

                /**
                 * @brief Resets the decoder without emitting anything.
                 *
                 * Drops any in-flight cue and any pending
                 * non-displayed memory.  Use when re-feeding the
                 * decoder from a new source mid-session.
                 */
                virtual void reset() = 0;

                /**
                 * @brief Constructs a concrete decoder for @p codec.
                 *
                 * Returns a null @ref UniquePtr when @p codec is
                 * unrecognised or carries no per-codec wire decoder
                 * (e.g. @ref CaptionCodec::Both — callers wanting
                 * dual decoding construct one decoder per codec).
                 */
                static UniquePtr<CaptionDecoder> create(CaptionCodec codec, const Config &cfg);

        protected:
                CaptionDecoder() = default;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
