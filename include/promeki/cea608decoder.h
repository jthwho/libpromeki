/**
 * @file      cea608decoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/captiondecoder.h>
#include <promeki/cea608encoder.h>
#include <promeki/cea608xds.h>
#include <promeki/cea708cdp.h>
#include <promeki/enums_subtitle.h>
#include <promeki/framenumber.h>
#include <promeki/list.h>
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
 * them.  Round-trip with @ref Cea608Encoder is byte-exact for all
 * three modes (pop-on, paint-on, roll-up); cue start / end
 * timestamps are the actual frame timestamps at which the mode's
 * commit control code fired (pop-on: @c EOC swap + @c EDM clear;
 * paint-on: first PAC start + @c EDM clear; roll-up: PAC start +
 * next @c CR), which are precisely the encoder's scheduled
 * `cue.start` and `cue.end` frames (modulo the inherent 1-frame
 * quantisation of the wire format).
 *
 * @par State machine
 *
 * The decoder operates as a state-driven mode automaton.  The
 * current mode is inferred from the most recently seen mode-
 * establishing control code:
 *
 *  - @c RCL (0x14 0x20) → enter pop-on mode.
 *  - @c RDC (0x14 0x29) → enter paint-on mode.
 *  - @c RU2/RU3/RU4 (0x14 0x25/26/27) → enter roll-up mode.
 *
 * Default mode (no control code seen yet) is pop-on.  The mode
 * controls how @c EOC, @c EDM, and @c CR are interpreted:
 *
 *  - **Pop-on**: chars accumulate in a non-displayed buffer.
 *    @c EOC swaps the buffer to displayed; @c EDM finalizes the
 *    displayed cue.
 *  - **Paint-on**: chars commit directly to the displayed buffer
 *    (visible immediately).  @c EDM finalizes the cue.
 *  - **Roll-up**: chars commit to the current row.  @c CR
 *    finalizes the row as a cue and starts a new one.
 *
 * Common state transitions:
 *
 *  - PAC (0x10..0x17 0x40..0x7F) → sets row / colour / italic /
 *    underline for the next text run.
 *  - Mid-row code (0x11 0x20..0x2F) → changes colour / italic /
 *    underline mid-line, flushing the current run as a styled span.
 *  - Character pair (0x20..0x7F first byte) → append to the active
 *    buffer.
 *  - @c ENM → clear loading memory (typically after @c RCL).
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
 *  - **BS (Backspace)**: pops the most recent codepoint from the
 *    loading buffer (typo-correction by a live captioner).
 *  - **DER (Delete to End of Row)**: drops every char accumulated
 *    since the last PAC on the loading row.
 *  - **FON (Flash On)**: subsequent chars get
 *    @c SubtitleOpacity::Flash applied to their span's foreground
 *    opacity until the next PAC / mid-row resets the style.
 *  - **TR / RTD** (Text Restart / Resume Text Display): text-
 *    channel control codes — caption decoding ignores them.
 *
 * @par Scope
 *
 * All three modes (pop-on, paint-on, roll-up) and all four
 * channels (CC1 / CC2 / CC3 / CC4) are decoded.  Character set
 * coverage is full EIA-608-B via @ref Cea608Ext: basic G0 (ASCII
 * plus the ten remapped Latin / arithmetic positions), the 16
 * Special Characters via @c (0x11, 0x30..0x3F), and the two
 * 32-glyph Extended Western European tables via
 * @c (0x12 / 0x13, 0x20..0x3F).
 *
 * @par What's not implemented
 *
 * The following CEA-608-E features are out of scope of this
 * caption decoder.  Callers needing them should compose a separate
 * extractor or post-processor:
 *
 *  - **Text mode (T1..T4, §7 of the spec) — surfacing**.  Caption
 *    decoders do not emit text-mode cues.  @ref TR (Text Restart,
 *    @c 0x14 0x2A) and @ref RTD (Resume Text Display, @c 0x14 0x2B)
 *    ARE recognised, however, as text-channel mode-establishing
 *    codes: their receipt flips the decoder's character-attribution
 *    context to "not our channel" so subsequent text-mode bytes
 *    don't bleed into the caption stream.  In digital delivery
 *    workflows (ATSC A/53, ATSC A/65, RTP-40 / SMPTE 334-2)
 *    text mode is virtually extinct — modern receivers use
 *    CEA-708 service blocks for non-caption text data — so the
 *    omission of full text-mode decode is not a practical
 *    compliance gap.  Callers needing T1..T4 content can compose
 *    a dedicated text-mode extractor on top of the raw byte stream.
 *
 * @par What's now implemented
 *
 *  - **XDS** (eXtended Data Services, §8.6 / §9) is decoded
 *    internally: every field-2 triple passed through @ref pushFrame
 *    is fed through an embedded @ref Cea608XdsExtractor in addition
 *    to the caption-channel processing.  Surfaced via
 *    @ref drainXdsPackets / @ref xdsPending /
 *    @ref xdsChecksumFailures.  Typed accessors cover Program
 *    Identification, Length / Time-in-Show, Program Name, Program
 *    Type, Content Advisory (US TV / MPAA / Canadian English /
 *    Canadian French), Copy and Redistribution Control (CGMS-A,
 *    APS, ASB, RCD), Audio Services, Caption Services, Network
 *    Name, Call Letters + Native Channel, Tape Delay, TSID, Time of
 *    Day, Impulse Capture ID, Supplemental Data Location, Local
 *    Time Zone & DST, Out-of-Band Channel, Channel Map Pointer /
 *    Header / Packet, WRSAME, NWS Message, and Program Description
 *    rows 1–8.
 *  - **§C.13 right-margin limitation**, **§C.22 safe-caption-area**,
 *    and most other Annex C "Preferred" rendering rules — those are
 *    receiver-rendering policy and live in the renderer layer.
 *
 * Implemented per spec: **F2 control-code remap** (§8.4(a)(b),
 * @c 0x14 ↔ 0x15 / @c 0x1C ↔ 0x1D for CC3 / CC4), **FA / FAU
 * Foreground Black attribute codes** (§6.2 Table 3), and the
 * **§C.9 16-second auto-erasure** (Preferred).
 *
 * @par Channel filtering
 *
 *  - @ref Cea708Cdp::CcData triples whose @c cc_type doesn't match
 *    the configured channel's field are skipped (CC1 / CC2 want
 *    @c cc_type = 0; CC3 / CC4 want @c cc_type = 1).
 *  - Within a field the intra-field channel bit (bit 3 of @c b1
 *    after parity strip) selects CC1 / CC3 (bit clear) vs
 *    CC2 / CC4 (bit set).  Control codes from the wrong channel
 *    are skipped.  Character pairs carry no channel info on the
 *    wire and inherit the context of the most recently received
 *    control code.
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
class Cea608Decoder : public CaptionDecoder {
        public:
                /** @brief Channel alias matching @ref Cea608Encoder::Channel. */
                using Channel = Cea608Encoder::Channel;

                /** @brief Decoder configuration. */
                struct Config {
                                /// @brief Which CEA-608 channel to decode
                                ///        (CC1 / CC2 / CC3 / CC4).
                                ///        Decoders for multi-channel streams
                                ///        (e.g. CC1 + CC2 English / Spanish)
                                ///        instantiate one per channel.
                                Channel channel = Channel::CC1;
                };

                Cea608Decoder();
                explicit Cea608Decoder(Config cfg);
                ~Cea608Decoder() override;

                /// @copydoc CaptionDecoder::codec
                CaptionCodec codec() const override { return CaptionCodec(CaptionCodec::Cea608); }

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
                void pushFrame(FrameNumber frame, TimeStamp ts, const Cea708Cdp::CcDataList &data) override;

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
                String displayedText() const override;

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
                Subtitle displayedCue() const override;

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
                SubtitleList finalize() override;

                /**
                 * @brief Resets the decoder without emitting anything.
                 *
                 * Drops any in-flight cue and any pending non-displayed
                 * memory.  Use when re-feeding the decoder from a new
                 * source mid-session.
                 */
                void reset() override;

                /**
                 * @brief Returns and clears the list of XDS packets
                 *        extracted from field-2 byte pairs since the
                 *        last call to @ref drainXdsPackets or
                 *        construction.
                 *
                 * The decoder runs an internal @ref Cea608XdsExtractor
                 * that watches every field-2 triple passed through
                 * @ref pushFrame — independent of the configured
                 * caption channel.  This lets a single decoder
                 * instance surface both the caption cue list and the
                 * XDS program-metadata stream without the caller
                 * needing to spin a separate extractor in parallel.
                 *
                 * Empty list when no validated XDS packets have
                 * arrived.
                 */
                List<Cea608XdsPacket> drainXdsPackets();

                /// @brief Number of validated XDS packets currently
                ///        buffered for the next @ref drainXdsPackets
                ///        call.
                size_t xdsPending() const;

                /// @brief Count of XDS packets that failed the §8.6.3
                ///        checksum check since construction / reset.
                ///        Useful for telemetry — non-zero indicates
                ///        line-21 bit-flip noise or a malformed
                ///        upstream encoder.
                uint32_t xdsChecksumFailures() const;

        private:
                SharedPtr<Cea608DecoderImpl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
