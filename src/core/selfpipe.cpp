/**
 * @file      selfpipe.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/selfpipe.h>
#include <promeki/logger.h>
#include <promeki/platform.h>

#include <cerrno>
#include <cstring>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <unistd.h>
#include <fcntl.h>
#endif

PROMEKI_NAMESPACE_BEGIN

#if defined(PROMEKI_PLATFORM_POSIX)

SelfPipe::SelfPipe() {
        int fds[2];
#if defined(PROMEKI_PLATFORM_LINUX)
        if(::pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0) {
                promekiWarn("SelfPipe: pipe2() failed (errno %d: %s)",
                            errno, std::strerror(errno));
                return;
        }
#else
        if(::pipe(fds) != 0) {
                promekiWarn("SelfPipe: pipe() failed (errno %d: %s)",
                            errno, std::strerror(errno));
                return;
        }
        for(int i = 0; i < 2; ++i) {
                int flags = ::fcntl(fds[i], F_GETFD);
                if(flags >= 0) ::fcntl(fds[i], F_SETFD, flags | FD_CLOEXEC);
        }
        for(int i = 0; i < 2; ++i) {
                int flags = ::fcntl(fds[i], F_GETFL);
                if(flags >= 0) ::fcntl(fds[i], F_SETFL, flags | O_NONBLOCK);
        }
#endif
        _readFd  = fds[0];
        _writeFd = fds[1];
}

SelfPipe::~SelfPipe() {
        if(_writeFd >= 0) ::close(_writeFd);
        if(_readFd  >= 0) ::close(_readFd);
        _readFd  = -1;
        _writeFd = -1;
}

void SelfPipe::wake() {
        if(_writeFd < 0) return;
        char byte = 1;
        ssize_t n = ::write(_writeFd, &byte, 1);
        (void)n;
}

void SelfPipe::drain() {
        if(_readFd < 0) return;
        char buf[64];
        while(::read(_readFd, buf, sizeof(buf)) > 0) {
                // keep draining
        }
}

#else // !PROMEKI_PLATFORM_POSIX

SelfPipe::SelfPipe()  { }
SelfPipe::~SelfPipe() { }
void SelfPipe::wake() { }
void SelfPipe::drain() { }

#endif

PROMEKI_NAMESPACE_END
