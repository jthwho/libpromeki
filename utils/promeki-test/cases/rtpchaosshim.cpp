/**
 * @file      cases/rtpchaosshim.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "rtpchaosshim.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>

#include <poll.h>

#include <promeki/error.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                // Generous MTU upper bound — plain Ethernet is 1500, jumbo
                // capped at 9000.  RTP loopback never exceeds 1500 in
                // practice but headroom keeps the recv buffer from
                // truncating an unexpected jumbo.
                constexpr size_t kRecvBufBytes = 9216;

                // RFC 3550 §5.1 — the SSRC field lives at byte offset 8 in
                // every RTP header, regardless of CSRC count or extensions.
                constexpr size_t kRtpSsrcOffset = 8;
                constexpr size_t kRtpHeaderMin  = 12;

                // Sample an exponential delay capped at maxMs.  The mean is
                // maxMs / 4 so most packets arrive promptly while the tail
                // exercises the reorder buffer's playoutDelay path.
                int sampleLateMs(std::mt19937_64 &rng, int maxMs) {
                        if (maxMs <= 0) return 0;
                        std::exponential_distribution<double> dist(4.0 / static_cast<double>(maxMs));
                        double                                v = dist(rng);
                        if (v > static_cast<double>(maxMs)) v = static_cast<double>(maxMs);
                        if (v < 0.0) v = 0.0;
                        return static_cast<int>(v);
                }

                // Heap-free RNG floor for uniform [0, 1).
                double sampleUniform(std::mt19937_64 &rng) {
                        std::uniform_real_distribution<double> dist(0.0, 1.0);
                        return dist(rng);
                }

        } // namespace

        // -------------------------------------------------------------------
        // Lifecycle
        // -------------------------------------------------------------------

        RtpChaosShim::~RtpChaosShim() {
                stop();
                for (size_t i = 0; i < _endpoints.size(); ++i) {
                        delete _endpoints[i]->socket;
                        delete _endpoints[i];
                }
                _endpoints.clear();
        }

        Error RtpChaosShim::addRelay(const SocketAddress &listen, const SocketAddress &forward, bool isRtcp) {
                if (_started) return Error::Invalid;
                Endpoint *ep = new Endpoint();
                ep->listen   = listen;
                ep->forward  = forward;
                ep->isRtcp   = isRtcp;
                ep->socket   = new UdpSocket();
                Error err = ep->socket->open(IODevice::ReadWrite);
                if (err.isError()) {
                        delete ep->socket;
                        delete ep;
                        return err;
                }
                err = ep->socket->bind(listen);
                if (err.isError()) {
                        ep->socket->close();
                        delete ep->socket;
                        delete ep;
                        return err;
                }
                ep->rng.seed(_cfg.seed ^ static_cast<uint64_t>(_endpoints.size() + 1));
                _endpoints.pushToBack(ep);
                return Error::Ok;
        }

        Error RtpChaosShim::start() {
                if (_started) return Error::Invalid;
                _stop.store(false);
                _started = true;
                for (size_t i = 0; i < _endpoints.size(); ++i) {
                        Endpoint *ep = _endpoints[i];
                        ep->thread = std::thread([this, ep]() { runEndpoint(*ep); });
                }
                return Error::Ok;
        }

        void RtpChaosShim::stop() {
                if (!_started) return;
                _stop.store(true);
                for (size_t i = 0; i < _endpoints.size(); ++i) {
                        Endpoint *ep = _endpoints[i];
                        if (ep->thread.joinable()) ep->thread.join();
                }
                _started = false;
        }

        // -------------------------------------------------------------------
        // Worker loop
        // -------------------------------------------------------------------

        void RtpChaosShim::runEndpoint(Endpoint &ep) {
                uint8_t recvBuf[kRecvBufBytes];

                while (!_stop.load(std::memory_order_acquire)) {
                        drainDueFromDelayQueue(ep);

                        // Block on recv with a deadline computed from the
                        // delay queue (or 50 ms default so the stop flag is
                        // responsive).  poll() lets us pick whichever
                        // happens first: data arrives, deadline expires,
                        // or stop is signalled.
                        struct pollfd pfd;
                        pfd.fd      = ep.socket->socketDescriptor();
                        pfd.events  = POLLIN;
                        pfd.revents = 0;
                        int timeoutMs = pollTimeoutMs(ep);
                        int rc        = ::poll(&pfd, 1, timeoutMs);
                        if (rc < 0) {
                                if (errno == EINTR) continue;
                                promekiWarn("RtpChaosShim: poll failed: %s", std::strerror(errno));
                                continue;
                        }
                        if (rc == 0) continue;
                        if ((pfd.revents & POLLIN) == 0) continue;

                        SocketAddress sender;
                        int64_t       n = ep.socket->readDatagram(recvBuf, sizeof(recvBuf), &sender);
                        if (n <= 0) continue;

                        _counters.received.fetch_add(1, std::memory_order_relaxed);

                        Buffer pkt(static_cast<size_t>(n));
                        pkt.copyFrom(recvBuf, static_cast<size_t>(n));
                        pkt.setSize(static_cast<size_t>(n));

                        processIncoming(ep, std::move(pkt));
                }

                // Final flush: forward whatever is still in the held
                // reorder window, drop everything in the delay queue (the
                // round-trip is tearing down anyway).
                releaseReorderHold(ep);
                ep.delayQueue.clear();
        }

        // -------------------------------------------------------------------
        // Per-packet decision tree
        // -------------------------------------------------------------------

        void RtpChaosShim::processIncoming(Endpoint &ep, Buffer bytes) {
                Mode mode = modeFor(ep);
                switch (mode) {
                        case Mode::None:
                                sendNow(ep, bytes);
                                return;

                        case Mode::RtcpBlocked:
                                _counters.dropped.fetch_add(1, std::memory_order_relaxed);
                                return;

                        case Mode::Loss:
                                if (sampleUniform(ep.rng) < _cfg.rate) {
                                        _counters.dropped.fetch_add(1, std::memory_order_relaxed);
                                        return;
                                }
                                sendNow(ep, bytes);
                                return;

                        case Mode::Dup:
                                sendNow(ep, bytes);
                                if (sampleUniform(ep.rng) < _cfg.rate) {
                                        _counters.duplicated.fetch_add(1, std::memory_order_relaxed);
                                        sendNow(ep, bytes);
                                }
                                return;

                        case Mode::Late: {
                                int       delayMs = sampleLateMs(ep.rng, _cfg.maxLateMs);
                                TimeStamp emitAt  = TimeStamp::now();
                                emitAt += std::chrono::milliseconds(delayMs);
                                _counters.delayed.fetch_add(1, std::memory_order_relaxed);
                                scheduleAt(ep, std::move(bytes), emitAt);
                                return;
                        }

                        case Mode::Reorder: {
                                ep.reorderHold.pushToBack({std::move(bytes), TimeStamp::now()});
                                if (ep.reorderHold.size() >= _cfg.reorderWindow) {
                                        releaseReorderHold(ep);
                                }
                                return;
                        }

                        case Mode::SsrcChange: {
                                // RTP only; RTCP packets carry their own
                                // sender SSRC field at a different offset
                                // and aren't worth mutating for this case.
                                if (!ep.isRtcp) {
                                        ep.rtpPacketCount += 1;
                                        if (ep.rtpPacketCount > _cfg.ssrcChangeAfter) {
                                                mutateSsrc(bytes);
                                                _counters.ssrcMutated.fetch_add(1, std::memory_order_relaxed);
                                        }
                                }
                                sendNow(ep, bytes);
                                return;
                        }
                }
        }

        // -------------------------------------------------------------------
        // Reorder + Late helpers
        // -------------------------------------------------------------------

        void RtpChaosShim::releaseReorderHold(Endpoint &ep) {
                if (ep.reorderHold.isEmpty()) return;

                // Shuffle the held window in place using the endpoint's
                // RNG, then schedule each at "now" so they queue up in
                // shuffled order while still going through the delay
                // queue path (keeps emission sequencing identical to
                // Late mode and avoids a separate code path).
                std::vector<size_t> idx(ep.reorderHold.size());
                for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
                std::shuffle(idx.begin(), idx.end(), ep.rng);

                TimeStamp now = TimeStamp::now();
                for (size_t pos = 0; pos < idx.size(); ++pos) {
                        size_t  src = idx[pos];
                        Pending p   = std::move(ep.reorderHold[src]);
                        if (src != pos) {
                                _counters.reordered.fetch_add(1, std::memory_order_relaxed);
                        }
                        scheduleAt(ep, std::move(p.bytes), now);
                }
                ep.reorderHold.clear();
        }

        void RtpChaosShim::scheduleAt(Endpoint &ep, Buffer bytes, TimeStamp emitAt) {
                Pending p{std::move(bytes), emitAt};
                // Insert in order — small-N, linear scan is fine.
                size_t pos = 0;
                while (pos < ep.delayQueue.size() &&
                       ep.delayQueue[pos].emitAt.value() <= p.emitAt.value()) {
                        ++pos;
                }
                ep.delayQueue.insert(pos, std::move(p));
        }

        void RtpChaosShim::drainDueFromDelayQueue(Endpoint &ep) {
                TimeStamp now = TimeStamp::now();
                while (!ep.delayQueue.isEmpty()) {
                        Pending &front = ep.delayQueue.front();
                        if (front.emitAt.value() > now.value()) break;
                        Buffer bytes = std::move(front.bytes);
                        ep.delayQueue.remove(0);
                        sendNow(ep, bytes);
                }
        }

        int RtpChaosShim::pollTimeoutMs(const Endpoint &ep) const {
                // Default cadence keeps the stop flag responsive.
                int defaultMs = 50;
                if (ep.delayQueue.isEmpty()) return defaultMs;

                TimeStamp now      = TimeStamp::now();
                TimeStamp deadline = ep.delayQueue.front().emitAt;
                if (deadline.value() <= now.value()) return 0;

                auto    delta = deadline.value() - now.value();
                int64_t ms    = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
                if (ms < 0) ms = 0;
                if (ms > defaultMs) ms = defaultMs;
                return static_cast<int>(ms);
        }

        // -------------------------------------------------------------------
        // Send + SSRC mutation
        // -------------------------------------------------------------------

        void RtpChaosShim::sendNow(Endpoint &ep, const Buffer &bytes) {
                int64_t n = ep.socket->writeDatagram(bytes.data(), bytes.size(), ep.forward);
                if (n < 0) {
                        promekiWarn("RtpChaosShim: writeDatagram failed: %s", std::strerror(errno));
                        return;
                }
                _counters.forwarded.fetch_add(1, std::memory_order_relaxed);
        }

        void RtpChaosShim::mutateSsrc(Buffer &bytes) const {
                if (bytes.size() < kRtpHeaderMin) return;
                uint8_t *p = static_cast<uint8_t *>(bytes.data());
                // Network byte order — write the new SSRC big-endian.
                p[kRtpSsrcOffset + 0] = static_cast<uint8_t>((_cfg.newSsrc >> 24) & 0xFF);
                p[kRtpSsrcOffset + 1] = static_cast<uint8_t>((_cfg.newSsrc >> 16) & 0xFF);
                p[kRtpSsrcOffset + 2] = static_cast<uint8_t>((_cfg.newSsrc >> 8) & 0xFF);
                p[kRtpSsrcOffset + 3] = static_cast<uint8_t>((_cfg.newSsrc >> 0) & 0xFF);
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
