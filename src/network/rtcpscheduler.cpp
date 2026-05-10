/**
 * @file      rtcpscheduler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtcpscheduler.h>

#include <chrono>
#include <utility>

#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

RtcpScheduler::RtcpScheduler(RtcpSchedulerContext ctx, const String &name)
    : _ctx(std::move(ctx)),
      _intervalMs(_ctx.intervalMs <= 0 ? kDefaultIntervalMs : _ctx.intervalMs) {
        _stopRequested.setValue(false);
        Thread::setName(name);
}

RtcpScheduler::~RtcpScheduler() {
        requestStop();
        if (!isCurrentThread()) wait();
}

void RtcpScheduler::requestStop() {
        // Set the flag and wake the cv-sleep — without the wake,
        // requestStop has to wait up to one full RTCP interval
        // (5 s by default) before the scheduler thread re-evaluates
        // the flag.
        _stopRequested.setValue(true);
        Mutex::Locker lock(_mutex);
        _cv.wakeAll();
}

void RtcpScheduler::run() {
        const unsigned int intervalMs = static_cast<unsigned int>(_intervalMs);

        // Phase 1: tight startup poll so the FIRST SR / RR for each
        // stream goes out as soon as the stream has produced data.
        // Receivers can't compute lip-sync until they have an SR
        // for both streams — without phase 1 the very first SR for
        // each stream would wait until intervalMs into the session.
        const auto startupDeadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
        while (!_stopRequested.value() &&
               std::chrono::steady_clock::now() < startupDeadline) {
                emitOnce();
                if (allStreamsHaveEmitted()) break;
                cvSleep(kStartupPollMs);
        }

        // Phase 2: steady-state cadence.  cvSleep returns early on
        // requestStop, so close doesn't block waiting for an
        // interval-long sleep_until to expire.
        while (!_stopRequested.value()) {
                cvSleep(intervalMs);
                if (_stopRequested.value()) break;
                emitOnce();
        }
}

void RtcpScheduler::runOnce() {
        emitOnce();
}

void RtcpScheduler::emitByeForAll() {
        for (RtcpSchedulerWriterStream &w : _ctx.writers) {
                emitByeForWriter(w);
        }
        for (RtcpSchedulerReaderStream &r : _ctx.readers) {
                emitByeForReader(r);
        }
}

void RtcpScheduler::emitOnce() {
        for (RtcpSchedulerWriterStream &w : _ctx.writers) {
                emitForStream(w);
        }
        // RR has no hasEmission gate — receivers can issue an RR
        // before any packet has arrived, but an RR with all-zero
        // fields against an SSRC we have not observed gives
        // receivers no information.  emitRrForStream skips the emit
        // when the seq tracker has yet to observe a packet.
        for (RtcpSchedulerReaderStream &r : _ctx.readers) {
                emitRrForStream(r);
                checkWireSilence(r);
        }
}

void RtcpScheduler::emitForStream(RtcpSchedulerWriterStream &s) {
        if (!s.active || s.session == nullptr) return;
        // Skip streams that have not emitted any RTP packet yet —
        // an SR with garbage (NTP, RTP) fields gives receivers a
        // worse starting point than waiting for a real one.
        if (!s.session->hasEmissionRecord()) return;
        // packetsSent / senderOctets are mutated by the per-stream
        // TX thread; @c Atomic<int64_t> gives an aligned acquire-
        // load here so the scheduler reads a coherent snapshot
        // rather than a partially-updated half.  When unwired
        // (@c nullptr), fall back to zero.
        const uint32_t pkts =
                s.packetsSent != nullptr
                        ? static_cast<uint32_t>(s.packetsSent->value() & 0xFFFFFFFFu)
                        : 0;
        const uint32_t octs =
                s.senderOctets != nullptr
                        ? static_cast<uint32_t>(s.senderOctets->value() & 0xFFFFFFFFu)
                        : 0;
        Error err = s.session->emitRtcpSr(pkts, octs);
        if (err.isError()) {
                promekiWarn("RtcpScheduler: RTCP SR send failed on %s stream: %s",
                            s.mediaType.cstr(), err.desc().cstr());
        }
}

void RtcpScheduler::emitRrForStream(RtcpSchedulerReaderStream &s) {
        if (!s.active || s.session == nullptr) return;
        if (s.seqTracker == nullptr) return;
        const RtpSeqTracker::Stats stats = s.seqTracker->snapshot();
        // Skip RR before the first packet arrives.  Once any
        // packet has been tracked, @c receivedPackets > 0.
        if (stats.receivedPackets == 0) return;
        RtcpPacket::ReportBlock block;
        // Today each ReaderStream's session pins a single SSRC via
        // SsrcPinState; we surface it via the session's own SSRC
        // as a stand-in.  Proper "report on observed source"
        // requires surfacing the pinned SSRC from the recv thread,
        // tracked as a separate cleanup.
        block.ssrc = s.session->ssrc();
        block.fractionLost = stats.fractionLost;
        block.cumulativeLost = stats.cumulativeLost;
        block.extendedHighestSeq = stats.extendedHighestSeq;
        block.interarrivalJitter = stats.interarrivalJitter;
        // lsr / dlsr are filled by emitRtcpRr from the receivedSr
        // cache — both zero until the first SR arrives.
        Error err = s.session->emitRtcpRr(block);
        if (err.isError()) {
                promekiWarn("RtcpScheduler: RTCP RR send failed on %s reader: %s",
                            s.mediaType.cstr(), err.desc().cstr());
        }
}

void RtcpScheduler::emitByeForWriter(RtcpSchedulerWriterStream &s) {
        if (!s.active || s.session == nullptr) return;
        // Receivers don't need a BYE if they never saw any RTCP
        // from us, but emitting one is harmless and matches RFC
        // 3550 §6.6 intent.
        Error err = s.session->emitRtcpBye();
        if (err.isError()) {
                promekiWarn("RtcpScheduler: RTCP BYE send failed on %s stream: %s",
                            s.mediaType.cstr(), err.desc().cstr());
        }
}

void RtcpScheduler::emitByeForReader(RtcpSchedulerReaderStream &s) {
        if (!s.active || s.session == nullptr) return;
        Error err = s.session->emitRtcpBye();
        if (err.isError()) {
                promekiWarn("RtcpScheduler: RTCP BYE send failed on %s reader: %s",
                            s.mediaType.cstr(), err.desc().cstr());
        }
}

bool RtcpScheduler::allStreamsHaveEmitted() const {
        for (const RtcpSchedulerWriterStream &w : _ctx.writers) {
                if (w.active && w.session != nullptr &&
                    !w.session->hasEmissionRecord()) {
                        return false;
                }
        }
        return true;
}

void RtcpScheduler::checkWireSilence(RtcpSchedulerReaderStream &s) {
        if (!s.active) return;
        if (s.wireSilenceEosSignaled == nullptr) return;
        if (*s.wireSilenceEosSignaled) return;
        if (s.lastPacketArrivalNs == nullptr) return;
        const int64_t lastNs = s.lastPacketArrivalNs->value();
        if (lastNs == 0) return; // no packets seen yet
        // Effective threshold: explicit override if set, else
        // 10 × the RTCP interval.
        int64_t timeoutMs = _ctx.wireSilenceTimeoutMs;
        if (timeoutMs <= 0) timeoutMs = static_cast<int64_t>(_intervalMs) * 10;
        const int64_t nowNs = TimeStamp::now().nanoseconds();
        const int64_t gapNs = nowNs - lastNs;
        if (gapNs < timeoutMs * 1'000'000) return;
        promekiInfo("RtcpScheduler: wire-silence timeout (%lldms) on %s reader — signalling EoS",
                    static_cast<long long>(gapNs / 1'000'000), s.mediaType.cstr());
        *s.wireSilenceEosSignaled = true;
        if (_ctx.onWireSilenceEos) {
                _ctx.onWireSilenceEos(s, gapNs);
        }
}

void RtcpScheduler::cvSleep(unsigned int ms) {
        Mutex::Locker lock(_mutex);
        if (_stopRequested.value()) return;
        (void)_cv.wait(_mutex, [this]() { return _stopRequested.value(); }, ms);
}

PROMEKI_NAMESPACE_END
