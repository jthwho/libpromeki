/**
 * @file      udpsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/udpsocket.h>
#include <promeki/platform.h>

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)

#if defined(PROMEKI_PLATFORM_WINDOWS)
#       include <winsock2.h>
#       include <ws2tcpip.h>
#else
#       include <sys/socket.h>
#       include <sys/ioctl.h>
#       include <netinet/in.h>
#       include <net/if.h>
#endif

PROMEKI_NAMESPACE_BEGIN

UdpSocket::UdpSocket(ObjectBase *parent)
        : AbstractSocket(UdpSocketType, parent) {
}

UdpSocket::~UdpSocket() {
        if(isOpen()) close();
}

Error UdpSocket::open(OpenMode mode) {
        _domain = AF_INET;
        Error err = createSocket(AF_INET, SOCK_DGRAM, 0);
        if(err.isError()) return err;
        setOpenMode(mode);
        return Error::Ok;
}

Error UdpSocket::openIpv6(OpenMode mode) {
        _domain = AF_INET6;
        Error err = createSocket(AF_INET6, SOCK_DGRAM, 0);
        if(err.isError()) return err;
        // Allow IPv4-mapped addresses by default
        int v6only = 0;
        setSocketOption(IPPROTO_IPV6, IPV6_V6ONLY, v6only);
        setOpenMode(mode);
        return Error::Ok;
}

Error UdpSocket::close() {
        closeSocket();
        _domain = 0;
        return Error::Ok;
}

int64_t UdpSocket::read(void *data, int64_t maxSize) {
        if(_fd < 0) return -1;
        ssize_t ret = ::recv(_fd, data, static_cast<size_t>(maxSize), 0);
        if(ret < 0) {
                setError(Error::syserr());
                return -1;
        }
        return static_cast<int64_t>(ret);
}

int64_t UdpSocket::write(const void *data, int64_t maxSize) {
        if(_fd < 0) return -1;
        ssize_t ret = ::send(_fd, data, static_cast<size_t>(maxSize), 0);
        if(ret < 0) {
                setError(Error::syserr());
                return -1;
        }
        return static_cast<int64_t>(ret);
}

int64_t UdpSocket::bytesAvailable() const {
        if(_fd < 0) return 0;
#if defined(PROMEKI_PLATFORM_WINDOWS)
        unsigned long bytes = 0;
        if(ioctlsocket(_fd, FIONREAD, &bytes) != 0) return 0;
        return static_cast<int64_t>(bytes);
#else
        int bytes = 0;
        if(::ioctl(_fd, FIONREAD, &bytes) < 0) return 0;
        return static_cast<int64_t>(bytes);
#endif
}

ssize_t UdpSocket::writeDatagram(const void *data, size_t size, const SocketAddress &dest) {
        if(_fd < 0) return -1;
        struct sockaddr_storage storage;
        size_t addrLen = dest.toSockAddr(&storage);
        if(addrLen == 0) return -1;
        return ::sendto(_fd, data, size, 0,
                        reinterpret_cast<struct sockaddr *>(&storage),
                        static_cast<socklen_t>(addrLen));
}

ssize_t UdpSocket::writeDatagram(const Buffer &data, const SocketAddress &dest) {
        return writeDatagram(data.data(), data.size(), dest);
}

ssize_t UdpSocket::readDatagram(void *data, size_t maxSize, SocketAddress *sender) {
        if(_fd < 0) return -1;
        struct sockaddr_storage storage;
        socklen_t addrLen = sizeof(storage);
        ssize_t ret = ::recvfrom(_fd, data, maxSize, 0,
                                 reinterpret_cast<struct sockaddr *>(&storage),
                                 &addrLen);
        if(ret < 0) return -1;
        if(sender != nullptr) {
                auto [addr, err] = SocketAddress::fromSockAddr(
                        reinterpret_cast<struct sockaddr *>(&storage), addrLen);
                if(err.isOk()) *sender = addr;
        }
        return ret;
}

bool UdpSocket::hasPendingDatagrams() const {
        return bytesAvailable() > 0;
}

ssize_t UdpSocket::pendingDatagramSize() const {
        if(_fd < 0) return -1;
        char buf;
        ssize_t ret = ::recv(_fd, &buf, 1, MSG_PEEK | MSG_TRUNC);
        return ret;
}

Error UdpSocket::joinMulticastGroup(const SocketAddress &group) {
        if(_fd < 0) return Error::NotOpen;
        if(!group.isMulticast()) return Error::Invalid;
        if(group.isIPv4()) {
                struct ip_mreq mreq;
                mreq.imr_multiaddr.s_addr = htonl(group.address().toIpv4().toUint32());
                mreq.imr_interface.s_addr = INADDR_ANY;
                if(::setsockopt(_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                &mreq, sizeof(mreq)) < 0) {
                        return Error::syserr();
                }
        } else {
                struct ipv6_mreq mreq6;
                const auto &v6 = group.address().toIpv6();
                std::memcpy(&mreq6.ipv6mr_multiaddr, v6.raw(), 16);
                mreq6.ipv6mr_interface = 0;
                if(::setsockopt(_fd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                                &mreq6, sizeof(mreq6)) < 0) {
                        return Error::syserr();
                }
        }
        return Error::Ok;
}

Error UdpSocket::joinMulticastGroup(const SocketAddress &group, const String &iface) {
        if(_fd < 0) return Error::NotOpen;
        if(!group.isMulticast()) return Error::Invalid;
        if(group.isIPv4()) {
                struct ip_mreqn mreq;
                mreq.imr_multiaddr.s_addr = htonl(group.address().toIpv4().toUint32());
                mreq.imr_address.s_addr = INADDR_ANY;
                mreq.imr_ifindex = static_cast<int>(if_nametoindex(iface.cstr()));
                if(mreq.imr_ifindex == 0) return Error::Invalid;
                if(::setsockopt(_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                &mreq, sizeof(mreq)) < 0) {
                        return Error::syserr();
                }
        } else {
                struct ipv6_mreq mreq6;
                const auto &v6 = group.address().toIpv6();
                std::memcpy(&mreq6.ipv6mr_multiaddr, v6.raw(), 16);
                mreq6.ipv6mr_interface = if_nametoindex(iface.cstr());
                if(mreq6.ipv6mr_interface == 0) return Error::Invalid;
                if(::setsockopt(_fd, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                                &mreq6, sizeof(mreq6)) < 0) {
                        return Error::syserr();
                }
        }
        return Error::Ok;
}

Error UdpSocket::leaveMulticastGroup(const SocketAddress &group) {
        if(_fd < 0) return Error::NotOpen;
        if(!group.isMulticast()) return Error::Invalid;
        if(group.isIPv4()) {
                struct ip_mreq mreq;
                mreq.imr_multiaddr.s_addr = htonl(group.address().toIpv4().toUint32());
                mreq.imr_interface.s_addr = INADDR_ANY;
                if(::setsockopt(_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                                &mreq, sizeof(mreq)) < 0) {
                        return Error::syserr();
                }
        } else {
                struct ipv6_mreq mreq6;
                const auto &v6 = group.address().toIpv6();
                std::memcpy(&mreq6.ipv6mr_multiaddr, v6.raw(), 16);
                mreq6.ipv6mr_interface = 0;
                if(::setsockopt(_fd, IPPROTO_IPV6, IPV6_LEAVE_GROUP,
                                &mreq6, sizeof(mreq6)) < 0) {
                        return Error::syserr();
                }
        }
        return Error::Ok;
}

Error UdpSocket::setMulticastTTL(int ttl) {
        if(_fd < 0) return Error::NotOpen;
        if(_domain == AF_INET6) {
                return setSocketOption(IPPROTO_IPV6, IPV6_MULTICAST_HOPS, ttl);
        }
        return setSocketOption(IPPROTO_IP, IP_MULTICAST_TTL, ttl);
}

Error UdpSocket::setMulticastLoopback(bool enable) {
        if(_fd < 0) return Error::NotOpen;
        int val = enable ? 1 : 0;
        if(_domain == AF_INET6) {
                return setSocketOption(IPPROTO_IPV6, IPV6_MULTICAST_LOOP, val);
        }
        return setSocketOption(IPPROTO_IP, IP_MULTICAST_LOOP, val);
}

Error UdpSocket::setMulticastInterface(const String &iface) {
        if(_fd < 0) return Error::NotOpen;
        unsigned int ifindex = if_nametoindex(iface.cstr());
        if(ifindex == 0) return Error::Invalid;
        if(_domain == AF_INET6) {
                return setSocketOption(IPPROTO_IPV6, IPV6_MULTICAST_IF,
                                       static_cast<int>(ifindex));
        }
        struct ip_mreqn mreq;
        std::memset(&mreq, 0, sizeof(mreq));
        mreq.imr_ifindex = static_cast<int>(ifindex);
        if(::setsockopt(_fd, IPPROTO_IP, IP_MULTICAST_IF,
                        &mreq, sizeof(mreq)) < 0) {
                return Error::syserr();
        }
        return Error::Ok;
}

Error UdpSocket::setReuseAddress(bool enable) {
        if(_fd < 0) return Error::NotOpen;
        return setSocketOption(SOL_SOCKET, SO_REUSEADDR, enable ? 1 : 0);
}

Error UdpSocket::setDscp(uint8_t dscp) {
        if(_fd < 0) return Error::NotOpen;
        int tos = static_cast<int>(dscp) << 2;
        if(_domain == AF_INET6) {
                return setSocketOption(IPPROTO_IPV6, IPV6_TCLASS, tos);
        }
        return setSocketOption(IPPROTO_IP, IP_TOS, tos);
}

PROMEKI_NAMESPACE_END

#endif // !PROMEKI_PLATFORM_EMSCRIPTEN
