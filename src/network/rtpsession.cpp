/**
 * @file      rtpsession.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpsession.h>
#include <algorithm>
#include <chrono>
#include <promeki/logger.h>
#include <promeki/packettransport.h>
#include <promeki/random.h>
#include <promeki/rtcppacket.h>
#include <promeki/rtpseqreorderbuffer.h>
#include <promeki/rtpseqtracker.h>
#include <promeki/rtpstreamclock.h>
#include <promeki/thread.h>
#include <promeki/udpsocket.h>
#include <promeki/udpsockettransport.h>
#include <promeki/timestamp.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

// ----------------------------------------------------------------------------
// ReceiveThread — internal receive loop.
//
// Declared inside the RtpSession class (as a private nested type via
// a forward-friend) so that the thread can reach into the session's
// private members without exposing them further.  The thread runs a
// blocking receivePacket() loop on the session's PacketTransport,
// wraps each datagram in an RtpPacket view of an owned Buffer,
// demuxes RTCP via byte[1] in [200..223], and dispatches RTP packets
// through the per-stream @c StreamReceiver entries: SSRC pin / change
// detection → @ref RtpSeqTracker observe → @ref RtpSeqReorderBuffer
// insert → post-reorder @c RtpPacket::Queue.  The matching depacketizer
// thread consumes the queue on its own thread.  The receive socket's
// SO_RCVTIMEO is used (for UDP transports) to poll the stop flag
// between datagrams.
// ----------------------------------------------------------------------------
class RtpSession::ReceiveThread : public Thread {
        public:
                ReceiveThread(RtpSession *session, PacketTransport *transport, const String &name)
                    : _session(session), _transport(transport) {
                        _stopRequested.setValue(false);
                        Thread::setName(name);
                }

                ~ReceiveThread() override {
                        requestStop();
                        // Wait for the worker to fully exit
                        // threadEntry() before we hit the vtable
                        // slice from ~ReceiveThread → ~Thread.  The
                        // worker is still inside the user-overridden
                        // run() loop until it observes _stopRequested,
                        // and any access to *this it makes against a
                        // stale (sliced) vtable is UB.  Skip the wait
                        // when the destructor is being driven from
                        // the worker itself (pathological self-delete
                        // path) — joining ourselves would deadlock.
                        if (!isCurrentThread()) wait();
                }

                void requestStop() { _stopRequested.setValue(true); }

        protected:
                void run() override {
                        // Figure out the largest datagram we care
                        // about — 2 KiB covers every common RTP wire
                        // format (MTU-safe payload + RTP header).
                        constexpr size_t kMaxPacketSize = 2048;

                        // If the transport is UDP-backed, set
                        // SO_RCVTIMEO on the underlying socket so we
                        // wake up periodically to check the stop
                        // flag even on an idle stream.  Non-UDP
                        // transports (e.g. Loopback) are left as-is
                        // and rely on their own timeout semantics.
                        if (auto *udpTransport = dynamic_cast<UdpSocketTransport *>(_transport)) {
                                if (UdpSocket *sock = udpTransport->socket()) {
                                        (void)sock->setReceiveTimeout(_session->_receivePollMs);
                                }
                        }

                        while (!_stopRequested.value()) {
                                Buffer  buf = Buffer(kMaxPacketSize);
                                ssize_t n = _transport->receivePacket(buf.data(), kMaxPacketSize);
                                // Stamp the per-packet arrival anchor as
                                // close to @c receivePacket return as
                                // possible, before any further work
                                // adds scheduling jitter to subsequent
                                // @c TimeStamp::now() calls.  The
                                // post-Phase-2 path uses this for
                                // RFC 3550 §A.8 interarrival jitter and
                                // for stream-anchor @c captureTime
                                // interpolation in the depacketizer
                                // thread.
                                const TimeStamp arrivalSteady = TimeStamp::now();
                                if (n <= 0) {
                                        // Timeout (EAGAIN on UDP),
                                        // transient error, or empty
                                        // datagram — poll again.
                                        continue;
                                }
                                if (n < 4) {
                                        // Too short to even hold an
                                        // RTCP common header.  Drop.
                                        promekiWarnThrottled(5000,
                                                             "RtpSession: dropping runt datagram (%zd bytes < 4)",
                                                             n);
                                        continue;
                                }
                                buf.setSize(static_cast<size_t>(n));

                                // RTCP / RTP demux.  We advertise rtcp-
                                // mux in SDP so the same socket carries
                                // both protocols.  RTP packet types are
                                // 0..127 (byte 1 is M | PT, with M
                                // optionally setting bit 7); RTCP
                                // packet types are 200..223.  The
                                // 192..199 / 224..255 ranges are
                                // reserved.  Distinguishing on the
                                // 200..223 RTCP range is unambiguous
                                // and what RFC 5761 §4 prescribes.
                                const uint8_t pt = static_cast<const uint8_t *>(buf.data())[1];
                                if (pt >= 200u && pt <= 223u) {
                                        _session->handleRtcp(static_cast<const uint8_t *>(buf.data()),
                                                             static_cast<size_t>(n));
                                        continue;
                                }

                                if (static_cast<size_t>(n) < RtpPacket::HeaderSize) {
                                        // Too short to contain a
                                        // fixed RTP header; drop.
                                        promekiWarnThrottled(2000,
                                                             "RtpSession: dropping short RTP datagram (%zd < %zu)",
                                                             n, RtpPacket::HeaderSize);
                                        continue;
                                }
                                RtpPacket pkt(buf, 0, static_cast<size_t>(n));
                                if (!pkt.isValid()) {
                                        // Not a valid RTP packet
                                        // (wrong version, truncated
                                        // extension, etc.) — drop.
                                        const uint8_t *bd = static_cast<const uint8_t *>(buf.data());
                                        promekiWarnThrottled(2000,
                                                             "RtpSession: dropping invalid RTP packet (size=%zd "
                                                             "bytes=%02x %02x %02x %02x ...)",
                                                             n, static_cast<unsigned>(bd[0]),
                                                             static_cast<unsigned>(bd[1]),
                                                             static_cast<unsigned>(bd[2]),
                                                             static_cast<unsigned>(bd[3]));
                                        continue;
                                }
                                pkt.arrivalSteady = arrivalSteady;

                                // Dispatch through the per-stream
                                // queue-mode receivers.  An empty
                                // list is rejected at
                                // @ref startReceiving time so the
                                // recv loop never runs without one.
                                dispatchToReceivers(pkt);
                                _session->packetReceivedSignal.emit(buf, pkt.timestamp(), pkt.payloadType(),
                                                                    pkt.marker());
                        }
                }

        private:
                /**
                 * @brief Queue-mode dispatch for one parsed packet.
                 *
                 * Selects the receiver entry whose @c payloadType
                 * matches the packet's PT, runs the SSRC pin /
                 * change machinery, then updates the per-source
                 * @ref RtpSeqTracker and pushes through the
                 * receiver's @ref RtpSeqReorderBuffer into the
                 * post-reorder output queue.  Single-receiver
                 * sessions (today's case) get a one-entry list and
                 * the PT match is trivial.
                 */
                void dispatchToReceivers(RtpPacket &pkt) {
                        // ST 2022-7 dual-leg recv: when a secondary
                        // recv thread is running, both threads call
                        // here concurrently and must not race on the
                        // per-source seq tracker / SSRC pin /
                        // reorder buffer mutations.  The mutex is a
                        // no-op cost in single-leg mode (uncontended);
                        // in dual-leg mode it serialises ~50µs of
                        // per-packet work, which is well below the
                        // packet inter-arrival time for any wire
                        // format we ship today.
                        Mutex::Locker lock(_session->_dispatchMutex);
                        const uint8_t pt = pkt.payloadType();
                        const uint32_t ssrc = pkt.ssrc();
                        for (size_t i = 0; i < _session->_streamReceivers.size(); ++i) {
                                RtpSession::StreamReceiver &r = _session->_streamReceivers[i];
                                if (r.payloadType != pt) continue;
                                RtpSession::SsrcPinState &pin = _session->_ssrcPinStates[i];
                                if (!checkSsrcPin(pt, ssrc, pin, r, pkt.arrivalSteady)) {
                                        // SSRC mismatch is being debounced —
                                        // drop the packet without polluting
                                        // the seq tracker / reorder buffer.
                                        return;
                                }
                                if (r.seqTracker == nullptr || r.reorderBuffer == nullptr ||
                                    r.outQueue == nullptr) {
                                        // Receiver entry is malformed —
                                        // surface as a one-shot warning
                                        // and skip dispatch.  The session
                                        // keeps running so a partial
                                        // configuration error doesn't
                                        // wedge the recv thread.
                                        if (!_warnedMissing) {
                                                promekiWarn("RtpSession: StreamReceiver pt=%u "
                                                            "missing tracker / reorderBuffer / outQueue "
                                                            "— packet dropped",
                                                            static_cast<unsigned>(pt));
                                                _warnedMissing = true;
                                        }
                                        return;
                                }
                                auto obs = r.seqTracker->observe(pkt.sequenceNumber(),
                                                                  pkt.timestamp(),
                                                                  pkt.arrivalSteady);
                                if (obs.duplicate) {
                                        // Tracker flagged the packet as a
                                        // very-large-jump candidate; drop
                                        // before the reorder buffer.
                                        promekiWarnThrottled(2000,
                                                             "RtpSession: dropping duplicate/large-jump RTP "
                                                             "(pt=%u ssrc=%08x seq=%u)",
                                                             static_cast<unsigned>(pt), ssrc,
                                                             static_cast<unsigned>(pkt.sequenceNumber()));
                                        return;
                                }
                                r.reorderBuffer->insert(pkt, obs.extendedSeq, pkt.arrivalSteady,
                                                         *r.outQueue);
                                return;
                        }
                        // PT did not match any receiver — drop.  No
                        // log because pre-handshake / mismatched-PT
                        // packets are common on a multicast group.
                }

                /**
                 * @brief SSRC pin / debounced-change implementation.
                 *
                 * @return @c true if the recv thread should accept
                 *         the packet, @c false to drop it (because
                 *         the SSRC change has not yet been
                 *         confirmed).
                 *
                 * Single stray packets from a wrong SSRC are
                 * ignored — only a sustained mismatch (≥
                 * @c kSsrcDebounceCount distinct packets within
                 * @c kSsrcDebounceWindowMs) triggers a reset.
                 */
                bool checkSsrcPin(uint8_t pt, uint32_t ssrc, RtpSession::SsrcPinState &pin,
                                  RtpSession::StreamReceiver &r, const TimeStamp &arrivalSteady) {
                        if (!pin.pinned) {
                                pin.expectedSsrc = ssrc;
                                pin.pinned = true;
                                pin.mismatchCount = 0;
                                return true;
                        }
                        if (ssrc == pin.expectedSsrc) {
                                pin.mismatchCount = 0;
                                return true;
                        }
                        // First mismatch in this stretch → start
                        // debouncing.  Subsequent mismatches outside
                        // the window also restart the count.
                        const auto windowDuration =
                                arrivalSteady.value() - pin.mismatchFirstTime.value();
                        const int64_t windowMs =
                                std::chrono::duration_cast<std::chrono::milliseconds>(windowDuration).count();
                        if (pin.mismatchCount == 0 ||
                            windowMs < 0 ||
                            windowMs > static_cast<int64_t>(kSsrcDebounceWindowMs)) {
                                pin.mismatchFirstTime = arrivalSteady;
                                pin.mismatchCount = 1;
                                return false; // drop the stray
                        }
                        pin.mismatchCount++;
                        if (pin.mismatchCount < kSsrcDebounceCount) {
                                return false; // still debouncing
                        }
                        // Sustained mismatch — accept the new SSRC.
                        const uint32_t oldSsrc = pin.expectedSsrc;
                        pin.expectedSsrc = ssrc;
                        pin.mismatchCount = 0;
                        if (r.seqTracker) r.seqTracker->reset();
                        if (r.reorderBuffer) r.reorderBuffer->clear();
                        promekiWarn("RtpSession: SSRC change accepted on pt=%u (%08x -> %08x)",
                                    static_cast<unsigned>(pt), oldSsrc, ssrc);
                        _session->ssrcChangeSignal.emit(oldSsrc, ssrc, pt);
                        return true;
                }

                static constexpr uint32_t kSsrcDebounceCount = 5;
                static constexpr uint32_t kSsrcDebounceWindowMs = 1000;

                RtpSession      *_session = nullptr;
                PacketTransport *_transport = nullptr; ///< Per-leg transport (primary or ST 2022-7 secondary).
                Atomic<bool>     _stopRequested;
                bool             _warnedMissing = false;
};

RtpSession::RtpSession(ObjectBase *parent) : ObjectBase(parent) {
        _receiving.setValue(false);
        generateSsrc();
}

RtpSession::~RtpSession() {
        stopReceiving();
        stop();
}

void RtpSession::generateSsrc() {
        (void)Random::trueRandom(reinterpret_cast<uint8_t *>(&_ssrc), sizeof(_ssrc));
}

Error RtpSession::start(const SocketAddress &localAddr) {
        if (_running) {
                promekiWarn("RtpSession::start(%s) called while already running", localAddr.toString().cstr());
                return Error::Busy;
        }

        // Build an owned UdpSocketTransport for the simple case.
        auto owned = UdpSocketTransport::UPtr::create();
        owned->setLocalAddress(localAddr);
        Error err = owned->open();
        if (err.isError()) {
                promekiWarn("RtpSession::start: failed to open UDP transport on %s (err=%s)",
                            localAddr.toString().cstr(), err.name().cstr());
                return err;
        }
        _transport = owned.ptr();
        _ownedTransport = std::move(owned);
        if (_scheduler.isValid()) _scheduler->setTransport(_transport);
        _running = true;
        return Error::Ok;
}

Error RtpSession::start(PacketTransport *transport) {
        return start(transport, nullptr);
}

Error RtpSession::start(PacketTransport *primary, PacketTransport *secondary) {
        if (_running) {
                promekiWarn("RtpSession::start(primary, secondary) called while already running");
                return Error::Busy;
        }
        if (primary == nullptr) {
                promekiWarn("RtpSession::start called with null primary transport");
                return Error::InvalidArgument;
        }
        if (!primary->isOpen()) {
                promekiWarn("RtpSession::start called with closed primary transport");
                return Error::NotOpen;
        }
        if (secondary != nullptr && !secondary->isOpen()) {
                promekiWarn("RtpSession::start called with closed secondary transport");
                return Error::NotOpen;
        }
        _transport = primary;
        _ownedTransport.clear();
        _transportSecondary = secondary;
        _ownedTransportSecondary.clear();
        if (_scheduler.isValid()) _scheduler->setTransport(_transport);
        if (_schedulerSecondary.isValid()) _schedulerSecondary->setTransport(_transportSecondary);
        _running = true;
        if (secondary != nullptr) {
                promekiInfo("RtpSession: started ST 2022-7 dual-leg session (primary=%s secondary=%s)",
                            _remote.toString().cstr(), _remoteSecondary.toString().cstr());
        }
        return Error::Ok;
}

void RtpSession::stop() {
        if (!_running) return;
        stopReceiving();
        if (_scheduler.isValid()) {
                (void)_scheduler->flushPending();
                _scheduler->setTransport(nullptr);
        }
        if (_schedulerSecondary.isValid()) {
                (void)_schedulerSecondary->flushPending();
                _schedulerSecondary->setTransport(nullptr);
        }
        if (_ownedTransport.isValid()) {
                _ownedTransport->close();
                _ownedTransport.clear();
        }
        if (_ownedTransportSecondary.isValid()) {
                _ownedTransportSecondary->close();
                _ownedTransportSecondary.clear();
        }
        _transport = nullptr;
        _transportSecondary = nullptr;
        _running = false;
}

Error RtpSession::startReceiving(List<StreamReceiver> receivers, const String &threadName) {
        if (!_running || _transport == nullptr) {
                promekiWarn("RtpSession::startReceiving called while not running (running=%d transport=%p)",
                            _running ? 1 : 0, _transport);
                return Error::NotOpen;
        }
        if (_receiving.value()) {
                promekiWarn("RtpSession::startReceiving called while already receiving");
                return Error::Busy;
        }
        if (receivers.isEmpty()) {
                promekiErr("RtpSession::startReceiving: empty receivers list");
                return Error::InvalidArgument;
        }
        for (const StreamReceiver &r : receivers) {
                if (r.outQueue == nullptr || r.seqTracker == nullptr ||
                    r.reorderBuffer == nullptr) {
                        promekiErr("RtpSession::startReceiving: receiver has null pointer");
                        return Error::InvalidArgument;
                }
                // Plumb the per-stream clock rate into the tracker
                // so §A.8 jitter accounting fires from the very first
                // observe() call.  initSource() is intentionally NOT
                // called here — the tracker's implicit init on the
                // first @ref RtpSeqTracker::observe sets the right
                // base seq for the actual stream.
                if (r.clockRateHz > 0) r.seqTracker->setClockRateHz(r.clockRateHz);
        }
        _streamReceivers = std::move(receivers);
        _ssrcPinStates.clear();
        for (size_t i = 0; i < _streamReceivers.size(); ++i) {
                _ssrcPinStates.pushToBack(SsrcPinState{});
        }
        _receiveThread = ReceiveThreadUPtr::create(this, _transport, threadName);
        if (_transportSecondary != nullptr) {
                _receiveThreadSecondary = ReceiveThreadUPtr::create(this, _transportSecondary,
                                                                    threadName + String("-sec"));
        }
        _receiving.setValue(true);
        _receiveThread->start();
        if (_receiveThreadSecondary.isValid()) _receiveThreadSecondary->start();
        return Error::Ok;
}

void RtpSession::stopReceiving() {
        if (!_receiving.value() && _receiveThread.isNull() && _receiveThreadSecondary.isNull()) return;

        // Signal both threads to stop before joining either one — the
        // recv loops poll the stop flag on @c SO_RCVTIMEO timeouts, so
        // issuing requestStop on both in advance lets them exit in
        // parallel instead of serially.
        if (_receiveThread.isValid()) _receiveThread->requestStop();
        if (_receiveThreadSecondary.isValid()) _receiveThreadSecondary->requestStop();

        if (_receiveThread.isValid()) {
                // Delete the Thread object — Thread's destructor
                // joins the underlying std::thread.  Skip the join
                // when we are ourselves running on the receive
                // thread; the destructor path then runs after the
                // current callback unwinds and the loop observes
                // the stop flag.
                if (!_receiveThread->isCurrentThread()) {
                        _receiveThread.clear();
                }
        }
        if (_receiveThreadSecondary.isValid()) {
                if (!_receiveThreadSecondary->isCurrentThread()) {
                        _receiveThreadSecondary.clear();
                }
        }
        _receiving.setValue(false);
        _streamReceivers.clear();
        _ssrcPinStates.clear();
}

void RtpSession::fillHeader(RtpPacket &pkt, uint8_t pt, bool marker, uint32_t timestamp) {
        pkt.setVersion(2);
        pkt.setMarker(marker);
        pkt.setPayloadType(pt);
        pkt.setSequenceNumber(_sequenceNumber++);
        pkt.setTimestamp(timestamp);
        pkt.setSsrc(_ssrc);
}

void RtpSession::fillTransportHeader(RtpPacket &pkt) {
        // Caller (TX thread) has already stamped marker and
        // timestamp.  We only fill the transport-owned fields here.
        pkt.setVersion(2);
        pkt.setPayloadType(_payloadType);
        pkt.setSequenceNumber(_sequenceNumber++);
        pkt.setSsrc(_ssrc);
}

Error RtpSession::sendPacket(const Buffer &payload, uint32_t timestamp, uint8_t payloadType, bool marker) {
        if (!_running || _transport == nullptr) {
                promekiWarnThrottled(5000, "RtpSession::sendPacket called while not running (pt=%u ts=%u)",
                                     static_cast<unsigned>(payloadType), timestamp);
                return Error::NotOpen;
        }
        if (_remote.isNull()) {
                promekiWarnThrottled(5000, "RtpSession::sendPacket called with null remote (pt=%u ts=%u)",
                                     static_cast<unsigned>(payloadType), timestamp);
                return Error::InvalidArgument;
        }

        RtpPacket pkt(RtpPacket::HeaderSize + payload.size());
        std::memcpy(pkt.payload(), payload.data(), payload.size());
        fillHeader(pkt, payloadType, marker, timestamp);

        ssize_t sent = _transport->sendPacket(pkt.data(), pkt.size(), _remote);
        if (sent < 0) {
                promekiWarnThrottled(1000,
                                     "RtpSession::sendPacket transport->sendPacket failed (pt=%u size=%zu dest=%s)",
                                     static_cast<unsigned>(payloadType), pkt.size(), _remote.toString().cstr());
                return Error::IOError;
        }
        return Error::Ok;
}

Error RtpSession::sendPackets(RtpPacketBatch &batch) {
        if (!_running || _transport == nullptr) {
                promekiWarnThrottled(5000, "RtpSession::sendPackets called while not running (count=%zu)",
                                     batch.packets.size());
                return Error::NotOpen;
        }
        if (_remote.isNull()) {
                promekiWarnThrottled(5000, "RtpSession::sendPackets called with null remote (count=%zu)",
                                     batch.packets.size());
                return Error::InvalidArgument;
        }
        if (batch.packets.isEmpty()) return Error::Ok;

        // VBR compressed-video path stamps a per-frame rate cap on
        // each batch.  Forward it to the scheduler so KernelFq /
        // future DPDK backends can update their underlying knob; non-
        // rate-aware schedulers (Burst / Cadence) ignore it.
        if (batch.rateCapBps > 0 && _scheduler.isValid()) {
                (void)_scheduler->setRate(batch.rateCapBps / 8u);
        }

        // The TX thread has already stamped marker + RTP-TS on each
        // packet.  We fill the transport-owned header fields and
        // build a parallel Datagram batch referencing the shared
        // backing buffer (zero copy — the transport's batch send does
        // the kernel copy).  Per-packet @c txTimeNs is stamped from
        // @ref RtpPacketBatch::deadlineTaiNs (with the optional
        // @ref RtpPacketBatch::deadlineStrideNs adding the
        // ST 2110-21 §7.1 @c TPR_j stride): every packet @c j gets
        // @c base + j × stride.  When stride is 0 every datagram
        // gets the same deadline (ST 2110-40 LLTM ANC); when stride
        // is non-zero packets land on the SMPTE Epoch grid at
        // @c TPR_j = T_VD + j × T_RS (ST 2110-21 narrow timing).
        PacketTransport::DatagramList dgs;
        dgs.reserve(batch.packets.size());
        for (size_t i = 0; i < batch.packets.size(); i++) {
                auto &pkt = batch.packets[i];
                if (pkt.isNull() || pkt.size() < RtpPacket::HeaderSize) continue;

                fillTransportHeader(pkt);

                PacketTransport::Datagram d;
                d.data = pkt.data();
                d.size = pkt.size();
                d.dest = _remote;
                if (batch.deadlineTaiNs != 0) {
                        d.txTimeNs = batch.deadlineTaiNs +
                                     static_cast<uint64_t>(i) * batch.deadlineStrideNs;
                } else {
                        d.txTimeNs = 0;
                }
                dgs.pushToBack(d);
        }
        if (dgs.isEmpty()) return Error::Ok;

        // Hand the datagrams to the primary scheduler / transport,
        // then — if ST 2022-7 dual-leg dispatch is configured —
        // re-stamp dest on a copy of the Datagram list and fan out
        // to the secondary scheduler / transport.  The packet
        // payload bytes are shared via the refcounted Buffer
        // underneath every @ref RtpPacket, so the fan-out is
        // zero-copy.
        //
        // Fall back to a raw transport sendmmsg loop when a leg has
        // no scheduler installed (test / bring-up paths); production
        // callers always set up a scheduler per leg at session start.
        auto dispatchLeg = [&](PacketTransport::DatagramList &legDgs, PacketScheduler *sched,
                               PacketTransport *xport, const char *legName) -> Error {
                if (sched != nullptr) {
                        const int accepted = sched->enqueue(legDgs);
                        if (accepted < 0) {
                                promekiWarnThrottled(1000,
                                                     "RtpSession::sendPackets %s scheduler->enqueue failed (count=%zu)",
                                                     legName, legDgs.size());
                                return Error::IOError;
                        }
                        return Error::Ok;
                }
                size_t offset = 0;
                while (offset < legDgs.size()) {
                        PacketTransport::DatagramList sub;
                        size_t                        remaining = legDgs.size() - offset;
                        sub.reserve(remaining);
                        for (size_t i = offset; i < legDgs.size(); i++) {
                                sub.pushToBack(legDgs[i]);
                        }
                        int sent = xport->sendPackets(sub);
                        if (sent < 0) {
                                promekiWarnThrottled(
                                        1000,
                                        "RtpSession::sendPackets %s transport->sendPackets failed (offset=%zu remaining=%zu)",
                                        legName, offset, remaining);
                                return Error::IOError;
                        }
                        if (sent == 0) {
                                promekiWarnThrottled(
                                        1000,
                                        "RtpSession::sendPackets %s stalled — no progress (offset=%zu remaining=%zu)",
                                        legName, offset, remaining);
                                return Error::IOError;
                        }
                        offset += static_cast<size_t>(sent);
                }
                return Error::Ok;
        };

        Error primaryErr = dispatchLeg(dgs, _scheduler.get(), _transport, "primary");
        if (primaryErr.isError()) return primaryErr;

        if (_transportSecondary != nullptr && !_remoteSecondary.isNull()) {
                // Apply the secondary leg's rate cap too — kernel fq /
                // future DPDK backends honour it; non-rate-aware
                // schedulers (Burst / Cadence) ignore.
                if (batch.rateCapBps > 0 && _schedulerSecondary.isValid()) {
                        (void)_schedulerSecondary->setRate(batch.rateCapBps / 8u);
                }
                // Rewrite dest on a shallow copy of the Datagram list.
                // The payload `data` pointers reference the same
                // RtpPacket Buffer storage as the primary list —
                // refcount stays at 2 until both legs finish their
                // sendmmsg, then drops as each leg's scheduler
                // releases its references.
                PacketTransport::DatagramList dgsSec;
                dgsSec.reserve(dgs.size());
                for (size_t i = 0; i < dgs.size(); i++) {
                        PacketTransport::Datagram d = dgs[i];
                        d.dest = _remoteSecondary;
                        dgsSec.pushToBack(d);
                }
                Error secondaryErr = dispatchLeg(dgsSec, _schedulerSecondary.get(),
                                                 _transportSecondary, "secondary");
                if (secondaryErr.isError()) {
                        // Single-leg-down is the whole point of 2022-7;
                        // log it but don't surface as a session error
                        // — the primary leg's bytes already left.
                        promekiWarnThrottled(2000,
                                             "RtpSession::sendPackets secondary leg dispatch failed (err=%s) — "
                                             "primary leg unaffected",
                                             secondaryErr.name().cstr());
                }
        }

        return Error::Ok;
}

Error RtpSession::setPacingRate(uint64_t bytesPerSec) {
        if (_scheduler.isValid()) {
                return _scheduler->setRate(bytesPerSec);
        }
        // Fallback: no scheduler installed, talk to the transport
        // directly so tests that exercise the prior API keep working.
        if (_transport == nullptr) return Error::NotOpen;
        return _transport->setPacingRate(bytesPerSec);
}

void RtpSession::setScheduler(PacketScheduler::UPtr scheduler) {
        _scheduler = std::move(scheduler);
        if (_scheduler.isValid()) {
                _scheduler->setTransport(_transport);
        }
}

void RtpSession::setSchedulerSecondary(PacketScheduler::UPtr scheduler) {
        _schedulerSecondary = std::move(scheduler);
        if (_schedulerSecondary.isValid()) {
                _schedulerSecondary->setTransport(_transportSecondary);
        }
}

Error RtpSession::configureScheduler(const PacketScheduler::Spec &spec) {
        if (!_scheduler.isValid()) return Error::NotOpen;
        return _scheduler->configure(spec);
}

void RtpSession::setRtpAnchor(NtpTime captureNtp, uint32_t rtpTs) {
        Mutex::Locker lock(_rtcpMutex);
        _anchorNtp = captureNtp;
        _anchorRtpTs = rtpTs;
}

void RtpSession::setRtpAnchor(const ClockDomain &domain, uint32_t rtpTs) {
        // Read the domain's provider before taking the RTCP lock so
        // we don't hold it across the user-supplied lambda.  Falls
        // back to NtpTime::now() (system_clock) when the domain has
        // no provider bound — emitting a zeroed SR would silently
        // break receiver alignment, so the legacy behaviour is
        // preferable.
        int64_t utcNs = 0;
        if (domain.isValid()) {
                utcNs = domain.nowUtcNs();
        }
        NtpTime captureNtp;
        if (utcNs > 0) {
                using clock_t = std::chrono::system_clock;
                const auto tp = clock_t::time_point(std::chrono::nanoseconds(utcNs));
                captureNtp = NtpTime::fromSystemClock(tp);
        } else {
                promekiWarnOnce("RtpSession::setRtpAnchor: domain '%s' has no bound "
                                "wallclock provider — falling back to system_clock",
                                domain.isValid() ? domain.name().cstr() : "(invalid)");
                captureNtp = NtpTime::now();
        }
        setRtpAnchor(captureNtp, rtpTs);
}

NtpTime RtpSession::anchorNtp() const {
        Mutex::Locker lock(_rtcpMutex);
        return _anchorNtp;
}

uint32_t RtpSession::anchorRtpTs() const {
        Mutex::Locker lock(_rtcpMutex);
        return _anchorRtpTs;
}

void RtpSession::noteRtpEmission(uint32_t rtpTs) {
        Mutex::Locker lock(_rtcpMutex);
        _lastEmissionRtpTs = rtpTs;
        _hasEmission = true;
}

bool RtpSession::hasEmissionRecord() const {
        Mutex::Locker lock(_rtcpMutex);
        return _hasEmission;
}

RtpSession::ReceivedSr RtpSession::receivedSr() const {
        Mutex::Locker lock(_rtcpMutex);
        return _lastReceivedSr;
}

uint32_t RtpSession::srObservedCount() const {
        Mutex::Locker lock(_rtcpMutex);
        return _srObservedCount;
}

TimeStamp RtpSession::firstSrAt() const {
        Mutex::Locker lock(_rtcpMutex);
        return _firstSrAt;
}

void RtpSession::handleRtcp(const uint8_t *data, size_t size) {
        // Walk the compound for SRs and BYEs.  Anything else (SDES /
        // APP / RR / forward-compatible types we don't recognise) is
        // dropped silently — RTCP is forward-compatible by design.
        const auto srs = RtcpPacket::findSenderReports(data, size);
        if (!srs.isEmpty()) {
                // Take the last SR in the compound — RFC 3550 §6.4
                // lets a single compound carry multiple SRs (one per
                // source the sender is also receiving) but for our
                // point-to-point model the sender's own SR is always
                // present and is the one we care about.  When
                // multiple SRs share the same SSRC the last entry
                // wins; that's also what an RFC-compliant sender
                // would intend (later == newer accounting).
                const auto   &sr = srs[srs.size() - 1];
                const TimeStamp now = TimeStamp::now();
                Mutex::Locker lock(_rtcpMutex);
                _lastReceivedSr.ntp = sr.ntp;
                _lastReceivedSr.rtpTs = sr.rtpTimestamp;
                _lastReceivedSr.arrivedAt = now;
                _lastReceivedSr.valid = true;
                _srObservedCount += static_cast<uint32_t>(srs.size());
                if (!_firstSrAt.isValid()) _firstSrAt = now;
        }
        const auto byeSsrcs = RtcpPacket::findByeSources(data, size);
        for (size_t i = 0; i < byeSsrcs.size(); i++) {
                byeReceivedSignal.emit(byeSsrcs[i]);
        }
}

NtpTime RtpSession::currentSrNtp() const {
        Mutex::Locker lock(_rtcpMutex);
        if (_clockRate == 0) return _anchorNtp;
        return RtpStreamClock(_anchorNtp, _anchorRtpTs, _clockRate).toNtp(_lastEmissionRtpTs);
}

Error RtpSession::emitRtcpSr(uint32_t senderPacketCount, uint32_t senderOctetCount) {
        if (!_running || _transport == nullptr) return Error::NotOpen;
        if (_remote.isNull()) return Error::InvalidArgument;

        // The SR's (NTP, RTP_TS) pair is derived deterministically
        // from the capture-anchor and the most-recently emitted
        // RTP-TS — never sampled from the system clock at emission
        // time.  This is what lets multi-stream receivers align
        // essences in a common wallclock domain even when the
        // sender's @c steady_clock and @c system_clock disagree by
        // parts-per-million: every stream from this RtpMediaIO
        // shares the same anchor NTP, so the receiver's
        // @c (anchor, rtpTs) → NTP arithmetic produces a
        // cross-stream-consistent wallclock for the original
        // capture instant, regardless of how the wire pacing
        // diverges from the source clock.
        NtpTime  ntp;
        uint32_t rtpTs = 0;
        bool     hasEmission = false;
        {
                Mutex::Locker lock(_rtcpMutex);
                hasEmission = _hasEmission;
                rtpTs = _lastEmissionRtpTs;
                if (_clockRate == 0) {
                        ntp = _anchorNtp;
                } else {
                        ntp = RtpStreamClock(_anchorNtp, _anchorRtpTs, _clockRate).toNtp(_lastEmissionRtpTs);
                }
        }
        if (!hasEmission) {
                // Caller should have skipped us via
                // @ref hasEmissionRecord — but if they didn't, the
                // SR is still structurally legal: NTP equals the
                // anchor, RTP-TS = 0.  Just less useful for sync
                // until the first RTP packet flows.
                rtpTs = 0;
        }

        Buffer sr = RtcpPacket::buildSenderReport(_ssrc, ntp, rtpTs, senderPacketCount, senderOctetCount);
        Buffer sdes = RtcpPacket::buildSourceDescriptionCname(_ssrc, _cname);
        List<Buffer> compoundParts;
        compoundParts.pushToBack(sr);
        compoundParts.pushToBack(sdes);
        Buffer compound = RtcpPacket::compound(compoundParts);

        // Send via the transport.  rtcp-mux is what we advertise in
        // SDP, so the same socket carries both RTP and RTCP — the
        // RTCP packet type field (200 / 202) is what receivers use to
        // demux.  No batched / paced send needed for one small
        // datagram per few seconds.
        PacketTransport::Datagram d;
        d.data = compound.data();
        d.size = compound.size();
        d.dest = _remote;
        PacketTransport::DatagramList list;
        list.pushToBack(d);
        int sent = _transport->sendPackets(list);
        if (sent <= 0) {
                promekiWarn("RtpSession: RTCP send failed to %s (sent=%d size=%zu)", _remote.toString().cstr(),
                            sent, compound.size());
                return Error::IOError;
        }
        return Error::Ok;
}

Error RtpSession::emitRtcpRr(const RtcpPacket::ReportBlock &block) {
        if (!_running || _transport == nullptr) return Error::NotOpen;
        if (_remote.isNull()) return Error::InvalidArgument;

        // Augment the caller's block with lsr / dlsr from
        // _lastReceivedSr — they are receiver-side derivations and
        // belong here rather than at every caller.  Per RFC 3550
        // §6.4.1, both stay zero when no SR has been observed.
        RtcpPacket::ReportBlock b = block;
        {
                Mutex::Locker lock(_rtcpMutex);
                if (_lastReceivedSr.valid) {
                        b.lsr = _lastReceivedSr.ntp.toCompact32();
                        const Duration delta = TimeStamp::now() - _lastReceivedSr.arrivedAt;
                        // dlsr is in 1/65536 s.  Clamp to non-negative.
                        const int64_t deltaNs = std::max<int64_t>(0, delta.nanoseconds());
                        // (deltaNs * 65536 / 1e9) — split to avoid overflow on long delays.
                        const uint64_t deltaUnits =
                                (static_cast<uint64_t>(deltaNs) * 65536ull) / 1'000'000'000ull;
                        b.dlsr = static_cast<uint32_t>(deltaUnits & 0xFFFFFFFFull);
                }
        }

        List<RtcpPacket::ReportBlock> blocks;
        blocks.pushToBack(b);
        Buffer       rr = RtcpPacket::buildReceiverReport(_ssrc, blocks);
        Buffer       sdes = RtcpPacket::buildSourceDescriptionCname(_ssrc, _cname);
        List<Buffer> compoundParts;
        compoundParts.pushToBack(rr);
        compoundParts.pushToBack(sdes);
        Buffer compound = RtcpPacket::compound(compoundParts);

        PacketTransport::Datagram d;
        d.data = compound.data();
        d.size = compound.size();
        d.dest = _remote;
        PacketTransport::DatagramList list;
        list.pushToBack(d);
        int sent = _transport->sendPackets(list);
        if (sent <= 0) {
                promekiWarn("RtpSession: RTCP send failed to %s (sent=%d size=%zu)", _remote.toString().cstr(),
                            sent, compound.size());
                return Error::IOError;
        }
        return Error::Ok;
}

Error RtpSession::emitRtcpBye() {
        if (!_running || _transport == nullptr) return Error::NotOpen;
        if (_remote.isNull()) return Error::InvalidArgument;

        Buffer       sdes = RtcpPacket::buildSourceDescriptionCname(_ssrc, _cname);
        Buffer       bye = RtcpPacket::buildBye(_ssrc);
        // RFC 3550 §6.6 — BYE must come AFTER an SR or RR in a
        // compound.  We don't track whether this session ever
        // emitted an SR vs only RRs, so synthesise a minimal RR with
        // no report blocks (legal per §6.1) to satisfy the
        // "compound starts with SR or RR" rule.  The SDES sits in
        // between for the CNAME.
        List<RtcpPacket::ReportBlock> noBlocks;
        Buffer                        leadingRr =
                RtcpPacket::buildReceiverReport(_ssrc, noBlocks);
        List<Buffer> compoundParts;
        compoundParts.pushToBack(leadingRr);
        compoundParts.pushToBack(sdes);
        compoundParts.pushToBack(bye);
        Buffer compound = RtcpPacket::compound(compoundParts);

        PacketTransport::Datagram d;
        d.data = compound.data();
        d.size = compound.size();
        d.dest = _remote;
        PacketTransport::DatagramList list;
        list.pushToBack(d);
        int sent = _transport->sendPackets(list);
        if (sent <= 0) {
                promekiWarn("RtpSession: RTCP send failed to %s (sent=%d size=%zu)", _remote.toString().cstr(),
                            sent, compound.size());
                return Error::IOError;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
