/**
 * @file      net/multicastmanager.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/network/multicastmanager.h>
#include <promeki/network/udpsocket.h>
#include <promeki/core/platform.h>

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)

#if defined(PROMEKI_PLATFORM_WINDOWS)
#       include <winsock2.h>
#       include <ws2tcpip.h>
#else
#       include <sys/socket.h>
#       include <netinet/in.h>
#endif

PROMEKI_NAMESPACE_BEGIN

MulticastManager::MulticastManager(ObjectBase *parent)
        : ObjectBase(parent) { }

MulticastManager::~MulticastManager() {
        leaveAllGroups();
}

Error MulticastManager::joinGroup(const SocketAddress &group, UdpSocket *socket) {
        if(!_defaultInterface.isEmpty()) {
                return joinGroup(group, socket, _defaultInterface);
        }
        Error err = socket->joinMulticastGroup(group);
        if(err.isError()) return err;

        Membership m;
        m.group = group;
        m.socket = socket;
        _memberships.pushToBack(m);
        groupJoinedSignal.emit(group);
        return Error::Ok;
}

Error MulticastManager::joinGroup(const SocketAddress &group, UdpSocket *socket,
                                   const String &iface) {
        Error err = socket->joinMulticastGroup(group, iface);
        if(err.isError()) return err;

        Membership m;
        m.group = group;
        m.socket = socket;
        _memberships.pushToBack(m);
        groupJoinedSignal.emit(group);
        return Error::Ok;
}

Error MulticastManager::leaveGroup(const SocketAddress &group, UdpSocket *socket) {
        Error err = socket->leaveMulticastGroup(group);
        if(err.isError()) return err;

        for(size_t i = 0; i < _memberships.size(); i++) {
                const auto &m = _memberships[i];
                if(m.group == group && m.socket == socket && !m.isSSM) {
                        _memberships.remove(i);
                        break;
                }
        }
        groupLeftSignal.emit(group);
        return Error::Ok;
}

void MulticastManager::leaveAllGroups() {
        // Iterate backwards so removals don't invalidate indices
        for(int i = static_cast<int>(_memberships.size()) - 1; i >= 0; i--) {
                const auto &m = _memberships[static_cast<size_t>(i)];
                if(m.isSSM) {
                        leaveSourceGroup(m.group, m.source, m.socket);
                } else {
                        m.socket->leaveMulticastGroup(m.group);
                        groupLeftSignal.emit(m.group);
                }
        }
        _memberships.clear();
}

List<SocketAddress> MulticastManager::activeGroups() const {
        List<SocketAddress> groups;
        for(const auto &m : _memberships) {
                bool found = false;
                for(const auto &g : groups) {
                        if(g == m.group) { found = true; break; }
                }
                if(!found) groups.pushToBack(m.group);
        }
        return groups;
}

bool MulticastManager::isMemberOf(const SocketAddress &group) const {
        for(const auto &m : _memberships) {
                if(m.group == group) return true;
        }
        return false;
}

Error MulticastManager::joinSourceGroup(const SocketAddress &group,
                                         const SocketAddress &source,
                                         UdpSocket *socket) {
        if(!group.isMulticast()) return Error::Invalid;
        if(!group.isIPv4() || !source.isIPv4()) return Error::Invalid;

        int fd = socket->socketDescriptor();
        if(fd < 0) return Error::NotOpen;

        struct ip_mreq_source mreq;
        mreq.imr_multiaddr.s_addr = htonl(group.address().toIpv4().toUint32());
        mreq.imr_sourceaddr.s_addr = htonl(source.address().toIpv4().toUint32());
        mreq.imr_interface.s_addr = INADDR_ANY;
        if(::setsockopt(fd, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP,
                        &mreq, sizeof(mreq)) < 0) {
                return Error::syserr();
        }

        Membership m;
        m.group = group;
        m.socket = socket;
        m.source = source;
        m.isSSM = true;
        _memberships.pushToBack(m);
        groupJoinedSignal.emit(group);
        return Error::Ok;
}

Error MulticastManager::leaveSourceGroup(const SocketAddress &group,
                                          const SocketAddress &source,
                                          UdpSocket *socket) {
        if(!group.isIPv4() || !source.isIPv4()) return Error::Invalid;

        int fd = socket->socketDescriptor();
        if(fd < 0) return Error::NotOpen;

        struct ip_mreq_source mreq;
        mreq.imr_multiaddr.s_addr = htonl(group.address().toIpv4().toUint32());
        mreq.imr_sourceaddr.s_addr = htonl(source.address().toIpv4().toUint32());
        mreq.imr_interface.s_addr = INADDR_ANY;
        if(::setsockopt(fd, IPPROTO_IP, IP_DROP_SOURCE_MEMBERSHIP,
                        &mreq, sizeof(mreq)) < 0) {
                return Error::syserr();
        }

        for(size_t i = 0; i < _memberships.size(); i++) {
                const auto &m = _memberships[i];
                if(m.group == group && m.socket == socket &&
                   m.isSSM && m.source == source) {
                        _memberships.remove(i);
                        break;
                }
        }
        groupLeftSignal.emit(group);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END

#endif // !PROMEKI_PLATFORM_EMSCRIPTEN
