/**
 * @file      cea608encoder.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/captionencoder.h>
#include <promeki/cea708cdp.h>
#include <promeki/enums_subtitle.h>
#include <promeki/error.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>

PROMEKI_NAMESPACE_BEGIN

struct Cea608EncoderImpl; // Pimpl — defined in cea608encoder.cpp.

/**
 * @brief Stateful CEA-608 caption encoder.
 * @ingroup proav
 *
 * Converts a @ref SubtitleList timeline into the per-frame
 * @ref Cea708Cdp::CcDataList byte-pair stream that the wire layer
 * expects.  At the configured @ref FrameRate the encoder emits
 * exactly one CcData triple per frame (matching the configured
 * channel's @c cc_type); empty frames carry the null pair
 * @c (0x80, 0x80) which receivers ignore.
 *
 * @par Mode support
 *
 * All three CEA-608 caption modes are supported:
 *
 *   - @c PopOn — pre-recorded caption mode.  Text is loaded into
 *     non-displayed memory via @c RCL, then "popped" onto the screen
 *     via @c EOC at the cue's start time.  Cleared via @c EDM at the
 *     cue's end time.  Standard for offline-authored captions.
 *   - @c PaintOn — live caption mode.  Text is written directly to
 *     displayed memory via @c RDC, character-by-character.  No
 *     swap — characters appear as transmitted.  Cleared via @c EDM
 *     at the cue's end time.
 *   - @c RollUp — continuous scrolling captions.  @c RU2/3/4
 *     initialises the mode (rows visible).  Each cue is appended as
 *     a new row via @c CR (carriage return).  No per-cue @c EDM —
 *     cues scroll off the top as new rows arrive.
 *
 * @par Channel support
 *
 * All four caption channels are supported: CC1 (field 1, channel 1),
 * CC2 (field 1, channel 2), CC3 (field 2, channel 1), CC4 (field 2,
 * channel 2).  The channel is set via @ref Config::channel.  Within a
 * field the channel selector rides in bit 3 of every control byte's
 * first byte (clear = CC1/CC3, set = CC2/CC4); cross-field routing
 * uses the CDP @c cc_type slot (0 = field 1, 1 = field 2).  The
 * encoder builds the schedule with CC1/CC3-shaped bytes internally
 * and OR-shifts them to CC2/CC4 in a single post-pass.
 *
 * Multi-channel emission (e.g. CC1 + CC2 in parallel for English /
 * Spanish) is achieved by instantiating one encoder per channel and
 * merging each frame's @c CcDataList output — the shared
 * @ref Cea708Cdp::CcData carrier accommodates multiple triples per
 * frame.
 *
 * @par Layout (multi-row word-wrap)
 *
 * The encoder lays a cue out on up to @ref Config::maxRows physical
 * 608 rows.  The cue's text is fed to @ref TextWrap with the cue's
 * @c '\n' boundaries tried first: if every author-broken line fits
 * within @ref Config::maxCols *and* the row count is within
 * @ref Config::maxRows, that explicit layout is used.  Otherwise
 * the @c '\n's collapse to spaces and the whole cue is re-flowed
 * via a balanced minimax wrap, producing the fewest rows that fit
 * the width cap (rows balanced as evenly as possible).
 *
 * The cue's @ref Subtitle::anchor selects the *target* row for
 * single-row layouts.  For multi-row layouts the rows extend from
 * the anchor inward — @c Bottom* anchors fill rows
 * @c (16 - N) .. @c 15 (broadcast convention), @c Top* anchors
 * fill @c 1 .. @c N, @c Middle* anchors centre on row 8.
 * Roll-up mode forces row 15 regardless of anchor (roll-up is
 * bottom-anchored by spec).
 *
 * The anchor's horizontal half drives PAC indent + Tab Offset
 * emission: @c *Left and @c Default land at column 0; @c *Center
 * computes @c (32 - rowWidth) / 2; @c *Right lands at @c 32 -
 * rowWidth.  PAC's 4-bit subfield carries colour OR italic OR
 * indent (mutually exclusive on a single PAC), so a coloured /
 * italic cue at a @c Center / @c Right anchor degrades back to
 * flush-left column 0 — colour is the more prominent visual
 * cue.  Plain (white, non-italic) cues honour the anchor's
 * horizontal half exactly via PAC indent (multiples of 4) +
 * doubled Tab Offset (T1 / T2 / T3) for the 1..3 column
 * residual.
 *
 * @par Overflow auto-split
 *
 * A cue whose re-flowed layout genuinely needs more rows than
 * @ref Config::maxRows is split into back-to-back sub-cues, each
 * fitting @ref Config::maxRows rows.  The cue's
 * @c [start, end] window is apportioned across the sub-cues by
 * character count so dense chunks get more screen time.  A warning
 * is logged identifying the source cue index.
 *
 * @par Character set
 *
 * Full EIA-608-B character coverage via @ref Cea608Ext: basic G0
 * (ASCII plus the ten remapped Latin / arithmetic positions at
 * @c 0x2A / 0x5C / 0x5E / 0x5F / 0x60 / 0x7B / 0x7C / 0x7D /
 * @c 0x7E / 0x7F), the 16 Special Characters (@c (0x11, 0x30..0x3F)
 * doubled control pair on CC1, channel-shifted for CC2 / CC4),
 * the 32 Extended Spanish / Misc glyphs (@c (0x12, 0x20..0x3F)),
 * and the 32 Extended Portuguese / German / box-drawing glyphs
 * (@c (0x13, 0x20..0x3F)).  Codepoints with no 608 representation
 * substitute @c 0x20 (space).  Special / Extended glyphs ride
 * with a best-fit ASCII placeholder so old decoders that ignore
 * the doubled control pair still render a recognisable fallback.
 *
 * @par Per-cue byte-stream layout (pop-on)
 *
 * @code
 *  +--------+--------+--------+--------+--------+ ... +--------+--------+
 *  |  RCL   |  RCL   |  PAC   |  PAC   | chars  | ... |  EOC   |  EOC   |
 *  +--------+--------+--------+--------+--------+ ... +--------+--------+
 *  ^                                                          ^
 *  firstFrame                                                 cueStart-1
 * @endcode
 *
 * Every two-byte control code is doubled per the spec (CEA-608
 * receivers ignore an immediate repeat; the doubling protects
 * against single-frame dropouts).  Character pairs are *not*
 * doubled.  The total per-cue frame count is therefore:
 *
 *     @c 2 (RCL) + 2 (PAC) + ceil(textLen / 2) (chars) + 2 (EOC)
 *
 * @par Cue end (clear)
 *
 * @code
 *  +--------+--------+
 *  |  EDM   |  EDM   |
 *  +--------+--------+
 *  ^
 *  cueEnd
 * @endcode
 *
 * @par Pre-roll error
 *
 * If a cue's @c firstFrame falls before frame 0, or before the
 * previous cue's @c EDM finished, @ref setSubtitles returns
 * @c Error::OutOfRange.  This is the encoder's signal that the
 * timeline is too dense for the configured frame rate.  Caller
 * should either re-time the cues, drop overlapping cues, or
 * increase the frame rate.
 *
 * @par Storage and copy semantics
 *
 * The encoder is a stateful worker (not a value type).  Copy /
 * move are deleted — instantiate one per encode session.
 *
 * @par Thread Safety
 *
 * Not thread-safe.  Each encoder instance is single-threaded
 * (the per-frame state machine assumes serialised
 * @ref nextFrame calls).
 *
 * @see Cea608, Cea608Decoder, Cea708Cdp, SubtitleList
 */
class Cea608Encoder : public CaptionEncoder {
        public:
                /**
                 * @brief Operating mode controlling the byte-stream shape.
                 *
                 * All three modes are supported; each emits a
                 * different control-code framing around the same
                 * character body.  See the class doc-comment for
                 * the per-mode byte-stream layouts.  Per-cue mode
                 * overrides via @ref Subtitle::mode are honoured;
                 * this enum is the default fall-back when a cue
                 * carries @c CaptionMode::Default.
                 */
                enum class Mode {
                        PopOn  = 0, ///< Pop-on (RCL → PAC → chars → EOC, EDM at cue end).
                        PaintOn = 1, ///< Paint-on (RDC → PAC → chars live, EDM at cue end).
                        RollUp  = 2, ///< Roll-up (RUx → CR → PAC → chars; no EDM).
                };

                /**
                 * @brief Channel selector inside the configured field.
                 *
                 * All four channels are fully supported.  CC1 / CC2
                 * ride in field 1 (CDP @c cc_type = 0); CC3 / CC4
                 * ride in field 2 (@c cc_type = 1).  Within a field,
                 * the second channel (CC2 / CC4) is selected by bit
                 * 3 of the first control byte being set; the encoder
                 * applies the channel-bit OR-mask in a single post-
                 * pass after building the CC1/CC3-shaped schedule.
                 */
                enum class Channel {
                        CC1 = 0, ///< Field 1 / Channel 1.
                        CC2 = 1, ///< Field 1 / Channel 2.
                        CC3 = 2, ///< Field 2 / Channel 1.
                        CC4 = 3, ///< Field 2 / Channel 2.
                };

                /** @brief Encoder configuration. */
                struct Config {
                                /// @brief Required.  Drives ms → frame conversion.
                                FrameRate frameRate;
                                /// @brief Default operating mode.  Used as the
                                ///        fall-back when a cue's @ref Subtitle::mode
                                ///        is @c CaptionMode::Default; cues with
                                ///        an explicit mode override this on a
                                ///        per-cue basis (mid-stream mode mixing
                                ///        is fully supported).
                                Mode mode = Mode::PopOn;
                                /// @brief Channel selector (CC1 / CC2 / CC3 /
                                ///        CC4).  Each instance emits one
                                ///        channel; multi-channel streams pair
                                ///        two encoder instances and merge their
                                ///        @c CcDataList output.
                                Channel channel = Channel::CC1;
                                /// @brief Default roll-up row count (2..4).
                                ///        Used as the fall-back when a cue's
                                ///        @ref Subtitle::rollUpRows is 0; cues
                                ///        with an explicit row count in
                                ///        @c [2, 4] override this on a per-cue
                                ///        basis (a different RUx is re-emitted
                                ///        when the count changes between
                                ///        consecutive roll-up cues).
                                int32_t rollUpRows = 2;
                                /// @brief Maximum characters per row.  The
                                ///        true 608 wire width is 32 columns
                                ///        (the decoder displays at most 32
                                ///        characters per row regardless of
                                ///        what is transmitted).  Reduce only
                                ///        when targeting a receiver with a
                                ///        narrower visible region.
                                int32_t maxCols = 32;
                                /// @brief Maximum physical 608 rows per cue.
                                ///        Cues that re-flow to more rows than
                                ///        this are split into time-displaced
                                ///        sub-cues (see class doc-comment).
                                ///        Broadcast convention is 2–3 rows;
                                ///        4 is the practical maximum.
                                int32_t maxRows = 3;
                                /// @brief Whether to double every control-code
                                ///        pair on the wire per CEA-608-E §8.4
                                ///        "Doubling of Control Pairs".
                                ///
                                ///        Default @c true matches the spec's
                                ///        normative requirement for caption
                                ///        data: every control pair (PAC, mid-
                                ///        row, RCL/RDC/RUx, EDM, EOC, CR, BS,
                                ///        DER, FON, BG attribute, BT, FA/FAU,
                                ///        Tab Offset) is transmitted twice
                                ///        adjacent on the wire so the
                                ///        receiver can recover from a single-
                                ///        byte transmission error.
                                ///
                                ///        Setting to @c false emits each
                                ///        control pair exactly once, halving
                                ///        the per-cue control overhead.  Per
                                ///        §D.2 this mode is suitable for
                                ///        non-caption F2 traffic where
                                ///        bandwidth is more valuable than
                                ///        error recovery — out of spec for
                                ///        caption data on F1, but useful in
                                ///        bandwidth-constrained encoder
                                ///        pipelines that pair this encoder
                                ///        with an aggressive interleaver.
                                ///
                                ///        Pre-roll budgets (RCL+EDM for
                                ///        pop-on, RUx+CR+PAC for first
                                ///        roll-up cue, etc.) scale with this
                                ///        setting automatically — the encoder
                                ///        emits half the byte pairs when
                                ///        @c false and starts the wire stream
                                ///        a proportional number of frames
                                ///        later before each cue.
                                bool doubleControls = true;
                };

                Cea608Encoder();
                explicit Cea608Encoder(Config cfg);
                ~Cea608Encoder() override;

                /** @brief Returns the configuration this encoder was constructed with. */
                const Config &config() const;

                /// @copydoc CaptionEncoder::codec
                CaptionCodec codec() const override { return CaptionCodec(CaptionCodec::Cea608); }

                /// @copydoc CaptionEncoder::frameRate
                FrameRate frameRate() const override;

                /**
                 * @brief Loads the timeline.  Computes the per-frame
                 *        schedule (pop-on pre-roll + EDM at cue end)
                 *        for every cue.
                 *
                 * Replaces any previous schedule.  Subsequent
                 * @ref nextFrame calls draw from this schedule until
                 * the next @ref setSubtitles or @ref reset.
                 *
                 * When the next cue's pre-roll would collide with the
                 * prior cue's EDM, the prior EDM is elided — the prior
                 * cue then persists on-screen until the next cue's EOC
                 * swaps it out.  This is the conventional CEA-608
                 * encoder behaviour for closely-spaced cues; the visible
                 * cost is a few extra frames of display for the prior
                 * cue (capped by the next cue's start).
                 *
                 * @return @c Error::Ok on success.
                 *         @c Error::Invalid when the configured
                 *         @ref FrameRate is not valid.
                 *         @c Error::OutOfRange when a cue's pre-roll
                 *         would fall before frame 0, when pre-roll
                 *         overlaps the prior cue's wire stream, or
                 *         when (paint-on / roll-up) the character
                 *         pairs would overrun the cue's display
                 *         window.  Use @ref encodableSubset to
                 *         pre-filter timing-dense input rather than
                 *         surfacing this error.
                 */
                Error setSubtitles(const SubtitleList &subs) override;

                /** @brief Clears the schedule. */
                void reset() override;

                /**
                 * @brief Returns the @c CcData triple for frame @p frame.
                 *
                 * Always returns a list of exactly one
                 * @ref Cea708Cdp::CcData with the configured channel's
                 * @c cc_type.  When the schedule has nothing to emit
                 * the triple carries the null pair @c (0x80, 0x80).
                 *
                 * @c cc_valid is set on every emitted triple.
                 *
                 * Frame numbers are interpreted in the configured
                 * @ref FrameRate.  Calling out of order is allowed
                 * (each call is independent — the encoder consults
                 * the pre-computed schedule, not an internal frame
                 * cursor).
                 */
                Cea708Cdp::CcDataList nextFrame(FrameNumber frame) const override;

                /**
                 * @brief Returns the *worst-case* frame number at which
                 *        the encoder must begin emitting control codes
                 *        so the cue is fully loaded by its @c start.
                 *
                 * Diagnostic helper for callers that want to inspect
                 * pre-roll without running the full @ref setSubtitles
                 * machinery.  The returned value reflects the cue
                 * inspected in isolation — the actual pre-roll a
                 * subsequent @ref setSubtitles call computes can be
                 * tighter (the prior cue's deferred EDM may absorb
                 * some of the budget) or shorter still when the cue
                 * is part of an auto-split chain.  Use this for a
                 * "fits at all?" pre-flight check, not as the exact
                 * frame the encoder will pick.
                 *
                 * Returns @c FrameNumber::unknown() when the configured
                 * @ref FrameRate is invalid, or when the worst-case
                 * pre-roll lands before frame 0 (the cue cannot fit).
                 */
                FrameNumber earliestStartFor(const Subtitle &cue) const;

                /**
                 * @brief Walks @p in in chronological order and returns
                 *        the subset whose cues fit the encoder's
                 *        pre-roll / back-to-back constraints.
                 *
                 * Cues that would have made @ref setSubtitles return
                 * @c Error::OutOfRange (start too close to t=0, or
                 * pre-roll overlapping the prior kept cue's wire stream)
                 * are dropped instead of causing the whole batch to
                 * fail.  Mirrors @ref setSubtitles' EDM-elision policy,
                 * so densely-spaced cues that only collide with the
                 * prior cue's EDM are kept (not dropped).  When
                 * @p outDropped is non-null the dropped cues are
                 * appended to it for diagnostics.
                 *
                 * @p in must be sorted by @c start; otherwise the
                 * "prior cue's wire stream" book-keeping mis-orders and
                 * the filter degrades.  Use
                 * @ref SubtitleList::sortByStart before calling.
                 *
                 * Other failure modes (invalid @ref FrameRate, non-PopOn
                 * mode, non-CC1 channel) still surface from
                 * @ref setSubtitles — this helper only handles the
                 * timing-density gap.
                 */
                SubtitleList encodableSubset(const SubtitleList &in,
                                             SubtitleList      *outDropped = nullptr) const override;

        private:
                SharedPtr<Cea608EncoderImpl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
