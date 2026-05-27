/**
 * @file      st2110vrxmonitor.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/st2110vrxmonitor.h>

PROMEKI_NAMESPACE_BEGIN

void St2110VrxMonitor::configure(uint64_t drainRateBytesPerSec, int64_t vrxFullBytes,
                                 int cmaxPackets, const Duration &burstWindow) {
        _drainRateBytesPerSec = drainRateBytesPerSec;
        _vrxFullBytes = vrxFullBytes;
        _cmaxPackets = cmaxPackets;
        _burstWindowNs = burstWindow.isValid() ? burstWindow.nanoseconds() : 5'000;
        reset();
}

void St2110VrxMonitor::reset() {
        _occupancyBytes = 0;
        _peakOccupancyBytes = 0;
        _currentBurstPackets = 0;
        _peakBurstPackets = 0;
        _vrxViolations = 0;
        _cmaxViolations = 0;
        _observedPackets = 0;
        _observedBytes = 0;
        _lastTimestampNs = -1;
}

void St2110VrxMonitor::observePacket(int64_t timestamp, int sizeBytes) {
        if (_drainRateBytesPerSec == 0 || sizeBytes <= 0) return;

        // Drain advance — bytes pulled out of the bucket since the
        // last observation.  Negative @c dt clamps to zero so a
        // non-monotone caller (e.g. one stamping wallclock instead
        // of steady) can't double-count.
        if (_lastTimestampNs >= 0 && timestamp > _lastTimestampNs) {
                const int64_t dtNs = timestamp - _lastTimestampNs;
                // Compute drained bytes as
                //   drainRate × dt / 1e9
                // using a wide intermediate so a 10 Gbps stream
                // (1.25 GB/s) × 1 ms (1e6 ns) = 1.25e15 / 1e9 = 1.25e6
                // fits.  Cast to int64 explicitly to avoid implicit
                // unsigned wrap on the @c drainRate term.
                const int64_t drainedBytes = static_cast<int64_t>(
                        (static_cast<__int128>(_drainRateBytesPerSec) * dtNs) /
                        1'000'000'000LL);
                _occupancyBytes -= drainedBytes;
                if (_occupancyBytes < 0) _occupancyBytes = 0;
        }
        _occupancyBytes += sizeBytes;
        if (_occupancyBytes > _peakOccupancyBytes) {
                _peakOccupancyBytes = _occupancyBytes;
        }
        if (_vrxFullBytes > 0 && _occupancyBytes > _vrxFullBytes) {
                ++_vrxViolations;
        }

        // Burst tracking — a packet within @c burstWindowNs of the
        // previous one extends the current burst; otherwise the
        // burst counter resets to 1 (this packet alone).
        if (_lastTimestampNs >= 0 &&
            (timestamp - _lastTimestampNs) <= _burstWindowNs) {
                ++_currentBurstPackets;
        } else {
                _currentBurstPackets = 1;
        }
        if (_currentBurstPackets > _peakBurstPackets) {
                _peakBurstPackets = _currentBurstPackets;
        }
        if (_cmaxPackets > 0 && _currentBurstPackets > _cmaxPackets) {
                ++_cmaxViolations;
        }

        _lastTimestampNs = timestamp;
        ++_observedPackets;
        _observedBytes += sizeBytes;
}

PROMEKI_NAMESPACE_END
