/**
 * @file      packetscheduler.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/duration.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/namespace.h>
#include <promeki/packettransport.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Per-packet egress timing policy for an @ref RtpSession.
 * @ingroup network
 *
 * The scheduler sits between @ref RtpSession::sendPackets and the
 * underlying @ref PacketTransport.  Its job is to apply a pacing
 * policy — burst, userspace cadence, kernel @c fq rate cap, or
 * per-packet @c SO_TXTIME deadlines — to a frame's worth of packets
 * the session has already framed and addressed.
 *
 * The split matters for ST 2110-21: the SMPTE traffic-shaping models
 * (Type N / NL / W) all share the same RTP wire format but differ
 * only in **when each packet is allowed to leave**.  Selecting a
 * scheduler subclass picks the pacing policy without disturbing the
 * RTP framing, the SDP signalling, the per-stream TX threads, or the
 * @ref MediaIO backend.  A future @c DpdkPacketScheduler /
 * @c IntelI225PacketScheduler drops in here without further work
 * upstream.
 *
 * @par Responsibilities
 *
 * - **Configuration** — @ref configure receives a @ref Spec
 *   describing the per-frame budget (@c frameInterval), an optional
 *   byte-rate target, and ST 2110-21 inputs.  Subclasses translate
 *   the spec into transport state (e.g.  @c setPacingRate for
 *   kernel fq, @c setTxTime for SCM_TXTIME) and into their own
 *   internal pacing state (cadence interval, leaky-bucket parameters,
 *   etc).
 *
 * - **Dispatch** — @ref enqueue accepts a list of
 *   @ref PacketTransport::Datagram and synchronously dispatches them
 *   through the bound transport.  The contract is:
 *   1. The session has already filled the RTP header on each
 *      packet's @c data buffer.
 *   2. The session has already addressed each @ref Datagram::dest.
 *   3. @ref Datagram::txTimeNs is zero on entry; the @c TxTime
 *      scheduler is the only one that stamps it before dispatch.
 *
 * - **Self-reporting** — @ref predictedTxDelayUs returns the
 *   scheduler's expected @c D_TX (egress delay) in microseconds at
 *   the current configuration.  The SDP builder reads this to fill
 *   the ST 2110-10 §7.9 @c TSDELAY fmtp value when the user has not
 *   pinned it explicitly.
 *
 * @par Thread safety
 *
 * A single instance is thread-affine.  The owning TX thread is the
 * only caller of @ref enqueue / @ref configure / @ref setRate.
 *
 * @par Lifetime
 *
 * The scheduler borrows its @ref PacketTransport — it does not own
 * it, and the transport must outlive the scheduler.
 */
class PacketScheduler {
        public:
                /** @brief Unique-ownership pointer. */
                using UPtr = UniquePtr<PacketScheduler>;

                /**
                 * @brief Configuration knob bundle.
                 *
                 * Schedulers consume a subset of the fields depending
                 * on their pacing policy — fields they don't use are
                 * left alone.  All durations are absolute (no
                 * implicit "0 = unspaced" convention; check
                 * @ref Duration::isValid where needed).
                 */
                struct Spec {
                                /**
                                 * @brief Total time budget for one frame's
                                 *        worth of packets.
                                 *
                                 * For video, this is @c T_FRAME
                                 * (1 / @c frameRate).  For audio,
                                 * this is the AES67 packet time
                                 * (@c ptime).  For data / ANC, this
                                 * is one frame interval.
                                 *
                                 * A zero / invalid value collapses
                                 * to burst behaviour even for paced
                                 * scheduler subclasses.
                                 */
                                Duration frameInterval;

                                /**
                                 * @brief Optional byte-rate cap.  0 = unlimited.
                                 *
                                 * @ref KernelFqPacketScheduler hands
                                 * this value to
                                 * @ref PacketTransport::setPacingRate.
                                 * Other schedulers ignore it for
                                 * dispatch but may use it for
                                 * @ref predictedTxDelayUs accounting
                                 * when @c frameInterval is unset.
                                 */
                                uint64_t bytesPerSec = 0;

                                /**
                                 * @brief Optional fixed packets-per-frame
                                 *        for tighter @c D_TX prediction.
                                 *
                                 * Pass 0 to let the scheduler derive
                                 * it from each batch's
                                 * @ref PacketTransport::DatagramList
                                 * size — the right default for
                                 * variable-bitrate video.
                                 */
                                int packetsPerFrame = 0;

                                /**
                                 * @brief ST 2110-21 TR_OFFSET in microseconds.
                                 *
                                 * Reserved for future TXTIME / DPDK
                                 * backends that compute @c TPR_j
                                 * against PTP.  Unused by the kernel
                                 * fq / userspace cadence backends.
                                 */
                                int trOffsetUs = 0;

                                /**
                                 * @brief Maximum UDP datagram size in octets.
                                 *
                                 * Informational; the @ref PacketTransport
                                 * decides actual MTU.  Captured here so
                                 * future shapers can validate against
                                 * the ST 2110-10 §6.3 standard /
                                 * extended limits.
                                 */
                                int maxUdp = 0;
                };

                /**
                 * @brief Audio's stream-long cadence vs. video's per-frame cadence.
                 *
                 * Only @ref CadencePacketScheduler /
                 * @ref TxTimePacketScheduler honour this; burst and
                 * kernel-fq don't care.
                 *
                 * - @c PerBatch — Cadence is local to each
                 *   @ref enqueue call.  Anchored at @c TimeStamp::now()
                 *   inside the call, packets spread across
                 *   @c frameInterval.  Right for video (one batch
                 *   per frame, N may vary).
                 *
                 * - @c Streamwide — Cadence is anchored once at
                 *   configure time, tick-incremented per
                 *   @ref enqueue.  Right for audio (one packet per
                 *   AES67 ptime, must stay phase-locked to the
                 *   stream's start instant).
                 */
                enum class CadenceMode {
                        PerBatch,
                        Streamwide,
                };

                PacketScheduler() = default;
                virtual ~PacketScheduler() = default;

                PacketScheduler(const PacketScheduler &) = delete;
                PacketScheduler &operator=(const PacketScheduler &) = delete;

                /**
                 * @brief Binds the scheduler to a transport.
                 *
                 * Must be called before @ref configure / @ref enqueue.
                 * The scheduler does not own the transport; the caller
                 * is responsible for outliving it.
                 *
                 * @param t Borrowed transport pointer (may be null to detach).
                 */
                void setTransport(PacketTransport *t) { _transport = t; }

                /** @brief Returns the bound transport, or null. */
                PacketTransport *transport() const { return _transport; }

                /**
                 * @brief Applies a configuration.
                 *
                 * Default implementation stashes the spec; subclasses
                 * override to translate fields into transport state
                 * (kernel @c fq rate, @c SO_TXTIME enablement) and
                 * internal pacing state.
                 *
                 * @param spec The new configuration.
                 * @return Error::Ok on success, Error::NotOpen if no
                 *         transport is bound, or another error if the
                 *         transport rejects the configuration.
                 */
                virtual Error configure(const Spec &spec);

                /**
                 * @brief Convenience: update the byte-rate cap.
                 *
                 * Useful for VBR video where the per-frame rate cap
                 * derived from the just-packed byte count needs to be
                 * pushed to the kernel before each frame's batch is
                 * dispatched.  Calls @ref configure with the rate
                 * field swapped.
                 *
                 * @param bytesPerSec Rate cap (0 = unlimited).
                 * @return Error::Ok on success, otherwise the same
                 *         errors @ref configure returns.
                 */
                Error setRate(uint64_t bytesPerSec);

                /**
                 * @brief Selects per-batch vs. stream-wide cadence.
                 *
                 * Mode is read by the userspace-cadence and txtime
                 * backends; burst and kernel-fq ignore it.  See
                 * @ref CadenceMode.  Default is @c PerBatch.
                 */
                void setCadenceMode(CadenceMode mode) { _cadenceMode = mode; }

                /** @brief Returns the current cadence mode. */
                CadenceMode cadenceMode() const { return _cadenceMode; }

                /**
                 * @brief Hands a frame's worth of datagrams to the scheduler.
                 *
                 * The caller has already framed the RTP packets onto
                 * each @c Datagram::data buffer and addressed each
                 * @c dest.  The scheduler applies its pacing policy
                 * and dispatches through the bound transport.
                 *
                 * Returns the cumulative count of datagrams accepted
                 * by the kernel.  When the kernel accepts fewer than
                 * requested in one syscall, schedulers loop until the
                 * batch is drained or an error is reported.
                 *
                 * @param datagrams The list of datagrams to send.  Not
                 *                  modified, except that the
                 *                  @c TxTime scheduler stamps
                 *                  @ref PacketTransport::Datagram::txTimeNs
                 *                  on a working copy before dispatch.
                 * @return Count of datagrams accepted, or -1 on
                 *         transport failure.
                 */
                virtual int enqueue(const PacketTransport::DatagramList &datagrams) = 0;

                /**
                 * @brief Returns the predicted egress delay @c D_TX in microseconds.
                 *
                 * The ST 2110-10 §7.9 @c TSDELAY fmtp value the SDP
                 * builder picks up when the user has not pinned an
                 * explicit @c TSDELAY.  Returns 0 when the policy
                 * makes no temporal guarantee (burst), or when the
                 * scheduler hasn't been configured yet.
                 */
                virtual int predictedTxDelayUs() const { return 0; }

                /**
                 * @brief Drains any pending packets to the transport.
                 *
                 * Synchronous schedulers (current implementations) are
                 * always drained, so this is a no-op.  Reserved for
                 * future async schedulers (DPDK / NIC TXTIME) that
                 * queue internally.
                 *
                 * @return Error::Ok on success, otherwise the
                 *         scheduler-specific failure.
                 */
                virtual Error flushPending() { return Error::Ok; }

                /**
                 * @brief Discards any pending packets.
                 *
                 * Counterpart to @ref flushPending for emergency
                 * shutdown.
                 *
                 * @return Error::Ok on success.
                 */
                virtual Error abort() { return Error::Ok; }

                /** @brief Returns the most-recent @ref Spec passed to @ref configure. */
                const Spec &spec() const { return _spec; }

                /**
                 * @brief Factory: pick the right scheduler subclass for the mode.
                 *
                 * Maps an @ref RtpPacingMode enum value to its
                 * concrete scheduler:
                 *
                 * - @c None      → @ref BurstPacketScheduler
                 * - @c Userspace → @ref CadencePacketScheduler
                 * - @c KernelFq  → @ref KernelFqPacketScheduler
                 * - @c TxTime    → @ref TxTimePacketScheduler
                 *
                 * Unknown modes fall back to @ref BurstPacketScheduler
                 * with a warning.
                 *
                 * @param mode      Pacing-mode enum (typically
                 *                  resolved from
                 *                  @c MediaConfig::RtpPacingMode).
                 * @param transport Transport the scheduler will bind
                 *                  to.  Stored via @ref setTransport.
                 * @return Owned scheduler instance bound to @p transport.
                 */
                static UPtr create(const Enum &mode, PacketTransport *transport);

        protected:
                PacketTransport *_transport = nullptr;
                Spec             _spec;
                CadenceMode      _cadenceMode = CadenceMode::PerBatch;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
