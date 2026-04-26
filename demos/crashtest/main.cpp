/**
 * @file      crashtest/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Demonstrates the CrashHandler by spinning up several named worker
 * threads, letting them run briefly, and then triggering a crash on
 * one of them.  The resulting crash report (stderr + temp log file)
 * should show all threads with their names and a stack trace from
 * the crashing thread.
 *
 * Usage:
 *   crashtest [mode]
 *
 *   mode    What to do:
 *             "main"    — crash the main thread (default)
 *             "worker"  — crash worker-0
 *             "0".."3"  — crash the numbered worker
 *             "trace"   — call Application::writeTrace() a few times
 *                         and exit normally (no crash)
 */

#include <cstdio>
#include <cstring>
#include <atomic>
#include <promeki/application.h>
#include <promeki/thread.h>
#include <promeki/eventloop.h>
#include <promeki/logger.h>

using namespace promeki;

static constexpr int     NumWorkers = 4;
static std::atomic<bool> g_running{true};
static std::atomic<int>  g_crashTarget{-1}; // -1 = main, 0..N-1 = worker index

// --------------------------------------------------------------------
// Worker thread — loops until told to stop or asked to crash.
// --------------------------------------------------------------------

class WorkerThread : public Thread {
                PROMEKI_OBJECT(WorkerThread, Thread)
        public:
                WorkerThread(int index, ObjectBase *parent = nullptr) : Thread(parent), _index(index) {}

        protected:
                void run() override {
                        int cycle = 0;
                        while (g_running.load()) {
                                // Simulate useful work.
                                volatile int sum = 0;
                                for (int i = 0; i < 100000; ++i) sum += i;
                                (void)sum;

                                if (g_crashTarget.load() == _index) {
                                        promekiInfo("worker-%d: triggering crash", _index);
                                        promekiLogSync();
                                        triggerCrash();
                                }
                                ++cycle;

                                // Yield briefly so we don't peg a core.
                                struct timespec ts = {0, 10'000'000}; // 10 ms
                                nanosleep(&ts, nullptr);
                        }
                }

        private:
                int _index;

                // Crash through a few stack frames so the trace is
                // more interesting than a one-liner.
                [[gnu::noinline]] void triggerCrash() { crashLevelA(); }

                [[gnu::noinline]] void crashLevelA() { crashLevelB(42); }

                [[gnu::noinline]] void crashLevelB(int val) {
                        volatile int *bad = nullptr;
                        *bad = val; // SIGSEGV
                }
};

// Same nested-call trick for a main-thread crash.
[[gnu::noinline]] static void mainCrashA();
[[gnu::noinline]] static void mainCrashB(int val);

static void mainCrashA() {
        mainCrashB(99);
}

static void mainCrashB(int val) {
        volatile int *bad = nullptr;
        *bad = val;
}

// --------------------------------------------------------------------

int main(int argc, char **argv) {
        Application app(argc, argv);
        Application::setAppName("crashtest");

        // Parse which thread should crash (or trace).
        int  crashThread = -1; // default: main
        bool traceOnly = false;
        if (argc > 1) {
                if (std::strcmp(argv[1], "main") == 0) {
                        crashThread = -1;
                } else if (std::strcmp(argv[1], "worker") == 0) {
                        crashThread = 0;
                } else if (std::strcmp(argv[1], "trace") == 0) {
                        traceOnly = true;
                } else {
                        crashThread = std::atoi(argv[1]);
                        if (crashThread < 0 || crashThread >= NumWorkers) {
                                std::fprintf(stderr, "Usage: %s [main|worker|0-%d|trace]\n", argv[0], NumWorkers - 1);
                                return 1;
                        }
                }
        }

        promekiInfo("crashtest: starting %d worker threads", NumWorkers);
        if (traceOnly) {
                promekiInfo("crashtest: mode = trace (no crash)");
        } else {
                promekiInfo("crashtest: crash target = %s",
                            crashThread < 0 ? "main" : String::sprintf("worker-%d", crashThread).cstr());
        }

        // Start worker threads.
        WorkerThread *workers[NumWorkers];
        for (int i = 0; i < NumWorkers; ++i) {
                workers[i] = new WorkerThread(i);
                workers[i]->setName(String::sprintf("worker-%d", i));
                workers[i]->start();
        }

        // Give threads a moment to spin up so they appear in the
        // crash report.
        struct timespec ts = {0, 100'000'000}; // 100 ms
        nanosleep(&ts, nullptr);

        if (traceOnly) {
                // Call writeTrace a few times with different reasons
                // to exercise the sequence number and show that it
                // does NOT terminate the process.
                promekiInfo("crashtest: writing trace #1");
                Application::writeTrace("startup snapshot");
                promekiInfo("crashtest: writing trace #2");
                Application::writeTrace("mid-run checkpoint");
                promekiInfo("crashtest: writing trace #3 (no reason)");
                Application::writeTrace();
                promekiInfo("crashtest: all traces written, shutting down");
        } else if (crashThread < 0) {
                // Crash on main thread.
                promekiInfo("crashtest: crashing on main thread");
                promekiLogSync();
                mainCrashA();
        } else {
                // Tell the target worker to crash.
                g_crashTarget.store(crashThread);

                // Wait for the crash to propagate.
                struct timespec wait = {2, 0};
                nanosleep(&wait, nullptr);

                // If we get here the worker didn't crash for some reason.
                promekiErr("crashtest: worker didn't crash — exiting normally");
        }

        g_running.store(false);
        for (int i = 0; i < NumWorkers; ++i) {
                workers[i]->wait();
                delete workers[i];
        }
        return 0;
}
