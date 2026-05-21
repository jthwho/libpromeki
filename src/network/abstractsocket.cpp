/**
 * @file      abstractsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/abstractsocket.h>
#include <promeki/platform.h>
#include <promeki/logger.h>
#include <cerrno>
#include <cstring>

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#define SOCK_CLOSE(fd) closesocket(fd)
#define SOCK_IOCTL(fd, cmd, arg) ioctlsocket(fd, cmd, arg)
using socklen_t = int;
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#define SOCK_CLOSE(fd) ::close(fd)
#endif

PROMEKI_NAMESPACE_BEGIN

AbstractSocket::AbstractSocket(SocketType type, ObjectBase *parent) : IODevice(parent), _type(type) {}

AbstractSocket::~AbstractSocket() {
        if (_fd >= 0) {
                closeSocket();
        }
}

Error AbstractSocket::bind(const SocketAddress &address) {
        if (_fd < 0) {
                promekiWarn("AbstractSocket::bind(%s) on closed socket", address.toString().cstr());
                return Error::NotOpen;
        }
        struct sockaddr_storage storage;
        size_t                  len = address.toSockAddr(&storage);
        if (len == 0) {
                promekiWarn("AbstractSocket::bind invalid address '%s'", address.toString().cstr());
                return Error::Invalid;
        }
        if (::bind(_fd, reinterpret_cast<struct sockaddr *>(&storage), static_cast<socklen_t>(len)) < 0) {
                promekiWarn("AbstractSocket::bind(%s) failed (errno=%d %s)", address.toString().cstr(), errno,
                            strerror(errno));
                return Error::syserr();
        }
        updateLocalAddress();
        setState(Bound);
        return Error::Ok;
}

Error AbstractSocket::connectToHost(const SocketAddress &address) {
        if (_fd < 0) {
                promekiWarn("AbstractSocket::connectToHost(%s) on closed socket", address.toString().cstr());
                return Error::NotOpen;
        }
        struct sockaddr_storage storage;
        size_t                  len = address.toSockAddr(&storage);
        if (len == 0) {
                promekiWarn("AbstractSocket::connectToHost invalid address '%s'", address.toString().cstr());
                return Error::Invalid;
        }
        if (::connect(_fd, reinterpret_cast<struct sockaddr *>(&storage), static_cast<socklen_t>(len)) < 0) {
#if defined(PROMEKI_PLATFORM_WINDOWS)
                int e = WSAGetLastError();
                if (e == WSAEWOULDBLOCK) {
                        _peerAddress = address;
                        setState(Connecting);
                        return Error::Ok;
                }
#else
                if (errno == EINPROGRESS) {
                        _peerAddress = address;
                        setState(Connecting);
                        return Error::Ok;
                }
#endif
                promekiWarn("AbstractSocket::connect(%s) failed (errno=%d %s)", address.toString().cstr(), errno,
                            strerror(errno));
                return Error::syserr();
        }
        _peerAddress = address;
        updateLocalAddress();
        setState(Connected);
        connectedSignal.emit();
        return Error::Ok;
}

void AbstractSocket::disconnectFromHost() {
        if (_fd < 0) return;
        if (_type == UdpSocketType) {
                // For UDP, "disconnect" by connecting to AF_UNSPEC
                struct sockaddr_storage storage;
                std::memset(&storage, 0, sizeof(storage));
                storage.ss_family = AF_UNSPEC;
                ::connect(_fd, reinterpret_cast<struct sockaddr *>(&storage), sizeof(storage));
                _peerAddress = SocketAddress();
                setState(Bound);
        } else {
                closeSocket();
                setState(Unconnected);
        }
        disconnectedSignal.emit();
}

bool AbstractSocket::waitForReadyRead(unsigned int timeoutMs) {
        if (_fd < 0) return false;
        struct pollfd pfd;
        pfd.fd = _fd;
        pfd.events = POLLIN;
        int timeout = (timeoutMs == 0) ? -1 : static_cast<int>(timeoutMs);
        int ret = ::poll(&pfd, 1, timeout);
        if (ret <= 0) return false;
        // Return true on POLLHUP / POLLERR as well as POLLIN.  POLLHUP
        // is sticky once the peer FINs / RSTs, so a "POLLIN-only" filter
        // would spin every subsequent caller (poll returns instantly,
        // bytesAvailable() reports zero, we'd loop without ever calling
        // read() to surface the EOF).  Callers must treat a subsequent
        // read() of zero as peer-closed.
        return (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

Error AbstractSocket::waitForConnected(unsigned int timeoutMs) {
        if (_state == Connected) return Error::Ok;
        if (_state != Connecting) {
                promekiWarn("AbstractSocket::waitForConnected called in wrong state (state=%d)",
                            static_cast<int>(_state));
                return Error::Invalid;
        }

        struct pollfd pfd;
        pfd.fd = _fd;
        pfd.events = POLLOUT;
        int timeout = (timeoutMs == 0) ? -1 : static_cast<int>(timeoutMs);
        int ret = ::poll(&pfd, 1, timeout);
        if (ret < 0) {
                promekiWarn("AbstractSocket::waitForConnected poll failed (errno=%d %s)", errno, strerror(errno));
                return Error::syserr();
        }
        if (ret == 0) {
                promekiWarn("AbstractSocket::waitForConnected timed out after %u ms (peer=%s)", timeoutMs,
                            _peerAddress.toString().cstr());
                return Error::Timeout;
        }

        // Check for connection error
        int       err = 0;
        socklen_t errLen = sizeof(err);
        if (::getsockopt(_fd, SOL_SOCKET, SO_ERROR, &err, &errLen) < 0) {
                promekiWarn("AbstractSocket::waitForConnected getsockopt SO_ERROR failed (errno=%d %s)", errno,
                            strerror(errno));
                return Error::syserr();
        }
        if (err != 0) {
                promekiWarn("AbstractSocket::waitForConnected connect to %s failed (so_error=%d %s)",
                            _peerAddress.toString().cstr(), err, strerror(err));
                return Error::syserr(err);
        }

        updateLocalAddress();
        setState(Connected);
        connectedSignal.emit();
        return Error::Ok;
}

void AbstractSocket::setSocketDescriptor(int fd) {
        if (_fd >= 0) closeSocket();
        _fd = fd;
        if (fd >= 0) {
                setOpenMode(ReadWrite);
                updateLocalAddress();
        }
}

Error AbstractSocket::setReceiveTimeout(unsigned int timeoutMs) {
        if (_fd < 0) return Error::NotOpen;
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        if (::setsockopt(_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
                return Error::syserr();
        }
        return Error::Ok;
}

Error AbstractSocket::setSendTimeout(unsigned int timeoutMs) {
        if (_fd < 0) return Error::NotOpen;
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        if (::setsockopt(_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
                return Error::syserr();
        }
        return Error::Ok;
}

Error AbstractSocket::setSocketOption(int level, int option, int value) {
        if (_fd < 0) return Error::NotOpen;
        if (::setsockopt(_fd, level, option, &value, sizeof(value)) < 0) {
                return Error::syserr();
        }
        return Error::Ok;
}

Result<int> AbstractSocket::socketOption(int level, int option) const {
        if (_fd < 0) return makeError<int>(Error::NotOpen);
        int       value = 0;
        socklen_t len = sizeof(value);
        if (::getsockopt(_fd, level, option, &value, &len) < 0) {
                return makeError<int>(Error::syserr());
        }
        return makeResult(value);
}

Error AbstractSocket::createSocket(int domain, int type, int protocol) {
        if (_fd >= 0) {
                promekiWarn("AbstractSocket::createSocket called on already-open socket");
                return Error::AlreadyOpen;
        }
        _fd = ::socket(domain, type, protocol);
        if (_fd < 0) {
                promekiWarn("AbstractSocket::createSocket socket(domain=%d type=%d proto=%d) failed (errno=%d %s)",
                            domain, type, protocol, errno, strerror(errno));
                return Error::syserr();
        }
        setOpenMode(ReadWrite);
        setReceiveTimeout(DefaultReceiveTimeoutMs);
        setSendTimeout(DefaultSendTimeoutMs);
        return Error::Ok;
}

void AbstractSocket::closeSocket() {
        if (_fd >= 0) {
                aboutToCloseSignal.emit();
                SOCK_CLOSE(_fd);
                _fd = -1;
                setOpenMode(NotOpen);
                _localAddress = SocketAddress();
                _peerAddress = SocketAddress();
                _state = Unconnected;
        }
}

Error AbstractSocket::setNonBlocking(bool enable) {
        if (_fd < 0) return Error::NotOpen;
#if defined(PROMEKI_PLATFORM_WINDOWS)
        unsigned long mode = enable ? 1 : 0;
        if (SOCK_IOCTL(_fd, FIONBIO, &mode) != 0) return Error::syserr();
#else
        int flags = ::fcntl(_fd, F_GETFL, 0);
        if (flags < 0) return Error::syserr();
        if (enable)
                flags |= O_NONBLOCK;
        else
                flags &= ~O_NONBLOCK;
        if (::fcntl(_fd, F_SETFL, flags) < 0) return Error::syserr();
#endif
        return Error::Ok;
}

void AbstractSocket::updateLocalAddress() {
        if (_fd < 0) return;
        struct sockaddr_storage storage;
        socklen_t               len = sizeof(storage);
        if (::getsockname(_fd, reinterpret_cast<struct sockaddr *>(&storage), &len) == 0) {
                auto [addr, err] = SocketAddress::fromSockAddr(reinterpret_cast<struct sockaddr *>(&storage), len);
                if (err.isOk()) _localAddress = addr;
        }
}

void AbstractSocket::setState(SocketState state) {
        if (_state != state) {
                _state = state;
                stateChangedSignal.emit(state);
        }
}

PROMEKI_NAMESPACE_END

#endif // !PROMEKI_PLATFORM_EMSCRIPTEN
