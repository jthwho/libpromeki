/**
 * @file      udpsockettransport.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/udpsockettransport.h>
#include <promeki/udpsocket.h>

PROMEKI_NAMESPACE_BEGIN

UdpSocketTransport::UdpSocketTransport() : _localAddress(SocketAddress::any(0)) {}

UdpSocketTransport::~UdpSocketTransport() {
        close();
}

Error UdpSocketTransport::open() {
        if (_socket.isValid()) return Error::Busy;

        _socket = UdpSocket::UPtr::create();
        Error err = _ipv6 ? _socket->openIpv6(IODevice::ReadWrite) : _socket->open(IODevice::ReadWrite);
        if (err.isError()) {
                _socket.clear();
                return err;
        }

        if (_reuseAddress) {
                err = _socket->setReuseAddress(true);
                if (err.isError()) {
                        close();
                        return err;
                }
        }

        err = _socket->bind(_localAddress);
        if (err.isError()) {
                close();
                return err;
        }

        if (_dscp != 0) {
                // DSCP is advisory; a failure here is non-fatal but
                // we want to surface it so the caller can log it.
                Error dscpErr = _socket->setDscp(_dscp);
                if (dscpErr.isError()) {
                        close();
                        return dscpErr;
                }
        }

        if (_multicastTTL > 0) {
                Error ttlErr = _socket->setMulticastTTL(_multicastTTL);
                if (ttlErr.isError()) {
                        close();
                        return ttlErr;
                }
        }

        if (!_multicastInterface.isEmpty()) {
                Error ifErr = _socket->setMulticastInterface(_multicastInterface);
                if (ifErr.isError()) {
                        close();
                        return ifErr;
                }
        }

        if (_multicastLoopback) {
                Error loopErr = _socket->setMulticastLoopback(true);
                if (loopErr.isError()) {
                        close();
                        return loopErr;
                }
        }

        return Error::Ok;
}

void UdpSocketTransport::close() {
        if (_socket.isNull()) return;
        _socket->close();
        _socket.clear();
}

bool UdpSocketTransport::isOpen() const {
        return _socket.isValid() && _socket->isOpen();
}

ssize_t UdpSocketTransport::sendPacket(const void *data, size_t size, const SocketAddress &dest) {
        if (!isOpen()) return -1;
        return _socket->writeDatagram(data, size, dest);
}

int UdpSocketTransport::sendPackets(const DatagramList &datagrams) {
        if (!isOpen()) return -1;
        if (datagrams.isEmpty()) return 0;

        // The transport's Datagram layout matches UdpSocket::Datagram
        // exactly but the types are distinct.  Translate on the way
        // through — the list is small (one frame's worth of RTP
        // packets) so the copy is negligible.
        UdpSocket::DatagramList batch;
        batch.resize(datagrams.size());
        for (size_t i = 0; i < datagrams.size(); i++) {
                batch[i].data = datagrams[i].data;
                batch[i].size = datagrams[i].size;
                batch[i].dest = datagrams[i].dest;
                batch[i].txTimeNs = datagrams[i].txTimeNs;
        }
        return _socket->writeDatagrams(batch);
}

ssize_t UdpSocketTransport::receivePacket(void *data, size_t maxSize, SocketAddress *sender) {
        if (!isOpen()) return -1;
        return _socket->readDatagram(data, maxSize, sender);
}

Error UdpSocketTransport::setPacingRate(uint64_t bytesPerSec) {
        if (!isOpen()) return Error::NotOpen;
        return _socket->setPacingRate(bytesPerSec);
}

Error UdpSocketTransport::setTxTime(bool enable) {
        if (!isOpen()) return Error::NotOpen;
        return _socket->setTxTime(enable);
}

PROMEKI_NAMESPACE_END
