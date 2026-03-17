/**
 * @file      net/rtpsession.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/network/rtpsession.h>
#include <promeki/network/udpsocket.h>
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
        _socket = new UdpSocket(this);
        Error err = _socket->open(IODevice::ReadWrite);
        if(err.isError()) {
                delete _socket;
                _socket = nullptr;
                return err;
        }
        err = _socket->bind(localAddr);
        if(err.isError()) {
                delete _socket;
                _socket = nullptr;
                return err;
        }
        _running = true;
        return Error::Ok;
}

void RtpSession::stop() {
        if(!_running) return;
        if(_socket) {
                _socket->close();
                delete _socket;
                _socket = nullptr;
        }
        _running = false;
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
                              uint8_t payloadType, const SocketAddress &dest,
                              bool marker) {
        if(!_running) return Error::NotOpen;

        RtpPacket pkt(RtpPacket::HeaderSize + payload.size());
        std::memcpy(pkt.payload(), payload.data(), payload.size());
        fillHeader(pkt, payloadType, marker, timestamp);

        ssize_t sent = _socket->writeDatagram(pkt.data(), pkt.size(), dest);
        if(sent < 0) return Error::IOError;
        return Error::Ok;
}

Error RtpSession::sendPackets(RtpPacket::List &packets, uint32_t timestamp,
                               const SocketAddress &dest, bool markerOnLast) {
        if(!_running) return Error::NotOpen;

        for(size_t i = 0; i < packets.size(); i++) {
                auto &pkt = packets[i];
                if(pkt.isNull() || pkt.size() < RtpPacket::HeaderSize) continue;

                bool marker = markerOnLast && (i == packets.size() - 1);
                fillHeader(pkt, _payloadType, marker, timestamp);

                ssize_t sent = _socket->writeDatagram(pkt.data(), pkt.size(), dest);
                if(sent < 0) return Error::IOError;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
