/**
 * @file      process.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <sys/types.h>
#include <promeki/namespace.h>
#include <promeki/platform.h>
#include <promeki/objectbase.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/buffer.h>
#include <promeki/filepath.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Subprocess execution with pipe-based I/O.
 * @ingroup io
 *
 * Wraps subprocess creation and management using pipe() + fork() + exec()
 * on POSIX platforms. Provides access to the child process's stdin, stdout,
 * and stderr via pipe-based I/O methods.
 *
 * On unsupported platforms (e.g. WASM/Emscripten), start() returns
 * Error::NotSupported.
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase: thread-affine.  A Process instance
 * must only be used from the thread that created it (typically
 * its owning EventLoop's thread).  The pipe I/O accessors
 * (@c readAllStdout, @c writeToStdin, etc.) follow the same
 * affinity.
 *
 * @code
 * Process proc;
 * proc.setProgram("/usr/bin/echo");
 * proc.setArguments({"Hello", "world"});
 * Error err = proc.start();
 * if(err.isOk()) {
 *     proc.waitForFinished();
 *     Buffer output = proc.readAllStdout();
 * }
 * @endcode
 */
class Process : public ObjectBase {
                PROMEKI_OBJECT(Process, ObjectBase)
        public:
                /**
                 * @brief Process state.
                 */
                enum State {
                        NotRunning = 0, ///< @brief Process is not running.
                        Starting,       ///< @brief Process is being started.
                        Running         ///< @brief Process is running.
                };

                /** @brief Deleted copy constructor (non-copyable). */
                Process(const Process &) = delete;
                /** @brief Deleted copy assignment operator (non-copyable). */
                Process &operator=(const Process &) = delete;

                /**
                 * @brief Default constructor.
                 * @param parent The parent object, or nullptr.
                 */
                Process(ObjectBase *parent = nullptr);

                /**
                 * @brief Destructor. Kills the child process if still running.
                 */
                ~Process();

                /**
                 * @brief Returns the program path.
                 * @return A const reference to the program string.
                 */
                const String &program() const { return _program; }

                /**
                 * @brief Sets the program to execute.
                 * @param program Path to the executable.
                 */
                void setProgram(const String &program) { _program = program; }

                /**
                 * @brief Returns the argument list.
                 * @return A const reference to the argument list.
                 */
                const List<String> &arguments() const { return _arguments; }

                /**
                 * @brief Sets the argument list for the subprocess.
                 * @param args The arguments to pass to the program.
                 */
                void setArguments(const List<String> &args) { _arguments = args; }

                /**
                 * @brief Returns the working directory for the subprocess.
                 * @return A const reference to the working directory path.
                 */
                const FilePath &workingDirectory() const { return _workingDirectory; }

                /**
                 * @brief Sets the working directory for the subprocess.
                 *
                 * If empty, the child inherits the parent's working directory.
                 * @param dir The directory to run the subprocess in.
                 */
                void setWorkingDirectory(const FilePath &dir) { _workingDirectory = dir; }

                /**
                 * @brief Returns the environment variables for the subprocess.
                 * @return A const reference to the environment map.
                 */
                const Map<String, String> &environment() const { return _environment; }

                /**
                 * @brief Sets the environment variables for the subprocess.
                 *
                 * If empty, the child inherits the parent's environment.
                 * @param env A map of environment variable names to values.
                 */
                void setEnvironment(const Map<String, String> &env) { _environment = env; }

                /**
                 * @brief Starts the subprocess using the previously set program and arguments.
                 * @return Error::Ok on success, or an error if the process cannot be started.
                 */
                Error start();

                /**
                 * @brief Convenience: sets program and arguments, then starts.
                 * @param program Path to the executable.
                 * @param args The arguments to pass to the program.
                 * @return Error::Ok on success, or an error if the process cannot be started.
                 */
                Error start(const String &program, const List<String> &args);

                /**
                 * @brief Blocks until the subprocess has started or the timeout expires.
                 * @param timeoutMs Maximum time to wait in milliseconds (0 = wait indefinitely).
                 * @return Error::Ok if started, Error::Timeout on expiry.
                 */
                Error waitForStarted(unsigned int timeoutMs = 0);

                /**
                 * @brief Blocks until the subprocess has finished or the timeout expires.
                 *
                 * Reads any remaining stdout/stderr data and collects the exit status.
                 * @param timeoutMs Maximum time to wait in milliseconds (0 = wait indefinitely).
                 * @return Error::Ok if finished, Error::Timeout on expiry.
                 */
                Error waitForFinished(unsigned int timeoutMs = 0);

                /**
                 * @brief Returns the exit code of the finished process.
                 *
                 * Only valid after the process has finished.
                 * @return The exit code, or -1 if the process has not finished.
                 */
                int exitCode() const { return _exitCode; }

                /**
                 * @brief Returns true if the subprocess is currently running.
                 * @return true if running.
                 */
                bool isRunning() const { return _state == Running; }

                /**
                 * @brief Returns the current process state.
                 * @return The State enumeration value.
                 */
                State state() const { return _state; }

                /**
                 * @brief Returns the child process ID.
                 *
                 * Only valid while the process is running or has recently finished.
                 * @return The PID, or -1 if no process has been started.
                 */
                pid_t pid() const { return _pid; }

                /**
                 * @brief Sends SIGKILL to the child process.
                 */
                void kill();

                /**
                 * @brief Sends SIGTERM to the child process.
                 */
                void terminate();

                /**
                 * @brief Writes data to the child's stdin pipe.
                 * @param buf Pointer to the data to write.
                 * @param bytes Number of bytes to write.
                 * @return The number of bytes written, or -1 on error.
                 */
                ssize_t writeToStdin(const void *buf, size_t bytes);

                /**
                 * @brief Reads all available data from the child's stdout.
                 * @return A Buffer containing the stdout data.
                 */
                Buffer readAllStdout();

                /**
                 * @brief Reads all available data from the child's stderr.
                 * @return A Buffer containing the stderr data.
                 */
                Buffer readAllStderr();

                /**
                 * @brief Closes the write end of the stdin pipe.
                 *
                 * Signals EOF to the child's stdin.
                 */
                void closeWriteChannel();

                /** @brief Emitted when the subprocess starts running.
                 *  @signal */
                PROMEKI_SIGNAL(started);

                /** @brief Emitted when the subprocess finishes (carries the exit code).
                 *  @signal */
                PROMEKI_SIGNAL(finished, int);

                /** @brief Emitted when data is available on the child's stdout.
                 *  @signal */
                PROMEKI_SIGNAL(readyReadStdout);

                /** @brief Emitted when data is available on the child's stderr.
                 *  @signal */
                PROMEKI_SIGNAL(readyReadStderr);

                /** @brief Emitted when an error occurs during subprocess execution.
                 *  @signal */
                PROMEKI_SIGNAL(errorOccurred, Error);

        private:
                String              _program;
                List<String>        _arguments;
                FilePath            _workingDirectory;
                Map<String, String> _environment;
                State               _state = NotRunning;
                pid_t               _pid = -1;
                int                 _exitCode = -1;
                int                 _stdinPipe[2] = {-1, -1};
                int                 _stdoutPipe[2] = {-1, -1};
                int                 _stderrPipe[2] = {-1, -1};
                List<Buffer>        _stdoutChunks;
                List<Buffer>        _stderrChunks;
                size_t              _stdoutTotal = 0;
                size_t              _stderrTotal = 0;

                /** @brief Closes a pipe file descriptor and sets it to -1. */
                void closeFd(int &fd);

                /** @brief Closes all pipe file descriptors. */
                void closeAllPipes();

                /** @brief Drains available data from non-blocking pipe into chunk list.
                 *  Closes the fd on EOF to prevent poll() from spinning on POLLHUP. */
                void drainFd(int &fd, List<Buffer> &chunks, size_t &total);

                /** @brief Drains both stdout and stderr pipes. */
                void drainPipes();

                /** @brief Assembles accumulated chunks into a single Buffer. */
                Buffer assembleChunks(List<Buffer> &chunks, size_t &total);

                /** @brief Collects child exit status without blocking. Returns true if collected. */
                bool collectExitStatus();
};

PROMEKI_NAMESPACE_END
