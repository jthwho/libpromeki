/**
 * @file      ntv2capabilities.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/pixelformat.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/string.h>

class CNTV2Card;

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Cached, hardware-discovered capability snapshot for one AJA card.
 * @ingroup proav
 *
 * @ref Ntv2Device populates a @c Ntv2Capabilities instance once at
 * acquisition time by querying @c CNTV2Card::features() and
 * @c IsSupported.  The snapshot is immutable for the lifetime of
 * the device handle and is shared (by reference) across every
 * @ref Ntv2MediaIO that opens a logical channel on the card.
 *
 * Snapshotting matters because @c CNTV2Card's feature queries are
 * not cheap and not reentrant-safe — taking the snapshot once
 * eliminates per-frame hot-path SDK calls and lets the open path
 * answer "does this card support X" questions without acquiring the
 * device mutex.
 *
 * @par Derived booleans
 *
 * Beyond the raw counts the SDK exposes, the snapshot answers a
 * handful of higher-level questions the backend needs without
 * forcing every caller to know AJA's device-feature enums:
 *
 * - @ref supportsLinkStandard — "can this card carry a Quad-Link
 *   12G signal?", computed from the SDI cable count + the standard's
 *   cable requirement.
 * - @ref hasAudioCounter — "does the card expose a sample-counter
 *   audio register that @ref Ntv2DeviceClock can read?".  Drives the
 *   clock's choice between sample-counter mode and VBI fallback.
 */
class Ntv2Capabilities {
        public:
                /**
                 * @brief Default-constructs an empty / invalid capability set.
                 *
                 * @ref isValid returns @c false; every numeric accessor
                 * returns @c 0; every boolean accessor returns @c false.
                 * Useful for default member initialisers before the
                 * device is opened.
                 */
                Ntv2Capabilities() = default;

                /**
                 * @brief Probes @p card and fills the snapshot.
                 *
                 * Pure read-only SDK queries; safe to call from any
                 * thread that holds the only handle to @p card.
                 * Returns @c false when the card reports as not-ready
                 * (in which case the snapshot remains empty).
                 */
                bool probe(CNTV2Card &card);

                /** @brief Returns @c true after a successful @ref probe. */
                bool isValid() const { return _valid; }

                // ---- Connector counts ----

                /** @brief Number of physical SDI inputs on the card. */
                int sdiInputCount() const { return _sdiInputs; }

                /** @brief Number of physical SDI outputs on the card. */
                int sdiOutputCount() const { return _sdiOutputs; }

                /** @brief Number of physical HDMI inputs on the card. */
                int hdmiInputCount() const { return _hdmiInputs; }

                /** @brief Number of physical HDMI outputs on the card. */
                int hdmiOutputCount() const { return _hdmiOutputs; }

                /** @brief Number of independent audio systems. */
                int audioSystemCount() const { return _audioSystems; }

                /** @brief Total number of framebuffer channels (logical I/O slots). */
                int channelCount() const { return _channels; }

                /**
                 * @brief Number of on-board Colour-Space Converter widgets.
                 *
                 * Each CSC widget can bridge an RGB ↔ YCbCr mismatch
                 * between a framestore and the wire (SDI or HDMI)
                 * inside the routing fabric, saving a CPU-side CSC
                 * pass.  Phase-5-plus uses one CSC per quadrant for
                 * multi-link 4K paths, so a card with four CSCs can
                 * cover Quad-Link 2SI / Squares end-to-end on its own.
                 */
                int cscCount() const { return _cscs; }

                // ---- Feature flags ----

                /**
                 * @brief @c true when channels can run at independent
                 *        video formats (AJA's @c MultiFormatMode).
                 */
                bool canDoMultiFormat() const { return _canDoMultiFormat; }

                /**
                 * @brief @c true when SDI connectors are bi-directional
                 *        and must be explicitly toggled to receive.
                 */
                bool hasBiDirectionalSdi() const { return _hasBiDirectionalSdi; }

                /**
                 * @brief @c true when the card has the per-channel ANC
                 *        extractor / inserter firmware.
                 */
                bool canDoCustomAnc() const { return _canDoCustomAnc; }

                /** @brief @c true when the card supports capture (input). */
                bool canCapture() const { return _canCapture; }

                /** @brief @c true when the card supports playout (output). */
                bool canPlayout() const { return _canPlayout; }

                // ---- Derived ----

                /**
                 * @brief @c true when @ref Ntv2DeviceClock can read the
                 *        FPGA's free-running 48 kHz sample counter
                 *        (@c kRegAud1Counter) on this card.
                 *
                 * The counter is part of the audio subsystem in the
                 * FPGA and is present on every shipping NTV2 card,
                 * including playback-only cards like the T-Tap — it
                 * doesn't require an audio system to be in capture or
                 * playout to tick.  This cap exists as a safety net
                 * for hypothetical future hardware that lacks the
                 * audio subsystem entirely; @ref Ntv2DeviceClock falls
                 * back to VBI mode when it returns @c false.
                 */
                bool hasAudioCounter() const { return _audioSystems > 0; }

                /**
                 * @brief @c true when this card has enough SDI cables
                 *        to carry @p standard.
                 *
                 * Does not validate routing-preset availability or
                 * 12G connector presence; callers that need the
                 * finer-grained check consult the @ref Ntv2Device
                 * directly.
                 */
                bool supportsLinkStandard(const SdiLinkStandard &standard) const;

                /**
                 * @brief @c true when @p pixelFormat maps to a frame-
                 *        buffer format supported by the card.
                 *
                 * Both the libpromeki → NTV2 mapping
                 * (@ref Ntv2Format::toNtv2PixelFormat) and the card's
                 * own @c CanDoFrameBufferFormat must agree.
                 */
                bool supportsPixelFormat(PixelFormat::ID pixelFormat) const;

                /**
                 * @brief One-line human-readable summary suitable for
                 *        log lines and @c --probe output.
                 *
                 * Example: <code>"SDI in 4 / out 4, HDMI in 0 / out 1,
                 * audio sys 4, ch 4, multifmt, biSDI, anc"</code>.
                 */
                String toString() const;

                /**
                 * @brief Hand-builds a populated capability snapshot
                 *        without touching a real @c CNTV2Card.
                 *
                 * Used by the hardware-free reservation / multi-channel
                 * tests so they can construct an @ref Ntv2Device shape
                 * matching a synthetic card.  Production code never
                 * calls this — @ref probe is the authoritative path.
                 *
                 * Every NTV2 frame-buffer format is marked as supported
                 * since the test cases don't exercise the pixel-format
                 * negotiator.
                 */
                static Ntv2Capabilities createForTest(int channelCount, int audioSystemCount,
                                                      int sdiInputs, int sdiOutputs,
                                                      bool canMultiFormat = true,
                                                      bool hasBiSdi      = true,
                                                      bool canDoAnc      = true,
                                                      int  cscCount      = 4);

        private:
                bool _valid               = false;
                int  _sdiInputs           = 0;
                int  _sdiOutputs          = 0;
                int  _hdmiInputs          = 0;
                int  _hdmiOutputs         = 0;
                int  _audioSystems        = 0;
                int  _channels            = 0;
                int  _cscs                = 0;
                bool _canDoMultiFormat    = false;
                bool _hasBiDirectionalSdi = false;
                bool _canDoCustomAnc      = false;
                bool _canCapture          = false;
                bool _canPlayout          = false;

                // Per-frame-buffer-format support bitmap (indexed by
                // NTV2FrameBufferFormat).  Sized to cover the entire
                // NTV2_FBF_* range with comfortable headroom; missed
                // formats hash to "not supported", which is the safe
                // default — the open path returns Error::NotSupported
                // and the planner splices a CSC bridge.
                static constexpr int kFbfMapSize = 128;
                bool                 _fbfSupported[kFbfMapSize] = {};
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
