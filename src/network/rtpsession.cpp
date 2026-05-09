/**
 * @file      rtpsession.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpsession.h>
#include <promeki/logger.h>
#include <promeki/packettransport.h>
#include <promeki/rtcppacket.h>
#include <promeki/rtpstreamclock.h>
#include <promeki/thread.h>
#include <promeki/udpsocket.h>
#include <promeki/udpsockettransport.h>
#include <promeki/timestamp.h>
#include <cstring>
#include <random>

PROMEKI_NAMESPACE_BEGIN

// ----------------------------------------------------------------------------
// ReceiveThread — internal receive loop.
//
// Declared inside the RtpSession class (as a private nested type via
// a forward-friend) so that the thread can reach into the session's
// private members without exposing them further.  The thread runs a
// blocking receivePacket() loop on the session's PacketTransport,
// wraps each datagram in an RtpPacket view of an owned Buffer, and
// hands it off via the configured callback.  The receive socket's
// SO_RCVTIMEO is used (for UDP transports) to poll the stop flag
// between datagrams.
// ----------------------------------------------------------------------------
class RtpSession::ReceiveThread : public Thread {
        public:
                ReceiveThread(RtpSession *session, const String &name) : _session(session) {
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
                        if (auto *udpTransport = dynamic_cast<UdpSocketTransport *>(_session->_transport)) {
                                if (UdpSocket *sock = udpTransport->socket()) {
                                        (void)sock->setReceiveTimeout(_session->_receivePollMs);
                                }
                        }

                        while (!_stopRequested.value()) {
                                Buffer   buf = Buffer(kMaxPacketSize);
                                SocketAddress sender;
                                ssize_t n = _session->_transport->receivePacket(buf.data(), kMaxPacketSize, &sender);
                                if (n <= 0) {
                                        // Timeout (EAGAIN on UDP),
                                        // transient error, or empty
                                        // datagram — poll again.
                                        continue;
                                }
                                if (n < 4) {
                                        // Too short to even hold an
                                        // RTCP common header.  Drop.
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
                                        continue;
                                }
                                RtpPacket pkt(buf, 0, static_cast<size_t>(n));
                                if (!pkt.isValid()) {
                                        // Not a valid RTP packet
                                        // (wrong version, truncated
                                        // extension, etc.) — drop.
                                        continue;
                                }

                                if (_session->_receiveCallback) {
                                        _session->_receiveCallback(pkt, sender);
                                }
                                _session->packetReceivedSignal.emit(buf, pkt.timestamp(), pkt.payloadType(),
                                                                    pkt.marker());
                        }
                }

        private:
                RtpSession  *_session = nullptr;
                Atomic<bool> _stopRequested;
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
        std::random_device                      rd;
        std::mt19937                            gen(rd());
        std::uniform_int_distribution<uint32_t> dist;
        _ssrc = dist(gen);
}

Error RtpSession::start(const SocketAddress &localAddr) {
        if (_running) return Error::Busy;

        // Build an owned UdpSocketTransport for the simple case.
        auto owned = UdpSocketTransport::UPtr::create();
        owned->setLocalAddress(localAddr);
        Error err = owned->open();
        if (err.isError()) return err;
        _transport = owned.ptr();
        _ownedTransport = std::move(owned);
        _running = true;
        return Error::Ok;
}

Error RtpSession::start(PacketTransport *transport) {
        if (_running) return Error::Busy;
        if (transport == nullptr) return Error::InvalidArgument;
        if (!transport->isOpen()) return Error::NotOpen;
        _transport = transport;
        _ownedTransport.clear();
        _running = true;
        return Error::Ok;
}

void RtpSession::stop() {
        if (!_running) return;
        stopReceiving();
        if (_ownedTransport.isValid()) {
                _ownedTransport->close();
                _ownedTransport.clear();
        }
        _transport = nullptr;
        _running = false;
}

Error RtpSession::startReceiving(PacketCallback callback, const String &threadName) {
        if (!_running || _transport == nullptr) return Error::NotOpen;
        if (_receiving.value()) return Error::Busy;
        if (!callback) {
                promekiErr("RtpSession::startReceiving: null callback");
                return Error::InvalidArgument;
        }

        _receiveCallback = std::move(callback);
        _receiveThread = ReceiveThreadUPtr::create(this, threadName);
        _receiving.setValue(true);
        _receiveThread->start();
        return Error::Ok;
}

void RtpSession::stopReceiving() {
        if (!_receiving.value() && _receiveThread.isNull()) return;

        if (_receiveThread.isValid()) {
                _receiveThread->requestStop();
                // Delete the Thread object — Thread's destructor
                // joins the underlying std::thread.  If we are
                // ourselves running on the receive thread (the
                // callback is calling us), skip the join — the
                // destructor path will be reached after the callback
                // unwinds and the loop checks the stop flag.
                if (!_receiveThread->isCurrentThread()) {
                        _receiveThread.clear();
                }
        }
        _receiving.setValue(false);
        _receiveCallback = nullptr;
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
        if (!_running || _transport == nullptr) return Error::NotOpen;
        if (_remote.isNull()) return Error::InvalidArgument;

        RtpPacket pkt(RtpPacket::HeaderSize + payload.size());
        std::memcpy(pkt.payload(), payload.data(), payload.size());
        fillHeader(pkt, payloadType, marker, timestamp);

        ssize_t sent = _transport->sendPacket(pkt.data(), pkt.size(), _remote);
        if (sent < 0) return Error::IOError;
        return Error::Ok;
}

Error RtpSession::sendPackets(RtpPacketBatch &batch) {
        if (!_running || _transport == nullptr) return Error::NotOpen;
        if (_remote.isNull()) return Error::InvalidArgument;
        if (batch.packets.isEmpty()) return Error::Ok;

        // VBR compressed-video path stamps a per-frame rate cap on
        // each batch.  Apply it here so the kernel @c fq qdisc has a
        // chance to spread the bytes before we hand them over.
        if (batch.rateCapBps > 0) {
                (void)_transport->setPacingRate(batch.rateCapBps / 8u);
        }

        // The TX thread has already stamped marker + RTP-TS on each
        // packet.  We fill the transport-owned header fields and
        // build a parallel Datagram batch referencing the shared
        // backing buffer (zero copy — the transport's batch send does
        // the kernel copy).
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
                dgs.pushToBack(d);
        }
        if (dgs.isEmpty()) return Error::Ok;

        // sendmmsg may not accept all datagrams in one call when
        // the socket buffer is full (common for large uncompressed
        // video frames that produce thousands of packets).  Loop
        // to drain the remainder.
        size_t offset = 0;
        while (offset < dgs.size()) {
                PacketTransport::DatagramList sub;
                size_t                        remaining = dgs.size() - offset;
                sub.reserve(remaining);
                for (size_t i = offset; i < dgs.size(); i++) {
                        sub.pushToBack(dgs[i]);
                }
                int sent = _transport->sendPackets(sub);
                if (sent < 0) return Error::IOError;
                if (sent == 0) return Error::IOError; // no progress
                offset += static_cast<size_t>(sent);
        }
        return Error::Ok;
}

Error RtpSession::setPacingRate(uint64_t bytesPerSec) {
        if (_transport == nullptr) return Error::NotOpen;
        return _transport->setPacingRate(bytesPerSec);
}

void RtpSession::setRtpAnchor(NtpTime captureNtp, uint32_t rtpTs) {
        Mutex::Locker lock(_rtcpMutex);
        _anchorNtp = captureNtp;
        _anchorRtpTs = rtpTs;
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

void RtpSession::handleRtcp(const uint8_t *data, size_t size) {
        // Walk the compound for SRs.  Anything else (SDES / BYE /
        // APP / RR / forward-compatible types we don't recognise) is
        // dropped silently — RTCP is forward-compatible by design.
        const auto srs = RtcpPacket::findSenderReports(data, size);
        if (srs.isEmpty()) return;
        // Take the last SR in the compound — RFC 3550 §6.4 lets a
        // single compound carry multiple SRs (one per source the
        // sender is also receiving) but for our point-to-point
        // model the sender's own SR is always present and is the
        // one we care about.  When multiple SRs share the same
        // SSRC the last entry wins; that's also what an RFC-compliant
        // sender would intend (later == newer accounting).
        const auto       &sr = srs[srs.size() - 1];
        Mutex::Locker     lock(_rtcpMutex);
        _lastReceivedSr.ntp = sr.ntp;
        _lastReceivedSr.rtpTs = sr.rtpTimestamp;
        _lastReceivedSr.arrivedAt = TimeStamp::now();
        _lastReceivedSr.valid = true;
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
        return sent <= 0 ? Error::IOError : Error::Ok;
}

PROMEKI_NAMESPACE_END
