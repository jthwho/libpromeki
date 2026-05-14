/**
 * @file      rtpancdepacketizerthread.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <promeki/function.h>
#include <promeki/ancdesc.h>
#include <promeki/atomic.h>
#include <promeki/clockdomain.h>
#include <promeki/namespace.h>
#include <promeki/queue.h>
#include <promeki/rtpdepacketizerthread.h>
#include <promeki/rtppacket.h>
#include <promeki/rtpstreamclock.h>
#include <promeki/rxpayloadbundle.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

class RtpPayloadAnc;

/**
 * @brief Static dependencies handed to @ref RtpAncDepacketizerThread
 *        at construction time.
 * @ingroup network
 *
 * Mirrors @ref RtpDataDepacketizerContext for the per-stream ANC
 * reader path.  The ANC depacketizer accumulates RTP packets across
 * marker-bit boundaries, runs @ref RtpPayloadAnc::unpackAncPackets
 * on the reassembled batch, and pushes a single @ref RxAncFrame
 * onto the per-stream payload queue.
 *
 * @par Lifetime
 * Every pointer in this struct must outlive the depacketizer thread.
 */
struct RtpAncDepacketizerContext {
                /// @brief Output queue the depacketizer pushes
                ///        @ref RxAncFrame bundles onto.  Must be set.
                Queue<RxAncFrame> *payloadQueue = nullptr;

                /// @brief Per-stream SSRC reset epoch.  Bumped by
                ///        @c RtpSession on every SSRC change; the
                ///        depacketizer compares each iteration and
                ///        drops in-flight reassembly on mismatch.
                const Atomic<uint32_t> *resetEpoch = nullptr;

                /// @brief @c true once the surrounding @c RtpMediaIO
                ///        has activated this stream.  @c nullptr is
                ///        treated as "always active".
                const bool *active = nullptr;

                /// @brief The @ref RtpPayloadAnc that owns
                ///        @c unpackAncPackets.  Must be set.
                RtpPayloadAnc *payload = nullptr;

                /// @brief Static per-stream descriptor — typically the
                ///        @ref AncDesc resolved at configure time
                ///        (allowedFormats from the fmtp DID_SDID list,
                ///        raster + scan mode inherited from the paired
                ///        video).  Stamped onto every produced
                ///        @ref RxAncFrame.
                AncDesc desc;

                /// @brief SDP-derived clock domain stamped onto every
                ///        @c MediaTimeStamp the depacketizer produces.
                ClockDomain clockDomain;

                /// @brief @c true once the first SR has seeded
                ///        @ref streamClock.
                const bool *hasSr = nullptr;

                /// @brief Per-stream RTP <-> NTP clock mapping.
                const RtpStreamClock *streamClock = nullptr;

                /// @brief Stat counters — non-owning pointers.  Each
                ///        may be @c nullptr in tests.
                Atomic<int64_t> *packetsReceived = nullptr;
                Atomic<int64_t> *bytesReceived = nullptr;
                Atomic<int64_t> *lastPacketArrivalNs = nullptr;
                Atomic<int64_t> *framesReassembled = nullptr;
                Atomic<int64_t> *framesDroppedSsrcReset = nullptr;

                /// @brief Bumps the @c FrameCount on the owning
                ///        ReaderStream.  May be @c nullptr.
                Function<void()> noteFrameReceived;

                /// @brief Refreshes @ref streamClock from the owning
                ///        RtpSession's most-recent SR.
                Function<void()> refreshStreamClock;

                /// @brief Converts an NTP timestamp to local steady
                ///        time using the owning @c RtpMediaIO 's
                ///        anchor.  Returns a default TimeStamp when
                ///        the anchor is not yet valid.
                Function<TimeStamp(const NtpTime &)> ntpToSteady;
};

/**
 * @brief Per-stream depacketizer for RFC 8331 ANC streams.
 * @ingroup network
 *
 * One instance per active ANC reader stream.  Pops post-reorder
 * @ref RtpPacket bundles off @ref RtpDepacketizerThread::inputQueue,
 * accumulates them across marker-bit boundaries (timestamp-change
 * flush + marker flush), runs
 * @c RtpPayloadAnc::unpackAncPackets, stamps a per-frame
 * @c captureTime, and pushes an @ref RxAncFrame onto the per-stream
 * @c payloadQueue the @c RtpAggregatorThread drains.
 *
 * @par Threading
 * Runs on its own worker thread.  Reassembly state lives on the
 * worker; only the queue pushes and counter bumps cross threads.
 */
class RtpAncDepacketizerThread : public RtpDepacketizerThread {
        public:
                /**
                 * @brief Constructs an unstarted ANC depacketizer.
                 *
                 * @param ctx         Static dependencies.
                 * @param name        Short OS thread name.
                 * @param clockRateHz Per-stream RTP clock rate (90000
                 *                    for RFC 8331).
                 */
                RtpAncDepacketizerThread(RtpAncDepacketizerContext ctx,
                                         const String              &name,
                                         uint32_t                   clockRateHz);

                ~RtpAncDepacketizerThread() override;

                RtpAncDepacketizerThread(const RtpAncDepacketizerThread &) = delete;
                RtpAncDepacketizerThread &operator=(const RtpAncDepacketizerThread &) = delete;

                /// @brief Direct entry point for unit tests — runs
                ///        @ref handlePacket once on the test thread.
                void handlePacketForTest(const RtpPacket &pkt) { handlePacket(pkt); }

                /// @brief Forces an emit of any in-flight reassembly.
                ///        Intended for tests; production code relies
                ///        on the marker bit / timestamp change to
                ///        trigger emission.
                void flushForTest() { emitFrame(); }

        protected:
                void handlePacket(const RtpPacket &pkt) override;
                void onStop() override;

        private:
                void emitFrame();

                RtpAncDepacketizerContext _ctx;
                RtpPacket::List           _reasmPackets;
                uint32_t                  _reasmTimestamp = 0;
                bool                      _reasmHasTimestamp = false;
                uint32_t                  _lastEpoch = 0;
};

PROMEKI_NAMESPACE_END
