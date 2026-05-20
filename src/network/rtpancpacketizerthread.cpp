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
#include <promeki/rtppayloadanc.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

RtpAncPacketizerThread::RtpAncPacketizerThread(
        RtpAncPacketizerContext ctx, const String &name, size_t depth)
    : RtpPacketizerThread(name, depth), _ctx(std::move(ctx)) {}

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

        Error pushErr = _ctx.txPacketQueue->pushBlocking(std::move(batch));
        if (pushErr.isError() && pushErr != Error::Cancelled) {
                promekiWarn("RtpAncPacketizerThread: TX queue pushBlocking failed: %s",
                            pushErr.desc().cstr());
        }
}

PROMEKI_NAMESPACE_END
