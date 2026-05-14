/**
 * @file      rtpseqreorderbuffer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpseqreorderbuffer.h>

PROMEKI_NAMESPACE_BEGIN

RtpSeqReorderBuffer::RtpSeqReorderBuffer(const Config &c) : _config(c) {}

void RtpSeqReorderBuffer::insert(RtpPacket pkt, uint32_t extendedSeq,
                                 const TimeStamp &arrivalSteady, RtpPacket::Queue &out) {
        Mutex::Locker lock(_mutex);

        // First insert anchors the next-expected seq cursor.  Until
        // that's set, any extendedSeq is the next-expected one (this
        // packet); the buffer never holds it for ordering work.
        if (!_haveExpected) {
                _expectedSeq = extendedSeq;
                _haveExpected = true;
        }

        // Duplicate detection: a seq strictly less than _expectedSeq
        // has already been emitted (and the cursor advanced past
        // it); a seq already in @c _buf is an in-window dup.  Both
        // are silently dropped — the recv thread does not log per-
        // dup events, only the cumulative count.
        if (static_cast<int32_t>(extendedSeq - _expectedSeq) < 0) {
                _stats.droppedAsDuplicate++;
                return;
        }
        if (_buf.contains(extendedSeq)) {
                _stats.droppedAsDuplicate++;
                return;
        }

        // Drop-oldest on overflow.  Done BEFORE the deadline check
        // below so the new packet always finds room.  Only one
        // eviction per insert is ever needed because the buffer
        // tracks size deterministically and we drop exactly one
        // when full.
        if (_buf.size() >= _config.maxWindow) {
                auto it = _buf.begin();
                if (it != _buf.end()) {
                        _stats.droppedOnOverflow++;
                        // The dropped packet's seq becomes the new
                        // floor — anything strictly older than it
                        // is a stale dup from this point onward.
                        const uint32_t droppedSeq = it->first;
                        _buf.remove(it);
                        if (static_cast<int32_t>(droppedSeq - _expectedSeq) >= 0) {
                                // Advancing the cursor past the
                                // dropped seq guarantees subsequent
                                // late arrivals of the dropped seq
                                // count as duplicates rather than
                                // re-occupying a window slot.
                                _expectedSeq = droppedSeq + 1;
                        }
                }
        }

        _buf.tryEmplace(extendedSeq, Entry{std::move(pkt), arrivalSteady});
        _stats.inserted++;

        // In-order delivery: if the next-expected seq is now in the
        // buffer, drain the in-order prefix.
        drainInOrderLocked(out);

        // Deadline-driven emission: any buffered head whose
        // (arrival + playoutDelay) is in the past gets emitted as
        // a gap-fill.  Only run when playoutDelay is non-zero —
        // the zero default preserves the legacy LAN immediate-emit
        // behaviour by relying purely on the in-order path.
        if (!_config.playoutDelay.isZero()) {
                const TimeStamp now = TimeStamp::now();
                while (!_buf.isEmpty()) {
                        auto       it = _buf.begin();
                        const auto deadline = it->second.arrival + _config.playoutDelay;
                        if (now.value() < deadline.value()) break;
                        emitHeadLocked(out, /*deadline=*/true);
                        // After deadline-fill the cursor advanced;
                        // re-check the in-order tail too.
                        drainInOrderLocked(out);
                }
        }
}

void RtpSeqReorderBuffer::drainInOrderLocked(RtpPacket::Queue &out) {
        while (!_buf.isEmpty()) {
                auto it = _buf.begin();
                if (it->first != _expectedSeq) break;
                RtpPacket pkt = std::move(it->second.pkt);
                _buf.remove(it);
                _expectedSeq++;
                _stats.emittedInOrder++;
                (void)out.pushDropOldest(std::move(pkt));
        }
}

void RtpSeqReorderBuffer::emitHeadLocked(RtpPacket::Queue &out, bool deadline) {
        if (_buf.isEmpty()) return;
        auto      it = _buf.begin();
        const uint32_t seq = it->first;
        RtpPacket pkt = std::move(it->second.pkt);
        _buf.remove(it);
        // Force-advance the cursor past any gap.  This is the case
        // where the §A reorder window proved too short for the
        // wire's actual jitter, or the gap was a real loss; either
        // way, future arrivals at lower seq become duplicates.
        if (static_cast<int32_t>(seq - _expectedSeq) >= 0) {
                _expectedSeq = seq + 1;
        }
        if (deadline) {
                _stats.emittedOnDeadline++;
        } else {
                _stats.emittedInOrder++;
        }
        (void)out.pushDropOldest(std::move(pkt));
}

void RtpSeqReorderBuffer::flush(RtpPacket::Queue &out) {
        Mutex::Locker lock(_mutex);
        while (!_buf.isEmpty()) {
                emitHeadLocked(out, /*deadline=*/true);
        }
}

void RtpSeqReorderBuffer::clear() {
        Mutex::Locker lock(_mutex);
        _buf.clear();
        _haveExpected = false;
        _expectedSeq = 0;
}

RtpSeqReorderBuffer::Stats RtpSeqReorderBuffer::snapshot() const {
        Mutex::Locker lock(_mutex);
        return _stats;
}

RtpSeqReorderBuffer::Config RtpSeqReorderBuffer::config() const {
        Mutex::Locker lock(_mutex);
        return _config;
}

size_t RtpSeqReorderBuffer::size() const {
        Mutex::Locker lock(_mutex);
        return _buf.size();
}

PROMEKI_NAMESPACE_END
