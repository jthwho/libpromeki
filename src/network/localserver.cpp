/**
 * @file      localserver.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/localserver.h>
#include <promeki/localsocket.h>
#include <promeki/platform.h>
#include <promeki/logger.h>

#include <cstring>
#include <utility>
#include <vector>

#if defined(PROMEKI_PLATFORM_POSIX)
#       include <errno.h>
#       include <grp.h>
#       include <poll.h>
#       include <sys/socket.h>
#       include <sys/stat.h>
#       include <sys/types.h>
#       include <sys/un.h>
#       include <unistd.h>
#endif

PROMEKI_NAMESPACE_BEGIN

#if defined(PROMEKI_PLATFORM_POSIX)

namespace {

Error fillSunPath(const String &path, struct sockaddr_un &addr,
                  socklen_t &addrLen) {
        if(path.isEmpty()) return Error::Invalid;
        if(path.byteCount() >= sizeof(addr.sun_path)) {
                return Error::InvalidArgument;
        }
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.cstr(), path.byteCount());
        addr.sun_path[path.byteCount()] = '\0';
        addrLen = static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path)
                                        + path.byteCount() + 1);
        return Error::Ok;
}

// Remove a stale socket file, but only if the path exists and is a socket —
// we never clobber a regular file or directory that happens to collide.
Error removeStaleSocket(const String &path) {
        struct stat st{};
        if(::lstat(path.cstr(), &st) != 0) {
                if(errno == ENOENT) return Error::Ok;
                return Error::syserr();
        }
        if(!S_ISSOCK(st.st_mode)) return Error::Exists;
        if(::unlink(path.cstr()) != 0 && errno != ENOENT) {
                return Error::syserr();
        }
        return Error::Ok;
}

Error resolveGid(const String &groupName, gid_t &gidOut) {
        long bufMax = ::sysconf(_SC_GETGR_R_SIZE_MAX);
        if(bufMax <= 0) bufMax = 16384;
        std::vector<char> buf(static_cast<size_t>(bufMax));
        struct group gr;
        struct group *result = nullptr;
        int rc = ::getgrnam_r(groupName.cstr(), &gr, buf.data(), buf.size(),
                              &result);
        if(rc != 0) return Error::syserr(rc);
        if(result == nullptr) return Error::NotExist;
        gidOut = gr.gr_gid;
        return Error::Ok;
}

} // namespace

#endif // PROMEKI_PLATFORM_POSIX

bool LocalServer::isSupported() {
#if defined(PROMEKI_PLATFORM_POSIX)
        return true;
#else
        return false;
#endif
}

LocalServer::LocalServer(ObjectBase *parent) : ObjectBase(parent) {
}

LocalServer::~LocalServer() {
        close();
}

#if defined(PROMEKI_PLATFORM_POSIX)

Error LocalServer::listen(const String &path, uint32_t mode,
                          const String &groupName, int backlog) {
        if(_listening) return Error::AlreadyOpen;
        if(path.isEmpty()) return Error::Invalid;

        // Clean up a stale socket from a previous crashed run.
        Error err = removeStaleSocket(path);
        if(err.isError()) return err;

        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if(fd < 0) return Error::syserr();

        struct sockaddr_un addr;
        socklen_t addrLen = 0;
        err = fillSunPath(path, addr, addrLen);
        if(err.isError()) {
                ::close(fd);
                return err;
        }

        if(::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), addrLen) != 0) {
                Error bindErr = Error::syserr();
                ::close(fd);
                return bindErr;
        }
        _unlinkOnClose = true;

        // Force mode against umask.
        if(::chmod(path.cstr(), static_cast<mode_t>(mode)) != 0) {
                Error chErr = Error::syserr();
                ::close(fd);
                ::unlink(path.cstr());
                _unlinkOnClose = false;
                return chErr;
        }

        if(!groupName.isEmpty()) {
                gid_t gid = 0;
                Error gErr = resolveGid(groupName, gid);
                if(gErr.isError()) {
                        ::close(fd);
                        ::unlink(path.cstr());
                        _unlinkOnClose = false;
                        return gErr;
                }
                if(::chown(path.cstr(), static_cast<uid_t>(-1), gid) != 0) {
                        Error chErr = Error::syserr();
                        ::close(fd);
                        ::unlink(path.cstr());
                        _unlinkOnClose = false;
                        return chErr;
                }
        }

        if(::listen(fd, backlog) != 0) {
                Error lErr = Error::syserr();
                ::close(fd);
                ::unlink(path.cstr());
                _unlinkOnClose = false;
                return lErr;
        }

        _fd = fd;
        _path = path;
        _listening = true;
        return Error::Ok;
}

void LocalServer::close() {
        if(_fd >= 0) {
                ::close(_fd);
                _fd = -1;
        }
        if(_unlinkOnClose && !_path.isEmpty()) {
                if(::unlink(_path.cstr()) != 0 && errno != ENOENT) {
                        promekiWarn("LocalServer: unlink('%s') failed: %s",
                                    _path.cstr(), ::strerror(errno));
                }
                _unlinkOnClose = false;
        }
        _path = String();
        _listening = false;
}

LocalSocket *LocalServer::nextPendingConnection() {
        if(_fd < 0) return nullptr;
        struct sockaddr_un peer;
        socklen_t peerLen = sizeof(peer);
        std::memset(&peer, 0, sizeof(peer));
        int clientFd = ::accept(_fd,
                                reinterpret_cast<struct sockaddr *>(&peer),
                                &peerLen);
        if(clientFd < 0) return nullptr;
        // The peer path is usually empty for unnamed clients; only set
        // it if the kernel returned one.
        String peerPath;
        if(peerLen > offsetof(struct sockaddr_un, sun_path)
           && peer.sun_path[0] != '\0') {
                peerPath = String(peer.sun_path);
        }
        LocalSocket *sock = new LocalSocket();
        sock->setSocketDescriptor(clientFd, peerPath);
        return sock;
}

bool LocalServer::hasPendingConnections() const {
        if(_fd < 0) return false;
        struct pollfd pfd;
        pfd.fd = _fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        return ::poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);
}

Error LocalServer::waitForNewConnection(unsigned int timeoutMs) {
        if(_fd < 0) return Error::NotOpen;
        struct pollfd pfd;
        pfd.fd = _fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int timeout = (timeoutMs == 0) ? -1 : static_cast<int>(timeoutMs);
        int ret = ::poll(&pfd, 1, timeout);
        if(ret < 0) return Error::syserr();
        if(ret == 0) return Error::Timeout;
        if(!(pfd.revents & POLLIN)) return Error::IOError;
        newConnectionSignal.emit();
        return Error::Ok;
}

#else // !PROMEKI_PLATFORM_POSIX

Error LocalServer::listen(const String &, uint32_t, const String &, int) {
        return Error::NotSupported;
}

void LocalServer::close() {
        _fd = -1;
        _listening = false;
        _unlinkOnClose = false;
        _path = String();
}

LocalSocket *LocalServer::nextPendingConnection()               { return nullptr; }
bool LocalServer::hasPendingConnections() const                 { return false; }
Error LocalServer::waitForNewConnection(unsigned int)           { return Error::NotSupported; }

#endif // PROMEKI_PLATFORM_POSIX

PROMEKI_NAMESPACE_END
