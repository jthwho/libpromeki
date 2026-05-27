/**
 * @file      rtpsession.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/atomic.h>
#include <promeki/list.h>
#include <promeki/mutex.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/buffer.h>
#include <promeki/clockdomain.h>
#include <promeki/duration.h>
#include <promeki/enums_rtp.h>
#include <promeki/packetscheduler.h>
#include <promeki/queue.h>
#include <promeki/rtcppacket.h>
#include <promeki/socketaddress.h>
#include <promeki/ntptime.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppacketbatch.h>
#include <promeki/packettransport.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

class Thread;
class RtpSeqTracker;
class RtpSeqReorderBuffer;

/**
 * @brief RTP session for sending and receiving packets (RFC 3550).
 * @ingroup network
 *
 * RtpSession manages the RTP protocol state for a single
 * synchronization source (SSRC).  It handles RTP header
 * construction, sequence number management, timestamp tracking,
 * and packet transmission via a @ref PacketTransport.
 *
 * The session operates on pre-built @ref RtpPacket lists from
 * @ref RtpPayload handlers: it fills in the RTP header fields
 * (version, payload type, sequence number, timestamp, SSRC, marker
 * bit) and hands the packets to the transport for delivery.
 *
 * @par Transport ownership
 *
 * RtpSession can either own its transport (when started via
 * @ref start(const SocketAddress&), which creates an internal
 * @ref UdpSocketTransport) or borrow a caller-owned transport
 * (when started via @ref start(PacketTransport*)).  Borrowing is
 * the right pattern when the caller needs to configure the
 * transport with options RtpSession doesn't expose, or when the
 * same transport is being shared across multiple RTP streams (not
 * currently supported, but forward-compatible with future DPDK
 * multiplexing).
 *
 * @par Destination
 *
 * The destination address is set once via @ref setRemote() before
 * sending any packets.  RTP streams have a single peer (unicast
 * address or multicast group), so per-send destination arguments
 * would only invite inconsistency.  To send to a multicast group,
 * pass the group address to @c setRemote() and configure the
 * transport (TTL, outgoing interface) before @c start().
 *
 * @par Example
 * @code
 * RtpSession session;
 * session.setPayloadType(96);
 * session.setClockRate(90000);
 * session.setRemote(SocketAddress(Ipv4Address(239, 0, 0, 1), 5004));
 * session.start(SocketAddress::any(0));
 *
 * RtpPayloadJpeg payload(1920, 1080);
 * RtpPacketBatch  batch;
 * batch.packets = payload.pack(jpegData, jpegSize);
 * for (size_t i = 0; i < batch.packets.size(); i++) {
 *         batch.packets[i].setTimestamp(timestamp);
 *         batch.packets[i].setMarker(i + 1 == batch.packets.size());
 * }
 * session.sendPackets(batch);
 * @endcode
 */
class RtpSession : public ObjectBase {
                PROMEKI_OBJECT(RtpSession, ObjectBase)
        public:
                /**
                 * @brief Constructs an RtpSession.
                 * @param parent The parent object, or nullptr.
                 */
                RtpSession(ObjectBase *parent = nullptr);

                /** @brief Destructor. Stops the session if running. */
                ~RtpSession() override;

                /**
                 * @brief Starts the session with an internally-owned UDP transport.
                 *
                 * Creates a @ref UdpSocketTransport bound to the given
                 * local address, opens it, and takes ownership.  The
                 * transport is automatically closed and destroyed by
                 * @ref stop() or the destructor.
                 *
                 * @param localAddr The local address and port to bind to.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error start(const SocketAddress &localAddr);

                /**
                 * @brief Starts the session with a caller-owned transport.
                 *
                 * The transport must be @ref PacketTransport::open "open"
                 * before this call.  RtpSession does not take ownership
                 * of the transport; the caller is responsible for
                 * closing and destroying it after @ref stop().
                 *
                 * @param transport The caller-owned, already-open transport.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error start(PacketTransport *transport);

                /**
                 * @brief Starts the session with two caller-owned
                 *        transports for ST 2022-7 dual-leg dispatch.
                 *
                 * Both transports must be @ref PacketTransport::open
                 * "open" before the call.  Every @ref sendPackets call
                 * stamps the RTP header (sequence number + SSRC) once,
                 * then fans the resulting packet list out to both
                 * transports with leg-specific destinations (set via
                 * @ref setRemote and @ref setRemoteSecondary) — the
                 * receiver sees bit-identical packets on both legs and
                 * dedups by extended sequence number per RFC 7104 §3.
                 * Each transport runs its own
                 * @ref PacketScheduler (set via @ref setScheduler and
                 * @ref setSchedulerSecondary) so back-pressure on one
                 * leg's socket never stalls the other.  The receive
                 * loop spawns one @c "rtp-rx" thread per leg, both
                 * dispatching into the same per-stream receivers (the
                 * @ref RtpSeqReorderBuffer's silent duplicate discard
                 * handles dedup).
                 *
                 * RtpSession does not take ownership of either
                 * transport; the caller closes / destroys them after
                 * @ref stop().
                 *
                 * @param primary   The primary leg's already-open transport.
                 * @param secondary The secondary leg's already-open
                 *                  transport.  Passing @c nullptr is
                 *                  equivalent to the single-arg
                 *                  @ref start(PacketTransport*)
                 *                  overload.
                 * @return @c Error::Ok on success, otherwise an error.
                 */
                Error start(PacketTransport *primary, PacketTransport *secondary);

                /** @brief Stops the session and closes any owned transport. */
                void stop();

                /** @brief Returns true if the session is started. */
                bool isRunning() const { return _running; }

                /**
                 * @brief Sets the destination address for all subsequent sends.
                 *
                 * Can be called before or after @ref start().  Unicast
                 * and multicast addresses are both accepted; multicast
                 * TTL and outgoing interface are configured on the
                 * transport, not here.
                 *
                 * @param dest The destination address and port.
                 */
                void setRemote(const SocketAddress &dest) { _remote = dest; }

                /** @brief Returns the current destination address. */
                const SocketAddress &remote() const { return _remote; }

                /**
                 * @brief Sets the ST 2022-7 secondary-leg destination.
                 *
                 * Only meaningful when the session was started with the
                 * dual-transport @ref start(PacketTransport*,PacketTransport*)
                 * overload.  In single-leg mode the secondary remote is
                 * unused; setting it has no effect on the wire.
                 */
                void setRemoteSecondary(const SocketAddress &dest) { _remoteSecondary = dest; }

                /** @brief Returns the secondary-leg destination address (may be null). */
                const SocketAddress &remoteSecondary() const { return _remoteSecondary; }

                /** @brief Returns true when a secondary transport is attached (ST 2022-7 mode). */
                bool hasSecondaryLeg() const { return _transportSecondary != nullptr; }

                /**
                 * @brief Sends a single RTP packet with raw payload data.
                 *
                 * @param payload The payload data.
                 * @param timestamp The RTP timestamp for this packet.
                 * @param payloadType The RTP payload type.
                 * @param marker If true, the marker bit is set.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error sendPacket(const Buffer &payload, uint32_t timestamp, uint8_t payloadType, bool marker = false);

                /**
                 * @brief Sends a packetizer-prepared @ref RtpPacketBatch
                 *        on the wire.
                 *
                 * The TX thread is the single owner of per-packet
                 * RTP-TS and marker — those fields must already be
                 * stamped on every packet in @ref RtpPacketBatch::packets
                 * before this call.  The session fills the
                 * transport-side header fields the packetizer cannot
                 * know (RTP version, sequence number, SSRC, payload
                 * type), then dispatches the resulting datagram batch
                 * to the transport.  When @ref RtpPacketBatch::rateCapBps
                 * is non-zero the transport's per-second rate cap is
                 * updated before the batch flies — used by the VBR
                 * compressed-video path so each frame's bytes are
                 * spread by kernel @c fq over exactly one frame
                 * interval.
                 *
                 * @param batch The packet batch to transmit.  Caller
                 *              owns the underlying packet buffers;
                 *              the session writes only the header
                 *              fields enumerated above and never
                 *              touches @ref RtpPacketBatch::packets'
                 *              payload bytes or the batch struct's
                 *              other fields.
                 * @return Error::Ok on success, or an error on failure.
                 *
                 * The legacy @c sendPackets(packets, ts, markerOnLast),
                 * @c sendPackets(packets, startTs, stride, marker), and
                 * @c sendPacketsPaced(...) overloads were deleted in
                 * Phase 3 of the RTP TX refactor.  Pacing strategy now
                 * lives in the per-stream TX thread (see
                 * @c VideoTxThread::run inside @c rtpmediaio.cpp),
                 * driven by the @c Cadence helper for userspace pacing
                 * and by @c setPacingRate() for kernel @c fq pacing.
                 */
                Error sendPackets(RtpPacketBatch &batch);

                /**
                 * @brief Per-stream RX dispatch entry plumbed
                 *        through the @ref startReceiving overload
                 *        below.
                 *
                 * Each entry describes how the recv socket thread
                 * should route packets that match a given RTP
                 * payload type:
                 * - Update the per-source @ref RtpSeqTracker with
                 *   the packet's seq / RTP-TS / @c arrivalSteady
                 *   (RFC 3550 §A.1 / §A.3 / §A.8 bookkeeping).
                 * - Push the packet through the
                 *   @ref RtpSeqReorderBuffer for in-order /
                 *   deadline-fill / drop-oldest dispatch into the
                 *   per-stream @c outQueue, where the matching
                 *   per-stream depacketizer thread consumes it.
                 *
                 * All pointers are caller-owned and must outlive
                 * the receive loop.  The recv thread does not take
                 * ownership; @ref stopReceiving releases the
                 * receivers list before the storage is freed.
                 */
                struct StreamReceiver {
                                /// @brief Per-stream post-reorder
                                ///        output queue.  The
                                ///        depacketizer thread
                                ///        consumes from here.
                                RtpPacket::Queue *outQueue = nullptr;

                                /// @brief Per-source seq /
                                ///        loss / jitter tracker.
                                RtpSeqTracker *seqTracker = nullptr;

                                /// @brief Windowed reorder buffer
                                ///        sitting in front of
                                ///        @ref outQueue.
                                RtpSeqReorderBuffer *reorderBuffer = nullptr;

                                /// @brief Per-stream RTP clock
                                ///        rate in Hz; plumbed
                                ///        into the seq tracker
                                ///        via implicit init for
                                ///        §A.8 jitter accounting,
                                ///        and into the
                                ///        depacketizer's
                                ///        @c StreamAnchor for
                                ///        pre-SR @c captureTime
                                ///        interpolation.
                                uint32_t clockRateHz = 0;

                                /// @brief RTP payload-type byte
                                ///        the recv thread matches
                                ///        against on dispatch.
                                ///        Today's single-stream-
                                ///        per-session case uses
                                ///        a one-entry list and
                                ///        the dispatch is a
                                ///        no-op; multi-stream
                                ///        sessions (later) will
                                ///        select by PT.
                                uint8_t payloadType = 0;
                };

                /**
                 * @brief Starts the per-session RTP receive loop.
                 *
                 * Spawns an internal @ref Thread named
                 * @c "rtp-rx" that calls
                 * @ref PacketTransport::receivePacket in a loop,
                 * wraps each datagram in an @ref RtpPacket view of
                 * an owned @ref Buffer, demuxes RTCP via byte[1] in
                 * the @c [200..223] range, and routes RTP packets
                 * through the supplied per-stream @ref StreamReceiver
                 * entries: the recv socket thread updates each
                 * entry's seq tracker and pushes through its
                 * reorder buffer into its post-reorder
                 * @c RtpPacket::Queue.  The matching per-stream
                 * depacketizer thread consumes from the queue on
                 * its own thread, so the recv thread never runs
                 * reassembly / payload->unpack / FIFO push work
                 * inline.  See @c devplan/network/rtp-rx.md for
                 * the full threading topology.
                 *
                 * The session must already be running
                 * (@ref isRunning() == true) — call one of the
                 * @ref start overloads first.
                 *
                 * The receive thread polls the stop flag every few
                 * hundred milliseconds using @c SO_RCVTIMEO on the
                 * underlying socket, so @ref stopReceiving() returns
                 * within one poll interval.
                 *
                 * The @ref packetReceived signal still fires for
                 * external consumers (test fixtures, future
                 * diagnostic UIs) that prefer event-loop dispatch.
                 *
                 * @param receivers  Per-stream dispatch entries.
                 *                   Empty list is rejected with
                 *                   @c Error::InvalidArgument.
                 * @param threadName Thread name surfaced to
                 *                   debuggers and OS monitors.
                 *                   Defaults to @c "rtp-rx".
                 * @return @c Error::Ok on success,
                 *         @c Error::Busy if already receiving,
                 *         @c Error::NotOpen if the session has
                 *         not been started, or
                 *         @c Error::InvalidArgument on a malformed
                 *         receivers list.
                 */
                Error startReceiving(List<StreamReceiver> receivers,
                                     const String &threadName = "rtp-rx");

                /**
                 * @brief Stops the receive loop and joins the receive thread.
                 *
                 * Safe to call from any thread, including the
                 * receive thread itself (in which case the join is
                 * skipped).  Safe to call when no receive loop is
                 * running.
                 */
                void stopReceiving();

                /** @brief Returns true if the receive loop is running. */
                bool isReceiving() const { return _receiving.value(); }

                /**
                 * @brief Sets the receive-loop poll interval in milliseconds.
                 *
                 * Shorter intervals make @ref stopReceiving more
                 * responsive at the cost of extra idle wakeups.
                 * Default is 200 ms.  Must be set before
                 * @ref startReceiving.
                 *
                 * @param timeoutMs Poll interval in milliseconds.
                 */
                void setReceivePollIntervalMs(unsigned int timeoutMs) {
                        _receivePollMs = timeoutMs == 0 ? 200 : timeoutMs;
                }

                /** @brief Returns the receive-loop poll interval. */
                unsigned int receivePollIntervalMs() const { return _receivePollMs; }

                /**
                 * @brief Applies a transmit-rate cap to the bound scheduler.
                 *
                 * Convenience wrapper that updates the active
                 * @ref PacketScheduler's @c bytesPerSec field.  For
                 * @ref KernelFqPacketScheduler this maps through to
                 * the kernel @c SO_MAX_PACING_RATE option; other
                 * scheduler subclasses use the value for
                 * @c predictedTxDelayUs accounting only.
                 *
                 * @param bytesPerSec Maximum transmit rate in bytes/sec.
                 * @return Error::Ok on success, Error::NotOpen if no
                 *         scheduler has been installed, or another
                 *         error if the scheduler rejects the update.
                 */
                Error setPacingRate(uint64_t bytesPerSec);

                /**
                 * @brief Installs (or replaces) the per-session @ref PacketScheduler.
                 *
                 * Called by @ref RtpMediaIO at @ref start() time once
                 * @c MediaConfig::RtpPacingMode has been resolved.
                 * Ownership transfers to the session; the prior
                 * scheduler (if any) is destroyed.  The new scheduler
                 * is bound to the session's current transport via
                 * @ref PacketScheduler::setTransport.
                 *
                 * @param scheduler New scheduler instance.  Passing
                 *                  null detaches the scheduler — used
                 *                  by tests; production callers always
                 *                  pass a real scheduler.
                 */
                void setScheduler(PacketScheduler::UPtr scheduler);

                /** @brief Returns the bound scheduler, or null if none is installed. */
                PacketScheduler *scheduler() const { return _scheduler.get(); }

                /**
                 * @brief Installs the per-session secondary
                 *        @ref PacketScheduler for ST 2022-7 dual-leg
                 *        dispatch.
                 *
                 * Mirrors @ref setScheduler — the scheduler is bound to
                 * the secondary transport at the next
                 * @ref start(PacketTransport*,PacketTransport*) call.
                 * Passing null detaches the secondary scheduler; an
                 * RtpSession with a secondary transport but no
                 * secondary scheduler falls back to a raw transport
                 * sendmmsg loop for that leg.
                 */
                void setSchedulerSecondary(PacketScheduler::UPtr scheduler);

                /** @brief Returns the secondary scheduler, or null if none is installed. */
                PacketScheduler *schedulerSecondary() const { return _schedulerSecondary.get(); }

                /**
                 * @brief Applies a scheduler configuration.
                 *
                 * Thin pass-through to the installed scheduler's
                 * @ref PacketScheduler::configure.  Useful for callers
                 * that change the per-frame budget (e.g.  framerate
                 * change) after start time.
                 *
                 * @param spec Configuration to apply.
                 * @return Error::Ok on success, Error::NotOpen if no
                 *         scheduler is installed, otherwise the
                 *         scheduler's failure.
                 */
                Error configureScheduler(const PacketScheduler::Spec &spec);

                /** @brief Returns the locally generated SSRC. */
                uint32_t ssrc() const { return _ssrc; }

                /** @brief Overrides the auto-generated SSRC. */
                void setSsrc(uint32_t ssrc) { _ssrc = ssrc; }

                /** @brief Returns the current sequence number. */
                uint16_t sequenceNumber() const { return _sequenceNumber; }

                /** @brief Sets the default payload type. */
                void setPayloadType(uint8_t pt) { _payloadType = pt; }

                /** @brief Returns the default payload type. */
                uint8_t payloadType() const { return _payloadType; }

                /** @brief Sets the RTP timestamp clock rate in Hz. */
                void setClockRate(uint32_t hz) { _clockRate = hz; }

                /** @brief Returns the RTP timestamp clock rate. */
                uint32_t clockRate() const { return _clockRate; }

                /**
                 * @brief Anchors this session's RTP-timestamp clock to
                 *        a known capture-wallclock instant.
                 *
                 * The anchor establishes the reference pair the SR
                 * uses to derive NTP from any subsequent RTP-TS:
                 * @f$ NTP(t) = anchor.ntp + (t - anchor.rtpTs) /
                 * clockRate @f$, with the subtraction taken modulo
                 * @c uint32_t so wraparound is handled uniformly on
                 * both sides.  Receivers run the same arithmetic
                 * against the SR they receive, so the wallclock
                 * mapping survives end-to-end without needing a
                 * shared grandmaster.
                 *
                 * Writer-only.  Called by @ref RtpMediaIO at open
                 * time (default @c NtpTime::now() / @c 0) and
                 * refined the first time a Frame with a valid
                 * @ref Frame::captureTime arrives — the upstream
                 * code converts the captureTime's
                 * @c MediaTimeStamp to NTP using a single observed
                 * @c (steady, wall) reference instant.
                 *
                 * Multiple sessions in the same RtpMediaIO are
                 * seeded with the same NTP so receivers can
                 * cross-correlate their RTP-TS streams against one
                 * shared wallclock domain — a single SR observation
                 * per stream is sufficient for cross-stream sync.
                 *
                 * @param captureNtp The NTP wallclock value
                 *                   corresponding to RTP-TS
                 *                   @p rtpTs on this stream.
                 * @param rtpTs      The RTP-TS that aligns with
                 *                   @p captureNtp.  Conventionally
                 *                   @c 0 at the first frame /
                 *                   sample, but any deterministic
                 *                   pair works.
                 */
                void setRtpAnchor(NtpTime captureNtp, uint32_t rtpTs);

                /**
                 * @brief Overload that derives the anchor NTP from a
                 *        @ref ClockDomain with a bound wallclock-now
                 *        provider.
                 *
                 * The classic @ref setRtpAnchor(NtpTime, uint32_t)
                 * captures @c NtpTime::now() (which on Linux reads
                 * @c CLOCK_REALTIME) — fine when the host's
                 * @c CLOCK_REALTIME is itself driven by @c phc2sys, but
                 * lying when the host's wallclock is unsynced and the
                 * media stream is supposed to ride a PTP grandmaster.
                 * This overload reads
                 * @c domain.nowUtcNs() — typically PHC-derived for
                 * @ref ClockDomain::Ptp — and feeds it through
                 * @ref NtpTime::fromSystemClock so the SR carries the
                 * PTP-traceable wallclock instead.
                 *
                 * Falls back to @c NtpTime::now() (with a one-shot
                 * warn) when @p domain has no provider bound, so an
                 * application that opts into PTP signalling without
                 * actually wiring a clock degrades to today's
                 * behaviour rather than emitting a zeroed SR.
                 *
                 * Writer-only — same semantics as the @c NtpTime
                 * overload otherwise.
                 *
                 * @param domain A registered @ref ClockDomain with a
                 *               bound @ref ClockDomain::WallClockProvider.
                 * @param rtpTs  The RTP-TS that aligns with
                 *               @c domain's @c nowUtcNs.
                 */
                void setRtpAnchor(const ClockDomain &domain, uint32_t rtpTs);

                /**
                 * @brief Returns the most-recently configured anchor
                 *        NTP timestamp.
                 *
                 * Inspect-only — receivers and tests use it to
                 * cross-check the SR derivation; production callers
                 * should not need to read it back.
                 */
                NtpTime anchorNtp() const;

                /**
                 * @brief Returns the most-recently configured anchor
                 *        RTP-TS.  See @ref anchorNtp.
                 */
                uint32_t anchorRtpTs() const;

                /**
                 * @brief Records the RTP timestamp of the most-recent
                 *        packet that was actually placed on the wire.
                 *
                 * @ref emitRtcpSr derives the SR's NTP wallclock
                 * deterministically from this rtpTs and the anchor —
                 * no system-clock sample is taken at emission time.
                 * That keeps SRs aligned with the stream's nominal
                 * wire-RTP-TS clock even when the sender's
                 * @c steady_clock disagrees with @c system_clock by
                 * parts-per-million; receivers run the same anchor
                 * arithmetic so any drift between the two domains
                 * has no effect on the receiver-side mapping.
                 *
                 * Silence packets emitted by an audio TX thread
                 * whose source has stalled count exactly like
                 * real-content packets — they are real wire
                 * emissions, the SR's RTP-TS must reflect them, and
                 * @ref hasEmissionRecord must return true so the
                 * RTCP scheduler keeps the SR cadence going for an
                 * audio session that's currently producing only
                 * silence.
                 *
                 * @param rtpTs The RTP timestamp of the packet that
                 *              was just emitted.  For multi-packet
                 *              batches the convention is the
                 *              timestamp of the FIRST packet so the
                 *              SR's wallclock mapping references
                 *              the same instant a receiver would
                 *              see for that packet.
                 */
                void noteRtpEmission(uint32_t rtpTs);

                /**
                 * @brief True if this session has captured at least
                 *        one RTP emission via @ref noteRtpEmission.
                 *
                 * Used by the RTCP scheduler to skip sending an SR for
                 * streams that have not emitted any packets yet — an
                 * SR with garbage (NTP, RTP) fields gives receivers a
                 * worse sync starting point than no SR at all.
                 */
                bool hasEmissionRecord() const;

                /// @brief Returns the configured CNAME for RTCP SDES.
                const String &cname() const { return _cname; }

                /// @brief Sets the CNAME emitted in RTCP SDES.
                void setCname(const String &cname) { _cname = cname; }

                /**
                 * @brief Builds and transmits a single RTCP compound
                 *        packet (SR + SDES/CNAME) on this session's
                 *        transport.
                 *
                 * Best-effort: returns @c Error::NotOpen when the
                 * session is not running, otherwise the transport's
                 * sendmsg result.  The NTP timestamp is derived
                 * from the active @ref setRtpAnchor and the
                 * most-recent @ref noteRtpEmission rtpTs — no
                 * caller has to compute it explicitly.
                 *
                 * @param senderPacketCount Cumulative RTP packets the
                 *                          stream has emitted (drawn
                 *                          from the per-stream stats
                 *                          counter — RtpSession does
                 *                          not maintain its own).
                 * @param senderOctetCount  Cumulative RTP-payload
                 *                          octets (excludes RTP
                 *                          headers).
                 */
                Error emitRtcpSr(uint32_t senderPacketCount, uint32_t senderOctetCount);

                /**
                 * @brief Emits a Receiver Report compound (RR + SDES).
                 *
                 * @c block describes the source the receiver is
                 * reporting on; @c lsr / @c dlsr are derived from
                 * @ref receivedSr — when no SR has been observed yet
                 * the RFC 3550 §6.4.1 convention is to leave both
                 * fields zero, which @ref RtcpPacket::buildReceiverReport
                 * does naturally.
                 *
                 * The compound includes an SDES with this session's
                 * CNAME so receivers correlate this session with any
                 * other streams sharing the CNAME.
                 *
                 * @return @c Error::NotOpen when the session has not
                 *         started, otherwise the transport's send
                 *         result.
                 */
                Error emitRtcpRr(const RtcpPacket::ReportBlock &block);

                /**
                 * @brief Emits a Goodbye / BYE compound for this
                 *        session's SSRC.
                 *
                 * Called on clean shutdown so receivers can drop the
                 * source immediately rather than waiting for a
                 * timeout.  The compound includes a final SDES so
                 * the receiver still has a CNAME for any cross-stream
                 * correlation it has cached.
                 */
                Error emitRtcpBye();

                /**
                 * @brief Snapshot of the most-recently parsed
                 *        Sender Report received on this session.
                 *
                 * @ref valid stays @c false until at least one SR
                 * has been parsed; the other fields are zero in
                 * that case.  Once an SR has arrived, @ref ntp /
                 * @ref rtpTs hold the SR's @c (NTP, RTP-TS) anchor
                 * pair (a receiver-side @ref RtpStreamClock can be
                 * built directly from these), and @ref arrivedAt
                 * is the local @c steady_clock instant the receive
                 * thread parsed the packet at — useful for "has a
                 * fresh SR landed since I last looked?" checks.
                 */
                struct ReceivedSr {
                        NtpTime   ntp;
                        uint32_t  rtpTs = 0;
                        TimeStamp arrivedAt;
                        bool      valid = false;
                };

                /**
                 * @brief Returns the most-recent SR snapshot parsed
                 *        by the receive thread.
                 *
                 * Invalid (@c valid == false) until the first SR has
                 * arrived.  Some senders delay emission of the first
                 * SR (RFC 3550 lets the first interval be randomized
                 * to spread RTCP load), so receivers that rely on
                 * SR-driven sync must tolerate a brief startup
                 * window where this returns invalid.
                 */
                ReceivedSr receivedSr() const;

                /**
                 * @brief Returns the cumulative count of SRs the
                 *        receive thread has parsed for this session.
                 *
                 * Zero until the first SR arrives.  Surfaced through
                 * @ref RtpMediaIO::StatsRxSrObserved.  Wraps
                 * silently after 2^32 SRs (~6800 years at the
                 * default 5-second RTCP interval).
                 */
                uint32_t srObservedCount() const;

                /**
                 * @brief Returns the local steady-clock timestamp
                 *        of the first SR observed for this session.
                 *
                 * Default-constructed (epoch) until the first SR
                 * arrives.  Surfaced indirectly via
                 * @ref RtpMediaIO::StatsRxFirstSrLatency, which
                 * subtracts the session's open-time anchor.
                 */
                TimeStamp firstSrAt() const;

                /**
                 * @brief Computes the NTP wallclock timestamp the SR
                 *        would carry right now without sending it.
                 *
                 * Pure-function preview into the SR-derivation logic
                 * so unit tests can pin the
                 * @f$ anchor.ntp + (rtpTs - anchor.rtpTs) /
                 * clockRate @f$ arithmetic without standing up a
                 * transport.  Returns the anchor NTP unchanged when
                 * no emission has been recorded yet — @p rtpTs
                 * defaults to @c 0 in that case so the result is
                 * deterministic.
                 *
                 * @return The derived NTP timestamp.
                 */
                NtpTime currentSrNtp() const;

                /**
                 * @brief Returns the active packet transport.
                 *
                 * This is the transport the session is currently
                 * using — either the one created internally by
                 * @ref start(const SocketAddress&), or the one passed
                 * to @ref start(PacketTransport*).
                 *
                 * @return Pointer to the transport, or nullptr if the
                 *         session is not running.
                 */
                PacketTransport *transport() const { return _transport; }

                /**
                 * @brief Emitted when a packet is received.
                 *
                 * Parameters: payload data (Buffer), RTP timestamp (uint32_t),
                 * payload type (uint8_t), marker bit (bool).
                 *
                 * @signal
                 */
                PROMEKI_SIGNAL(packetReceived, Buffer, uint32_t, uint8_t, bool);

                /** @brief Emitted when an SSRC collision is detected. @signal */
                PROMEKI_SIGNAL(ssrcCollision, uint32_t);

                /**
                 * @brief Emitted when the queue-mode receive loop
                 *        detects a sustained SSRC change on an
                 *        active stream and resets per-source state.
                 *
                 * Parameters: @c oldSsrc (the SSRC the recv thread
                 * had pinned), @c newSsrc (the SSRC the wire is
                 * carrying now), and the @c payloadType the change
                 * was observed on.  Distinct from
                 * @ref ssrcCollision, which fires only when the
                 * incoming SSRC equals our own outgoing SSRC.
                 *
                 * @signal
                 */
                PROMEKI_SIGNAL(ssrcChange, uint32_t, uint32_t, uint8_t);

                /**
                 * @brief Emitted when an inbound RTCP BYE is
                 *        observed for the given SSRC.
                 *
                 * The recv thread parses BYE inside @ref handleRtcp
                 * and emits this signal so RtpMediaIO can flush
                 * the affected stream's depacketizer + aggregator
                 * and surface EoS to the strand.
                 *
                 * @signal
                 */
                PROMEKI_SIGNAL(byeReceived, uint32_t);

        private:
                class ReceiveThread;
                friend class ReceiveThread;

                /**
                 * @brief Consumes an inbound RTCP datagram.
                 *
                 * Walks the compound packet for Sender Reports and
                 * caches the most-recent one in @c _lastReceivedSr so
                 * @ref receivedSr can surface it to reader-side
                 * aggregators.  All other packet types (SDES, BYE,
                 * APP, RR, …) are dropped silently — RTCP is
                 * forward-compatible by design.
                 *
                 * Called from the receive thread for every datagram
                 * whose second byte is in the @c [200..223] RTCP
                 * packet-type range.
                 *
                 * @param data Pointer to the first byte of the RTCP
                 *             datagram (which may be a compound).
                 * @param size Bytes available at @p data.
                 */
                void handleRtcp(const uint8_t *data, size_t size);

                void fillHeader(RtpPacket &pkt, uint8_t pt, bool marker, uint32_t timestamp);
                /**
                 * @brief Fills only the transport-side RTP header
                 *        fields on @p pkt.
                 *
                 * Sets version, payload type, sequence number, and
                 * SSRC.  Leaves marker and timestamp untouched —
                 * those are filled by the caller (TX thread) before
                 * @ref sendPackets dispatches the packet.  Used by
                 * the @ref RtpPacketBatch send path; @ref fillHeader
                 * is the all-fields helper used by @ref sendPacket.
                 */
                void fillTransportHeader(RtpPacket &pkt);
                void generateSsrc();

                PacketTransport      *_transport = nullptr;
                PacketTransport::UPtr _ownedTransport;
                PacketScheduler::UPtr _scheduler;

                // ST 2022-7 dual-leg state.  When _transportSecondary is
                // non-null sendPackets stamps the RTP header once and
                // fans the resulting Datagram list out to both
                // transports — leg-specific destinations are taken from
                // _remote (primary) and _remoteSecondary; each scheduler
                // is bound to its own transport so back-pressure on
                // one leg cannot stall the other.  Receive side: a
                // second receive thread services the secondary transport
                // and joins the primary thread on shutdown.  Both
                // recv threads dispatch through _dispatchMutex so the
                // per-stream seq tracker / SSRC pin / reorder buffer
                // stay coherent.
                PacketTransport      *_transportSecondary = nullptr;
                PacketTransport::UPtr _ownedTransportSecondary;
                PacketScheduler::UPtr _schedulerSecondary;
                SocketAddress         _remoteSecondary;

                bool                  _running = false;
                SocketAddress         _remote;
                uint32_t              _ssrc = 0;
                uint16_t              _sequenceNumber = 0;
                uint8_t               _payloadType = 96;
                uint32_t              _clockRate = 90000;

                // RTCP SR / SDES state.  CNAME is the SDES item every
                // SR-bearing compound packet carries.  The
                // capture-anchor is established once at openStream
                // time and refined by the first arriving Frame's
                // @ref Frame::captureTime; thereafter, @c emitRtcpSr
                // derives the SR's NTP from
                // @c anchorNtp + (lastEmissionRtpTs - anchorRtpTs) /
                // clockRate without sampling the system clock.
                // @c _hasEmission gates SR emission on whether any
                // packet has actually gone out, so the scheduler
                // never sends an SR for a session that has yet to
                // produce wire activity.  Mutex-guarded because the
                // noter runs on the per-stream TX thread while the
                // scheduler runs on its own thread.
                mutable Mutex _rtcpMutex;
                NtpTime       _anchorNtp;
                uint32_t      _anchorRtpTs = 0;
                uint32_t      _lastEmissionRtpTs = 0;
                bool          _hasEmission = false;
                String        _cname;
                /// @brief Cumulative count of SRs parsed by the
                ///        receive thread.  Mutex-guarded against
                ///        @ref srObservedCount readers on other
                ///        threads (RTCP scheduler, stats path).
                uint32_t      _srObservedCount = 0;
                /// @brief Local steady-clock timestamp of the
                ///        first SR observed.  Invalid until the
                ///        first SR arrives — callers detect that
                ///        via @c isValid().  Used by
                ///        @ref firstSrAt + @ref RtpMediaIO to
                ///        compute @c firstSrLatency.
                TimeStamp     _firstSrAt;

                // Most-recently parsed inbound SR.  The receive thread
                // demuxes RTCP from RTP via the second byte of every
                // datagram (PT in [200..223] → RTCP) and walks each
                // RTCP compound for SRs.  Reader-side helpers like
                // @ref RtpStreamClock pick this up via
                // @ref receivedSr to map any future RTP-TS on this
                // session to a wallclock instant for cross-stream
                // alignment.
                ReceivedSr _lastReceivedSr;

                // Receive path.  @c _streamReceivers is populated
                // by @ref startReceiving and consumed by the recv
                // socket thread; per-stream depacketizer threads
                // pull from the post-reorder queues each entry
                // points at.
                using ReceiveThreadUPtr = UniquePtr<ReceiveThread>;
                ReceiveThreadUPtr    _receiveThread;
                ReceiveThreadUPtr    _receiveThreadSecondary; ///< ST 2022-7 secondary-leg recv thread.
                Mutex                _dispatchMutex;          ///< Guards dispatchToReceivers under dual-leg recv.
                List<StreamReceiver> _streamReceivers;
                Atomic<bool>         _receiving;
                unsigned int         _receivePollMs = 200;

                // Per-stream-receiver SSRC pin state.  Sized to
                // match @c _streamReceivers.size() at
                // @c startReceiving time.  Index-parallel — entry
                // @c i tracks the SSRC pin for receivers[i].
                struct SsrcPinState {
                                uint32_t  expectedSsrc = 0;
                                bool      pinned = false;
                                uint32_t  mismatchCount = 0;
                                TimeStamp mismatchFirstTime;
                };
                List<SsrcPinState> _ssrcPinStates;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
