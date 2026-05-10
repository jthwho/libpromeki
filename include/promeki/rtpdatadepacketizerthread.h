/**
 * @file      rtpdatadepacketizerthread.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <promeki/atomic.h>
#include <promeki/clockdomain.h>
#include <promeki/namespace.h>
#include <promeki/queue.h>
#include <promeki/rtpdepacketizerthread.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayload.h>
#include <promeki/rtpstreamclock.h>
#include <promeki/rxpayloadbundle.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Static dependencies handed to @ref RtpDataDepacketizerThread
 *        at construction time.
 * @ingroup network
 *
 * Mirrors @ref RtpAudioDepacketizerContext but for the per-stream
 * data (JSON metadata) reader path.  The data depacketizer
 * accumulates packets across marker-bit boundaries, then runs
 * @c payload->unpack on the reassembled bytes, parses the result
 * as JSON, and pushes an @ref RxDataMessage onto the per-stream
 * payload queue.
 *
 * @par Lifetime
 * Every pointer in this struct must outlive the depacketizer
 * thread.
 */
struct RtpDataDepacketizerContext {
                /// @brief Output queue the depacketizer pushes
                ///        @ref RxDataMessage bundles onto.  Must
                ///        be set.
                Queue<RxDataMessage> *payloadQueue = nullptr;

                /// @brief Per-stream RFC 3550 SSRC reset epoch.
                ///        Compared at the top of every
                ///        @c handlePacket; on mismatch the
                ///        depacketizer drops in-flight reassembly
                ///        and resets the anchor.
                const Atomic<uint32_t> *resetEpoch = nullptr;

                /// @brief @c true once the surrounding
                ///        @c RtpMediaIO has activated this stream.
                ///        @c nullptr is treated as "always active".
                const bool *active = nullptr;

                /// @brief The @ref RtpPayload that owns
                ///        @c unpack — typically @c RtpPayloadJson
                ///        for the metadata path.  Must be set.
                RtpPayload *payload = nullptr;

                /// @brief SDP-derived clock domain.  Stamped onto
                ///        every @c MediaTimeStamp the depacketizer
                ///        produces.
                ClockDomain clockDomain;

                /// @brief @c true once the first SR has seeded
                ///        @ref streamClock.  Read with
                ///        @ref streamClock to decide whether
                ///        wallclock alignment is in effect.
                const bool *hasSr = nullptr;

                /// @brief Per-stream RTP <-> NTP clock mapping.
                const RtpStreamClock *streamClock = nullptr;

                /// @brief Stat counters — non-owning pointers.
                ///        Each may be @c nullptr in tests.
                Atomic<int64_t> *packetsReceived = nullptr;
                Atomic<int64_t> *bytesReceived = nullptr;
                Atomic<int64_t> *lastPacketArrivalNs = nullptr;
                Atomic<int64_t> *framesReassembled = nullptr;
                Atomic<int64_t> *framesDroppedSsrcReset = nullptr;

                /// @brief Bumps the @c FrameCount on the owning
                ///        ReaderStream.  May be @c nullptr.
                std::function<void()> noteFrameReceived;

                /// @brief Refreshes @ref streamClock from the
                ///        owning RtpSession's most-recent SR.
                std::function<void()> refreshStreamClock;

                /// @brief Converts an NTP timestamp to local
                ///        steady time using the owning
                ///        @c RtpMediaIO 's anchor.  Returns a
                ///        default TimeStamp when the anchor is
                ///        not yet valid.
                std::function<TimeStamp(const NtpTime &)> ntpToSteady;
};

/**
 * @brief Per-stream depacketizer for JSON metadata streams.
 * @ingroup network
 *
 * One instance per active data reader stream.  Pops post-reorder
 * @ref RtpPacket bundles off
 * @ref RtpDepacketizerThread::inputQueue, accumulates them across
 * marker-bit boundaries (timestamp-change flush + marker flush),
 * runs @c payload->unpack, parses the resulting bytes as JSON,
 * stamps a per-message @c captureTime, and pushes an
 * @ref RxDataMessage onto the per-stream @c payloadQueue the
 * @ref RtpAggregatorThread drains.
 *
 * @par Threading
 * Runs on its own worker thread.  Reassembly state lives on the
 * worker; only the queue pushes and counter bumps cross threads.
 */
class RtpDataDepacketizerThread : public RtpDepacketizerThread {
        public:
                /**
                 * @brief Constructs an unstarted data depacketizer.
                 *
                 * @param ctx         Static dependencies.
                 * @param name        Short OS thread name.
                 * @param clockRateHz Per-stream RTP clock rate.
                 */
                RtpDataDepacketizerThread(RtpDataDepacketizerContext ctx,
                                          const String &name,
                                          uint32_t clockRateHz);

                ~RtpDataDepacketizerThread() override;

                RtpDataDepacketizerThread(const RtpDataDepacketizerThread &) = delete;
                RtpDataDepacketizerThread &operator=(const RtpDataDepacketizerThread &) = delete;

                /// @brief Direct entry point for unit tests — runs
                ///        @ref handlePacket once on the test thread.
                void handlePacketForTest(const RtpPacket &pkt) { handlePacket(pkt); }

                /// @brief Forces an emit of any in-flight reassembly.
                ///        Intended for tests; production code
                ///        relies on the marker bit / timestamp
                ///        change to trigger emission.
                void flushForTest() { emitMessage(); }

        protected:
                void handlePacket(const RtpPacket &pkt) override;
                void onStop() override;

        private:
                void emitMessage();

                RtpDataDepacketizerContext _ctx;
                RtpPacket::List            _reasmPackets;
                uint32_t                   _reasmTimestamp = 0;
                bool                       _reasmHasTimestamp = false;
                uint32_t                   _lastEpoch = 0;
};

PROMEKI_NAMESPACE_END
