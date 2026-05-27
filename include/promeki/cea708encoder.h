/**
 * @file      cea708encoder.h
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
#include <promeki/sharedptr.h>
#include <promeki/subtitle.h>

PROMEKI_NAMESPACE_BEGIN

struct Cea708EncoderImpl; // Pimpl — defined in cea708encoder.cpp.

/**
 * @brief Stateful CEA-708 DTVCC caption encoder.
 * @ingroup proav
 *
 * Inverse of @ref Cea708Decoder: converts a @ref SubtitleList
 * timeline into the per-frame @c CcData triple stream that the
 * CDP's @c cc_data section carries.  The wire layer is
 * @c cc_type=2 (DTVCC_PACKET_START) and @c cc_type=3
 * (DTVCC_PACKET_DATA) triples wrapped around one or more
 * @ref Cea708Service blocks.
 *
 * @par Window model (single window, per-cue transaction)
 *
 * Each cue is emitted as a self-contained transaction over one or
 * more consecutive frames starting at @c cue.start:
 *
 *  - DefineWindow (DF0) with @ref Config::windowCols and the cue's
 *    row count (1 for pop-on / paint-on, 3 for roll-up so the
 *    receiver scrolls instead of overwriting).
 *  - SetWindowAttributes (SWA) when the cue's spans share a
 *    uniform background colour.
 *  - SetPenAttributes / SetPenColor (SPA / SPC) defaults to bring
 *    the wire pen to a known state.
 *  - Per-span SPA / SPC re-asserted whenever a span's style
 *    differs from the wire's current pen state, followed by the
 *    span's character bytes.
 *  - DisplayWindow (DSW) terminates the transaction.
 *
 *  For pop-on cues a HideWindow (HDW) packet is also scheduled
 *  at @c cue.end so the cue clears.  Paint-on and roll-up cues
 *  skip the HideWindow — the window stays visible until the next
 *  cue's DefineWindow overwrites it.  When a subsequent cue's
 *  startFrame coincides with this cue's endFrame, the redundant
 *  HideWindow is elided (the next cue's DefineWindow handles the
 *  visibility transition atomically).
 *
 * @par Mode support
 *
 *  - @ref CaptionMode::PopOn (and @ref CaptionMode::Default) —
 *    DefineWindow(visible, 1 row) + chars + DSW at @c cue.start,
 *    HideWindow at @c cue.end.
 *  - @ref CaptionMode::PaintOn — same show transaction; no
 *    HideWindow boundary.  The window stays visible until the
 *    next cue's transaction overwrites it.
 *  - @ref CaptionMode::RollUp — DefineWindow declares a multi-row
 *    window so the receiver scrolls; no HideWindow.
 *
 *  Per-cue mode mixing is fully supported because each cue's
 *  transaction is self-contained.
 *
 * @par Per-frame cc_count budget
 *
 * SMPTE 334-2 / CEA-708 cap the CDP's cc_count at 5 bits
 * (max 31 cc_data triples per video frame).  Each DTVCC packet
 * the encoder emits produces ~17 triples, so the encoder
 * schedules at most one packet per frame.  A cue whose show
 * transaction needs more frames than the cue's duration is
 * dropped with a warning; collisions that would exceed the
 * per-frame cap return @c Error::OutOfRange from
 * @ref setSubtitles.
 *
 * @par Service multiplexing
 *
 * Service number defaults to 1 (the primary English caption
 * service); other services are reachable via @ref Config but
 * the wire output is otherwise identical.
 *
 * @par Storage and copy semantics
 *
 * Stateful worker (pimpl, copy/move-deleted).  Instantiate one
 * per encode session.
 *
 * @par Thread Safety
 *
 * Not thread-safe.  Each encoder instance is single-threaded.
 *
 * @see Cea708Cdp, Cea708Service, Cea708DtvccPacket, Cea708Decoder
 */
class Cea708Encoder : public CaptionEncoder {
        public:
                /** @brief Encoder configuration. */
                struct Config {
                                /// @brief Required.  Drives ms → frame conversion.
                                FrameRate frameRate;
                                /// @brief DTVCC service number to emit
                                ///        (1..63).  Default 1.
                                uint8_t serviceNumber = 1;
                                /// @brief Maximum visible columns the
                                ///        DefineWindow command declares.
                                ///        Defaults to 32 (a common
                                ///        broadcast width).  Clamped to
                                ///        @c Cea708Window::MaxCols.
                                int windowCols = 32;
                };

                Cea708Encoder();
                explicit Cea708Encoder(Config cfg);
                ~Cea708Encoder() override;

                /** @brief Returns the configuration this encoder was constructed with. */
                const Config &config() const;

                /// @copydoc CaptionEncoder::codec
                CaptionCodec codec() const override { return CaptionCodec(CaptionCodec::Cea708); }

                /// @copydoc CaptionEncoder::frameRate
                FrameRate frameRate() const override;

                /**
                 * @brief Loads the timeline.  Computes the per-frame
                 *        DTVCC packet schedule for every cue.
                 *
                 * @return @c Error::Ok on success.
                 *         @c Error::Invalid when the configured
                 *         @ref FrameRate is not valid.
                 *         @c Error::OutOfRange when a cue's
                 *         character payload exceeds the wire-packet
                 *         maximum (one frame's worth of DTVCC bytes).
                 */
                Error setSubtitles(const SubtitleList &subs) override;

                /** @brief Clears the schedule. */
                void reset() override;

                /**
                 * @brief Returns the @c CcData triples for frame
                 *        @p frame.  Empty list when no DTVCC payload
                 *        is scheduled for this frame.
                 *
                 * Frame numbers are interpreted in the configured
                 * @ref FrameRate.  Calling out of order is allowed.
                 */
                Cea708Cdp::CcDataList nextFrame(FrameNumber frame) const override;

        private:
                SharedPtr<Cea708EncoderImpl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
