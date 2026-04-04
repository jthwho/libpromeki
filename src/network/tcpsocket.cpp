/**
 * @file      tcpsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tcpsocket.h>
#include <promeki/platform.h>

#if !defined(PROMEKI_PLATFORM_EMSCRIPTEN)

#if defined(PROMEKI_PLATFORM_WINDOWS)
#       include <winsock2.h>
#       include <ws2tcpip.h>
#else
#       include <sys/socket.h>
#       include <sys/ioctl.h>
#       include <netinet/in.h>
#       include <netinet/tcp.h>
#endif

PROMEKI_NAMESPACE_BEGIN

TcpSocket::TcpSocket(ObjectBase *parent)
        : AbstractSocket(TcpSocketType, parent) {
}

TcpSocket::~TcpSocket() {
        if(isOpen()) close();
}

Error TcpSocket::open(OpenMode mode) {
        Error err = createSocket(AF_INET, SOCK_STREAM, 0);
        if(err.isError()) return err;
        setOpenMode(mode);
        return Error::Ok;
}

Error TcpSocket::openIpv6(OpenMode mode) {
        Error err = createSocket(AF_INET6, SOCK_STREAM, 0);
        if(err.isError()) return err;
        int v6only = 0;
        setSocketOption(IPPROTO_IPV6, IPV6_V6ONLY, v6only);
        setOpenMode(mode);
        return Error::Ok;
}

Error TcpSocket::close() {
        closeSocket();
        return Error::Ok;
}

int64_t TcpSocket::read(void *data, int64_t maxSize) {
        if(_fd < 0) return -1;
        ssize_t ret = ::recv(_fd, data, static_cast<size_t>(maxSize), 0);
        if(ret < 0) {
                setError(Error::syserr());
                return -1;
        }
        if(ret == 0) {
                // Peer disconnected
                setState(Unconnected);
                disconnectedSignal.emit();
        }
        return static_cast<int64_t>(ret);
}

int64_t TcpSocket::write(const void *data, int64_t maxSize) {
        if(_fd < 0) return -1;
        ssize_t ret = ::send(_fd, data, static_cast<size_t>(maxSize), MSG_NOSIGNAL);
        if(ret < 0) {
                setError(Error::syserr());
                return -1;
        }
        return static_cast<int64_t>(ret);
}

int64_t TcpSocket::bytesAvailable() const {
        if(_fd < 0) return 0;
        int bytes = 0;
        if(::ioctl(_fd, FIONREAD, &bytes) < 0) return 0;
        return static_cast<int64_t>(bytes);
}

Error TcpSocket::setNoDelay(bool enable) {
        if(_fd < 0) return Error::NotOpen;
        return setSocketOption(IPPROTO_TCP, TCP_NODELAY, enable ? 1 : 0);
}

Error TcpSocket::setKeepAlive(bool enable) {
        if(_fd < 0) return Error::NotOpen;
        return setSocketOption(SOL_SOCKET, SO_KEEPALIVE, enable ? 1 : 0);
}

PROMEKI_NAMESPACE_END

#endif // !PROMEKI_PLATFORM_EMSCRIPTEN
