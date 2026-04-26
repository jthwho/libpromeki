/**
 * @file      localsocket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/localsocket.h>
#include <promeki/platform.h>
#include <promeki/logger.h>

#include <cstring>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

PROMEKI_NAMESPACE_BEGIN

#if defined(PROMEKI_PLATFORM_POSIX)

namespace {

        Error fillSunPath(const String &path, struct sockaddr_un &addr, socklen_t &addrLen) {
                if (path.isEmpty()) return Error::Invalid;
                // sun_path is a C-string buffer with a fixed size.  Leave room
                // for the trailing NUL; reject paths that would be truncated.
                if (path.byteCount() >= sizeof(addr.sun_path)) {
                        return Error::InvalidArgument;
                }
                std::memset(&addr, 0, sizeof(addr));
                addr.sun_family = AF_UNIX;
                std::memcpy(addr.sun_path, path.cstr(), path.byteCount());
                addr.sun_path[path.byteCount()] = '\0';
                addrLen = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + path.byteCount() + 1);
                return Error::Ok;
        }

        Error applyTimeout(int fd, int optName, unsigned int timeoutMs) {
                struct timeval tv;
                tv.tv_sec = static_cast<time_t>(timeoutMs / 1000);
                tv.tv_usec = static_cast<suseconds_t>((timeoutMs % 1000) * 1000);
                if (::setsockopt(fd, SOL_SOCKET, optName, &tv, sizeof(tv)) != 0) {
                        return Error::syserr();
                }
                return Error::Ok;
        }

} // namespace

#endif // PROMEKI_PLATFORM_POSIX

bool LocalSocket::isSupported() {
#if defined(PROMEKI_PLATFORM_POSIX)
        return true;
#else
        return false;
#endif
}

LocalSocket::LocalSocket(ObjectBase *parent) : IODevice(parent) {}

LocalSocket::~LocalSocket() {
        close();
}

#if defined(PROMEKI_PLATFORM_POSIX)

Error LocalSocket::open(OpenMode mode) {
        if (isOpen()) return Error::AlreadyOpen;
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return Error::syserr();
        _fd = fd;
        _connected = false;
        _peerPath = String();
        setOpenMode(mode);
        // Apply default timeouts; callers can override afterwards.
        applyTimeout(_fd, SO_RCVTIMEO, DefaultReceiveTimeoutMs);
        applyTimeout(_fd, SO_SNDTIMEO, DefaultSendTimeoutMs);
        return Error::Ok;
}

Error LocalSocket::close() {
        if (_fd >= 0) {
                if (_connected) {
                        _connected = false;
                        disconnectedSignal.emit();
                }
                ::close(_fd);
                _fd = -1;
        }
        _peerPath = String();
        setOpenMode(NotOpen);
        return Error::Ok;
}

Error LocalSocket::connectTo(const String &path, unsigned int /*timeoutMs*/) {
        if (_connected) return Error::AlreadyOpen;
        if (!isOpen()) {
                Error err = open(ReadWrite);
                if (err.isError()) return err;
        }

        struct sockaddr_un addr;
        socklen_t          addrLen = 0;
        Error              err = fillSunPath(path, addr, addrLen);
        if (err.isError()) return err;

        if (::connect(_fd, reinterpret_cast<struct sockaddr *>(&addr), addrLen) != 0) {
                return Error::syserr();
        }

        _connected = true;
        _peerPath = path;
        connectedSignal.emit();
        return Error::Ok;
}

int64_t LocalSocket::read(void *data, int64_t maxSize) {
        if (_fd < 0) return -1;
        ssize_t ret = ::recv(_fd, data, static_cast<size_t>(maxSize), 0);
        if (ret < 0) {
                errorOccurredSignal.emit(Error::syserr());
                return -1;
        }
        if (ret == 0 && _connected) {
                _connected = false;
                disconnectedSignal.emit();
        }
        return static_cast<int64_t>(ret);
}

int64_t LocalSocket::write(const void *data, int64_t maxSize) {
        if (_fd < 0) return -1;
        // MSG_NOSIGNAL avoids SIGPIPE when the peer has already closed.
        ssize_t ret = ::send(_fd, data, static_cast<size_t>(maxSize), MSG_NOSIGNAL);
        if (ret < 0) {
                int e = errno;
                errorOccurredSignal.emit(Error::syserr(e));
                // Terminal peer-gone errors: flip the socket to
                // disconnected so callers' isConnected() checks see
                // the truth without having to probe for EOF.  Without
                // this the caller keeps invoking write() and logging
                // the same failure over and over.
                if (e == EPIPE || e == ECONNRESET) {
                        if (_connected) {
                                _connected = false;
                                disconnectedSignal.emit();
                        }
                }
                return -1;
        }
        return static_cast<int64_t>(ret);
}

int64_t LocalSocket::bytesAvailable() const {
        if (_fd < 0) return 0;
        int bytes = 0;
        if (::ioctl(_fd, FIONREAD, &bytes) < 0) return 0;
        return static_cast<int64_t>(bytes);
}

void LocalSocket::setSocketDescriptor(int fd, const String &peerPath) {
        close();
        _fd = fd;
        _connected = (fd >= 0);
        _peerPath = peerPath;
        if (fd >= 0) {
                setOpenMode(ReadWrite);
                applyTimeout(_fd, SO_RCVTIMEO, DefaultReceiveTimeoutMs);
                applyTimeout(_fd, SO_SNDTIMEO, DefaultSendTimeoutMs);
        }
}

Error LocalSocket::setReceiveTimeout(unsigned int timeoutMs) {
        if (_fd < 0) return Error::NotOpen;
        return applyTimeout(_fd, SO_RCVTIMEO, timeoutMs);
}

Error LocalSocket::setSendTimeout(unsigned int timeoutMs) {
        if (_fd < 0) return Error::NotOpen;
        return applyTimeout(_fd, SO_SNDTIMEO, timeoutMs);
}

#else // !PROMEKI_PLATFORM_POSIX

Error LocalSocket::open(OpenMode) {
        return Error::NotSupported;
}
Error LocalSocket::close() {
        return Error::NotSupported;
}
Error LocalSocket::connectTo(const String &, unsigned int) {
        return Error::NotSupported;
}
int64_t LocalSocket::read(void *, int64_t) {
        return -1;
}
int64_t LocalSocket::write(const void *, int64_t) {
        return -1;
}
int64_t LocalSocket::bytesAvailable() const {
        return 0;
}
void  LocalSocket::setSocketDescriptor(int, const String &) {}
Error LocalSocket::setReceiveTimeout(unsigned int) {
        return Error::NotSupported;
}
Error LocalSocket::setSendTimeout(unsigned int) {
        return Error::NotSupported;
}

#endif // PROMEKI_PLATFORM_POSIX

PROMEKI_NAMESPACE_END
