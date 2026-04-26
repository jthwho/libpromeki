/**
 * @file      signalhandler.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/signalhandler.h>
#include <promeki/application.h>
#include <promeki/eventloop.h>
#include <promeki/libraryoptions.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/mutex.h>
#include <promeki/platform.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#elif defined(PROMEKI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SignalHandler);

namespace {

        // ============================================================================
        // Shared state (platform-independent)
        // ============================================================================

        std::atomic<bool> g_installed{false};

        // Number of termination signals delivered since install().  Used to
        // drive the "second Ctrl-C force-exits" escape hatch.
        std::atomic<int> g_deliveryCount{0};

        // Translate a delivered signal number into the standard
        // shell-convention exit code.
        int exitCodeForSignal(int signo) {
                return 128 + signo;
        }

        // Called from the platform-specific delivery path when a termination
        // signal arrives.  Runs in a normal thread context (self-pipe reader
        // on POSIX, OS-provided ctrl-handler thread on Windows), so it may
        // take locks and allocate freely.
        void deliverQuit(int signo) {
                const int code = exitCodeForSignal(signo);
                const int seen = g_deliveryCount.fetch_add(1) + 1;

                if (seen >= 2 && LibraryOptions::instance().getAs<bool>(LibraryOptions::SignalDoubleTapExit)) {
                        // Second delivery and the user hasn't opted out — the
                        // event loop is unresponsive, abort the process.
                        promekiWarn("SignalHandler: second termination signal (%d) received, "
                                    "forcing exit",
                                    signo);
                        std::_Exit(code);
                }

                promekiInfo("SignalHandler: received signal %d, requesting quit (code %d)", signo, code);

                // Application::quit runs any installed QuitRequestHandler,
                // sets the shouldQuit flag, and wakes the main EventLoop.
                // A handler that defers quitting (e.g. kicking off an async
                // pipeline close) intercepts the request and leaves the
                // exec() loop running so the cascade can complete.
                Application::quit(code);
        }

#if defined(PROMEKI_PLATFORM_POSIX)

        // ============================================================================
        // POSIX: async sigaction handler + self-pipe + watcher thread
        // ============================================================================
        //
        // Why a self-pipe rather than a dedicated sigwait() thread?  Because
        // pthread_sigmask only blocks signals for the calling thread and its
        // descendants, not for threads already running at install time.  The
        // Logger's worker thread in particular is spawned on the first log
        // call — which may well be before Application() runs — so we can't
        // rely on mask inheritance to funnel signals at a single consumer.
        //
        // The self-pipe trick sidesteps the problem entirely: sigaction
        // installs a process-wide handler that any thread can run; the
        // handler does only async-signal-safe work (one write() of a byte);
        // the watcher thread blocks in read() and then runs the rest of the
        // quit logic in a normal context.

        // Signals that trigger a clean quit.
        constexpr int kTerminationSignals[] = {
                SIGINT,
                SIGTERM,
                SIGHUP,
                SIGQUIT,
        };
        constexpr int kNumTerminationSignals = sizeof(kTerminationSignals) / sizeof(kTerminationSignals[0]);

        // Wake-byte written to the pipe by uninstall() to break the watcher
        // out of its read() loop without looking like a real signal.
        constexpr unsigned char kWakeByte = 0xFF;

        // Self-pipe file descriptors.  [0] = read end (watcher), [1] = write
        // end (signal handler + uninstall).
        int g_wakePipe[2] = {-1, -1};

        // Previous sigaction dispositions, so uninstall() can restore them.
        struct sigaction g_savedActions[kNumTerminationSignals];
        bool             g_savedValid[kNumTerminationSignals] = {false};

        std::thread      *g_watcher = nullptr;
        std::atomic<bool> g_stopRequested{false};

        // Async-signal-safe handler: just forwards the signal number as one
        // byte through the self-pipe.  Anything more ambitious would risk
        // deadlocking against whatever the interrupted thread was doing.
        extern "C" void asyncSignalHandler(int signo) {
                // Preserve errno so we don't disturb the interrupted code.
                const int savedErrno = errno;

                if (g_wakePipe[1] >= 0) {
                        unsigned char byte = static_cast<unsigned char>(signo);
                        // write() is explicitly async-signal-safe per POSIX.
                        // Loop on EINTR; drop the byte on any other failure
                        // because there is nothing a signal handler can do
                        // about a broken pipe.
                        while (::write(g_wakePipe[1], &byte, 1) < 0) {
                                if (errno != EINTR) break;
                        }
                }

                errno = savedErrno;
        }

        void applyWatcherThreadName() {
#if defined(PROMEKI_PLATFORM_LINUX)
                pthread_setname_np(pthread_self(), "promeki-signals");
#elif defined(PROMEKI_PLATFORM_APPLE)
                pthread_setname_np("promeki-signals");
#endif
                // Also update the logger's per-thread tag so log lines emitted
                // from this thread are labelled clearly.
                Logger::setThreadName("promeki-signals");
        }

        // ============================================================================
        // Custom (non-termination) signal subscribers
        // ============================================================================
        //
        // Subsystems like TuiSubsystem (SIGWINCH) route their non-termination
        // signals through this single handler rather than installing their
        // own sigaction hooks.  The async sigaction forwards the signal
        // number through the self-pipe exactly as it does for termination
        // signals; the watcher distinguishes based on signo and dispatches
        // to the subscribers list in normal-context.

        struct Subscriber {
                        int                     handle;
                        int                     signo;
                        SignalHandler::Callback cb;
        };

        Mutex            g_subscribersMutex;
        List<Subscriber> g_subscribers;
        std::atomic<int> g_nextHandle{1};

        // Per-custom-signal saved sigaction.  Entries live from the first
        // subscribe() for that signal until uninstall() tears them down;
        // unsubscribe() does not remove the sigaction, so a stray signal for
        // a signal with no live subscribers is drained and ignored.
        struct CustomSignalInstall {
                        int              signo;
                        struct sigaction saved;
        };
        List<CustomSignalInstall> g_customInstalls;

        bool isTerminationSignal(int signo) {
                for (int i = 0; i < kNumTerminationSignals; ++i) {
                        if (kTerminationSignals[i] == signo) return true;
                }
                return false;
        }

        // Caller must hold @c g_subscribersMutex.
        bool customSignalAlreadyInstalled(int signo) {
                for (size_t i = 0; i < g_customInstalls.size(); ++i) {
                        if (g_customInstalls[i].signo == signo) return true;
                }
                return false;
        }

        // Installs the async sigaction hook for @p signo and records the
        // previous disposition so uninstall() can restore it.  Caller must
        // hold @c g_subscribersMutex.  Returns true on success.
        bool installCustomSignalLocked(int signo) {
                struct sigaction sa;
                std::memset(&sa, 0, sizeof(sa));
                sa.sa_handler = asyncSignalHandler;
                sigemptyset(&sa.sa_mask);
                // Block all termination signals while the custom handler
                // runs so a quit signal doesn't race against a mid-dispatch
                // custom signal on the same thread.
                for (int j = 0; j < kNumTerminationSignals; ++j) {
                        sigaddset(&sa.sa_mask, kTerminationSignals[j]);
                }
                // SA_RESTART: a custom signal shouldn't unwind blocking
                // syscalls on unrelated threads.  Subscribers that want to
                // wake a loop post a callable from the watcher thread
                // instead.
                sa.sa_flags = SA_RESTART;
                CustomSignalInstall entry;
                entry.signo = signo;
                if (sigaction(signo, &sa, &entry.saved) != 0) {
                        promekiWarn("SignalHandler::subscribe: sigaction(%d) failed (errno %d)", signo, errno);
                        return false;
                }
                g_customInstalls.pushToBack(entry);
                return true;
        }

        // Dispatches any subscribers registered for @p signo.  Runs on the
        // watcher thread in normal context.
        void dispatchSubscribers(int signo) {
                List<SignalHandler::Callback> callbacks;
                {
                        Mutex::Locker lock(g_subscribersMutex);
                        for (size_t i = 0; i < g_subscribers.size(); ++i) {
                                if (g_subscribers[i].signo == signo) {
                                        callbacks.pushToBack(g_subscribers[i].cb);
                                }
                        }
                }
                for (size_t i = 0; i < callbacks.size(); ++i) {
                        if (callbacks[i]) callbacks[i](signo);
                }
        }

        // Worker body: block on read() until a byte arrives from the signal
        // handler or uninstall() pokes us.
        void watcherMain() {
                applyWatcherThreadName();

                while (!g_stopRequested.load(std::memory_order_acquire)) {
                        unsigned char byte = 0;
                        ssize_t       n = ::read(g_wakePipe[0], &byte, 1);
                        if (n < 0) {
                                if (errno == EINTR) continue;
                                break;
                        }
                        if (n == 0) break; // pipe closed
                        if (byte == kWakeByte) {
                                // Shutdown sentinel from uninstall().
                                break;
                        }
                        const int signo = static_cast<int>(byte);
                        if (isTerminationSignal(signo)) {
                                deliverQuit(signo);
                        } else {
                                dispatchSubscribers(signo);
                        }
                }
        }

        // Set O_CLOEXEC and O_NONBLOCK on a pipe fd where available.  The
        // read end stays blocking (we want the watcher to block), the write
        // end goes non-blocking so the signal handler never stalls the
        // interrupted thread on a full pipe.
        void setFdFlags(int fd, bool nonblocking) {
                int flags = fcntl(fd, F_GETFD);
                if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
                if (nonblocking) {
                        int statusFlags = fcntl(fd, F_GETFL);
                        if (statusFlags >= 0) {
                                fcntl(fd, F_SETFL, statusFlags | O_NONBLOCK);
                        }
                }
        }

#elif defined(PROMEKI_PLATFORM_WINDOWS)

        // ============================================================================
        // Windows: SetConsoleCtrlHandler
        // ============================================================================
        //
        // SetConsoleCtrlHandler spawns a dedicated OS thread to run the
        // callback, so we don't need a self-pipe or watcher thread of our
        // own — the callback runs in normal context and can call deliverQuit
        // directly.

        BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
                switch (ctrlType) {
                        case CTRL_C_EVENT: deliverQuit(SIGINT); return TRUE;
                        case CTRL_BREAK_EVENT: deliverQuit(SIGINT); return TRUE;
                        case CTRL_CLOSE_EVENT: deliverQuit(SIGTERM); return TRUE;
                        case CTRL_LOGOFF_EVENT: deliverQuit(SIGTERM); return TRUE;
                        case CTRL_SHUTDOWN_EVENT: deliverQuit(SIGTERM); return TRUE;
                        default: return FALSE;
                }
        }

#endif

} // namespace

// ============================================================================
// Public API
// ============================================================================

void SignalHandler::install() {
        if (g_installed.load(std::memory_order_acquire)) return;

        g_deliveryCount.store(0, std::memory_order_release);

#if defined(PROMEKI_PLATFORM_POSIX)
        // Create the self-pipe.  Prefer pipe2(O_CLOEXEC) where it is
        // available; fall back to pipe() + fcntl otherwise.
#if defined(PROMEKI_PLATFORM_LINUX)
        if (::pipe2(g_wakePipe, O_CLOEXEC) != 0) {
                promekiWarn("SignalHandler: pipe2() failed (errno %d)", errno);
                return;
        }
        // pipe2() already sets CLOEXEC on both ends.  Make the write
        // end non-blocking so the signal handler never stalls.
        int wflags = fcntl(g_wakePipe[1], F_GETFL);
        if (wflags >= 0) fcntl(g_wakePipe[1], F_SETFL, wflags | O_NONBLOCK);
#else
        if (::pipe(g_wakePipe) != 0) {
                promekiWarn("SignalHandler: pipe() failed (errno %d)", errno);
                return;
        }
        setFdFlags(g_wakePipe[0], /*nonblocking=*/false);
        setFdFlags(g_wakePipe[1], /*nonblocking=*/true);
#endif

        // Install the async signal handler for each termination
        // signal.  Remember the previous disposition so uninstall()
        // can restore it.
        for (int i = 0; i < kNumTerminationSignals; ++i) {
                struct sigaction sa;
                std::memset(&sa, 0, sizeof(sa));
                sa.sa_handler = asyncSignalHandler;
                sigemptyset(&sa.sa_mask);
                // Block all of our termination signals while the
                // handler runs so a SIGINT mid-SIGTERM doesn't race.
                for (int j = 0; j < kNumTerminationSignals; ++j) {
                        sigaddset(&sa.sa_mask, kTerminationSignals[j]);
                }
                // No SA_RESTART — we *want* blocking syscalls to
                // return EINTR so event loops unwind promptly.
                sa.sa_flags = 0;
                if (sigaction(kTerminationSignals[i], &sa, &g_savedActions[i]) == 0) {
                        g_savedValid[i] = true;
                } else {
                        g_savedValid[i] = false;
                        promekiWarn("SignalHandler: sigaction(%d) failed (errno %d)", kTerminationSignals[i], errno);
                }
        }

        g_stopRequested.store(false, std::memory_order_release);
        g_watcher = new std::thread(watcherMain);
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        if (!SetConsoleCtrlHandler(consoleCtrlHandler, TRUE)) {
                promekiWarn("SignalHandler: SetConsoleCtrlHandler(add) failed "
                            "(error %lu)",
                            (unsigned long)GetLastError());
                return;
        }
#endif

        g_installed.store(true, std::memory_order_release);
        promekiDebug("SignalHandler: installed");
}

void SignalHandler::uninstall() {
        if (!g_installed.load(std::memory_order_acquire)) return;

#if defined(PROMEKI_PLATFORM_POSIX)
        // Refuse to deadlock if called from the watcher thread itself.
        if (g_watcher != nullptr && pthread_equal(pthread_self(), g_watcher->native_handle())) {
                promekiWarn("SignalHandler: uninstall() called from the "
                            "signal-waiting thread — skipping join");
                return;
        }

        // Restore the previous sigaction dispositions first so that
        // any signal arriving mid-shutdown runs the user's old
        // handler instead of ours.
        for (int i = 0; i < kNumTerminationSignals; ++i) {
                if (g_savedValid[i]) {
                        sigaction(kTerminationSignals[i], &g_savedActions[i], nullptr);
                        g_savedValid[i] = false;
                }
        }
        // Restore the saved disposition for every custom signal that
        // had been installed via subscribe(), and drop the subscriber
        // list.  Take the lock only long enough to move the entries
        // out — sigaction() itself is not required to be called
        // under the lock (it is a syscall with its own kernel-side
        // synchronization).
        {
                List<CustomSignalInstall> toRestore;
                {
                        Mutex::Locker lock(g_subscribersMutex);
                        toRestore = std::move(g_customInstalls);
                        g_customInstalls = List<CustomSignalInstall>();
                        g_subscribers = List<Subscriber>();
                }
                for (size_t i = 0; i < toRestore.size(); ++i) {
                        sigaction(toRestore[i].signo, &toRestore[i].saved, nullptr);
                }
        }

        // Ask the watcher to exit, then poke it with a sentinel byte
        // so the blocked read() returns.
        g_stopRequested.store(true, std::memory_order_release);
        if (g_wakePipe[1] >= 0) {
                unsigned char byte = kWakeByte;
                ssize_t       written;
                do {
                        written = ::write(g_wakePipe[1], &byte, 1);
                } while (written < 0 && errno == EINTR);
        }

        if (g_watcher != nullptr) {
                g_watcher->join();
                delete g_watcher;
                g_watcher = nullptr;
        }

        // There is a narrow benign race here: a signal delivered on
        // another thread between the sigaction restore above and the
        // close() below may already be inside asyncSignalHandler with
        // g_wakePipe[1] cached on its stack.  Closing the fd from
        // under that thread just causes its write() to return EBADF,
        // which the handler tolerates (the byte is dropped, errno is
        // restored from savedErrno, and control returns to the
        // interrupted code).  Do not add a lock around this path —
        // asyncSignalHandler runs in async-signal context and taking
        // a mutex there is undefined behaviour.
        if (g_wakePipe[0] >= 0) {
                ::close(g_wakePipe[0]);
                g_wakePipe[0] = -1;
        }
        if (g_wakePipe[1] >= 0) {
                ::close(g_wakePipe[1]);
                g_wakePipe[1] = -1;
        }
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        SetConsoleCtrlHandler(consoleCtrlHandler, FALSE);
#endif

        g_installed.store(false, std::memory_order_release);
        g_deliveryCount.store(0, std::memory_order_release);
        promekiDebug("SignalHandler: uninstalled");
}

bool SignalHandler::isInstalled() {
        return g_installed.load(std::memory_order_acquire);
}

int SignalHandler::subscribe(int signo, Callback cb) {
#if defined(PROMEKI_PLATFORM_POSIX)
        if (!cb) {
                promekiWarn("SignalHandler::subscribe: empty callback");
                return -1;
        }
        if (isTerminationSignal(signo)) {
                promekiWarn("SignalHandler::subscribe: signal %d is reserved "
                            "for termination handling",
                            signo);
                return -1;
        }
        if (!g_installed.load(std::memory_order_acquire)) {
                promekiWarn("SignalHandler::subscribe: handler not installed — "
                            "call install() first");
                return -1;
        }
        const int     handle = g_nextHandle.fetch_add(1, std::memory_order_relaxed);
        Mutex::Locker lock(g_subscribersMutex);
        if (!customSignalAlreadyInstalled(signo)) {
                if (!installCustomSignalLocked(signo)) return -1;
        }
        Subscriber sub{handle, signo, std::move(cb)};
        g_subscribers.pushToBack(std::move(sub));
        return handle;
#else
        (void)signo;
        (void)cb;
        promekiWarn("SignalHandler::subscribe: not implemented on this platform");
        return -1;
#endif
}

void SignalHandler::unsubscribe(int handle) {
#if defined(PROMEKI_PLATFORM_POSIX)
        if (handle < 0) return;
        Mutex::Locker lock(g_subscribersMutex);
        g_subscribers.removeIf([handle](const Subscriber &s) { return s.handle == handle; });
#else
        (void)handle;
#endif
}

PROMEKI_NAMESPACE_END
