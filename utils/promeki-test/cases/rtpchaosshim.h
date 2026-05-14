/**
 * @file      cases/rtpchaosshim.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Loopback UDP relay used by the @c rtp.chaos.* matrix to exercise the
 * receiver-correctness machinery (@c RtpSeqTracker /
 * @c RtpSeqReorderBuffer / SSRC-pin debounce / wire-silence watchdog /
 * stream-anchor captureTime fallback) under controlled adversity.
 *
 * Each @ref RtpChaosShim instance owns one or more relay endpoints.
 * An endpoint binds a UDP socket on @c listen, forwards every received
 * datagram to @c forward, and applies the configured chaos
 * transformation along the way.  The TX side of the round-trip points
 * its @c VideoRtpDestination / @c AudioRtpDestination at the shim's
 * @c listen ports; the RX side binds on the @c forward ports.  The
 * shim sits between them and mangles RTP / RTCP traffic in flight.
 *
 * The shim is one-shot: configure with @ref setConfig, register
 * endpoints with @ref addRelay, then @ref start.  @ref stop joins the
 * worker threads; the destructor calls it implicitly.
 *
 * Thread topology: one worker @c std::thread per endpoint.  Each
 * worker uses @c poll() on the socket fd with a deadline so the same
 * loop services both incoming traffic and time-deferred emissions
 * (used by @ref Mode::Late for per-packet delays and @ref Mode::Reorder
 * for held-window release).  No cross-endpoint synchronisation;
 * counters are atomic.
 */

#pragma once

#include <promeki/atomic.h>
#include <thread>

#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/random.h>
#include <promeki/socketaddress.h>
#include <promeki/timestamp.h>
#include <promeki/udpsocket.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        /**
         * @brief Bidirectional UDP relay that injects chaos into the RTP / RTCP
         *        loopback path used by the @c rtp.chaos.* matrix.
         *
         * The shim relays one direction (TX → RX) only.  The RX side's
         * outbound RTCP receiver reports go to the TX side's RTCP
         * destination directly, bypassing the shim — which is fine
         * because @c rtp.chaos.* asserts on RX-side behaviour, not on
         * the TX RR-receive path.
         */
        class RtpChaosShim {
                public:
                        /**
                         * @brief Selects the chaos transformation applied to packets on
                         *        a given endpoint.
                         *
                         * The shim distinguishes RTP-port and RTCP-port modes via
                         * @ref Config::rtpMode and @ref Config::rtcpMode so a single
                         * test case can (for example) drop loss into the RTP stream
                         * while leaving RTCP intact, or zero out RTCP for the
                         * @c rtp.chaos.rtcpblocked case while keeping RTP whole.
                         */
                        enum class Mode {
                                None,        ///< @brief Forward verbatim, no chaos.
                                Loss,        ///< @brief Drop @ref Config::rate fraction of packets uniformly.
                                Reorder,     ///< @brief Hold @ref Config::reorderWindow packets, release in random order.
                                Dup,         ///< @brief Duplicate @ref Config::rate fraction of packets.
                                Late,        ///< @brief Defer each packet by an exponential delay capped at @ref Config::maxLateMs.
                                SsrcChange,  ///< @brief Mutate the wire SSRC after @ref Config::ssrcChangeAfter packets (RTP only).
                                RtcpBlocked, ///< @brief Drop every packet (used for the RTCP port in the @c rtcpblocked case).
                        };

                        /**
                         * @brief Per-shim configuration.
                         *
                         * @ref rtpMode applies to endpoints registered with
                         * @c isRtcp = false; @ref rtcpMode applies to endpoints
                         * registered with @c isRtcp = true.  Numeric parameters are
                         * shared — only one chaos mode is exercised per test case so
                         * there is no need for per-mode parameter blocks.
                         */
                        struct Config {
                                Mode     rtpMode  = Mode::None;  ///< @brief Chaos mode for RTP endpoints.
                                Mode     rtcpMode = Mode::None;  ///< @brief Chaos mode for RTCP endpoints.

                                double   rate = 0.0;             ///< @brief Loss / Dup probability in @c [0, 1).
                                size_t   reorderWindow = 0;      ///< @brief Reorder hold-window size in packets.
                                int      maxLateMs = 0;          ///< @brief Late mode upper bound on per-packet delay (milliseconds).
                                size_t   ssrcChangeAfter = 0;    ///< @brief Number of RTP packets to forward before mutating SSRC.
                                uint32_t newSsrc = 0;            ///< @brief Replacement SSRC value for @ref Mode::SsrcChange.

                                /**
                                 * @brief Deterministic seed for the chaos RNG.
                                 *
                                 * Each endpoint gets its own engine seeded as
                                 * @c seed XOR endpointIndex so reorder / loss / dup
                                 * decisions stay reproducible run-to-run while still
                                 * differing between endpoints.
                                 */
                                uint64_t seed = 0xCAFEBABEull;
                        };

                        /**
                         * @brief Atomic counters published while the shim runs.
                         *
                         * Tests assert on these to verify the shim actually injected
                         * the configured chaos.  Counters survive @ref stop so the
                         * test thread can read them after the shim has joined.
                         */
                        struct Counters {
                                Atomic<uint64_t> received{0};   ///< @brief Total datagrams received across every endpoint.
                                Atomic<uint64_t> forwarded{0};  ///< @brief Datagrams forwarded (after chaos drops + dup).
                                Atomic<uint64_t> dropped{0};    ///< @brief Datagrams dropped by Loss / RtcpBlocked.
                                Atomic<uint64_t> duplicated{0}; ///< @brief Datagrams the shim duplicated (Dup mode).
                                Atomic<uint64_t> reordered{0};  ///< @brief Datagrams whose emit position changed under Reorder.
                                Atomic<uint64_t> delayed{0};    ///< @brief Datagrams Late mode held past their arrival time.
                                Atomic<uint64_t> ssrcMutated{0}; ///< @brief RTP datagrams whose SSRC field was rewritten.
                        };

                        RtpChaosShim() = default;

                        /** @brief Joins worker threads if still running. */
                        ~RtpChaosShim();

                        // No copy / move — the worker threads hold raw pointers
                        // into the endpoint list, so its addresses must not move.
                        RtpChaosShim(const RtpChaosShim &) = delete;
                        RtpChaosShim &operator=(const RtpChaosShim &) = delete;

                        /** @brief Sets the chaos configuration; must be called before @ref start. */
                        void setConfig(const Config &cfg) { _cfg = cfg; }

                        /**
                         * @brief Registers a relay endpoint.
                         *
                         * Binds @c listen and forwards each received datagram to
                         * @c forward.  Must be called before @ref start.  Returns an
                         * @c Error if the bind fails.
                         *
                         * @param listen  Local address the shim binds to.
                         * @param forward Destination address packets are forwarded to.
                         * @param isRtcp  True if this endpoint relays RTCP traffic
                         *                (selects @ref Config::rtcpMode); false for RTP.
                         * @return Error::Ok on success.
                         */
                        Error addRelay(const SocketAddress &listen, const SocketAddress &forward, bool isRtcp);

                        /**
                         * @brief Spawns one worker thread per registered endpoint.
                         *
                         * The shim is one-shot: @ref start may be called once.  After
                         * @ref stop, a fresh @ref RtpChaosShim instance is required.
                         *
                         * @return Error::Ok on success.
                         */
                        Error start();

                        /**
                         * @brief Signals every worker to exit and joins them.
                         *
                         * Safe to call multiple times.  Called from the destructor.
                         */
                        void stop();

                        /** @brief Returns the live counter snapshot accessor. */
                        const Counters &counters() const { return _counters; }

                private:
                        /**
                         * @brief A datagram queued for time-deferred emission.
                         *
                         * Used by @ref Mode::Late (per-packet delay) and the post-hold
                         * release path of @ref Mode::Reorder.  Stored in the
                         * endpoint's @c delayQueue (sorted by @c emitAt) and drained
                         * by the same worker thread that received it.
                         */
                        struct Pending {
                                Buffer    bytes;   ///< @brief Datagram bytes to forward.
                                TimeStamp emitAt;  ///< @brief Scheduled emission deadline.
                        };

                        /**
                         * @brief One relay endpoint's runtime state.
                         *
                         * Heap-allocated and owned through @c _endpoints so the
                         * worker thread can hold a stable pointer into it across
                         * the shim's lifetime.
                         */
                        struct Endpoint {
                                SocketAddress    listen;        ///< @brief Local bind address.
                                SocketAddress    forward;       ///< @brief Forwarding destination.
                                bool             isRtcp = false;///< @brief True for RTCP, false for RTP.
                                UdpSocket       *socket = nullptr; ///< @brief Bound socket (heap-owned).
                                std::thread      thread;        ///< @brief Worker thread.
                                Random           rng;           ///< @brief Per-endpoint chaos RNG.
                                size_t           rtpPacketCount = 0; ///< @brief RTP-only running count for SsrcChange.
                                List<Pending>    reorderHold;   ///< @brief Held window for @ref Mode::Reorder.
                                List<Pending>    delayQueue;    ///< @brief Scheduled emissions sorted by emitAt.
                        };

                        Mode modeFor(const Endpoint &ep) const {
                                return ep.isRtcp ? _cfg.rtcpMode : _cfg.rtpMode;
                        }

                        // Worker entry point.  Each endpoint thread runs one
                        // instance of this loop.
                        void runEndpoint(Endpoint &ep);

                        // Scheduling helpers used inside the worker loop.
                        void processIncoming(Endpoint &ep, Buffer bytes);
                        void scheduleAt(Endpoint &ep, Buffer bytes, TimeStamp emitAt);
                        void sendNow(Endpoint &ep, const Buffer &bytes);
                        void drainDueFromDelayQueue(Endpoint &ep);
                        int  pollTimeoutMs(const Endpoint &ep) const;
                        void releaseReorderHold(Endpoint &ep);
                        void mutateSsrc(Buffer &bytes) const;

                        Config             _cfg;
                        List<Endpoint *>   _endpoints;       // heap-owned; freed in stop().
                        Counters           _counters;
                        Atomic<bool>  _stop{false};
                        bool               _started = false;
        };

} // namespace promekitest

PROMEKI_NAMESPACE_END
