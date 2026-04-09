/**
 * @file      rtpsession.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpsession.h>
#include <promeki/packettransport.h>
#include <promeki/udpsockettransport.h>
#include <promeki/timestamp.h>
#include <cstring>
#include <random>

PROMEKI_NAMESPACE_BEGIN

RtpSession::RtpSession(ObjectBase *parent)
        : ObjectBase(parent) {
        generateSsrc();
}

RtpSession::~RtpSession() {
        stop();
}

void RtpSession::generateSsrc() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist;
        _ssrc = dist(gen);
}

Error RtpSession::start(const SocketAddress &localAddr) {
        if(_running) return Error::Busy;

        // Build an owned UdpSocketTransport for the simple case.
        auto *transport = new UdpSocketTransport();
        transport->setLocalAddress(localAddr);
        Error err = transport->open();
        if(err.isError()) {
                delete transport;
                return err;
        }
        _transport     = transport;
        _ownsTransport = true;
        _running       = true;
        return Error::Ok;
}

Error RtpSession::start(PacketTransport *transport) {
        if(_running) return Error::Busy;
        if(transport == nullptr) return Error::InvalidArgument;
        if(!transport->isOpen()) return Error::NotOpen;
        _transport     = transport;
        _ownsTransport = false;
        _running       = true;
        return Error::Ok;
}

void RtpSession::stop() {
        if(!_running) return;
        if(_ownsTransport && _transport != nullptr) {
                _transport->close();
                delete _transport;
        }
        _transport     = nullptr;
        _ownsTransport = false;
        _running       = false;
}

void RtpSession::fillHeader(RtpPacket &pkt, uint8_t pt, bool marker,
                             uint32_t timestamp) {
        pkt.setVersion(2);
        pkt.setMarker(marker);
        pkt.setPayloadType(pt);
        pkt.setSequenceNumber(_sequenceNumber++);
        pkt.setTimestamp(timestamp);
        pkt.setSsrc(_ssrc);
}

Error RtpSession::sendPacket(const Buffer &payload, uint32_t timestamp,
                              uint8_t payloadType, bool marker) {
        if(!_running || _transport == nullptr) return Error::NotOpen;
        if(_remote.isNull()) return Error::InvalidArgument;

        RtpPacket pkt(RtpPacket::HeaderSize + payload.size());
        std::memcpy(pkt.payload(), payload.data(), payload.size());
        fillHeader(pkt, payloadType, marker, timestamp);

        ssize_t sent = _transport->sendPacket(pkt.data(), pkt.size(), _remote);
        if(sent < 0) return Error::IOError;
        return Error::Ok;
}

Error RtpSession::sendPackets(RtpPacket::List &packets, uint32_t timestamp,
                               bool markerOnLast) {
        if(!_running || _transport == nullptr) return Error::NotOpen;
        if(_remote.isNull()) return Error::InvalidArgument;
        if(packets.isEmpty()) return Error::Ok;

        // Fill headers in place and build a parallel Datagram batch
        // referencing the shared backing buffer (zero copy — the
        // transport's batch send does the kernel copy).
        PacketTransport::DatagramList batch;
        batch.reserve(packets.size());
        for(size_t i = 0; i < packets.size(); i++) {
                auto &pkt = packets[i];
                if(pkt.isNull() || pkt.size() < RtpPacket::HeaderSize) continue;

                bool marker = markerOnLast && (i == packets.size() - 1);
                fillHeader(pkt, _payloadType, marker, timestamp);

                PacketTransport::Datagram d;
                d.data = pkt.data();
                d.size = pkt.size();
                d.dest = _remote;
                batch.pushToBack(d);
        }
        if(batch.isEmpty()) return Error::Ok;

        int sent = _transport->sendPackets(batch);
        if(sent < 0) return Error::IOError;
        if(static_cast<size_t>(sent) != batch.size()) return Error::IOError;
        return Error::Ok;
}

Error RtpSession::sendPackets(RtpPacket::List &packets,
                               uint32_t startTimestamp,
                               uint32_t timestampStride,
                               bool marker) {
        if(!_running || _transport == nullptr) return Error::NotOpen;
        if(_remote.isNull()) return Error::InvalidArgument;
        if(packets.isEmpty()) return Error::Ok;

        // Fill headers in place with monotonically advancing
        // timestamps, then hand the whole batch to the transport so
        // sendmmsg() (or a future DPDK burst) can see all packets at
        // once.
        PacketTransport::DatagramList batch;
        batch.reserve(packets.size());
        uint32_t ts = startTimestamp;
        for(size_t i = 0; i < packets.size(); i++) {
                auto &pkt = packets[i];
                if(pkt.isNull() || pkt.size() < RtpPacket::HeaderSize) continue;

                fillHeader(pkt, _payloadType, marker, ts);
                ts += timestampStride;

                PacketTransport::Datagram d;
                d.data = pkt.data();
                d.size = pkt.size();
                d.dest = _remote;
                batch.pushToBack(d);
        }
        if(batch.isEmpty()) return Error::Ok;

        int sent = _transport->sendPackets(batch);
        if(sent < 0) return Error::IOError;
        if(static_cast<size_t>(sent) != batch.size()) return Error::IOError;
        return Error::Ok;
}

Error RtpSession::sendPacketsPaced(RtpPacket::List &packets, uint32_t timestamp,
                                    const Duration &spreadInterval,
                                    bool markerOnLast) {
        if(!_running || _transport == nullptr) return Error::NotOpen;
        if(_remote.isNull()) return Error::InvalidArgument;
        if(packets.isEmpty()) return Error::Ok;

        // For a single packet pacing is a no-op; just send it.
        if(packets.size() == 1) {
                return sendPackets(packets, timestamp, markerOnLast);
        }

        // Compute inter-packet interval.
        int64_t intervalNs = spreadInterval.nanoseconds() / (int64_t)(packets.size() - 1);
        Duration packetInterval = Duration::fromNanoseconds(intervalNs);

        TimeStamp sendTime = TimeStamp::now();

        for(size_t i = 0; i < packets.size(); i++) {
                auto &pkt = packets[i];
                if(pkt.isNull() || pkt.size() < RtpPacket::HeaderSize) continue;

                bool marker = markerOnLast && (i == packets.size() - 1);
                fillHeader(pkt, _payloadType, marker, timestamp);

                ssize_t sent = _transport->sendPacket(pkt.data(), pkt.size(), _remote);
                if(sent < 0) return Error::IOError;

                // Pace: sleep until next packet's send time.
                if(i < packets.size() - 1) {
                        sendTime += TimeStamp::secondsToDuration(
                                packetInterval.toSecondsDouble());
                        sendTime.sleepUntil();
                }
        }
        return Error::Ok;
}

Error RtpSession::setPacingRate(uint64_t bytesPerSec) {
        if(_transport == nullptr) return Error::NotOpen;
        return _transport->setPacingRate(bytesPerSec);
}

PROMEKI_NAMESPACE_END
