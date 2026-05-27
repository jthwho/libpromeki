/**
 * @file      txtimepacketscheduler.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/txtimepacketscheduler.h>

#include <promeki/logger.h>
#include <promeki/timestamp.h>

#if defined(PROMEKI_PLATFORM_LINUX)
#include <time.h>
#ifndef CLOCK_TAI
#define CLOCK_TAI 11
#endif
#endif

PROMEKI_NAMESPACE_BEGIN

Error TxTimePacketScheduler::configure(const Spec &spec) {
        Error err = PacketScheduler::configure(spec);
        if (err.isError()) return err;

        _streamAnchored = false;
        _streamCadence = Cadence(spec.frameInterval);

        // Enable per-datagram SCM_TXTIME on the transport.  The
        // setsockopt is gated on Linux's ETF qdisc + a NIC that
        // supports launch-time; on platforms that lack support, the
        // transport returns NotSupported and we degrade to burst
        // dispatch (see @ref enqueue).
        _txTimeEnabled = false;
        if (_transport != nullptr) {
                Error tt = _transport->setTxTime(true);
                if (tt.isError()) {
                        if (tt != Error::NotSupported) return tt;
                        promekiWarnOnce("TxTimePacketScheduler: SO_TXTIME unsupported — degrading to burst");
                } else {
                        _txTimeEnabled = true;
                }
        }

#if defined(PROMEKI_PLATFORM_LINUX)
        // Snapshot the CLOCK_TAI − steady-clock offset.  Refreshing
        // it on every enqueue would defeat the SCM_TXTIME deadline's
        // precision — TAI and steady are both driven by the same HPET
        // / TSC source on Linux, so the offset is constant within a
        // boot.
        struct timespec tsTai = {};
        struct timespec tsSteady = {};
        if (clock_gettime(CLOCK_TAI, &tsTai) == 0 &&
            clock_gettime(CLOCK_MONOTONIC, &tsSteady) == 0) {
                int64_t taiNs = static_cast<int64_t>(tsTai.tv_sec) * 1'000'000'000LL + tsTai.tv_nsec;
                int64_t steadyNs = static_cast<int64_t>(tsSteady.tv_sec) * 1'000'000'000LL + tsSteady.tv_nsec;
                _taiOffsetNs = taiNs - steadyNs;
        } else {
                _taiOffsetNs = 0;
        }
#else
        _taiOffsetNs = 0;
#endif
        return Error::Ok;
}

uint64_t TxTimePacketScheduler::taiNanosNow() const {
        const TimeStamp now = TimeStamp::now();
        return static_cast<uint64_t>(now.nanoseconds() + _taiOffsetNs);
}

int TxTimePacketScheduler::enqueue(const PacketTransport::DatagramList &datagrams) {
        if (_transport == nullptr) return -1;
        if (datagrams.isEmpty()) return 0;

        // No TXTIME support → behave like burst, ignoring deadlines.
        if (!_txTimeEnabled) {
                return _transport->sendPackets(datagrams);
        }

        // Caller-supplied deadlines (e.g. ST 2110-40 §6.4 LLTM ANC via
        // @ref RtpPacketBatch::deadlineTaiNs) arrive with @c txTimeNs
        // already populated; pass straight through so the kernel's ETF
        // qdisc honours the caller's deadline instead of the
        // scheduler's per-batch cadence derivation.  The check looks
        // at the first datagram only because the @ref RtpSession
        // batch-stamps all datagrams in lockstep — partial pre-stamps
        // (some zero, some not) are a programming error.
        if (!datagrams.isEmpty() && datagrams[0].txTimeNs != 0) {
                return _transport->sendPackets(datagrams);
        }

        const size_t   n = datagrams.size();
        const Duration interval = _spec.frameInterval;
        if (!interval.isValid()) {
                // Without an interval there is no deadline math to
                // do — pass through.
                return _transport->sendPackets(datagrams);
        }

        // Build a per-packet copy with TXTIME stamped.  The kernel
        // reads txTimeNs into the SCM_TXTIME cmsg per
        // @ref UdpSocket::writeDatagrams.
        PacketTransport::DatagramList stamped;
        stamped.reserve(n);

        if (cadenceMode() == CadenceMode::Streamwide) {
                if (!_streamAnchored) {
                        _streamCadence = Cadence(interval);
                        _streamCadence.anchor(TimeStamp::now());
                        _streamAnchored = true;
                }
                const Duration stallThreshold = interval * _stallMultiplier;
                for (size_t i = 0; i < n; i++) {
                        TimeStamp       deadline = _streamCadence.next();
                        const TimeStamp now = TimeStamp::now();
                        if (now - deadline > stallThreshold) {
                                _streamCadence.reanchor(now);
                                deadline = _streamCadence.next();
                        }
                        PacketTransport::Datagram d = datagrams[i];
                        d.txTimeNs = static_cast<uint64_t>(deadline.nanoseconds() + _taiOffsetNs);
                        stamped.pushToBack(d);
                }
        } else {
                // PerBatch: anchor at now, stride by interval / n.
                const TimeStamp base = TimeStamp::now();
                const int64_t   baseNs = base.nanoseconds() + _taiOffsetNs;
                const int64_t   strideNs =
                        n > 1 ? interval.nanoseconds() / static_cast<int64_t>(n) : interval.nanoseconds();
                for (size_t i = 0; i < n; i++) {
                        PacketTransport::Datagram d = datagrams[i];
                        d.txTimeNs = static_cast<uint64_t>(baseNs + strideNs * static_cast<int64_t>(i));
                        stamped.pushToBack(d);
                }
        }

        return _transport->sendPackets(stamped);
}

int TxTimePacketScheduler::predictedTxDelayUs() const {
        const Duration interval = _spec.frameInterval;
        if (!interval.isValid()) return 0;
        if (cadenceMode() == CadenceMode::Streamwide) return 0;
        if (_spec.packetsPerFrame <= 1) {
                return static_cast<int>(interval.microseconds());
        }
        const int64_t perPacketUs = interval.microseconds() / _spec.packetsPerFrame;
        return static_cast<int>(perPacketUs * (_spec.packetsPerFrame - 1));
}

PROMEKI_NAMESPACE_END
