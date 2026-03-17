/**
 * @file      net/rawsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/network/rawsocket.h>
#include <promeki/core/platform.h>

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)

#if defined(PROMEKI_PLATFORM_LINUX)
#       include <sys/socket.h>
#       include <linux/if_packet.h>
#       include <linux/if_ether.h>
#       include <net/if.h>
#       include <cstring>
#       include <unistd.h>
#elif defined(PROMEKI_PLATFORM_APPLE)
        // BPF support deferred
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        // Raw sockets on Windows require different approach
#endif

PROMEKI_NAMESPACE_BEGIN

RawSocket::RawSocket(ObjectBase *parent)
        : AbstractSocket(RawSocketType, parent) {
}

RawSocket::~RawSocket() {
        if(isOpen()) close();
}

Error RawSocket::open(OpenMode mode) {
#if defined(PROMEKI_PLATFORM_LINUX)
        int protocol = (_protocol != 0) ? htons(_protocol) : htons(ETH_P_ALL);
        _fd = ::socket(AF_PACKET, SOCK_RAW, protocol);
        if(_fd < 0) {
                if(errno == EPERM || errno == EACCES) return Error::PermissionDenied;
                return Error::syserr();
        }

        // Bind to specific interface if set
        if(!_interface.isEmpty()) {
                unsigned int ifindex = if_nametoindex(_interface.cstr());
                if(ifindex == 0) {
                        ::close(_fd);
                        _fd = -1;
                        return Error::Invalid;
                }
                struct sockaddr_ll sll;
                std::memset(&sll, 0, sizeof(sll));
                sll.sll_family = AF_PACKET;
                sll.sll_protocol = protocol;
                sll.sll_ifindex = static_cast<int>(ifindex);
                if(::bind(_fd, reinterpret_cast<struct sockaddr *>(&sll), sizeof(sll)) < 0) {
                        Error err = Error::syserr();
                        ::close(_fd);
                        _fd = -1;
                        return err;
                }
        }

        setOpenMode(mode);
        return Error::Ok;
#else
        (void)mode;
        return Error::NotSupported;
#endif
}

Error RawSocket::close() {
        closeSocket();
        return Error::Ok;
}

int64_t RawSocket::read(void *data, int64_t maxSize) {
        if(_fd < 0) return -1;
        ssize_t ret = ::recv(_fd, data, static_cast<size_t>(maxSize), 0);
        if(ret < 0) {
                setError(Error::syserr());
                return -1;
        }
        return static_cast<int64_t>(ret);
}

int64_t RawSocket::write(const void *data, int64_t maxSize) {
        if(_fd < 0) return -1;
        ssize_t ret = ::send(_fd, data, static_cast<size_t>(maxSize), 0);
        if(ret < 0) {
                setError(Error::syserr());
                return -1;
        }
        return static_cast<int64_t>(ret);
}

Error RawSocket::setPromiscuous(bool enable) {
#if defined(PROMEKI_PLATFORM_LINUX)
        if(_fd < 0) return Error::NotOpen;
        if(_interface.isEmpty()) return Error::Invalid;
        unsigned int ifindex = if_nametoindex(_interface.cstr());
        if(ifindex == 0) return Error::Invalid;
        struct packet_mreq mreq;
        std::memset(&mreq, 0, sizeof(mreq));
        mreq.mr_ifindex = static_cast<int>(ifindex);
        mreq.mr_type = PACKET_MR_PROMISC;
        int action = enable ? PACKET_ADD_MEMBERSHIP : PACKET_DROP_MEMBERSHIP;
        if(::setsockopt(_fd, SOL_PACKET, action, &mreq, sizeof(mreq)) < 0) {
                return Error::syserr();
        }
        return Error::Ok;
#else
        (void)enable;
        return Error::NotSupported;
#endif
}

PROMEKI_NAMESPACE_END

#endif // !PROMEKI_PLATFORM_EMSCRIPTEN
