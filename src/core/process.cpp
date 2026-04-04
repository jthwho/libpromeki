/**
 * @file      process.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/process.h>
#include <promeki/elapsedtimer.h>

#ifndef __EMSCRIPTEN__
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstdlib>
#endif

PROMEKI_NAMESPACE_BEGIN

Process::Process(ObjectBase *parent) : ObjectBase(parent) {
}

Process::~Process() {
        if(_state == Running) {
                kill();
                waitForFinished(5000);
        }
        closeAllPipes();
}

void Process::closeFd(int &fd) {
        if(fd != -1) {
                ::close(fd);
                fd = -1;
        }
}

void Process::closeAllPipes() {
        closeFd(_stdinPipe[0]);
        closeFd(_stdinPipe[1]);
        closeFd(_stdoutPipe[0]);
        closeFd(_stdoutPipe[1]);
        closeFd(_stderrPipe[0]);
        closeFd(_stderrPipe[1]);
}

void Process::drainFd(int &fd, List<Buffer> &chunks, size_t &total) {
        if(fd == -1) return;
        const size_t chunkSize = 4096;
        for(;;) {
                Buffer chunk(chunkSize);
                ssize_t n = ::read(fd, chunk.data(), chunkSize);
                if(n > 0) {
                        chunk.setSize(static_cast<size_t>(n));
                        total += static_cast<size_t>(n);
                        chunks += std::move(chunk);
                } else if(n == 0) {
                        // EOF — child closed the write end. Close our read end
                        // so poll() doesn't spin on POLLHUP.
                        closeFd(fd);
                        break;
                } else {
                        if(errno == EINTR) continue;
                        break; // EAGAIN or error
                }
        }
}

void Process::drainPipes() {
        drainFd(_stdoutPipe[0], _stdoutChunks, _stdoutTotal);
        drainFd(_stderrPipe[0], _stderrChunks, _stderrTotal);
}

Buffer Process::assembleChunks(List<Buffer> &chunks, size_t &total) {
        if(total == 0) return Buffer();
        Buffer result(total);
        size_t offset = 0;
        for(size_t i = 0; i < chunks.size(); i++) {
                result.copyFrom(chunks[i].data(), chunks[i].size(), offset);
                offset += chunks[i].size();
        }
        result.setSize(total);
        chunks.clear();
        total = 0;
        return result;
}

Error Process::start() {
#ifdef __EMSCRIPTEN__
        return Error::NotSupported;
#else
        if(_state == Running) return Error::AlreadyOpen;
        if(_program.isEmpty()) return Error::Invalid;

        _exitCode = -1;
        _stdoutChunks.clear();
        _stderrChunks.clear();
        _stdoutTotal = 0;
        _stderrTotal = 0;
        _state = Starting;

        // Create pipes for stdin, stdout, stderr
        if(::pipe(_stdinPipe) != 0 || ::pipe(_stdoutPipe) != 0 || ::pipe(_stderrPipe) != 0) {
                Error err = Error::syserr();
                closeAllPipes();
                _state = NotRunning;
                errorOccurredSignal.emit(err);
                return err;
        }

        // Create an exec-notify pipe with O_CLOEXEC. The write end is
        // automatically closed on successful exec, signaling the parent.
        // If exec fails, the child writes the errno before _exit.
        int execPipe[2];
        if(::pipe2(execPipe, O_CLOEXEC) != 0) {
                Error err = Error::syserr();
                closeAllPipes();
                _state = NotRunning;
                errorOccurredSignal.emit(err);
                return err;
        }

        _pid = ::fork();
        if(_pid < 0) {
                Error err = Error::syserr();
                closeAllPipes();
                ::close(execPipe[0]);
                ::close(execPipe[1]);
                _state = NotRunning;
                _pid = -1;
                errorOccurredSignal.emit(err);
                return err;
        }

        if(_pid == 0) {
                // Child process
                ::close(execPipe[0]); // Close read end

                // Reset signal state — the parent (or test framework) may have
                // blocked or ignored signals. SIG_IGN persists across exec(),
                // so we must explicitly restore default dispositions.
                for(int sig = 1; sig < NSIG; sig++) {
                        ::signal(sig, SIG_DFL);
                }
                sigset_t allSigs;
                ::sigfillset(&allSigs);
                ::sigprocmask(SIG_UNBLOCK, &allSigs, nullptr);

                // Redirect stdin/stdout/stderr
                ::dup2(_stdinPipe[0], STDIN_FILENO);
                ::dup2(_stdoutPipe[1], STDOUT_FILENO);
                ::dup2(_stderrPipe[1], STDERR_FILENO);

                // Close all pipe fds in the child
                ::close(_stdinPipe[0]);
                ::close(_stdinPipe[1]);
                ::close(_stdoutPipe[0]);
                ::close(_stdoutPipe[1]);
                ::close(_stderrPipe[0]);
                ::close(_stderrPipe[1]);

                // Change working directory if set
                if(!_workingDirectory.isEmpty()) {
                        if(::chdir(_workingDirectory.toString().cstr()) != 0) {
                                int e = errno;
                                ssize_t dummy = ::write(execPipe[1], &e, sizeof(e));
                                (void)dummy;
                                _exit(127);
                        }
                }

                // Set environment if provided
                if(!_environment.isEmpty()) {
                        ::clearenv();
                        for(auto it = _environment.begin(); it != _environment.end(); ++it) {
                                ::setenv(it->first.cstr(), it->second.cstr(), 1);
                        }
                }

                // Build argv
                List<char *> argv;
                argv += const_cast<char *>(_program.cstr());
                for(size_t i = 0; i < _arguments.size(); i++) {
                        argv += const_cast<char *>(_arguments[i].cstr());
                }
                argv += nullptr;

                ::execvp(_program.cstr(), argv.data());

                // exec failed — notify parent via the exec pipe
                int e = errno;
                ssize_t dummy = ::write(execPipe[1], &e, sizeof(e));
                (void)dummy;
                _exit(127);
        }

        // Parent process

        // Close child-side pipe ends
        closeFd(_stdinPipe[0]);   // Child reads from stdin
        closeFd(_stdoutPipe[1]);  // Child writes to stdout
        closeFd(_stderrPipe[1]);  // Child writes to stderr

        // Wait for exec to complete (or fail) via the exec-notify pipe.
        // On success, the O_CLOEXEC write end closes and we read EOF (0 bytes).
        // On failure, the child writes errno before _exit(127).
        ::close(execPipe[1]); // Close write end in parent
        int execErr = 0;
        ssize_t n = ::read(execPipe[0], &execErr, sizeof(execErr));
        ::close(execPipe[0]);

        if(n > 0) {
                // exec failed — child wrote errno. Child will _exit(127).
                // We still need to reap the child.
                int status;
                ::waitpid(_pid, &status, 0);
                closeAllPipes();
                _state = NotRunning;
                _pid = -1;
                Error err = Error::syserr(execErr);
                errorOccurredSignal.emit(err);
                return err;
        }

        // exec succeeded (read returned 0 = EOF from CLOEXEC close)

        // Set stdout/stderr read ends to non-blocking
        ::fcntl(_stdoutPipe[0], F_SETFL, ::fcntl(_stdoutPipe[0], F_GETFL) | O_NONBLOCK);
        ::fcntl(_stderrPipe[0], F_SETFL, ::fcntl(_stderrPipe[0], F_GETFL) | O_NONBLOCK);

        _state = Running;
        startedSignal.emit();
        return Error::Ok;
#endif
}

Error Process::start(const String &program, const List<String> &args) {
        _program = program;
        _arguments = args;
        return start();
}

Error Process::waitForStarted(unsigned int timeoutMs) {
        (void)timeoutMs;
        // start() is synchronous via fork()+exec-notify pipe, so if we reach
        // here the process is either Running or failed to start.
        if(_state == Running || _state == Starting) return Error::Ok;
        return Error::NotOpen;
}

Error Process::waitForFinished(unsigned int timeoutMs) {
#ifdef __EMSCRIPTEN__
        (void)timeoutMs;
        return Error::NotSupported;
#else
        if(_state != Running) return Error::NotOpen;

        // Use poll() to drain stdout/stderr while waiting for the child.
        // This prevents deadlock when the child writes more than the pipe buffer.
        // ElapsedTimer provides accurate monotonic timeout tracking since
        // poll() may return early (e.g. SIGCHLD interrupting it).
        int status = 0;
        const int pollMs = 50;
        ElapsedTimer timer;

        for(;;) {
                // Drain any available pipe data
                drainPipes();

                // Check if child has exited
                pid_t result = ::waitpid(_pid, &status, WNOHANG);
                if(result < 0) {
                        int e = errno;
                        if(e == EINTR) continue;
                        if(e == ECHILD) {
                                // Child was already reaped.
                                _exitCode = 0;
                                _state = NotRunning;
                                closeFd(_stdinPipe[1]);
                                closeFd(_stdoutPipe[0]);
                                closeFd(_stderrPipe[0]);
                                finishedSignal.emit(_exitCode);
                                return Error::Ok;
                        }
                        Error err = Error::syserr(e);
                        errorOccurredSignal.emit(err);
                        return err;
                }
                if(result > 0) {
                        // Child exited — do a final drain to get any remaining data
                        drainPipes();
                        break;
                }

                // Child still running — check timeout
                if(timeoutMs > 0 && timer.hasExpired(timeoutMs)) {
                        return Error::Timeout;
                }

                // Wait a bit for pipe activity or child state change
                struct pollfd fds[2];
                int nfds = 0;
                if(_stdoutPipe[0] != -1) {
                        fds[nfds].fd = _stdoutPipe[0];
                        fds[nfds].events = POLLIN;
                        nfds++;
                }
                if(_stderrPipe[0] != -1) {
                        fds[nfds].fd = _stderrPipe[0];
                        fds[nfds].events = POLLIN;
                        nfds++;
                }

                int waitMs = pollMs;
                if(timeoutMs > 0) {
                        int64_t el = timer.elapsed();
                        unsigned int left = (el < timeoutMs) ? static_cast<unsigned int>(timeoutMs - el) : 0;
                        if(left < static_cast<unsigned int>(pollMs))
                                waitMs = static_cast<int>(left);
                }

                if(nfds == 0) {
                        // No pipes to poll — just sleep briefly and retry waitpid
                        ::usleep(static_cast<unsigned>(waitMs) * 1000);
                } else {
                        ::poll(fds, nfds, waitMs);
                }
        }

        if(WIFEXITED(status)) {
                _exitCode = WEXITSTATUS(status);
        } else if(WIFSIGNALED(status)) {
                _exitCode = -WTERMSIG(status);
        } else {
                _exitCode = -1;
        }

        _state = NotRunning;
        closeFd(_stdinPipe[1]);
        closeFd(_stdoutPipe[0]);
        closeFd(_stderrPipe[0]);
        finishedSignal.emit(_exitCode);
        return Error::Ok;
#endif
}

bool Process::collectExitStatus() {
#ifdef __EMSCRIPTEN__
        return false;
#else
        if(_state != Running) return false;
        int status = 0;
        pid_t result = ::waitpid(_pid, &status, WNOHANG);
        if(result <= 0) return false;
        drainPipes();
        if(WIFEXITED(status)) {
                _exitCode = WEXITSTATUS(status);
        } else if(WIFSIGNALED(status)) {
                _exitCode = -WTERMSIG(status);
        } else {
                _exitCode = -1;
        }
        _state = NotRunning;
        closeAllPipes();
        finishedSignal.emit(_exitCode);
        return true;
#endif
}

void Process::kill() {
#ifndef __EMSCRIPTEN__
        if(_state == Running && _pid > 0) {
                ::kill(_pid, SIGKILL);
        }
#endif
}

void Process::terminate() {
#ifndef __EMSCRIPTEN__
        if(_state == Running && _pid > 0) {
                ::kill(_pid, SIGTERM);
        }
#endif
}

ssize_t Process::writeToStdin(const void *buf, size_t bytes) {
#ifdef __EMSCRIPTEN__
        (void)buf; (void)bytes;
        return -1;
#else
        if(_stdinPipe[1] == -1) return -1;
        ssize_t written = ::write(_stdinPipe[1], buf, bytes);
        if(written < 0) {
                errorOccurredSignal.emit(Error::syserr());
        }
        return written;
#endif
}

Buffer Process::readAllStdout() {
        // Drain any new data from the live pipe first
        drainFd(_stdoutPipe[0], _stdoutChunks, _stdoutTotal);
        return assembleChunks(_stdoutChunks, _stdoutTotal);
}

Buffer Process::readAllStderr() {
        // Drain any new data from the live pipe first
        drainFd(_stderrPipe[0], _stderrChunks, _stderrTotal);
        return assembleChunks(_stderrChunks, _stderrTotal);
}

void Process::closeWriteChannel() {
        closeFd(_stdinPipe[1]);
}

PROMEKI_NAMESPACE_END
