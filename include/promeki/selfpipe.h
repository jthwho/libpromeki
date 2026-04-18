/**
 * @file      selfpipe.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Nonblocking self-pipe used to wake a poll()-based event loop.
 * @ingroup events
 *
 * Wraps a POSIX pipe pair opened with @c O_CLOEXEC + @c O_NONBLOCK.
 * The write end is safe to call from any thread or from an
 * async-signal handler — @c write() on a pipe is both thread-safe
 * and async-signal-safe, and @c EAGAIN is ignored since a saturated
 * pipe already has a wake pending.  The read end is drained by the
 * loop thread after @c poll() reports it readable.
 *
 * On non-POSIX platforms (Windows, Emscripten) the class compiles
 * but all operations are no-ops and @ref isValid returns @c false.
 * That is enough to let code that uses @c SelfPipe to escape a
 * blocked wait compile everywhere; the embedder supplies an
 * equivalent wake mechanism on those platforms.
 */
class SelfPipe {
        public:
                /**
                 * @brief Opens the pipe.
                 *
                 * Prefers @c pipe2(O_CLOEXEC|O_NONBLOCK) on Linux;
                 * falls back to @c pipe() + @c fcntl on other POSIX
                 * systems that lack @c pipe2.  A warning is logged
                 * on failure; @ref isValid returns @c false.
                 */
                SelfPipe();

                /** @brief Closes both ends of the pipe. */
                ~SelfPipe();

                SelfPipe(const SelfPipe &) = delete;
                SelfPipe &operator=(const SelfPipe &) = delete;

                /** @brief Returns @c true if the pipe opened successfully. */
                bool isValid() const { return _readFd >= 0; }

                /** @brief Returns the read-end fd, or -1 if not open. */
                int readFd() const { return _readFd; }

                /** @brief Returns the write-end fd, or -1 if not open. */
                int writeFd() const { return _writeFd; }

                /**
                 * @brief Writes a single byte to the write end.
                 *
                 * Safe from any thread or from an async-signal
                 * handler.  Ignores @c EAGAIN (wake already queued)
                 * and @c EINTR (another caller will re-wake).
                 */
                void wake();

                /**
                 * @brief Drains all pending bytes from the read end.
                 *
                 * Loops @c read() until @c EAGAIN.  Must be called on
                 * the loop thread after @c poll() reports the read
                 * end readable.
                 */
                void drain();

        private:
                int _readFd  = -1;
                int _writeFd = -1;
};

PROMEKI_NAMESPACE_END
