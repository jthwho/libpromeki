/**
 * @file      srtepoll.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_SRT
#include <promeki/function.h>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/uniqueptr.h>

#include <atomic>
#include <functional>

PROMEKI_NAMESPACE_BEGIN

class SrtSocket;
class SrtServer;

/**
 * @brief I/O multiplexer for SRT sockets.
 * @ingroup network
 *
 * libsrt's @c SRTSOCKET handles are not real file descriptors, so
 * the kernel @c epoll / @c poll / @c select APIs cannot wait on
 * them.  SRT ships its own multiplexer (@c srt_epoll_*) that
 * tracks SRTSOCKETs internally.  SrtEpoll is a thin RAII C++
 * wrapper around it.
 *
 * SrtEpoll is intentionally not coupled to @ref EventLoop — the
 * project's main EventLoop is built around kernel fds and an
 * SRT-specific multiplexer is a separate concern.  Callers who
 * want EventLoop integration can either:
 *
 *  - drive @ref wait from a worker thread that signals the
 *    main loop via an @c eventfd / pipe / Promeki signal, or
 *  - register a recurring timer in their EventLoop that calls
 *    @ref wait with a short non-blocking timeout.
 *
 * @par Thread safety
 * SrtEpoll is *not* thread-safe; one instance must only be used
 * from one thread at a time.  Different SrtEpoll instances are
 * independent and may be used concurrently from different threads.
 *
 * @par Two dispatch styles
 *
 *  - @b Pull: call @ref wait, iterate the returned list, decide
 *    yourself which socket to read/write/accept.  Lowest-level
 *    surface — every detail is in the caller's hands.
 *  - @b Push: register a per-socket callback via @ref setCallback,
 *    then either call @ref dispatchOnce from your own event loop
 *    or @ref run on a dedicated thread.  The callback is invoked
 *    with the readiness bitmask whenever its socket fires.
 *
 * @par Example (push style with a worker thread)
 * @code
 * SrtEpoll mux;
 * mux.add(server, SrtEpoll::ReadReady);
 * mux.setCallback(server, [&](int events) {
 *         if (events & SrtEpoll::ReadReady)
 *                 handleAccept(server);
 * });
 * std::thread worker([&] { mux.run(); });
 * // ... later ...
 * mux.stop();
 * worker.join();
 * @endcode
 *
 * @par Example (pull style)
 * @code
 * SrtEpoll mux;
 * mux.add(serverA, SrtEpoll::ReadReady);
 * mux.add(socketB, SrtEpoll::ReadReady | SrtEpoll::WriteReady);
 * SrtEpoll::ReadyList ready;
 * while (mux.wait(ready, 100) > 0) {
 *         for (const auto &r : ready) { ... }
 * }
 * @endcode
 */
class SrtEpoll {
        public:
                /** @brief Unique-ownership pointer to a SrtEpoll. */
                using UPtr = UniquePtr<SrtEpoll>;

                /** @brief Sentinel for an uninitialized SRT epoll group. */
                static constexpr int InvalidId = -1;

                /** @brief Subscription / readiness event flags. */
                enum Event {
                        ReadReady  = 0x01, ///< Read-side data ready (also accept-ready on listeners).
                        WriteReady = 0x02, ///< Write-side has buffer space (or connect completed).
                        ErrorEvent = 0x04  ///< Connection broke / fatal error.
                };

                /** @brief One ready entry returned by @ref wait. */
                struct Ready {
                                /** @brief @c SRTSOCKET handle that became ready. */
                                int handle = 0;
                                /** @brief Bitwise OR of @ref Event flags. */
                                int events = 0;
                };

                /** @brief List of ready entries. */
                using ReadyList = ::promeki::List<Ready>;

                /**
                 * @brief Per-socket callback for the push-dispatch APIs.
                 *
                 * Receives the bitmask of @ref Event flags that fired
                 * on the socket.  Runs on whichever thread is driving
                 * @ref dispatchOnce or @ref run — most callers want
                 * to forward the work to their own thread via
                 * Promeki signals.
                 */
                using ReadyCallback = Function<void(int events)>;

                /** @brief Constructs an unopened SrtEpoll group. */
                SrtEpoll();

                /** @brief Destructor.  Releases the SRT epoll group if open. */
                ~SrtEpoll();

                SrtEpoll(const SrtEpoll &) = delete;
                SrtEpoll &operator=(const SrtEpoll &) = delete;

                /**
                 * @brief Lazily creates the underlying SRT epoll group.
                 *
                 * Called automatically by the first @ref add.  Useful
                 * to call eagerly so any failure surfaces before the
                 * caller starts wiring up sockets.
                 *
                 * @return @ref Error::Ok on success, or
                 *         @ref Error::LibraryFailure if SRT refused.
                 */
                Error open();

                /** @brief Releases the SRT epoll group. */
                void close();

                /** @brief Returns true while the group is open. */
                bool isOpen() const { return _eid != InvalidId; }

                /**
                 * @brief Adds a connected SrtSocket to the multiplexer.
                 *
                 * @param sock   The socket — must already be open.
                 * @param events Bitwise OR of @ref Event flags.
                 * @return @ref Error::Ok or @ref Error::LibraryFailure.
                 */
                Error add(SrtSocket &sock, int events);

                /**
                 * @brief Adds a listening SrtServer to the multiplexer.
                 *
                 * Pass @ref ReadReady to wait for accept-readiness
                 * (callers should call @ref SrtServer::accept when
                 * woken).  @ref ErrorEvent is also useful.
                 *
                 * @param server The server — must already be listening.
                 * @param events Bitwise OR of @ref Event flags.
                 * @return @ref Error::Ok or @ref Error::LibraryFailure.
                 */
                Error add(SrtServer &server, int events);

                /**
                 * @brief Updates the event mask for a previously-added socket.
                 * @param sock   The socket.
                 * @param events New event mask.
                 * @return @ref Error::Ok or @ref Error::LibraryFailure.
                 */
                Error modify(SrtSocket &sock, int events);

                /**
                 * @brief Removes a socket from the multiplexer.
                 * @param sock The socket.
                 * @return @ref Error::Ok or @ref Error::LibraryFailure.
                 */
                Error remove(SrtSocket &sock);

                /** @copydoc remove(SrtSocket &) */
                Error remove(SrtServer &server);

                /**
                 * @brief Waits up to @p timeoutMs for ready events.
                 *
                 * @param[out] ready Populated with one entry per ready
                 *                    socket.  Cleared on entry.
                 * @param      timeoutMs Wait budget in milliseconds.
                 *                       @c 0 = poll (return immediately),
                 *                       @c -1 = block forever.
                 * @return The number of ready entries (0 = timeout),
                 *         or -1 on error.
                 */
                int wait(ReadyList &ready, int timeoutMs);

                /**
                 * @brief Returns the underlying SRT epoll-group id.
                 *
                 * Exposed so callers that need to call additional
                 * @c srt_epoll_* APIs not surfaced here can do so.
                 */
                int handle() const { return _eid; }

                /**
                 * @brief Registers a per-socket readiness callback.
                 *
                 * The socket must already have been registered via
                 * @ref add.  When an event fires on it during a
                 * @ref dispatchOnce or @ref run cycle, @p cb is
                 * invoked synchronously with the bitmask of
                 * @ref Event flags that triggered.  Pass an empty
                 * @c std::function to clear a previous callback.
                 *
                 * @param sock The socket — must be open and added.
                 * @param cb   The callback (or empty to clear).
                 * @return @ref Error::Ok on success.
                 */
                Error setCallback(SrtSocket &sock, ReadyCallback cb);

                /** @copydoc setCallback(SrtSocket&, ReadyCallback) */
                Error setCallback(SrtServer &server, ReadyCallback cb);

                /**
                 * @brief Polls once and dispatches registered callbacks.
                 *
                 * Equivalent to calling @ref wait followed by a loop
                 * that invokes the registered @ref ReadyCallback for
                 * each ready socket.  Sockets without a registered
                 * callback are silently skipped — the caller can
                 * still mix-and-match push and pull (the pull form
                 * via @ref wait would surface them explicitly).
                 *
                 * Safe to call from any thread; intended for callers
                 * that pump SRT alongside their own event loop.
                 *
                 * @param timeoutMs Wait budget in ms (0 = poll, -1 = block).
                 * @return The number of callbacks invoked, or -1 on error.
                 */
                int dispatchOnce(int timeoutMs);

                /**
                 * @brief Runs @ref dispatchOnce in a loop until @ref stop.
                 *
                 * Designed for a dedicated worker thread.  Blocks on
                 * @c srt_epoll_uwait with a 100 ms timeout so a
                 * concurrent @ref stop is observed promptly without
                 * needing to inject an artificial wakeup event.
                 *
                 * @return Total callbacks invoked across all cycles.
                 */
                int run();

                /**
                 * @brief Asks an in-progress @ref run to exit.
                 *
                 * Thread-safe.  After @ref run returns, @ref stop is
                 * automatically reset so subsequent calls to
                 * @ref run start fresh.
                 */
                void stop();

        private:
                int                              _eid = InvalidId;
                Map<int, ReadyCallback>          _callbacks;
                std::atomic<bool>                _stopRequested{false};
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_SRT
