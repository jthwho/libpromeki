/**
 * @file      rtpvideodepacketizerthread.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <functional>
#include <promeki/function.h>
#include <promeki/atomic.h>
#include <promeki/clockdomain.h>
#include <promeki/eui64.h>
#include <promeki/framenumber.h>
#include <promeki/histogram.h>
#include <promeki/imagedesc.h>
#include <promeki/jpeggeometryprobe.h>
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
 * @brief Static dependencies handed to @ref RtpVideoDepacketizerThread
 *        at construction time.
 * @ingroup network
 *
 * Mirrors @ref RtpAudioDepacketizerContext but for the per-stream
 * video reader path.  The video depacketizer accumulates packets
 * across marker-bit boundaries, runs @c payload->unpack on the
 * reassembled bytes, validates the result via the codec's
 * @c validate gate, runs JPEG geometry discovery for RFC 2435
 * streams, builds an @c RxVideoFrame with a fresh payload, and
 * pushes it onto the per-stream output queue.
 *
 * @par Lifetime
 * Every pointer must outlive the depacketizer thread.
 */
struct RtpVideoDepacketizerContext {
                /// @brief Output queue the depacketizer pushes
                ///        @ref RxVideoFrame bundles onto.  Must
                ///        be set.
                Queue<RxVideoFrame> *payloadQueue = nullptr;

                /// @brief Per-stream RFC 3550 SSRC reset epoch.
                const Atomic<uint32_t> *resetEpoch = nullptr;

                /// @brief @c true once the surrounding
                ///        @c RtpMediaIO has activated this stream.
                ///        @c nullptr is treated as "always active".
                const bool *active = nullptr;

                /// @brief The @ref RtpPayload that owns @c unpack
                ///        and @c validate.  Must be set.  The
                ///        depacketizer also calls
                ///        @c clearParamSets on this payload as
                ///        part of the SSRC-reset cascade so codec
                ///        gates re-arm.
                RtpPayload *payload = nullptr;

                /// @brief Mutable per-stream image descriptor.
                ///        For RFC 2435 JPEG streams the
                ///        depacketizer writes the discovered
                ///        geometry here on the first frame and on
                ///        any detected geometry change.  For every
                ///        other codec the descriptor is set by
                ///        the SDP-driven configure path and used
                ///        as-is.  Must be non-null.
                ImageDesc *readerImageDesc = nullptr;

                /// @brief SDP @c a=fmtp value for this stream.
                ///        Passed into the JPEG geometry probe as a
                ///        hint for colorimetry / range.  May be
                ///        empty.
                String fmtp;

                /// @brief PTP grandmaster ID extracted from the
                ///        SDP's @c ts-refclk line.  Stamped into
                ///        the emitted Frame's metadata.  May be
                ///        the null EUI64.
                EUI64 ptpGrandmaster;

                /// @brief SDP-derived clock domain.  Stamped onto
                ///        every @c MediaTimeStamp the depacketizer
                ///        produces.
                ClockDomain clockDomain;

                /// @brief @c true once the first SR has seeded
                ///        @ref streamClock.
                const bool *hasSr = nullptr;

                /// @brief Per-stream RTP <-> NTP clock mapping.
                const RtpStreamClock *streamClock = nullptr;

                /// @brief Stat counters — non-owning pointers.
                ///        Each may be @c nullptr in tests.
                Atomic<int64_t> *packetsReceived = nullptr;
                Atomic<int64_t> *bytesReceived = nullptr;
                Atomic<int64_t> *lastPacketArrivalNs = nullptr;
                Atomic<int64_t> *framesReassembled = nullptr;
                Atomic<int64_t> *framesDroppedValidate = nullptr;
                Atomic<int64_t> *framesWaitingParamSets = nullptr;
                Atomic<int64_t> *framesDroppedSsrcReset = nullptr;

                /// @brief Diagnostic histograms — non-owning
                ///        pointers.  May be @c nullptr in tests.
                Histogram *rxPacketInterval = nullptr;
                Histogram *rxFrameInterval = nullptr;
                Histogram *rxFrameAssembleTime = nullptr;

                /// @brief Bumps the @c FrameCount on the owning
                ///        ReaderStream.  May be @c nullptr.
                Function<void()> noteFrameReceived;

                /// @brief Refreshes @ref streamClock from any
                ///        newly-arrived SR.
                Function<void()> refreshStreamClock;

                /// @brief Converts an NTP timestamp to local
                ///        steady time using the owning
                ///        @c RtpMediaIO 's anchor.
                Function<TimeStamp(const NtpTime &)> ntpToSteady;
};

/**
 * @brief Per-stream depacketizer for RFC 2435 JPEG / RFC 4175 raw
 *        / RFC 6184 H.264 / RFC 7798 HEVC video.
 * @ingroup network
 *
 * One instance per active video reader stream.  Pops post-reorder
 * @ref RtpPacket bundles, accumulates them across marker-bit
 * boundaries (timestamp-change flush + marker flush), runs
 * @c payload->unpack, validates the access unit via
 * @c payload->validate, runs the JPEG geometry probe for
 * RFC 2435 streams, stamps a per-frame @c captureTime, and pushes
 * an @ref RxVideoFrame onto the per-stream payload queue the
 * @ref RtpAggregatorThread drains.
 *
 * @par Threading
 * Runs on its own worker thread.  Reassembly state — including
 * the @ref JpegGeometryProbe cache and per-frame index — lives on
 * the worker; only the queue pushes and counter bumps cross
 * threads.
 */
class RtpVideoDepacketizerThread : public RtpDepacketizerThread {
        public:
                /**
                 * @brief Constructs an unstarted video depacketizer.
                 *
                 * @param ctx         Static dependencies.
                 * @param name        Short OS thread name.
                 * @param clockRateHz Per-stream RTP clock rate.
                 * @param queueDepth  Post-reorder queue depth.  Sized
                 *                    by the caller from the configured
                 *                    stream's packets-per-frame budget
                 *                    (see
                 *                    @ref RtpMediaIO::computeStreamPacketBudget);
                 *                    defaults to the small base-class
                 *                    default for callers that don't
                 *                    have format info handy.
                 */
                RtpVideoDepacketizerThread(RtpVideoDepacketizerContext ctx,
                                           const String &name,
                                           uint32_t clockRateHz,
                                           size_t queueDepth = DefaultInputQueueDepth);

                ~RtpVideoDepacketizerThread() override;

                RtpVideoDepacketizerThread(const RtpVideoDepacketizerThread &) = delete;
                RtpVideoDepacketizerThread &operator=(const RtpVideoDepacketizerThread &) = delete;

                /// @brief Direct entry point for unit tests — runs
                ///        @ref handlePacket once on the test thread.
                void handlePacketForTest(const RtpPacket &pkt) { handlePacket(pkt); }

                /// @brief Forces an emit of any in-flight reassembly.
                void flushForTest() { emitFrame(); }

        protected:
                void handlePacket(const RtpPacket &pkt) override;
                void onStop() override;

        private:
                void emitFrame();

                RtpVideoDepacketizerContext _ctx;
                RtpPacket::List             _reasmPackets;
                uint32_t                    _reasmTimestamp = 0;
                bool                        _reasmHasTimestamp = false;
                TimeStamp                   _frameStartTime;
                bool                        _hasFrameStart = false;
                TimeStamp                   _lastPacketTime;
                bool                        _hasLastPacket = false;
                TimeStamp                   _lastFrameTime;
                bool                        _hasLastFrame = false;
                FrameNumber                 _streamFrameIndex{0};
                JpegGeometryProbe           _jpegProbe;
                uint32_t                    _lastEpoch = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
