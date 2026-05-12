/**
 * @file      cea708decoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/cea708cdp.h>
#include <promeki/cea708service.h>
#include <promeki/cea708windowstate.h>
#include <promeki/framenumber.h>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

struct Cea708DecoderImpl; // Pimpl — defined in cea708decoder.cpp.

/**
 * @brief Stateful CEA-708 DTVCC caption decoder.
 * @ingroup proav
 *
 * Consumes the @c cc_data triple stream from a CDP (typically
 * obtained via @ref Cea708Cdp::ccData), filters the
 * @c cc_type=2 / @c cc_type=3 triples that carry DTVCC, reassembles
 * them into @ref Cea708DtvccPacket packets, walks each packet's
 * @ref Cea708Service blocks, and feeds the service-data bytes into
 * an internal @ref Cea708WindowState.  Cues are emitted whenever
 * the visible-text content of the window state changes between
 * frames, and finalised with @c end = the timestamp at which the
 * new content arrived.
 *
 * @par Service selection
 *
 * @ref Config::serviceNumber selects which of the up-to-63 DTVCC
 * services the decoder pays attention to (default 1 — the primary
 * English caption service in 99% of broadcast streams).  Service
 * blocks for other services are skipped.
 *
 * @par Pop-on vs roll-up vs paint-on semantics in 708
 *
 * Unlike CEA-608, 708's wire format doesn't expose a "mode" — the
 * mode is implicit in how the encoder uses the window-manipulation
 * commands (Hide/Display + ClearWindow for pop-on, DisplayWindow
 * + characters in real time for paint-on, etc.).  The decoder
 * tracks visible content and emits a cue every time the
 * visible-text content changes:
 *
 *  - When the visible text transitions from "non-empty" → "empty"
 *    (window cleared or hidden) the in-flight cue is finalised
 *    with @c end set to the current frame's timestamp.
 *  - When the visible text transitions from "empty" → "non-empty"
 *    a new cue starts with @c start = the current frame's
 *    timestamp.
 *  - When the visible text *changes* without going through empty,
 *    the prior cue is finalised and the new one starts at the
 *    same timestamp.
 *
 * @par Storage and copy semantics
 *
 * Stateful worker (pimpl, copy/move-deleted).  Instantiate one
 * per decode session.
 *
 * @par Thread Safety
 *
 * Not thread-safe.  @ref pushFrame must be called serially.
 *
 * @see Cea708Cdp, Cea708Service, Cea708DtvccPacket, Cea708WindowState
 */
class Cea708Decoder {
        public:
                /** @brief Decoder configuration. */
                struct Config {
                                /// @brief Which DTVCC service to decode (1..63).
                                ///        Default 1 (primary).
                                uint8_t serviceNumber = 1;
                };

                Cea708Decoder();
                explicit Cea708Decoder(Config cfg);
                ~Cea708Decoder();

                Cea708Decoder(const Cea708Decoder &) = delete;
                Cea708Decoder &operator=(const Cea708Decoder &) = delete;
                Cea708Decoder(Cea708Decoder &&) = delete;
                Cea708Decoder &operator=(Cea708Decoder &&) = delete;

                /** @brief Returns the configuration this decoder was constructed with. */
                const Config &config() const;

                /**
                 * @brief Feeds one frame's worth of @c CcData triples.
                 *
                 * Filters out @c cc_type=0/1 triples (those carry
                 * CEA-608, handled by @ref Cea608Decoder), keeps the
                 * @c cc_type=2 (DTVCC_PACKET_START) and @c cc_type=3
                 * (DTVCC_PACKET_DATA) ones, and reassembles them into
                 * complete @ref Cea708DtvccPacket packets.  Each
                 * complete packet's service blocks are dispatched to
                 * the internal @ref Cea708WindowState.
                 *
                 * @param frame Frame number (advisory; the decoder
                 *              uses @p ts for cue timestamping).
                 * @param ts    Media-relative @ref TimeStamp.  Cue
                 *              boundaries are recorded at this
                 *              timestamp.
                 * @param data  The frame's CcData list (typically the
                 *              @c ccData member of a @ref Cea708Cdp).
                 */
                void pushFrame(FrameNumber frame, TimeStamp ts, const Cea708Cdp::CcDataList &data);

                /**
                 * @brief Returns the currently visible text (the
                 *        @ref Cea708WindowState's flattened
                 *        @c visibleText).  Empty when no cue is on
                 *        screen.
                 */
                String displayedText() const;

                /**
                 * @brief Returns the currently displayed cue as a
                 *        @ref Subtitle.  @c start = the timestamp at
                 *        which the visible-text content became
                 *        non-empty; @c end = the most recent
                 *        @ref pushFrame timestamp (tentative — will
                 *        be finalised when the cue ends).
                 *
                 * Empty (@ref Subtitle::isEmpty returns @c true) when
                 * no cue is on screen.
                 */
                Subtitle displayedCue() const;

                /**
                 * @brief Read-only access to the underlying window
                 *        state, for callers that want to inspect or
                 *        render the full grid (not just the flattened
                 *        text).
                 */
                const Cea708WindowState &windowState() const;

                /**
                 * @brief Emits the accumulated @ref SubtitleList.
                 *
                 * If a cue is still displayed at finalize time, it
                 * is emitted with @c end = the most recent
                 * @ref pushFrame timestamp.  After finalize, the
                 * decoder is reset to its initial state.
                 */
                SubtitleList finalize();

                /**
                 * @brief Resets the decoder without emitting anything.
                 *
                 * Drops any in-flight cue + clears every window.
                 */
                void reset();

        private:
                SharedPtr<Cea708DecoderImpl> _d;
};

PROMEKI_NAMESPACE_END
