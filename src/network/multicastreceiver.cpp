/**
 * @file      multicastreceiver.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/multicastreceiver.h>
#include <promeki/logger.h>
#include <promeki/platform.h>
#include <promeki/udpsocket.h>

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)

PROMEKI_NAMESPACE_BEGIN

MulticastReceiver::MulticastReceiver(ObjectBase *parent)
    : Thread(parent), _localAddress(SocketAddress::any(0)), _threadName("multicast-rx") {
        _active.setValue(false);
        _stopRequested.setValue(false);
        _datagramCount.setValue(0);
        _byteCount.setValue(0);
}

MulticastReceiver::~MulticastReceiver() {
        stop();
}

void MulticastReceiver::setLocalAddress(const SocketAddress &address) {
        _localAddress = address;
}

void MulticastReceiver::setInterface(const String &iface) {
        _interfaceName = iface;
}

void MulticastReceiver::setMaxPacketSize(size_t bytes) {
        _maxPacketSize = (bytes == 0) ? DefaultMaxPacketSize : bytes;
}

void MulticastReceiver::setReceiveTimeout(unsigned int timeoutMs) {
        _receiveTimeoutMs = (timeoutMs == 0) ? DefaultReceiveTimeoutMs : timeoutMs;
}

void MulticastReceiver::setThreadName(const String &name) {
        _threadName = name;
        // Propagate to the underlying Thread immediately so the OS
        // name matches the configured value even when the receiver
        // is already running (e.g. a rename from a debug tool).
        Thread::setName(name);
}

void MulticastReceiver::setDatagramCallback(DatagramCallback callback) {
        _callback = std::move(callback);
}

Error MulticastReceiver::addGroup(const SocketAddress &group) {
        if (!group.isMulticast()) {
                promekiErr("MulticastReceiver: %s is not a multicast address", group.toString().cstr());
                return Error::Invalid;
        }
        GroupEntry entry;
        entry.group = group;
        entry.isSSM = false;
        _groups.pushToBack(entry);
        return Error::Ok;
}

Error MulticastReceiver::addSourceGroup(const SocketAddress &group, const SocketAddress &source) {
        if (!group.isMulticast()) {
                promekiErr("MulticastReceiver: %s is not a multicast address", group.toString().cstr());
                return Error::Invalid;
        }
        if (source.isNull()) {
                promekiErr("MulticastReceiver: SSM requires a non-null source address");
                return Error::Invalid;
        }
        GroupEntry entry;
        entry.group = group;
        entry.source = source;
        entry.isSSM = true;
        _groups.pushToBack(entry);
        return Error::Ok;
}

Error MulticastReceiver::openAndJoin() {
        if (_socket.isValid()) return Error::Busy;

        _socket = UdpSocket::UPtr::create();
        Error err = _socket->open(IODevice::ReadWrite);
        if (err.isError()) {
                promekiErr("MulticastReceiver: failed to open socket: %s", err.desc().cstr());
                _socket.clear();
                return err;
        }

        // SO_REUSEADDR is mandatory for multicast: it lets multiple
        // processes (and multiple sockets in this process) bind to
        // the same group+port combo, which is the standard pattern
        // for a SAP / RTP receive fleet.
        err = _socket->setReuseAddress(true);
        if (err.isError()) {
                promekiErr("MulticastReceiver: setReuseAddress failed: %s", err.desc().cstr());
                _socket->close();
                _socket.clear();
                return err;
        }

        err = _socket->bind(_localAddress);
        if (err.isError()) {
                promekiErr("MulticastReceiver: bind to %s failed: %s", _localAddress.toString().cstr(),
                           err.desc().cstr());
                _socket->close();
                _socket.clear();
                return err;
        }

        // Short SO_RCVTIMEO so the receive loop can poll the stop
        // flag between datagrams.  Without this, a dead stream would
        // wedge the worker in recvfrom() forever.
        err = _socket->setReceiveTimeout(_receiveTimeoutMs);
        if (err.isError()) {
                promekiWarn("MulticastReceiver: setReceiveTimeout(%u ms) failed: %s "
                            "(stop flag polling may be sluggish)",
                            _receiveTimeoutMs, err.desc().cstr());
                // Non-fatal — loop will still function, just without
                // a prompt stop response.
        }

        // Join each configured group through our member
        // MulticastManager.  The manager tracks memberships, honours
        // the default interface, and handles both ASM and SSM with a
        // single API.  Ownership of the join lives with the
        // MulticastManager for the duration of the receiver, and
        // closeAndLeave() unwinds everything through
        // leaveAllGroups().
        if (!_interfaceName.isEmpty()) {
                _multicastManager.setDefaultInterface(_interfaceName);
        }
        for (size_t i = 0; i < _groups.size(); i++) {
                const auto &entry = _groups[i];
                Error       jerr;
                if (entry.isSSM) {
                        jerr = _multicastManager.joinSourceGroup(entry.group, entry.source, _socket.ptr());
                } else if (!_interfaceName.isEmpty()) {
                        jerr = _multicastManager.joinGroup(entry.group, _socket.ptr(), _interfaceName);
                } else {
                        jerr = _multicastManager.joinGroup(entry.group, _socket.ptr());
                }
                if (jerr.isError()) {
                        promekiErr("MulticastReceiver: join %s failed: %s", entry.group.toString().cstr(),
                                   jerr.desc().cstr());
                        closeAndLeave();
                        return jerr;
                }
        }

        return Error::Ok;
}

void MulticastReceiver::closeAndLeave() {
        if (_socket.isNull()) return;
        // Leave every joined group via MulticastManager so the
        // kernel emits IGMPv2 LEAVE_GROUP / MLDv1 DONE messages for
        // ASM memberships and IP_DROP_SOURCE_MEMBERSHIP for SSM.
        _multicastManager.leaveAllGroups();
        _socket->close();
        _socket.clear();
}

Error MulticastReceiver::start() {
        if (_active.value()) return Error::Busy;
        if (!_callback) {
                promekiErr("MulticastReceiver: start() without a datagram callback");
                return Error::InvalidArgument;
        }

        _datagramCount.setValue(0);
        _byteCount.setValue(0);
        _stopRequested.setValue(false);

        Error err = openAndJoin();
        if (err.isError()) return err;

        _active.setValue(true);
        // Thread::setName is applied by applyOsName() inside the
        // Thread machinery once the worker starts; setting it here
        // ensures the name is propagated to the OS on launch.
        Thread::setName(_threadName);
        Thread::start();
        return Error::Ok;
}

void MulticastReceiver::stop() {
        if (!_active.value() && _socket.isNull()) return;
        _stopRequested.setValue(true);

        // Wait for the receive thread to exit.  Thread::wait() is
        // a no-op for adopted threads and safe to call more than
        // once; if we are being called from the thread itself (a
        // pathological but legal case) we skip the join.
        if (!isCurrentThread()) {
                Thread::wait();
        }

        closeAndLeave();
        _active.setValue(false);
}

void MulticastReceiver::run() {
        // Receive loop.  We allocate a small reusable buffer for
        // readDatagram() to recv into, then copy each datagram into
        // a fresh Buffer so the callback can outlive the loop
        // iteration (for example by posting the pointer onto a
        // consumer's queue).  An alternative design would be to hand
        // the callback a raw pointer + size for zero-copy, but the
        // Buffer approach keeps the signal and callback shapes
        // uniform and lets the callback stash the pointer without
        // an extra copy.
        List<uint8_t> scratch;
        scratch.resize(_maxPacketSize);

        while (!_stopRequested.value()) {
                SocketAddress sender;
                int64_t       n = _socket->readDatagram(scratch.data(), scratch.size(), &sender);
                if (n <= 0) {
                        // Negative return = timeout (EAGAIN) or
                        // genuine error.  The socket is non-blocking
                        // only by virtue of SO_RCVTIMEO so we cannot
                        // tell the two apart without inspecting
                        // errno, which would leak platform
                        // dependencies into this file.  A zero-length
                        // datagram is legal for UDP but uninteresting
                        // to consumers.  Either way, continue.
                        continue;
                }

                // Copy into an owned Buffer.  One allocation +
                // one memcpy per packet; profile if this ever becomes
                // a bottleneck (it won't for SAP / mDNS, and RTP goes
                // through a different path inside the task).
                Buffer datagram = Buffer(static_cast<size_t>(n));
                std::memcpy(datagram.data(), scratch.data(), static_cast<size_t>(n));
                datagram.setSize(static_cast<size_t>(n));

                _datagramCount.setValue(_datagramCount.value() + 1);
                _byteCount.setValue(_byteCount.value() + static_cast<uint64_t>(n));

                if (_callback) _callback(datagram, sender);
                datagramReceivedSignal.emit(datagram, sender);
        }
}

PROMEKI_NAMESPACE_END

#endif // !PROMEKI_PLATFORM_EMSCRIPTEN
