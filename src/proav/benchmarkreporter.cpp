/**
 * @file      benchmarkreporter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/benchmarkreporter.h>
#include <promeki/units.h>

PROMEKI_NAMESPACE_BEGIN

void BenchmarkReporter::submit(const Benchmark &bm) {
        const auto &entries = bm.entries();
        if (entries.size() < 2) return;

        Mutex::Locker lock(_mutex);
        _submittedCount++;

        for (size_t i = 0; i + 1 < entries.size(); i++) {
                const auto &from = entries[i];
                const auto &to = entries[i + 1];
                auto        diff = to.timestamp.value() - from.timestamp.value();
                double      dt = std::chrono::duration<double>(diff).count();

                StepKey key(from.id.id(), to.id.id());
                if (!_accumulators.contains(key)) {
                        Accumulator acc;
                        acc.fromId = from.id;
                        acc.toId = to.id;
                        acc.count = 1;
                        acc.sum = dt;
                        acc.sumSq = dt * dt;
                        acc.min = dt;
                        acc.max = dt;
                        _accumulators.insert(key, acc);
                } else {
                        Accumulator &acc = _accumulators[key];
                        acc.count++;
                        acc.sum += dt;
                        acc.sumSq += dt * dt;
                        if (dt < acc.min) acc.min = dt;
                        if (dt > acc.max) acc.max = dt;
                }
        }
        return;
}

BenchmarkReporter::StepStats BenchmarkReporter::stepStats(Benchmark::Id fromId, Benchmark::Id toId) const {
        Mutex::Locker lock(_mutex);
        StepKey       key(fromId.id(), toId.id());
        if (!_accumulators.contains(key)) return StepStats{fromId, toId};
        return accumulatorToStats(_accumulators[key]);
}

promeki::List<BenchmarkReporter::StepStats> BenchmarkReporter::allStepStats() const {
        Mutex::Locker            lock(_mutex);
        promeki::List<StepStats> result;
        for (const auto &[key, acc] : _accumulators) {
                result.pushToBack(accumulatorToStats(acc));
        }
        return result;
}

uint64_t BenchmarkReporter::submittedCount() const {
        Mutex::Locker lock(_mutex);
        return _submittedCount;
}

void BenchmarkReporter::reset() {
        Mutex::Locker lock(_mutex);
        _accumulators.clear();
        _submittedCount = 0;
        return;
}

String BenchmarkReporter::summaryReport() const {
        auto stats = allStepStats();
        if (stats.isEmpty()) return String("No benchmark data collected");

        String result;
        result += "Benchmark Report (" + String::number(_submittedCount) + " frames)\n";
        result += String(80, '-') + "\n";
        for (const auto &s : stats) {
                result += s.fromId.name() + " -> " + s.toId.name() + "\n";
                result += "  count: " + String::number(s.count) + "  avg: " + Units::fromDuration(s.avg, 3) +
                          "  min: " + Units::fromDuration(s.min, 3) + "  max: " + Units::fromDuration(s.max, 3) +
                          "  stddev: " + Units::fromDuration(s.stddev, 3) + "\n";
        }
        return result;
}

BenchmarkReporter::StepStats BenchmarkReporter::accumulatorToStats(const Accumulator &acc) const {
        StepStats s;
        s.fromId = acc.fromId;
        s.toId = acc.toId;
        s.count = acc.count;
        s.min = acc.min;
        s.max = acc.max;
        s.total = acc.sum;
        if (acc.count > 0) {
                s.avg = acc.sum / (double)acc.count;
                if (acc.count > 1) {
                        double variance = (acc.sumSq - acc.sum * acc.sum / (double)acc.count) / (double)(acc.count - 1);
                        s.stddev = variance > 0.0 ? std::sqrt(variance) : 0.0;
                }
        }
        return s;
}

PROMEKI_NAMESPACE_END
