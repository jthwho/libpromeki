/**
 * @file      rtpsession.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpsession.h>
#include <promeki/logger.h>
#include <promeki/packettransport.h>
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

                ~ReceiveThread() override { requestStop(); }

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
                                Buffer::Ptr   buf = Buffer::Ptr::create(kMaxPacketSize);
                                SocketAddress sender;
                                ssize_t n = _session->_transport->receivePacket(buf->data(), kMaxPacketSize, &sender);
                                if (n <= 0) {
                                        // Timeout (EAGAIN on UDP),
                                        // transient error, or empty
                                        // datagram — poll again.
                                        continue;
                                }
                                if (static_cast<size_t>(n) < RtpPacket::HeaderSize) {
                                        // Too short to contain a
                                        // fixed RTP header; drop.
                                        continue;
                                }
                                buf->setSize(static_cast<size_t>(n));

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
                                _session->packetReceivedSignal.emit(Buffer(*buf), pkt.timestamp(), pkt.payloadType(),
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

Error RtpSession::sendPackets(RtpPacket::List &packets, uint32_t timestamp, bool markerOnLast) {
        if (!_running || _transport == nullptr) return Error::NotOpen;
        if (_remote.isNull()) return Error::InvalidArgument;
        if (packets.isEmpty()) return Error::Ok;

        // Fill headers in place and build a parallel Datagram batch
        // referencing the shared backing buffer (zero copy — the
        // transport's batch send does the kernel copy).
        PacketTransport::DatagramList batch;
        batch.reserve(packets.size());
        for (size_t i = 0; i < packets.size(); i++) {
                auto &pkt = packets[i];
                if (pkt.isNull() || pkt.size() < RtpPacket::HeaderSize) continue;

                bool marker = markerOnLast && (i == packets.size() - 1);
                fillHeader(pkt, _payloadType, marker, timestamp);

                PacketTransport::Datagram d;
                d.data = pkt.data();
                d.size = pkt.size();
                d.dest = _remote;
                batch.pushToBack(d);
        }
        if (batch.isEmpty()) return Error::Ok;

        // sendmmsg may not accept all datagrams in one call when
        // the socket buffer is full (common for large uncompressed
        // video frames that produce thousands of packets).  Loop
        // to drain the remainder.
        size_t offset = 0;
        while (offset < batch.size()) {
                PacketTransport::DatagramList sub;
                size_t                        remaining = batch.size() - offset;
                sub.reserve(remaining);
                for (size_t i = offset; i < batch.size(); i++) {
                        sub.pushToBack(batch[i]);
                }
                int sent = _transport->sendPackets(sub);
                if (sent < 0) return Error::IOError;
                if (sent == 0) return Error::IOError; // no progress
                offset += static_cast<size_t>(sent);
        }
        return Error::Ok;
}

Error RtpSession::sendPackets(RtpPacket::List &packets, uint32_t startTimestamp, uint32_t timestampStride,
                              bool marker) {
        if (!_running || _transport == nullptr) return Error::NotOpen;
        if (_remote.isNull()) return Error::InvalidArgument;
        if (packets.isEmpty()) return Error::Ok;

        // Fill headers in place with monotonically advancing
        // timestamps, then hand the whole batch to the transport so
        // sendmmsg() (or a future DPDK burst) can see all packets at
        // once.
        PacketTransport::DatagramList batch;
        batch.reserve(packets.size());
        uint32_t ts = startTimestamp;
        for (size_t i = 0; i < packets.size(); i++) {
                auto &pkt = packets[i];
                if (pkt.isNull() || pkt.size() < RtpPacket::HeaderSize) continue;

                fillHeader(pkt, _payloadType, marker, ts);
                ts += timestampStride;

                PacketTransport::Datagram d;
                d.data = pkt.data();
                d.size = pkt.size();
                d.dest = _remote;
                batch.pushToBack(d);
        }
        if (batch.isEmpty()) return Error::Ok;

        size_t offset = 0;
        while (offset < batch.size()) {
                PacketTransport::DatagramList sub;
                size_t                        remaining = batch.size() - offset;
                sub.reserve(remaining);
                for (size_t i = offset; i < batch.size(); i++) {
                        sub.pushToBack(batch[i]);
                }
                int sent = _transport->sendPackets(sub);
                if (sent < 0) return Error::IOError;
                if (sent == 0) return Error::IOError;
                offset += static_cast<size_t>(sent);
        }
        return Error::Ok;
}

Error RtpSession::sendPacketsPaced(RtpPacket::List &packets, uint32_t timestamp, const Duration &spreadInterval,
                                   bool markerOnLast) {
        if (!_running || _transport == nullptr) return Error::NotOpen;
        if (_remote.isNull()) return Error::InvalidArgument;
        if (packets.isEmpty()) return Error::Ok;

        // Pace by absolute deadlines so the call always lasts
        // exactly @p spreadInterval regardless of how many packets
        // there are.  Per-packet deadlines are spaced
        // @c spreadInterval/N apart starting at @c startTime, so
        // packet @c i is dispatched at @c startTime+i*(interval/N)
        // and the call returns at @c startTime+spreadInterval
        // (after a final sleep past the last packet).  This makes
        // sendPacketsPaced safe to use as a frame-rate enforcer:
        // a caller that hands it one frame interval gets exactly
        // one frame interval of wall-clock pacing per call, even
        // for single-packet frames where the previous "spread
        // between packets" formulation collapsed to a no-op and
        // bypassed pacing entirely.
        //
        // All time arithmetic uses promeki::TimeStamp +
        // promeki::Duration directly via the operator overloads
        // declared in @ref timestamp.h, so no double round-trip is
        // required to convert between the library's portable
        // Duration and the platform's clock duration.
        const TimeStamp startTime = TimeStamp::now();
        const TimeStamp endTime = startTime + spreadInterval;

        const int64_t n = static_cast<int64_t>(packets.size());
        for (int64_t i = 0; i < n; i++) {
                auto &pkt = packets[i];
                if (pkt.isNull() || pkt.size() < RtpPacket::HeaderSize) continue;

                bool marker = markerOnLast && (i == n - 1);
                fillHeader(pkt, _payloadType, marker, timestamp);

                // Absolute per-packet deadline = startTime + i*(interval/n).
                // The first packet (i=0) goes out immediately;
                // subsequent packets sleep until their slot.
                if (i > 0) {
                        const TimeStamp pktDeadline = startTime + (spreadInterval * i) / n;
                        pktDeadline.sleepUntil();
                }

                ssize_t sent = _transport->sendPacket(pkt.data(), pkt.size(), _remote);
                if (sent < 0) return Error::IOError;
        }

        // Hold the call open for the remainder of the interval so
        // the total wall-clock duration matches @p spreadInterval
        // regardless of packet count.  This is what makes
        // single-packet frames pace correctly.
        endTime.sleepUntil();
        return Error::Ok;
}

Error RtpSession::setPacingRate(uint64_t bytesPerSec) {
        if (_transport == nullptr) return Error::NotOpen;
        return _transport->setPacingRate(bytesPerSec);
}

PROMEKI_NAMESPACE_END
