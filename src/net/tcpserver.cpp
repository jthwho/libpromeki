/**
 * @file      net/tcpserver.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/network/tcpserver.h>
#include <promeki/network/tcpsocket.h>
#include <promeki/core/platform.h>

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)

#if defined(PROMEKI_PLATFORM_WINDOWS)
#       include <winsock2.h>
#       include <ws2tcpip.h>
#       define SOCK_CLOSE(fd) closesocket(fd)
        using socklen_t = int;
#else
#       include <unistd.h>
#       include <sys/socket.h>
#       include <netinet/in.h>
#       include <poll.h>
#endif

PROMEKI_NAMESPACE_BEGIN

TcpServer::TcpServer(ObjectBase *parent) : ObjectBase(parent) {
}

TcpServer::~TcpServer() {
        if(_listening) close();
}

Error TcpServer::listen(const SocketAddress &address, int backlog) {
        if(_listening) return Error::AlreadyOpen;

        // Determine address family
        int domain = AF_INET;
        if(address.isIPv6()) domain = AF_INET6;

        _fd = ::socket(domain, SOCK_STREAM, 0);
        if(_fd < 0) return Error::syserr();

        // Enable SO_REUSEADDR
        int reuse = 1;
        ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        struct sockaddr_storage storage;
        size_t addrLen = address.toSockAddr(&storage);
        if(addrLen == 0) {
#if defined(PROMEKI_PLATFORM_WINDOWS)
                SOCK_CLOSE(_fd);
#else
                ::close(_fd);
#endif
                _fd = -1;
                return Error::Invalid;
        }

        if(::bind(_fd, reinterpret_cast<struct sockaddr *>(&storage),
                  static_cast<socklen_t>(addrLen)) < 0) {
                Error err = Error::syserr();
#if defined(PROMEKI_PLATFORM_WINDOWS)
                SOCK_CLOSE(_fd);
#else
                ::close(_fd);
#endif
                _fd = -1;
                return err;
        }

        if(::listen(_fd, backlog) < 0) {
                Error err = Error::syserr();
#if defined(PROMEKI_PLATFORM_WINDOWS)
                SOCK_CLOSE(_fd);
#else
                ::close(_fd);
#endif
                _fd = -1;
                return err;
        }

        // Capture the actual bound address (port may have been 0)
        socklen_t slen = sizeof(storage);
        if(::getsockname(_fd, reinterpret_cast<struct sockaddr *>(&storage), &slen) == 0) {
                auto [addr, err] = SocketAddress::fromSockAddr(
                        reinterpret_cast<struct sockaddr *>(&storage), slen);
                if(err.isOk()) _address = addr;
        }

        _listening = true;
        return Error::Ok;
}

void TcpServer::close() {
        if(_fd >= 0) {
#if defined(PROMEKI_PLATFORM_WINDOWS)
                SOCK_CLOSE(_fd);
#else
                ::close(_fd);
#endif
                _fd = -1;
        }
        _listening = false;
        _address = SocketAddress();
}

TcpSocket *TcpServer::nextPendingConnection() {
        if(_fd < 0) return nullptr;
        struct sockaddr_storage storage;
        socklen_t addrLen = sizeof(storage);
        int clientFd = ::accept(_fd, reinterpret_cast<struct sockaddr *>(&storage), &addrLen);
        if(clientFd < 0) return nullptr;
        TcpSocket *sock = new TcpSocket();
        sock->setSocketDescriptor(clientFd);
        return sock;
}

bool TcpServer::hasPendingConnections() const {
        if(_fd < 0) return false;
        struct pollfd pfd;
        pfd.fd = _fd;
        pfd.events = POLLIN;
        return ::poll(&pfd, 1, 0) > 0;
}

Error TcpServer::waitForNewConnection(unsigned int timeoutMs) {
        if(_fd < 0) return Error::NotOpen;
        struct pollfd pfd;
        pfd.fd = _fd;
        pfd.events = POLLIN;
        int timeout = (timeoutMs == 0) ? -1 : static_cast<int>(timeoutMs);
        int ret = ::poll(&pfd, 1, timeout);
        if(ret < 0) return Error::syserr();
        if(ret == 0) return Error::Timeout;
        newConnectionSignal.emit();
        return Error::Ok;
}

PROMEKI_NAMESPACE_END

#endif // !PROMEKI_PLATFORM_EMSCRIPTEN
