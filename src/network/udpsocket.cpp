/**
 * @file      udpsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/udpsocket.h>
#include <promeki/platform.h>
#include <promeki/list.h>
#include <cstring>
#include <cerrno>

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

#if defined(PROMEKI_PLATFORM_LINUX)
#       include <linux/net_tstamp.h>
#       ifndef SO_TXTIME
#               define SO_TXTIME 61
#       endif
#       ifndef SCM_TXTIME
#               define SCM_TXTIME SO_TXTIME
#       endif
#       ifndef SO_MAX_PACING_RATE
#               define SO_MAX_PACING_RATE 47
#       endif
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

int64_t UdpSocket::writeDatagram(const void *data, size_t size, const SocketAddress &dest) {
        if(_fd < 0) return -1;
        struct sockaddr_storage storage;
        size_t addrLen = dest.toSockAddr(&storage);
        if(addrLen == 0) return -1;
        return ::sendto(_fd, data, size, 0,
                        reinterpret_cast<struct sockaddr *>(&storage),
                        static_cast<socklen_t>(addrLen));
}

int64_t UdpSocket::writeDatagram(const Buffer &data, const SocketAddress &dest) {
        return writeDatagram(data.data(), data.size(), dest);
}

int UdpSocket::writeDatagrams(const DatagramList &datagrams) {
        if(_fd < 0) return -1;
        if(datagrams.isEmpty()) return 0;

#if defined(PROMEKI_PLATFORM_LINUX)
        // Scratch storage for the per-datagram descriptors.  Held as
        // parallel arrays so the msghdr entries can reference them by
        // pointer without aliasing bugs on vector resize.
        const size_t count = datagrams.size();
        List<struct mmsghdr>        msgs;
        List<struct iovec>          iovs;
        List<struct sockaddr_storage> addrs;
        // Control buffer for an optional SCM_TXTIME cmsg per datagram.
        // Allocated whether or not any datagram actually uses txTime
        // because the per-message pointer into the shared control
        // block must be stable.
        static constexpr size_t kCmsgSpace = CMSG_SPACE(sizeof(uint64_t));
        List<uint8_t>               ctrl;
        msgs.resize(count);
        iovs.resize(count);
        addrs.resize(count);
        ctrl.resize(count * kCmsgSpace);
        std::memset(msgs.data(), 0, count * sizeof(struct mmsghdr));
        std::memset(ctrl.data(), 0, count * kCmsgSpace);

        bool anyTxTime = false;
        for(size_t i = 0; i < count; i++) {
                const Datagram &d = datagrams[i];
                if(d.data == nullptr || d.size == 0) return -1;

                size_t addrLen = d.dest.toSockAddr(&addrs[i]);
                if(addrLen == 0) return -1;

                iovs[i].iov_base = const_cast<void *>(d.data);
                iovs[i].iov_len  = d.size;

                struct msghdr &mh = msgs[i].msg_hdr;
                mh.msg_name    = &addrs[i];
                mh.msg_namelen = static_cast<socklen_t>(addrLen);
                mh.msg_iov     = &iovs[i];
                mh.msg_iovlen  = 1;

                if(d.txTimeNs != 0) {
                        anyTxTime = true;
                        uint8_t *cb = ctrl.data() + i * kCmsgSpace;
                        mh.msg_control    = cb;
                        mh.msg_controllen = kCmsgSpace;
                        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
                        cm->cmsg_level = SOL_SOCKET;
                        cm->cmsg_type  = SCM_TXTIME;
                        cm->cmsg_len   = CMSG_LEN(sizeof(uint64_t));
                        uint64_t v = d.txTimeNs;
                        std::memcpy(CMSG_DATA(cm), &v, sizeof(v));
                }
        }
        (void)anyTxTime;

        int sent = ::sendmmsg(_fd, msgs.data(),
                              static_cast<unsigned int>(count), 0);
        if(sent < 0) {
                setError(Error::syserr());
                return -1;
        }
        return sent;
#else
        // Portable fallback: loop on writeDatagram().  Returns the
        // count of datagrams accepted before the first failure.
        int sent = 0;
        for(size_t i = 0; i < datagrams.size(); i++) {
                const Datagram &d = datagrams[i];
                int64_t n = writeDatagram(d.data, d.size, d.dest);
                if(n < 0) {
                        return sent > 0 ? sent : -1;
                }
                sent++;
        }
        return sent;
#endif
}

Error UdpSocket::setPacingRate(uint64_t bytesPerSec) {
        if(_fd < 0) return Error::NotOpen;
#if defined(PROMEKI_PLATFORM_LINUX)
        // Kernel caps at 32 Gbps via a 32-bit setsockopt argument
        // before ~5.x; clamp to UINT32_MAX to match the older ABI.
        uint32_t rate = bytesPerSec > 0xFFFFFFFFu
                ? 0xFFFFFFFFu
                : static_cast<uint32_t>(bytesPerSec);
        if(::setsockopt(_fd, SOL_SOCKET, SO_MAX_PACING_RATE,
                        &rate, sizeof(rate)) < 0) {
                return Error::syserr();
        }
        return Error::Ok;
#else
        (void)bytesPerSec;
        return Error::NotSupported;
#endif
}

Error UdpSocket::clearPacingRate() {
        return setPacingRate(PacingRateUnlimited);
}

Error UdpSocket::setTxTime(bool enable, int clockId) {
        if(_fd < 0) return Error::NotOpen;
#if defined(PROMEKI_PLATFORM_LINUX)
        // Disabling is a no-op: a socket that was never configured
        // for SO_TXTIME (or that stops passing SCM_TXTIME cmsgs)
        // behaves identically to a plain UDP socket.  There is no
        // kernel-level "off" switch, so we just skip the setsockopt
        // and report success.
        if(!enable) return Error::Ok;
        struct sock_txtime cfg;
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.clockid = static_cast<clockid_t>(clockId);
        cfg.flags   = 0;
        if(::setsockopt(_fd, SOL_SOCKET, SO_TXTIME,
                        &cfg, sizeof(cfg)) < 0) {
                return Error::syserr();
        }
        return Error::Ok;
#else
        (void)enable;
        (void)clockId;
        return Error::NotSupported;
#endif
}

int64_t UdpSocket::readDatagram(void *data, size_t maxSize, SocketAddress *sender) {
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
        return static_cast<int64_t>(ret);
}

bool UdpSocket::hasPendingDatagrams() const {
        return bytesAvailable() > 0;
}

int64_t UdpSocket::pendingDatagramSize() const {
        if(_fd < 0) return -1;
        char buf;
        ssize_t ret = ::recv(_fd, &buf, 1, MSG_PEEK | MSG_TRUNC);
        return static_cast<int64_t>(ret);
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
