/**
 * @file      linuxnetworkinterfacemonitor.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_LINUX)

#include <promeki/networkinterfacemonitor.h>
#include <promeki/eventloop.h>
#include <promeki/mutex.h>
#include <promeki/map.h>
#include <promeki/logger.h>

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(NetworkInterfaceMonitorLinux)

namespace {

        // Per-monitor netlink state.  Stored in a process-global side
        // table keyed by monitor pointer so the public ABI doesn't
        // grow with platform-specific data.
        struct LinuxState {
                int fd     = -1;
                int handle = -1;
        };

        struct StateRegistry {
                Mutex                                       mutex;
                Map<NetworkInterfaceMonitor *, LinuxState *>states;
        };

        StateRegistry &states() {
                static StateRegistry r;
                return r;
        }

        LinuxState *get(NetworkInterfaceMonitor *m) {
                StateRegistry &r = states();
                Mutex::Locker  lock(r.mutex);
                auto           it = r.states.find(m);
                return it == r.states.end() ? nullptr : it->second;
        }

        LinuxState *getOrCreate(NetworkInterfaceMonitor *m) {
                StateRegistry &r = states();
                Mutex::Locker  lock(r.mutex);
                auto           it = r.states.find(m);
                if (it != r.states.end()) return it->second;
                LinuxState *s = new LinuxState();
                r.states.insert(m, s);
                return s;
        }

        void destroyState(NetworkInterfaceMonitor *m) {
                StateRegistry &r = states();
                Mutex::Locker  lock(r.mutex);
                auto           it = r.states.find(m);
                if (it == r.states.end()) return;
                delete it->second;
                r.states.remove(it);
        }

}

Error networkInterfaceMonitorPlatformOpen(NetworkInterfaceMonitor *m) {
        LinuxState *s = getOrCreate(m);
        s->fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE);
        if (s->fd < 0) {
                int e = errno;
                promekiWarn("NetworkInterfaceMonitor: AF_NETLINK socket() failed: %s", std::strerror(e));
                destroyState(m);
                return Error::syserr(e);
        }
        struct sockaddr_nl addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.nl_family = AF_NETLINK;
        addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR | RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE;
        if (::bind(s->fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
                int e = errno;
                promekiWarn("NetworkInterfaceMonitor: AF_NETLINK bind() failed: %s", std::strerror(e));
                ::close(s->fd);
                destroyState(m);
                return Error::syserr(e);
        }
        EventLoop *loop = m->eventLoop();
        if (loop == nullptr) {
                ::close(s->fd);
                destroyState(m);
                return Error::NotImplemented;
        }
        s->handle = loop->addIoSource(s->fd, EventLoop::IoRead, [m](int fd, uint32_t /*events*/) {
                // Drain whatever's pending on the netlink socket; the
                // RTM_* messages themselves don't drive logic — we
                // re-enumerate from scratch in the diff cycle.
                char    buf[8192];
                ssize_t n = 0;
                do {
                        n = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
                } while (n > 0);
                m->kickDebounce();
        });
        if (s->handle < 0) {
                ::close(s->fd);
                destroyState(m);
                return Error::NotImplemented;
        }
        return Error::Ok;
}

void networkInterfaceMonitorPlatformClose(NetworkInterfaceMonitor *m) {
        LinuxState *s = get(m);
        if (s == nullptr) return;
        if (s->handle >= 0) {
                if (EventLoop *loop = m->eventLoop()) loop->removeIoSource(s->handle);
                s->handle = -1;
        }
        if (s->fd >= 0) {
                ::close(s->fd);
                s->fd = -1;
        }
        destroyState(m);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_PLATFORM_LINUX
