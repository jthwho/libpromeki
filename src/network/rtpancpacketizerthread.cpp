/**
 * @file      rtpancpacketizerthread.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpancpacketizerthread.h>

#include <utility>

#include <promeki/ancpayload.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/logger.h>
#include <promeki/phcclock.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

RtpAncPacketizerThread::RtpAncPacketizerThread(
        RtpAncPacketizerContext ctx, const String &name, size_t depth)
    : RtpPacketizerThread(name, depth), _ctx(std::move(ctx)) {
        // ST 2110-40 §5.5 keep-alive F-bit: stamp from the per-stream
        // context so an interlaced ANC session emits Field1/Field2 on
        // its empty-frame keep-alives.  Default is Progressive.
        if (_ctx.payload != nullptr) {
                _ctx.payload->setKeepAliveField(_ctx.keepAliveField);
        }
}

RtpAncPacketizerThread::~RtpAncPacketizerThread() {
        requestStop();
        if (!isCurrentThread()) wait();
}

void RtpAncPacketizerThread::packetize(const RtpFrameWork &work) {
        if (_ctx.txPacketQueue == nullptr || _ctx.payload == nullptr) return;

        // Locate this stream's ANC essence inside the Frame.  The
        // strand pushes the same Frame to every ANC packetizer; each
        // packetizer pulls only the payload at its own stream index.
        auto ancList = work.frame.ancPayloads();
        AncPacket::List packets;
        if (_ctx.streamIdx < ancList.size()) {
                AncPayload::Ptr ap = ancList[_ctx.streamIdx];
                if (ap.isValid()) {
                        packets = ap->packets();
                }
        }

        // RTP TS is provisional — the TX thread re-stamps via
        // FrameRate::cumulativeTicks(clockRate, frameIndex) before
        // emit, matching the video pattern.  We pass 0 here so the
        // wire bytes carry a deterministic placeholder.
        //
        // Note: packAncFrame emits an ST 2110-40 §5.5 keep-alive
        // (ANC_Count=0, Marker=1) when @c packets is empty or carries
        // no St291 entries — we no longer early-exit here.
        RtpPacket::List rtpPackets = _ctx.payload->packAncFrame(packets, 0);
        if (rtpPackets.isEmpty()) {
                // Only reaches here on a hard pack failure (allocation
                // failed, payload size impossibly small).
                return;
        }

        RtpPacketBatch batch;
        batch.packets = std::move(rtpPackets);
        batch.frameIndex = work.frameIndex;
        batch.clockRate = _ctx.clockRateHz;
        batch.markerOnLast = true;
        batch.enqueuedAt = TimeStamp::now();

        // ST 2110-40 §6.4 LLTM deadline: sender shall transmit every
        // RTP packet for frame @c N no later than
        // @c T_FST + T_EPO + T_D, where @c T_FST is the SMPTE Epoch-
        // grid wallclock of frame @c N's first sample (UTC via the
        // PTP-anchored @ref RtpMediaClock), @c T_EPO is the per-stream
        // Epoch Offset (TR_OFFSET-equivalent), and @c T_D = 8 /
        // (FrameRate × TotalLines) is the egress slack window.
        //
        // When @c lltmEnabled is set and the media clock has a valid
        // PTP anchor we stamp the resulting CLOCK_TAI nanosecond
        // deadline onto @c batch.deadlineTaiNs; the session forwards
        // it to every datagram and the @ref TxTimePacketScheduler
        // passes the pre-stamped value through to the kernel's ETF
        // qdisc.  No PTP anchor → silently degrade to Compatible
        // (CTM) pacing (no per-batch deadline) so the SDP-advertised
        // TM=LLTM is best-effort honoured on hosts without a PHC.
        if (_ctx.lltmEnabled && _ctx.mediaClock != nullptr &&
            _ctx.mediaClock->hasPtpAnchor()) {
                const int64_t tFstUtcNs =
                        _ctx.mediaClock->tvdUtcNs(work.frameIndex.value());
                if (tFstUtcNs > 0) {
                        const int64_t tEpoNs = _ctx.trOffset.isValid()
                                                       ? _ctx.trOffset.nanoseconds()
                                                       : 0;
                        const int64_t tDNs = _ctx.tD.isValid()
                                                     ? _ctx.tD.nanoseconds()
                                                     : 0;
                        const int64_t deadlineUtcNs = tFstUtcNs + tEpoNs + tDNs;
                        batch.deadlineTaiNs = PhcClock::utcNsToTaiNs(deadlineUtcNs);
                }
        }

        Error pushErr = _ctx.txPacketQueue->pushBlocking(std::move(batch));
        if (pushErr.isError() && pushErr != Error::Cancelled) {
                promekiWarn("RtpAncPacketizerThread: TX queue pushBlocking failed: %s",
                            pushErr.desc().cstr());
        }
}

PROMEKI_NAMESPACE_END
