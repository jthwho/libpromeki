/**
 * @file      mediaiostats.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiostats.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/units.h>
#include <cstdint>

PROMEKI_NAMESPACE_BEGIN

String MediaIOStats::toString() const {
        // Compact single-line renderer for the standard telemetry
        // keys.  Centralised here so every call site that wants a
        // human-readable snapshot — log lines, --stats overlays,
        // periodic telemetry dumps — formats the same way.
        //
        // Key ordering is fixed so periodic output stays scannable
        // in a terminal.  Cheap counters that are still zero are
        // elided so the line stays quiet under normal operation —
        // the interesting thing a reader wants to notice is when
        // something shows up that wasn't there before.
        StringList parts;

        if (contains(BytesPerSecond)) {
                parts.pushToBack(Units::fromBytesPerSec(getAs<double>(BytesPerSecond)));
        }
        if (contains(FramesPerSecond)) {
                parts.pushToBack(String::format("{:.1f} fps", getAs<double>(FramesPerSecond)));
        }
        if (contains(FramesDropped)) {
                parts.pushToBack(String::format("drop={}", getAs<int64_t>(FramesDropped)));
        }
        if (contains(FramesRepeated)) {
                int64_t v = getAs<int64_t>(FramesRepeated);
                if (v > 0) parts.pushToBack(String::format("rep={}", v));
        }
        if (contains(FramesLate)) {
                int64_t v = getAs<int64_t>(FramesLate);
                if (v > 0) parts.pushToBack(String::format("late={}", v));
        }
        if (contains(AverageLatencyMs) || contains(PeakLatencyMs)) {
                parts.pushToBack(String::format("lat={:.2f}/{:.2f} ms", getAs<double>(AverageLatencyMs),
                                                getAs<double>(PeakLatencyMs)));
        }
        if (contains(AverageProcessingMs) || contains(PeakProcessingMs)) {
                parts.pushToBack(String::format("proc={:.2f}/{:.2f} ms", getAs<double>(AverageProcessingMs),
                                                getAs<double>(PeakProcessingMs)));
        }
        if (contains(QueueDepth) || contains(QueueCapacity)) {
                parts.pushToBack(String::format("q={}/{}", getAs<int64_t>(QueueDepth), getAs<int64_t>(QueueCapacity)));
        }
        if (contains(PendingOperations)) {
                parts.pushToBack(String::format("pend={}", getAs<int64_t>(PendingOperations)));
        }
        if (contains(LastErrorMessage)) {
                String msg = getAs<String>(LastErrorMessage);
                if (!msg.isEmpty()) {
                        parts.pushToBack(String::format("err={}", msg));
                }
        }

        return parts.join(String("  "));
}

PROMEKI_NAMESPACE_END
