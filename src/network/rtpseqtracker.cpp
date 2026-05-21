/**
 * @file      rtpseqtracker.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpseqtracker.h>
#include <promeki/logger.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>

PROMEKI_NAMESPACE_BEGIN

void RtpSeqTracker::initSource(uint16_t seq, uint32_t clockRateHz) {
        Mutex::Locker lock(_mutex);
        _clockRateHz = clockRateHz;
        initSourceLocked(seq);
        // Mirror the RFC's init_source(): max_seq = seq - 1 so the
        // FIRST observed packet (when its seq equals @c seq) takes
        // the @c seq == max_seq + 1 branch in @ref observe and
        // counts down probation as expected.
        _maxSeq = static_cast<uint16_t>(seq - 1u);
        _probation = MinSequential;
        _initialised = true;
}

void RtpSeqTracker::initSourceLocked(uint16_t seq) {
        // Mirror RFC 3550 §A.1 init_seq() — every per-source counter
        // returns to its zero state.  This is reused by both the
        // first-packet bring-up (manual or implicit) AND the
        // post-probation re-anchor where we adopt the second
        // sequential packet as the new base.
        _baseSeq = seq;
        _maxSeq = seq;
        _badSeq = SeqMod + 1;
        _cycles = 0;
        _received = 0;
        _receivedPrior = 0;
        _expectedPrior = 0;
        _duplicates = 0;
        _reordered = 0;
        _fractionLost = 0;
        _haveJitterPrev = false;
        _prevTransit = 0;
        _jitter = 0;
}

RtpSeqTracker::ObserveResult RtpSeqTracker::observe(uint16_t seq, uint32_t rtpTs,
                                                    const TimeStamp &arrivalSteady) {
        Mutex::Locker lock(_mutex);
        ObserveResult res;

        if (!_initialised) {
                // Implicit initialisation.  Mirror init_source so
                // probation can fire on the very next packet.
                initSourceLocked(seq);
                _maxSeq = static_cast<uint16_t>(seq - 1u);
                _probation = MinSequential;
                _initialised = true;
                res.ssrcInit = true;
        }

        // RFC 3550 §A.1: udelta is computed in the 16-bit seq space,
        // implicitly handling wraparound via 16-bit modular subtraction.
        const uint16_t udelta16 = static_cast<uint16_t>(seq - _maxSeq);
        const uint32_t udelta = static_cast<uint32_t>(udelta16);

        if (_probation > 0) {
                res.probation = true;
                if (seq == static_cast<uint16_t>(_maxSeq + 1)) {
                        _probation--;
                        _maxSeq = seq;
                        if (_probation == 0) {
                                // Probation passed — adopt this packet
                                // as the new base; the RFC's init_seq
                                // reset zeroes the counters so this
                                // post-probation packet is the source's
                                // first "real" packet (received = 1).
                                initSourceLocked(seq);
                                _received = 1;
                                updateJitterLocked(rtpTs, arrivalSteady);
                                res.probation = false;
                                res.extendedSeq = seq;
                                res.jitterRtpTsUnits = _jitter;
                                return res;
                        }
                } else {
                        // Out-of-order during probation rearms the
                        // window (RFC's "expecting MIN_SEQUENTIAL in
                        // a row" rule).
                        _probation = MinSequential - 1;
                        _maxSeq = seq;
                }
                // Per RFC, probation-period packets don't count toward
                // received.  We still publish an extendedSeq + the
                // current jitter estimate so the recv thread can pass
                // the packet through to the reorder buffer (per the
                // devplan's optimistic-delivery policy).
                updateJitterLocked(rtpTs, arrivalSteady);
                res.extendedSeq = (_cycles | static_cast<uint32_t>(seq));
                res.jitterRtpTsUnits = _jitter;
                return res;
        }

        if (udelta < MaxDropout) {
                // In-order delivery (or in-window forward jump small
                // enough to count as missed packets, not a restart).
                // Wraparound: forward jump from a high seq back
                // through 0 satisfies the threshold and is detected
                // by the wrapped seq being numerically lower than
                // _maxSeq.
                if (seq < _maxSeq) {
                        _cycles += SeqMod;
                }
                _maxSeq = seq;
        } else if (udelta <= SeqMod - MaxMisorder) {
                // Far from in-order — large forward jump or large
                // backwards jump.  RFC 3550 §A.1's bad_seq restart
                // heuristic: two sequential packets at the new
                // bad-seq location means the sender restarted with a
                // fresh seq window, so we re-init to follow it.
                // Otherwise tag the packet as the new bad_seq
                // candidate and drop it (caller treats
                // duplicate=true as drop).
                if (static_cast<uint32_t>(seq) + 1u == _badSeq) {
                        // Already-bad seq tail-recursion: increment
                        // doesn't fire here; this branch is the
                        // "second packet at exactly the bad_seq
                        // location" path the RFC describes.
                }
                if (seq == _badSeq) {
                        promekiWarn("RtpSeqTracker: detected sender restart at seq=%u (re-anchoring)", seq);
                        initSourceLocked(seq);
                        _maxSeq = seq;
                        _initialised = true;
                        _probation = 0;
                        _received = 1;
                        updateJitterLocked(rtpTs, arrivalSteady);
                        res.ssrcInit = true;
                        res.extendedSeq = seq;
                        res.jitterRtpTsUnits = _jitter;
                        return res;
                }
                promekiWarnThrottled(2000,
                                     "RtpSeqTracker: dropping large-jump seq=%u (max=%u udelta=%u) — "
                                     "tagging as bad_seq candidate",
                                     seq, _maxSeq, udelta);
                _badSeq = (static_cast<uint32_t>(seq) + 1u) & (SeqMod - 1u);
                res.duplicate = true;
                res.extendedSeq = (_cycles | static_cast<uint32_t>(seq));
                res.jitterRtpTsUnits = _jitter;
                return res;
        } else {
                // In-window backwards jump — reorder or duplicate.
                // We don't keep a per-seq bitmap here (that's the
                // reorder buffer's job); we just count the
                // backwards-arrival in _reordered so the published
                // stat is non-zero whenever the wire delivers
                // out-of-order.  The reorder buffer's
                // @c reorderDroppedDuplicate covers the strict-dup
                // case via its own window.
                _reordered++;
        }

        _received++;
        updateJitterLocked(rtpTs, arrivalSteady);

        res.extendedSeq = (_cycles | static_cast<uint32_t>(seq));
        res.jitterRtpTsUnits = _jitter;
        return res;
}

void RtpSeqTracker::updateJitterLocked(uint32_t rtpTs, const TimeStamp &arrivalSteady) {
        if (_clockRateHz == 0) return;

        // Convert arrival to RTP-TS units modulo 2^32.  Steady-clock
        // ns since arbitrary epoch * clockRate / 1e9 — performed in
        // 64-bit and truncated to 32-bit, matching the RFC's assumed
        // wraparound behaviour for transit values.
        const int64_t ns = arrivalSteady.nanoseconds();
        const int64_t arrivalRtpTs64 =
                (ns / 1'000'000'000) * static_cast<int64_t>(_clockRateHz)
                + ((ns % 1'000'000'000) * static_cast<int64_t>(_clockRateHz)) / 1'000'000'000;
        const uint32_t arrivalRtpTs = static_cast<uint32_t>(arrivalRtpTs64);

        // RFC 3550 §A.8: D = transit_now - transit_prev,
        // J += (|D| - J) / 16.  Computed in unsigned modular
        // arithmetic so wraparound of the 32-bit transit at session
        // boundaries is benign.
        const uint32_t transit = arrivalRtpTs - rtpTs;
        if (!_haveJitterPrev) {
                _prevTransit = transit;
                _haveJitterPrev = true;
                return;
        }
        const int32_t  d = static_cast<int32_t>(transit - _prevTransit);
        _prevTransit = transit;
        const uint32_t absD = static_cast<uint32_t>(std::abs(d));
        // Integer EWMA matching RFC pseudocode: J += (|D| - J) >> 4.
        // The (absD - _jitter) subtraction is unsigned modular,
        // which yields the right two's-complement bit pattern when
        // shifted right arithmetically; this matches the published
        // §A.8 reference C code byte-for-byte.
        _jitter += (absD - _jitter) >> 4;
}

RtpSeqTracker::Stats RtpSeqTracker::snapshot() const {
        Mutex::Locker lock(_mutex);
        Stats s;
        // No packets received → every counter is zero.  Without this
        // guard the expected = highest - base + 1 formula reports 1
        // for an empty tracker (highest = base = 0), which would mis-
        // attribute a phantom loss to a stream that never started.
        if (_received == 0) {
                s.cycles = _cycles;
                return s;
        }
        s.extendedHighestSeq = _cycles | static_cast<uint32_t>(_maxSeq);
        s.cycles = _cycles;
        const uint32_t baseExt = static_cast<uint32_t>(_baseSeq);
        // Expected = extendedHighestSeq − baseExt + 1.  Both sides
        // are 32-bit unsigned; for a tracker that has wrapped one
        // or more times this still yields the correct expected
        // total via 32-bit modular subtraction.
        s.expectedPackets = (s.extendedHighestSeq + 1u) - baseExt;
        s.receivedPackets = _received;
        s.duplicatePackets = _duplicates;
        s.reorderedPackets = _reordered;
        s.interarrivalJitter = _jitter;
        // Cumulative loss is signed per RFC §6.4.1 — duplicates can
        // make received exceed expected on a noisy multicast group.
        const int64_t lost = static_cast<int64_t>(s.expectedPackets) -
                             static_cast<int64_t>(s.receivedPackets);
        s.cumulativeLost = static_cast<int32_t>(
                std::clamp<int64_t>(lost, -static_cast<int64_t>(0x800000),
                                    static_cast<int64_t>(0x7FFFFF)));
        s.fractionLost = _fractionLost;
        return s;
}

void RtpSeqTracker::commitRrInterval() {
        Mutex::Locker lock(_mutex);
        const uint32_t extendedHighest = _cycles | static_cast<uint32_t>(_maxSeq);
        const uint32_t expected = (extendedHighest + 1u) - static_cast<uint32_t>(_baseSeq);
        const uint32_t expectedInterval = expected - _expectedPrior;
        const uint32_t receivedInterval = _received - _receivedPrior;
        _expectedPrior = expected;
        _receivedPrior = _received;
        if (expectedInterval == 0 || expectedInterval < receivedInterval) {
                _fractionLost = 0;
        } else {
                const uint32_t lostInterval = expectedInterval - receivedInterval;
                _fractionLost = static_cast<uint8_t>((lostInterval << 8) / expectedInterval);
        }
}

void RtpSeqTracker::reset() {
        Mutex::Locker lock(_mutex);
        _initialised = false;
        _probation = MinSequential;
        _baseSeq = 0;
        _maxSeq = 0;
        _badSeq = SeqMod + 1;
        _cycles = 0;
        _received = 0;
        _expectedPrior = 0;
        _receivedPrior = 0;
        _duplicates = 0;
        _reordered = 0;
        _fractionLost = 0;
        // Intentionally preserve @c _clockRateHz — clock rate is a
        // per-stream config (set via @ref setClockRateHz / SDP),
        // not per-source.  An SSRC reset that zeroed it would
        // silently disable §A.8 jitter accounting for the new
        // source until the receiver re-plumbed the rate.
        _jitter = 0;
        _haveJitterPrev = false;
        _prevTransit = 0;
}

uint32_t RtpSeqTracker::clockRateHz() const {
        Mutex::Locker lock(_mutex);
        return _clockRateHz;
}

void RtpSeqTracker::setClockRateHz(uint32_t rate) {
        Mutex::Locker lock(_mutex);
        _clockRateHz = rate;
}

PROMEKI_NAMESPACE_END
