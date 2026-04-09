/**
 * @file      loopbacktransport.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/loopbacktransport.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

LoopbackTransport::LoopbackTransport() = default;

LoopbackTransport::~LoopbackTransport() {
        close();
        // If we were paired, unhook the peer so it does not dangle.
        if(_peer != nullptr) {
                if(_peer->_peer == this) _peer->_peer = nullptr;
                _peer = nullptr;
        }
}

void LoopbackTransport::pair(LoopbackTransport *a, LoopbackTransport *b) {
        if(a == nullptr || b == nullptr) return;
        a->_peer = b;
        b->_peer = a;
}

Error LoopbackTransport::open() {
        if(_open) return Error::Busy;
        _open = true;
        return Error::Ok;
}

void LoopbackTransport::close() {
        if(!_open) return;
        _open = false;
        _recvQueue.clear();
}

ssize_t LoopbackTransport::sendPacket(const void *data, size_t size,
                                       const SocketAddress &dest) {
        if(!_open) return -1;
        if(_peer == nullptr || !_peer->_open) return -1;
        _peer->deliver(data, size, dest);
        return static_cast<ssize_t>(size);
}

int LoopbackTransport::sendPackets(const DatagramList &datagrams) {
        if(!_open) return -1;
        if(_peer == nullptr || !_peer->_open) return -1;
        int sent = 0;
        for(size_t i = 0; i < datagrams.size(); i++) {
                const Datagram &d = datagrams[i];
                if(d.data == nullptr || d.size == 0) return sent > 0 ? sent : -1;
                _peer->deliver(d.data, d.size, d.dest);
                sent++;
        }
        return sent;
}

void LoopbackTransport::deliver(const void *data, size_t size,
                                 const SocketAddress &sender) {
        QueueEntry e;
        e.data = Buffer(size);
        e.data.setSize(size);
        if(size > 0 && data != nullptr) std::memcpy(e.data.data(), data, size);
        e.sender = sender;
        _recvQueue.pushToBack(std::move(e));
}

ssize_t LoopbackTransport::receivePacket(void *data, size_t maxSize,
                                          SocketAddress *sender) {
        if(!_open) return -1;
        if(_recvQueue.isEmpty()) return -1;
        QueueEntry e = std::move(_recvQueue.front());
        _recvQueue.remove(static_cast<size_t>(0));
        size_t copy = e.data.size() < maxSize ? e.data.size() : maxSize;
        if(copy > 0) std::memcpy(data, e.data.data(), copy);
        if(sender != nullptr) *sender = e.sender;
        return static_cast<ssize_t>(copy);
}

Error LoopbackTransport::setPacingRate(uint64_t /*bytesPerSec*/) {
        // Accept and ignore — tests can call setPacingRate without
        // special-casing the loopback transport.
        return Error::Ok;
}

Error LoopbackTransport::setTxTime(bool /*enable*/) {
        // Same rationale as setPacingRate().
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
