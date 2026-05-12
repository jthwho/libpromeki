/**
 * @file      cea608decoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/cea608encoder.h>
#include <promeki/cea708cdp.h>
#include <promeki/framenumber.h>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

struct Cea608DecoderImpl; // Pimpl — defined in cea608decoder.cpp.

/**
 * @brief Stateful CEA-608 caption decoder.
 * @ingroup proav
 *
 * Inverse of @ref Cea608Encoder: consumes a frame-by-frame sequence
 * of @ref Cea708Cdp::CcDataList payloads (typically pulled from an
 * @ref AncPayload that arrived via RTP-40 or any other CDP-bearing
 * transport) and reconstructs the @ref SubtitleList that produced
 * them.  Round-trip with @ref Cea608Encoder is byte-exact for the
 * pop-on subset implemented here; cue start / end timestamps are
 * the actual frame timestamps at which @c EOC and @c EDM fired,
 * which are precisely the encoder's scheduled `cue.start` and
 * `cue.end` frames (modulo the inherent 1-frame quantisation of
 * the wire format).
 *
 * @par Pop-on state machine
 *
 * The decoder tracks the spec's two memory buffers:
 *
 *  - **Non-displayed** memory: where @c RCL puts the writer, and
 *    where character pairs accumulate while a cue is being loaded.
 *  - **Displayed** memory: the text currently on screen.  Swapped
 *    with non-displayed when @c EOC fires.
 *
 * The state transitions:
 *
 *  - @c RCL → clear non-displayed; remember "loading" state.
 *  - PAC (0x14 0x40..0x7F) → acknowledged; v1 ignores row/column
 *    details since the encoder only emits row 15 / col 0 / white.
 *  - Character pair (0x20..0x7F first byte) → append to
 *    non-displayed memory.
 *  - @c EOC → swap non-displayed ↔ displayed; record the current
 *    frame's @ref TimeStamp as the start of the now-visible cue.
 *  - @c EDM → finalize the visible cue with @c end = current
 *    @ref TimeStamp; emit it into the accumulated list.
 *  - @c ENM → clear non-displayed memory (rare; mostly a no-op
 *    after @c RCL has already cleared it).
 *
 * @par Spec robustness behaviours
 *
 *  - **Doubled control codes**: per CEA-608 §8.4, all control-code
 *    pairs are transmitted twice.  The decoder treats two
 *    consecutive identical control codes as a single occurrence
 *    (only the first is processed).  A character pair between two
 *    identical control codes breaks the "consecutive" rule and the
 *    second is processed again.
 *  - **Parity failure**: bytes whose parity is wrong are treated
 *    as the null pair (skipped).
 *  - **Null pair** (0x00 0x00 after parity strip, or 0x80 0x80 on
 *    the wire): no-op.
 *  - **Unknown control codes** (DER, RU2..4, FON, RDC, …) are
 *    ignored in v1 since the encoder does not emit them.
 *
 * @par Scope
 *
 * v1 implements **CC1 only** (field 1, channel 1) and pop-on mode.
 * Paint-on and roll-up modes are deferred to Phase 3.5d.  Multi-row
 * decoding lands alongside them.  Cue text reconstruction strips
 * parity but does not interpret extended character sets — the
 * basic ASCII range (0x20..0x7E) passes through verbatim.
 *
 * @par Channel filtering
 *
 *  - @ref Cea708Cdp::CcData triples with @c cc_type != 0 (i.e.
 *    field 2 = CC3/CC4) are skipped when configured for CC1.
 *  - Within field 1, control codes whose first byte falls in the
 *    CC2 range (0x18..0x1F) are skipped.  Character pairs inherit
 *    the most recently seen control code's channel.
 *
 * @par Storage and copy semantics
 *
 * Stateful worker (pimpl, copy/move-deleted).  Instantiate one per
 * decode session.
 *
 * @par Thread Safety
 *
 * Not thread-safe.  @ref pushFrame must be called serially.
 *
 * @see Cea608, Cea608Encoder, Cea708Cdp, SubtitleList
 */
class Cea608Decoder {
        public:
                /** @brief Channel alias matching @ref Cea608Encoder::Channel. */
                using Channel = Cea608Encoder::Channel;

                /** @brief Decoder configuration. */
                struct Config {
                                /// @brief Which CEA-608 channel to decode.  v1: must be @c CC1.
                                Channel channel = Channel::CC1;
                };

                Cea608Decoder();
                explicit Cea608Decoder(Config cfg);
                ~Cea608Decoder();

                Cea608Decoder(const Cea608Decoder &) = delete;
                Cea608Decoder &operator=(const Cea608Decoder &) = delete;
                Cea608Decoder(Cea608Decoder &&) = delete;
                Cea608Decoder &operator=(Cea608Decoder &&) = delete;

                /** @brief Returns the configuration this decoder was constructed with. */
                const Config &config() const;

                /**
                 * @brief Feeds one frame's worth of @c CcData triples.
                 *
                 * @param frame  Frame number (advisory; the decoder
                 *               uses @p ts for cue timestamping).
                 * @param ts     Media-relative @ref TimeStamp (epoch =
                 *               media t=0) at this frame.  Used as the
                 *               cue start when @c EOC fires and the
                 *               cue end when @c EDM fires.
                 * @param data   The frame's CcData list — typically
                 *               @ref Cea708Cdp::ccData for the CDP
                 *               attached to the frame, or the cc_data
                 *               triple list directly from an SEI /
                 *               other transport.
                 */
                void pushFrame(FrameNumber frame, TimeStamp ts, const Cea708Cdp::CcDataList &data);

                /**
                 * @brief Returns the text currently displayed
                 *        (post most-recent @c EOC).
                 *
                 * Empty when no cue is displayed (either nothing has
                 * been pushed yet or the last @c EDM cleared the
                 * displayed memory).  Useful for live renderers that
                 * want to query state between @ref pushFrame calls.
                 *
                 * For the *styled* cue (spans + anchor recovered
                 * from PAC + mid-row codes) use @ref displayedCue —
                 * this accessor is the flat-text fast path for
                 * callers that don't need the attributes.
                 */
                const String &displayedText() const;

                /**
                 * @brief Returns the currently displayed cue as a
                 *        @ref Subtitle, carrying the styled spans
                 *        and anchor recovered from PAC + mid-row
                 *        codes.
                 *
                 * Empty (@ref Subtitle::isEmpty returns @c true)
                 * when no cue is displayed.  The cue's @c start
                 * is the @ref TimeStamp at which @c EOC fired;
                 * @c end is the most recent @ref pushFrame
                 * timestamp (the cue is still live, so the end is
                 * tentative — finalised when @c EDM or the next
                 * @c EOC fires).
                 *
                 * Use this for live renderers that want the full
                 * attribute set rather than the flat-text fast path
                 * exposed by @ref displayedText.
                 */
                Subtitle displayedCue() const;

                /**
                 * @brief Emits the accumulated @ref SubtitleList.
                 *
                 * If a cue is still displayed at finalize time (the
                 * stream ended before an @c EDM fired), the cue is
                 * emitted with @c end set to the @ref TimeStamp of
                 * the most recent @ref pushFrame call.
                 *
                 * After finalize, the decoder is reset to its
                 * initial state.
                 */
                SubtitleList finalize();

                /**
                 * @brief Resets the decoder without emitting anything.
                 *
                 * Drops any in-flight cue and any pending non-displayed
                 * memory.  Use when re-feeding the decoder from a new
                 * source mid-session.
                 */
                void reset();

        private:
                SharedPtr<Cea608DecoderImpl> _d;
};

PROMEKI_NAMESPACE_END
