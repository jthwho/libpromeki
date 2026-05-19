/**
 * @file      cpumonitor.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/cpumonitor.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_LINUX)
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

/// Stable record kept across sample boundaries so the next tick's
/// deltas are stable even if a thread dies or is renamed.  Keyed on
/// TID inside a flat sorted list — TIDs are dense small integers
/// from the kernel so a binary search is cheaper than building a
/// hash map for every tick.
struct PrevSnapshot {
                uint64_t tid = 0;
                int64_t  utimeTicks = 0;
                int64_t  stimeTicks = 0;
};

#if defined(PROMEKI_PLATFORM_LINUX)
/// Returns @c sysconf(_SC_CLK_TCK), cached on first call.  POSIX
/// guarantees this is constant for the lifetime of the process.
int64_t clockTicksPerSecond() {
        static const int64_t v = []() -> int64_t {
                long t = sysconf(_SC_CLK_TCK);
                return t > 0 ? t : 100;  // Fallback: 100Hz USER_HZ on most Linux builds.
        }();
        return v;
}

/// Reads the entire contents of @p path into @p buf, returning the
/// number of bytes read or 0 on error.  Uses raw POSIX since /proc
/// files don't support stat-able size or seek.
ssize_t slurpProcFile(const char *path, char *buf, size_t bufLen) {
        int fd = ::open(path, O_RDONLY);
        if (fd < 0) return 0;
        ssize_t total = 0;
        while (total < static_cast<ssize_t>(bufLen) - 1) {
                ssize_t n = ::read(fd, buf + total, bufLen - 1 - static_cast<size_t>(total));
                if (n <= 0) break;
                total += n;
        }
        ::close(fd);
        if (total < 0) return 0;
        buf[total] = '\0';
        return total;
}

/// Parses utime + stime from a /proc/<pid>/stat blob.  Returns true
/// on success.  Format:
/// @code
/// <pid> (<comm>) <state> <ppid> <pgrp> <session> <tty_nr> <tpgid>
/// <flags> <minflt> <cminflt> <majflt> <cmajflt> <utime> <stime> ...
/// @endcode
/// @c comm may contain spaces and parentheses — find the LAST @c )
/// before scanning fields, the standard Linux trick.
bool parseStat(const char *buf, size_t len, int64_t &utimeTicks, int64_t &stimeTicks) {
        const char *closeParen = nullptr;
        for (const char *p = buf + len - 1; p >= buf; --p) {
                if (*p == ')') {
                        closeParen = p;
                        break;
                }
        }
        if (closeParen == nullptr) return false;
        const char *cur = closeParen + 1;
        // Field 1 after `)` is state, then ppid, pgrp, session,
        // tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt,
        // utime (12), stime (13).
        constexpr int utimeIdx = 12;
        constexpr int stimeIdx = 13;
        int           field = 0;
        utimeTicks = 0;
        stimeTicks = 0;
        while (*cur != '\0') {
                while (*cur == ' ') ++cur;
                if (*cur == '\0') break;
                ++field;
                if (field == utimeIdx) {
                        utimeTicks = std::strtoll(cur, nullptr, 10);
                } else if (field == stimeIdx) {
                        stimeTicks = std::strtoll(cur, nullptr, 10);
                        return true;
                }
                while (*cur != ' ' && *cur != '\0') ++cur;
        }
        return false;
}
#endif  // PROMEKI_PLATFORM_LINUX

/// Default reporter: log @ref CpuMonitor::formatReport at @c Info.
void defaultReportFunction(const CpuMonitor::Report &r) {
        promekiInfo("%s", CpuMonitor::formatReport(r).cstr());
}

}  // namespace

CpuMonitor::CpuMonitor() {
        _intervalNs.setValue(static_cast<int64_t>(DefaultIntervalSec) * 1'000'000'000LL);
        _stopRequested.setValue(false);
        _poolReportEnabled.setValue(true);
        _reportFn = defaultReportFunction;
        Thread::setName(String("cpumon"));
}

void CpuMonitor::setPoolReportEnabled(bool enabled) {
        _poolReportEnabled.setValue(enabled);
}

bool CpuMonitor::isPoolReportEnabled() const {
        return _poolReportEnabled.value();
}

CpuMonitor::~CpuMonitor() {
        stop();
}

Error CpuMonitor::start(const Duration &interval) {
        if (Thread::isRunning()) return Error::AlreadyOpen;
        const int64_t ns = interval.nanoseconds() > 0
                                   ? interval.nanoseconds()
                                   : Duration::fromSeconds(DefaultIntervalSec).nanoseconds();
        _intervalNs.setValue(ns);
        _stopRequested.setValue(false);
        Thread::start();
        return Error::Ok;
}

void CpuMonitor::stop() {
        if (!Thread::isRunning()) return;
        _stopRequested.setValue(true);
        Thread::wait();
}

void CpuMonitor::setInterval(const Duration &d) {
        if (d.nanoseconds() <= 0) return;
        _intervalNs.setValue(d.nanoseconds());
}

Duration CpuMonitor::interval() const {
        return Duration::fromNanoseconds(_intervalNs.value());
}

void CpuMonitor::setReportFunction(ReportFunction fn) {
        Mutex::Locker locker(_fnMutex);
        _reportFn = fn ? std::move(fn) : ReportFunction(defaultReportFunction);
}

List<CpuMonitor::ThreadCpuTimes> CpuMonitor::readThreadCpuTimes() const {
        List<ThreadCpuTimes> out;
#if defined(PROMEKI_PLATFORM_LINUX)
        DIR *dir = ::opendir("/proc/self/task");
        if (dir == nullptr) return out;
        struct dirent *ent;
        while ((ent = ::readdir(dir)) != nullptr) {
                if (ent->d_name[0] == '.') continue;
                char    *endp = nullptr;
                uint64_t tid = std::strtoull(ent->d_name, &endp, 10);
                if (tid == 0 || endp == nullptr || *endp != '\0') continue;

                char     path[64];
                char     buf[1024];
                ssize_t  n;

                std::snprintf(path, sizeof(path), "/proc/self/task/%llu/stat",
                              static_cast<unsigned long long>(tid));
                n = slurpProcFile(path, buf, sizeof(buf));
                if (n <= 0) continue;
                int64_t utime = 0, stime = 0;
                if (!parseStat(buf, static_cast<size_t>(n), utime, stime)) continue;

                ThreadCpuTimes rec;
                rec.tid = tid;
                rec.utimeTicks = utime;
                rec.stimeTicks = stime;

                std::snprintf(path, sizeof(path), "/proc/self/task/%llu/comm",
                              static_cast<unsigned long long>(tid));
                char nameBuf[32] = {0};
                n = slurpProcFile(path, nameBuf, sizeof(nameBuf));
                if (n > 0) {
                        // Strip trailing newline.
                        if (nameBuf[n - 1] == '\n') nameBuf[n - 1] = '\0';
                        rec.name = String(nameBuf);
                }

                out.pushToBack(rec);
        }
        ::closedir(dir);
#endif
        return out;
}

void CpuMonitor::run() {
#if !defined(PROMEKI_PLATFORM_LINUX)
        // Non-Linux: nothing to sample.  Sit idle until stop().
        while (!_stopRequested.value()) BasicThread::sleepMs(100);
        return;
#else
        // Establish a baseline so the first published report covers
        // a real interval (no synthetic 0-elapsed first sample).
        List<ThreadCpuTimes> prev = readThreadCpuTimes();
        TimeStamp            prevWall = TimeStamp::now();
        const int64_t        ticksPerSec = clockTicksPerSecond();

        while (!_stopRequested.value()) {
                // Sleep in small chunks so a runtime interval
                // change or a stop request is observed within
                // ~100ms instead of waiting out a long sleep.
                int64_t remainingNs = _intervalNs.value();
                while (remainingNs > 0 && !_stopRequested.value()) {
                        const int64_t step = std::min<int64_t>(remainingNs, 100'000'000LL);
                        BasicThread::sleepNs(step);
                        remainingNs -= step;
                }
                if (_stopRequested.value()) break;

                const TimeStamp            now = TimeStamp::now();
                const Duration             wallElapsed = now - prevWall;
                const List<ThreadCpuTimes> cur = readThreadCpuTimes();

                // Build the report.  Per-thread CPU% is normalised
                // to one core, so the process total is the sum
                // (which can exceed 100% on multi-core loads).
                Report report;
                report.wallElapsed = wallElapsed;
                const double wallSec = static_cast<double>(wallElapsed.nanoseconds()) / 1.0e9;
                const double tps = static_cast<double>(ticksPerSec);

                // prev is sorted by TID (readdir order is
                // arbitrary, but we keep it sorted in-place each
                // tick so the lookup below is binary-searchable).
                // Caveat: readdir does not promise sorted output —
                // sort once before lookups.
                auto byTid = [](const ThreadCpuTimes &a, const ThreadCpuTimes &b) {
                        return a.tid < b.tid;
                };
                prev.sortInPlace(byTid);

                for (const ThreadCpuTimes &t : cur) {
                        // Find matching prev entry by TID.  A new
                        // thread (no prev entry) is reported with
                        // its accumulated CPU since spawn — this
                        // overstates its share of the interval
                        // but is still a useful "where did this
                        // thread come from" signal.
                        int64_t prevU = 0, prevS = 0;
                        auto    it = std::lower_bound(prev.begin(), prev.end(), t, byTid);
                        if (it != prev.end() && it->tid == t.tid) {
                                prevU = it->utimeTicks;
                                prevS = it->stimeTicks;
                        }
                        const int64_t dU = t.utimeTicks - prevU;
                        const int64_t dS = t.stimeTicks - prevS;
                        if (dU < 0 || dS < 0) continue;  // Counter wrap.
                        ThreadSample sample;
                        sample.tid = t.tid;
                        sample.name = t.name;
                        const double dUs = static_cast<double>(dU) / tps;
                        const double dSs = static_cast<double>(dS) / tps;
                        sample.cpuTimeDelta = Duration::fromNanoseconds(
                                static_cast<int64_t>((dUs + dSs) * 1.0e9));
                        if (wallSec > 0.0) {
                                sample.userPercent = 100.0 * dUs / wallSec;
                                sample.systemPercent = 100.0 * dSs / wallSec;
                                sample.totalPercent = sample.userPercent + sample.systemPercent;
                        }
                        report.processPercent += sample.totalPercent;
                        report.threads.pushToBack(sample);
                }

                report.threads.sortInPlace([](const ThreadSample &a, const ThreadSample &b) {
                        return a.totalPercent > b.totalPercent;
                });

                ReportFunction fn;
                {
                        Mutex::Locker locker(_fnMutex);
                        fn = _reportFn;
                }
                if (fn) fn(report);

                // Per-pool, per-tag work report.  Each pool's
                // counters cover the same wall window as the
                // per-thread report above; we reset them so the
                // next tick reports just the next interval.
                if (_poolReportEnabled.value()) {
                        const List<ThreadPool *> pools = ThreadPool::allPools();
                        for (ThreadPool *pool : pools) {
                                if (pool == nullptr) continue;
                                List<ThreadPool::WorkStats> stats = pool->snapshotWorkStats();
                                pool->resetWorkStats();
                                bool anyWork = false;
                                for (const auto &s : stats) {
                                        if (s.count > 0) {
                                                anyWork = true;
                                                break;
                                        }
                                }
                                if (!anyWork) continue;
                                String poolName = pool->name();
                                if (poolName.isEmpty()) {
                                        char buf[32];
                                        std::snprintf(buf, sizeof(buf), "0x%p", static_cast<const void *>(pool));
                                        poolName = String(buf);
                                }
                                promekiInfo("%s", formatPoolReport(poolName, wallElapsed, stats).cstr());
                        }
                }

                prev = std::move(const_cast<List<ThreadCpuTimes> &>(cur));
                prevWall = now;
        }
#endif
}

String CpuMonitor::formatPoolReport(const String &poolName, const Duration &wallElapsed,
                                    const List<ThreadPool::WorkStats> &stats, size_t topN) {
        const double sec = static_cast<double>(wallElapsed.nanoseconds()) / 1.0e9;
        char         secBuf[32];
        std::snprintf(secBuf, sizeof(secBuf), "%.1f", sec);
        String out = String("pool/") + poolName + "/" + secBuf + "s";
        const size_t emit = topN == 0 ? stats.size() : std::min(topN, stats.size());
        size_t       emitted = 0;
        for (size_t i = 0; i < emit; i++) {
                const ThreadPool::WorkStats &s = stats[i];
                if (s.count <= 0) continue;
                const double cpuPct = sec > 0.0
                                              ? 100.0 * static_cast<double>(s.totalCpu.nanoseconds()) /
                                                        (sec * 1.0e9)
                                              : 0.0;
                const double wallPct = sec > 0.0
                                               ? 100.0 * static_cast<double>(s.totalWall.nanoseconds()) /
                                                         (sec * 1.0e9)
                                               : 0.0;
                if (cpuPct < 0.05 && wallPct < 0.05) continue;
                char pctBuf[64];
                std::snprintf(pctBuf, sizeof(pctBuf), "%.1f%%/%.1f%%", cpuPct, wallPct);
                out += String("  ") + s.name + "=" + pctBuf + " (n=" +
                       String::number(static_cast<int64_t>(s.count)) + ")";
                ++emitted;
        }
        if (topN > 0 && stats.size() > topN) {
                int64_t  otherCount = 0;
                Duration otherCpu;
                Duration otherWall;
                for (size_t i = topN; i < stats.size(); i++) {
                        otherCount += stats[i].count;
                        otherCpu = otherCpu + stats[i].totalCpu;
                        otherWall = otherWall + stats[i].totalWall;
                }
                if (otherCount > 0) {
                        const double cpuPct = sec > 0.0
                                                      ? 100.0 * static_cast<double>(otherCpu.nanoseconds()) /
                                                                (sec * 1.0e9)
                                                      : 0.0;
                        const double wallPct = sec > 0.0
                                                       ? 100.0 * static_cast<double>(otherWall.nanoseconds()) /
                                                                 (sec * 1.0e9)
                                                       : 0.0;
                        char pctBuf[64];
                        std::snprintf(pctBuf, sizeof(pctBuf), "%.1f%%/%.1f%%", cpuPct, wallPct);
                        out += String("  (") + String::number(static_cast<int64_t>(stats.size() - topN)) +
                               " others=" + pctBuf + ")";
                }
        }
        return out;
}

String CpuMonitor::formatReport(const Report &r, size_t topN) {
        const double sec = static_cast<double>(r.wallElapsed.nanoseconds()) / 1.0e9;
        char         secBuf[32];
        std::snprintf(secBuf, sizeof(secBuf), "%.1f", sec);
        char procBuf[32];
        std::snprintf(procBuf, sizeof(procBuf), "%.1f", r.processPercent);
        String out = String("cpu/") + secBuf + "s proc=" + procBuf + "%";
        const size_t emit = topN == 0 ? r.threads.size() : std::min(topN, r.threads.size());
        for (size_t i = 0; i < emit; i++) {
                const ThreadSample &s = r.threads[i];
                if (s.totalPercent < 0.05) break;  // Skip noise.
                char pct[32];
                std::snprintf(pct, sizeof(pct), "%.1f", s.totalPercent);
                String label = s.name.isEmpty()
                                       ? String("tid:") + String::number(static_cast<int64_t>(s.tid))
                                       : s.name;
                out += String("  ") + label + "=" + pct + "%";
        }
        if (topN > 0 && r.threads.size() > topN) {
                double otherPct = 0.0;
                for (size_t i = topN; i < r.threads.size(); i++) {
                        otherPct += r.threads[i].totalPercent;
                }
                if (otherPct >= 0.05) {
                        char pct[32];
                        std::snprintf(pct, sizeof(pct), "%.1f", otherPct);
                        out += String("  (") + String::number(static_cast<int64_t>(r.threads.size() - topN)) +
                               " others=" + pct + "%)";
                }
        }
        return out;
}

PROMEKI_NAMESPACE_END
