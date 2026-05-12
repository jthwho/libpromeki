/**
 * @file      cea708encoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/cea708cdp.h>
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
 * @par Window model (v1 — single window, single service)
 *
 * Each cue is emitted as a self-contained two-frame transaction:
 *
 *  - At @c cue.start: one DTVCC packet containing
 *    `[DF0(window 0, visible), char bytes..., DSW(bitmap=1)]`.
 *    The DefineWindow command declares window 0 as visible with
 *    1 row × N columns (N = max text length); the chars stream
 *    into it; DisplayWindow ensures it's on-screen.
 *  - At @c cue.end: one DTVCC packet containing
 *    `[HDW(bitmap=1)]` — hide window 0.
 *
 * Frames outside cue boundaries emit no DTVCC triples (the CDP's
 * @c cc_data list remains free for other services / padding).
 *
 * Service number defaults to 1 (the primary English caption
 * service); other services are reachable via @ref Config but
 * the wire output is otherwise identical.
 *
 * @par Mode support (v1)
 *
 * Only the "pop-on" style transaction above is currently emitted.
 * Paint-on / roll-up 708 modes would use a different sequence of
 * window commands (no DSW boundary; characters streamed live with
 * the window already visible).  Paint-on / roll-up is a follow-on
 * once a 608/708 cross-encoded broadcast stream demands it.
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
class Cea708Encoder {
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
                ~Cea708Encoder();

                Cea708Encoder(const Cea708Encoder &) = delete;
                Cea708Encoder &operator=(const Cea708Encoder &) = delete;
                Cea708Encoder(Cea708Encoder &&) = delete;
                Cea708Encoder &operator=(Cea708Encoder &&) = delete;

                /** @brief Returns the configuration this encoder was constructed with. */
                const Config &config() const;

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
                Error setSubtitles(const SubtitleList &subs);

                /** @brief Clears the schedule. */
                void reset();

                /**
                 * @brief Returns the @c CcData triples for frame
                 *        @p frame.  Empty list when no DTVCC payload
                 *        is scheduled for this frame.
                 *
                 * Frame numbers are interpreted in the configured
                 * @ref FrameRate.  Calling out of order is allowed.
                 */
                Cea708Cdp::CcDataList nextFrame(FrameNumber frame) const;

        private:
                SharedPtr<Cea708EncoderImpl> _d;
};

PROMEKI_NAMESPACE_END
