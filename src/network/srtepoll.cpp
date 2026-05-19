/**
 * @file      srtepoll.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/srtepoll.h>
#include <promeki/srtsocket.h>
#include <promeki/srtserver.h>
#include <promeki/logger.h>

#include <vector>

#include <srt/srt.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SrtEpoll);

namespace {

        // Convert promeki Event flags to SRT_EPOLL_OPT bitmask.
        static int toSrtFlags(int events) {
                int out = 0;
                if (events & SrtEpoll::ReadReady)  out |= SRT_EPOLL_IN;
                if (events & SrtEpoll::WriteReady) out |= SRT_EPOLL_OUT;
                if (events & SrtEpoll::ErrorEvent) out |= SRT_EPOLL_ERR;
                return out;
        }

        // Convert SRT_EPOLL_OPT bitmask to promeki Event flags.
        static int fromSrtFlags(int events) {
                int out = 0;
                if (events & SRT_EPOLL_IN)  out |= SrtEpoll::ReadReady;
                if (events & SRT_EPOLL_OUT) out |= SrtEpoll::WriteReady;
                if (events & SRT_EPOLL_ERR) out |= SrtEpoll::ErrorEvent;
                return out;
        }

} // anonymous namespace

SrtEpoll::SrtEpoll() = default;

SrtEpoll::~SrtEpoll() {
        close();
}

Error SrtEpoll::open() {
        if (_eid != InvalidId) return Error::Ok;
        _eid = srt_epoll_create();
        if (_eid < 0) {
                _eid = InvalidId;
                promekiWarn("SrtEpoll: srt_epoll_create failed: %s", srt_getlasterror_str());
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

void SrtEpoll::close() {
        if (_eid == InvalidId) return;
        srt_epoll_release(_eid);
        _eid = InvalidId;
}

Error SrtEpoll::add(SrtSocket &sock, int events) {
        if (!sock.isOpen()) return Error::NotOpen;
        Error e = open();
        if (e.isError()) return e;
        const int srtFlags = toSrtFlags(events);
        if (srt_epoll_add_usock(_eid, sock.handle(), &srtFlags) == SRT_ERROR) {
                promekiWarn("SrtEpoll: add_usock failed: %s", srt_getlasterror_str());
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtEpoll::add(SrtServer &server, int events) {
        if (server.handle() == SrtSocket::InvalidHandle) return Error::NotOpen;
        Error e = open();
        if (e.isError()) return e;
        const int srtFlags = toSrtFlags(events);
        if (srt_epoll_add_usock(_eid, server.handle(), &srtFlags) == SRT_ERROR) {
                promekiWarn("SrtEpoll: add_usock(server) failed: %s", srt_getlasterror_str());
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtEpoll::modify(SrtSocket &sock, int events) {
        if (_eid == InvalidId) return Error::NotOpen;
        const int srtFlags = toSrtFlags(events);
        if (srt_epoll_update_usock(_eid, sock.handle(), &srtFlags) == SRT_ERROR) {
                promekiWarn("SrtEpoll: update_usock failed: %s", srt_getlasterror_str());
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtEpoll::remove(SrtSocket &sock) {
        if (_eid == InvalidId) return Error::NotOpen;
        if (srt_epoll_remove_usock(_eid, sock.handle()) == SRT_ERROR) {
                promekiWarn("SrtEpoll: remove_usock failed: %s", srt_getlasterror_str());
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtEpoll::remove(SrtServer &server) {
        if (_eid == InvalidId) return Error::NotOpen;
        if (srt_epoll_remove_usock(_eid, server.handle()) == SRT_ERROR) {
                promekiWarn("SrtEpoll: remove_usock(server) failed: %s", srt_getlasterror_str());
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtEpoll::setCallback(SrtSocket &sock, ReadyCallback cb) {
        if (!sock.isOpen()) return Error::NotOpen;
        if (cb) _callbacks.insert(sock.handle(), std::move(cb));
        else    _callbacks.remove(sock.handle());
        return Error::Ok;
}

Error SrtEpoll::setCallback(SrtServer &server, ReadyCallback cb) {
        if (server.handle() == SrtSocket::InvalidHandle) return Error::NotOpen;
        if (cb) _callbacks.insert(server.handle(), std::move(cb));
        else    _callbacks.remove(server.handle());
        return Error::Ok;
}

int SrtEpoll::dispatchOnce(int timeoutMs) {
        ReadyList ready;
        const int n = wait(ready, timeoutMs);
        if (n <= 0) return n;
        int dispatched = 0;
        for (size_t i = 0; i < ready.size(); ++i) {
                const Ready &r = ready[i];
                auto         it = _callbacks.find(r.handle);
                if (it == _callbacks.end()) continue;
                it->second(r.events);
                ++dispatched;
        }
        return dispatched;
}

int SrtEpoll::run() {
        _stopRequested.store(false, MemoryOrder::Release);
        int total = 0;
        // Bounded wait so a concurrent stop() lands within ~100ms; SRT
        // epoll has no equivalent of eventfd-based wakeup, so polling
        // is the simplest way to keep stop() responsive without
        // smuggling a sentinel through the multiplexer.
        while (!_stopRequested.load(MemoryOrder::Acquire)) {
                const int n = dispatchOnce(100);
                if (n > 0) total += n;
                if (n < 0) break; // hard error — bail out
        }
        _stopRequested.store(false, MemoryOrder::Release);
        return total;
}

void SrtEpoll::stop() {
        _stopRequested.store(true, MemoryOrder::Release);
}

int SrtEpoll::wait(ReadyList &ready, int timeoutMs) {
        ready.clear();
        if (_eid == InvalidId) return -1;
        // Sized to a single iteration's batch — most callers will see
        // far fewer ready entries than this in any given wait, and
        // libsrt itself batches internally.  If callers need higher
        // throughput multiplexing, multiple SrtEpoll instances on
        // worker threads scale better than a deeper batch here.
        constexpr int          kBatch = 64;
        SRT_EPOLL_EVENT        events[kBatch];
        const int              n = srt_epoll_uwait(_eid, events, kBatch, timeoutMs);
        if (n < 0) {
                promekiWarn("SrtEpoll: uwait failed: %s", srt_getlasterror_str());
                return -1;
        }
        for (int i = 0; i < n; ++i) {
                Ready r;
                r.handle = events[i].fd;
                r.events = fromSrtFlags(events[i].events);
                ready.pushToBack(r);
        }
        return n;
}

PROMEKI_NAMESPACE_END
