/**
 * @file      cpumonitor.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include <promeki/application.h>
#include <promeki/cpumonitor.h>
#include <promeki/duration.h>
#include <promeki/mutex.h>

using namespace promeki;

TEST_CASE("CpuMonitor: idle construction / no thread until start") {
        CpuMonitor mon;
        CHECK_FALSE(mon.isRunning());
        // Default interval matches DefaultIntervalSec.
        CHECK(mon.interval().seconds() == CpuMonitor::DefaultIntervalSec);
}

TEST_CASE("CpuMonitor: start/stop is idempotent and produces at least one report") {
        CpuMonitor mon;

        std::atomic<int>    reportCount{0};
        std::atomic<double> lastProcessPct{0.0};
        Mutex               mutex;
        CpuMonitor::Report  captured;
        bool                haveCapture = false;

        mon.setReportFunction([&](const CpuMonitor::Report &r) {
                reportCount.fetch_add(1);
                lastProcessPct.store(r.processPercent);
                Mutex::Locker locker(mutex);
                captured = r;
                haveCapture = true;
        });

        // 100 ms interval — short enough for the test to finish
        // quickly, large enough that jiffy granularity (typically
        // 10 ms on Linux USER_HZ=100) doesn't dominate the noise.
        REQUIRE(mon.start(Duration::fromMilliseconds(100)).isOk());
        // Second start while running fails cleanly.
        CHECK(mon.start(Duration::fromMilliseconds(100)) == Error::AlreadyOpen);
        CHECK(mon.isRunning());

        // Burn some user-mode CPU on the test thread so the
        // sampler observes a non-zero reading for a known thread.
        // Spinning is the simplest portable way; cap it at 200 ms
        // so the test stays fast even on slow hosts.
        const auto burnUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        volatile uint64_t tally = 0;
        while (std::chrono::steady_clock::now() < burnUntil) {
                for (int i = 0; i < 10000; i++) tally += static_cast<uint64_t>(i);
        }
        // Force the compiler to keep the spin: use the value.
        CHECK(tally > 0);

        // Wait up to 1 s for at least one report.  100 ms cadence
        // means typically 2-4 reports inside the burn window.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (reportCount.load() < 1 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        mon.stop();
        // stop() is idempotent.
        mon.stop();
        CHECK_FALSE(mon.isRunning());

        REQUIRE(reportCount.load() >= 1);

        // The captured report should be non-empty (we have at
        // least the test thread + the sampler thread itself).
        Mutex::Locker locker(mutex);
        REQUIRE(haveCapture);
        CHECK(captured.threads.size() >= 2);

        // wallElapsed should be on the order of the requested
        // interval (100 ms) — allow generous slack for slow CI.
        CHECK(captured.wallElapsed.milliseconds() >= 50);
        CHECK(captured.wallElapsed.milliseconds() <= 1000);

        // Sorted descending: each entry's totalPercent <= the
        // previous one's.
        for (size_t i = 1; i < captured.threads.size(); i++) {
                CHECK(captured.threads[i - 1].totalPercent + 1e-9 >=
                      captured.threads[i].totalPercent);
        }
}

TEST_CASE("CpuMonitor: setInterval updates configured interval") {
        CpuMonitor mon;
        mon.setInterval(Duration::fromMilliseconds(250));
        CHECK(mon.interval().milliseconds() == 250);
        // Zero / negative is ignored — keeps the previous value.
        mon.setInterval(Duration::fromMilliseconds(0));
        CHECK(mon.interval().milliseconds() == 250);
}

TEST_CASE("CpuMonitor: formatReport produces a readable summary") {
        CpuMonitor::Report r;
        r.wallElapsed = Duration::fromMilliseconds(1000);
        r.processPercent = 215.4;

        CpuMonitor::ThreadSample a;
        a.tid = 42;
        a.name = "tpg";
        a.totalPercent = 98.4;
        r.threads.pushToBack(a);

        CpuMonitor::ThreadSample b;
        b.tid = 43;
        b.name = "RtpVidPkt";
        b.totalPercent = 42.1;
        r.threads.pushToBack(b);

        // A near-zero entry is skipped by formatReport so the
        // summary line stays short.
        CpuMonitor::ThreadSample c;
        c.tid = 44;
        c.name = "noise";
        c.totalPercent = 0.0;
        r.threads.pushToBack(c);

        const String line = CpuMonitor::formatReport(r);
        CHECK(line.contains("cpu/1.0s"));
        CHECK(line.contains("proc=215.4%"));
        CHECK(line.contains("tpg=98.4%"));
        CHECK(line.contains("RtpVidPkt=42.1%"));
        // Sub-threshold thread name omitted.
        CHECK_FALSE(line.contains("noise"));
}

TEST_CASE("CpuMonitor: formatReport summarizes overflow into '(N others)'") {
        CpuMonitor::Report r;
        r.wallElapsed = Duration::fromSeconds(1);
        r.processPercent = 0.0;
        // Build 12 threads, each contributing 1.0% — topN default
        // 8 means 4 threads should fold into the overflow bucket.
        for (int i = 0; i < 12; i++) {
                CpuMonitor::ThreadSample s;
                s.tid = static_cast<uint64_t>(100 + i);
                s.name = String("t") + String::number(i);
                s.totalPercent = 1.0;
                r.processPercent += s.totalPercent;
                r.threads.pushToBack(s);
        }
        const String line = CpuMonitor::formatReport(r);
        CHECK(line.contains("4 others=4.0%"));
}

TEST_CASE("CpuMonitor: Application::startCpuMonitor / stopCpuMonitor") {
        char        arg0[] = "cputest";
        char       *argv[] = {arg0};
        Application app(1, argv);

        CHECK(Application::cpuMonitor() == nullptr);
        REQUIRE(Application::startCpuMonitor(Duration::fromMilliseconds(100)).isOk());
        CHECK(Application::cpuMonitor() != nullptr);
        CHECK(Application::cpuMonitor()->isRunning());
        // Second start is rejected while the monitor is running.
        CHECK(Application::startCpuMonitor(Duration::fromMilliseconds(100)) == Error::AlreadyOpen);
        Application::stopCpuMonitor();
        CHECK(Application::cpuMonitor() == nullptr);
        // Idempotent.
        Application::stopCpuMonitor();
}
