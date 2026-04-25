/**
 * @file      logger.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <thread>
#include <variant>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/queue.h>
#include <promeki/deque.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/atomic.h>
#include <promeki/promise.h>
#include <promeki/datetime.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/** @brief Extracts the file name from a full path at compile time. */
static consteval const char *sourceFileName(const char *path) {
        const char *ret = path;
        for(const char *p = path; *p; ++p) {
                if(*p == '/' || *p == '\\') ret = p + 1;
        }
        return ret;
}

#ifdef __FILE_NAME__
#define PROMEKI_SOURCE_FILE __FILE_NAME__
#else
#define PROMEKI_SOURCE_FILE promeki::sourceFileName(__FILE__)
#endif

// Variadic macros for logging at various levels

#define PROMEKI_DEBUG(name) \
        namespace { \
                [[maybe_unused]] static const char *_promeki_debug_name = PROMEKI_STRINGIFY(name); \
                [[maybe_unused]] static bool _promeki_debug_enabled = \
                promekiRegisterDebug(&_promeki_debug_enabled, PROMEKI_STRINGIFY(name), PROMEKI_SOURCE_FILE, __LINE__); \
        }

#define promekiLogImpl(_plevel, format, ...) \
        do { if(!(_plevel) || (_plevel) >= Logger::defaultLogger().level()) \
                Logger::defaultLogger().log(_plevel, PROMEKI_SOURCE_FILE, __LINE__, String::sprintf(format, ##__VA_ARGS__)); \
        } while(0)
#define promekiLog(level, format, ...) promekiLogImpl(level, format, ##__VA_ARGS__)
#define promekiLogSync() Logger::defaultLogger().sync()
#define promekiLogStackTrace(_plevel) \
        do { if(!(_plevel) || (_plevel) >= Logger::defaultLogger().level()) \
                Logger::defaultLogger().log(_plevel, PROMEKI_SOURCE_FILE, __LINE__, promekiStackTrace()); \
        } while(0)

#ifdef PROMEKI_DEBUG_ENABLE
#define promekiDebug(format, ...) if(_promeki_debug_enabled) { \
        Logger::defaultLogger().log(Logger::LogLevel::Debug, PROMEKI_SOURCE_FILE, __LINE__, \
                String::sprintf(format, ##__VA_ARGS__)); }
#else
#define promekiDebug(format, ...)
#endif

#define promekiInfo(format, ...)  promekiLog(Logger::LogLevel::Info,  format, ##__VA_ARGS__)
#define promekiWarn(format, ...)  promekiLog(Logger::LogLevel::Warn,  format, ##__VA_ARGS__)
#define promekiErr(format, ...)   promekiLog(Logger::LogLevel::Err,   format, ##__VA_ARGS__)

bool promekiRegisterDebug(bool *enabler, const char *name, const char *file, int line);

#define PROMEKI_BENCHMARK_BEGIN(name) \
        TimeStamp _promeki_debug_timestamp_##name; \
        if(_promeki_debug_enabled) { _promeki_debug_timestamp_##name = TimeStamp::now(); }

#define PROMEKI_BENCHMARK_END(name) \
        if(_promeki_debug_enabled) { \
                Logger::defaultLogger().log(Logger::LogLevel::Debug, __FILE__, __LINE__, String::sprintf("[%s] %s took %.9lf sec", \
                        _promeki_debug_name, PROMEKI_STRINGIFY(name), _promeki_debug_timestamp_##name.elapsedSeconds())); \
        }

/**
 * @brief Asynchronous thread-safe logging facility.
 * @ingroup util
 *
 * All log messages are enqueued and written by a dedicated worker thread.
 *
 * @par Example
 * @code
 * // Use convenience macros (most common)
 * promekiInfo("Processing frame %d", frameNum);
 * promekiWarn("Buffer underrun at %s", tc.toString().first().cstr());
 * promekiErr("Failed to open %s", path.cStr());
 *
 * // Configure the logger
 * Logger::defaultLogger().setLogFile("/tmp/app.log");
 * Logger::defaultLogger().setLogLevel(Logger::Debug);
 * @endcode
 * Supports multiple log levels, optional console output, and file logging.
 */
class Logger {
        public:
                /** @brief Severity levels for log messages. */
                enum LogLevel {
                        Force   = 0,      ///< @brief Forced messages are always logged.
                        Debug   = 1,      ///< @brief Debug-level messages.
                        Info    = 2,      ///< @brief Informational messages.
                        Warn    = 3,      ///< @brief Warning messages.
                        Err     = 4       ///< @brief Error messages.
                };

                /** @brief A single log entry. */
                struct LogEntry {
                        DateTime        ts;
                        LogLevel        level;
                        const char *    file;
                        int             line;
                        uint64_t        threadId;
                        String          msg;
                };

                /** @brief Context passed to formatters, combining entry data with resolved thread name. */
                struct LogFormat {
                        const LogEntry  *entry;
                        const String    *threadName;
                };

                /**
                 * @brief Function type for formatting log messages.
                 *
                 * A formatter receives a LogFormat and returns a fully formatted
                 * string ready for output.
                 */
                using LogFormatter = std::function<String(const LogFormat &fmt)>;

                /**
                 * @brief Opaque identifier for a registered log listener.
                 *
                 * Returned by @ref installListener and accepted by
                 * @ref removeListener.  Zero is reserved for "no
                 * listener" and is never returned for a successful
                 * installation.
                 */
                using ListenerHandle = uint64_t;

                /**
                 * @brief Callback invoked for every log entry processed.
                 *
                 * Listeners receive the structured entry plus the
                 * resolved thread name as it was at the time the entry
                 * was processed.  Both arguments are valid only for the
                 * duration of the call — copy anything you need to
                 * retain.
                 *
                 * Listeners are always invoked on the logger's worker
                 * thread; if the consumer lives on a different
                 * @ref EventLoop, marshal the entry across via
                 * @ref EventLoop::postCallable rather than blocking.
                 */
                using LogListener = std::function<void(const LogEntry &entry,
                                                       const String &threadName)>;

                /** @brief Default size of the in-memory history ring used for replay. */
                static constexpr size_t DefaultHistorySize = 1024;

                /**
                 * @brief Returns the singleton default Logger instance.
                 * @return A reference to the default Logger.
                 */
                static Logger &defaultLogger();

                /**
                 * @brief Converts a LogLevel to its single character representation
                 * @param level The log level to convert.
                 * @return A character such as D (debug), W (warning), I (info), etc
                 */
                static char levelToChar(LogLevel level);

                /**
                 * @brief Updates the cached thread name used in log output.
                 *
                 * The logger caches the calling thread's name on first use.
                 * Call this to update the cached name if the thread is
                 * renamed after its first log message.  Thread::setName()
                 * calls this automatically.
                 *
                 * @param name The new thread name.
                 */
                static void setThreadName(const String &name);

                /** @brief Plain-value description of a registered debug channel. */
                struct DebugChannel {
                        String  name;           ///< Channel name as passed to PROMEKI_DEBUG.
                        String  file;           ///< Source file containing the registration.
                        int     line = 0;       ///< Line number of the registration.
                        bool    enabled = false;///< Current enabled state.

                        using List = promeki::List<DebugChannel>;
                };

                /**
                 * @brief Returns every PROMEKI_DEBUG channel known to the process.
                 *
                 * Channels are reported by name; the same name registered
                 * from multiple translation units appears once per
                 * registration site.  The returned list is a snapshot —
                 * subsequent toggles via @ref setDebugChannel will not
                 * be reflected in copies that have already been
                 * returned.
                 */
                static DebugChannel::List debugChannels();

                /**
                 * @brief Enables or disables every PROMEKI_DEBUG site bearing @p name.
                 *
                 * Iterates the debug-flag registry and sets every
                 * matching enabler.  Returns @c true if at least one
                 * site was updated; @c false if no channel by that
                 * name has been registered.
                 *
                 * Note: each site's enabler is a plain @c bool, so
                 * concurrent read/write is technically a data race —
                 * but the practical worst case is that a logging site
                 * sees the wrong value for a handful of evaluations
                 * around the toggle, which matches Qt's `setDebug`
                 * semantics and is acceptable for diagnostic flags.
                 */
                static bool setDebugChannel(const String &name, bool enabled);

                /**
                 * @brief Returns the default file log formatter.
                 *
                 * Produces plain text lines in the format:
                 * `TIMESTAMP SOURCE:LINE [LEVEL] MESSAGE`
                 */
                static LogFormatter defaultFileFormatter();

                /**
                 * @brief Returns the default console log formatter.
                 *
                 * Produces ANSI-colored lines suitable for terminal output.
                 */
                static LogFormatter defaultConsoleFormatter();

                /** @brief Constructs a Logger and starts the worker thread. */
                Logger();

                /**
                 * @brief Destructor.
                 *
                 * Drains any messages already enqueued, blocks further
                 * enqueues from the public API, signals the worker to
                 * terminate, and joins it.
                 */
                ~Logger();

                /**
                 * @brief Returns the current minimum log level.
                 * @return The log level as an integer.
                 */
                int level() const {
                        return _level.value();
                }

                /**
                 * @brief Enqueues a single log message.
                 *
                 * Captures the calling thread's native ID and name
                 * automatically.
                 *
                 * @param loglevel The severity level of the message.
                 * @param file     The source file name (typically __FILE__).
                 * @param line     The source line number (typically __LINE__).
                 * @param msg      The log message text.
                 */
                void log(LogLevel loglevel, const char *file, int line, const String &msg);

                /**
                 * @brief Enqueues multiple log messages with the same timestamp.
                 *
                 * Captures the calling thread's native ID and name
                 * automatically.
                 *
                 * @param loglevel The severity level of the messages.
                 * @param file     The source file name (typically __FILE__).
                 * @param line     The source line number (typically __LINE__).
                 * @param lines    A StringList where each entry becomes a separate log line.
                 */
                void log(LogLevel loglevel, const char *file, int line, const StringList &lines);

                /**
                 * @brief Sets the log output file.
                 * @param filename Path to the log file. The file is opened by the worker thread.
                 */
                void setLogFile(const String &filename) {
                        if(_terminating.value()) return;
                        _queue.emplace(CmdSetFile{filename});
                }

                /**
                 * @brief Changes the minimum log level.
                 * @param level The new minimum log level. Messages below this are discarded.
                 */
                void setLogLevel(LogLevel level) {
                        _level.setValue(level);
                        log(Force, "LOGGER", 0, String::sprintf("Logging Level Changed to %d", level));
                        return;
                }

                /**
                 * @brief Returns whether console logging is enabled.
                 * @return true if console output is active.
                 */
                bool consoleLoggingEnabled() const {
                        return _consoleLogging.value();
                }

                /**
                 * @brief Enables or disables console (stderr) log output.
                 * @param val true to enable console logging, false to disable.
                 */
                void setConsoleLoggingEnabled(bool val) {
                        _consoleLogging.setValue(val);
                        return;
                }

                /**
                 * @brief Returns the current file log formatter.
                 */
                LogFormatter fileFormatter() const {
                        Mutex::Locker lock(_formatterMutex);
                        return _fileFormatter;
                }

                /**
                 * @brief Returns the current console log formatter.
                 */
                LogFormatter consoleFormatter() const {
                        Mutex::Locker lock(_formatterMutex);
                        return _consoleFormatter;
                }

                /**
                 * @brief Sets a custom formatter for file log output.
                 * @param formatter The formatter function. Pass an empty std::function
                 *        to restore the default.
                 */
                void setFileFormatter(LogFormatter formatter) {
                        {
                                Mutex::Locker lock(_formatterMutex);
                                _fileFormatter = formatter ? formatter : defaultFileFormatter();
                        }
                        if(_terminating.value()) return;
                        _queue.emplace(CmdSetFormatter{std::move(formatter), false});
                }

                /**
                 * @brief Sets a custom formatter for console log output.
                 * @param formatter The formatter function. Pass an empty std::function
                 *        to restore the default.
                 */
                void setConsoleFormatter(LogFormatter formatter) {
                        {
                                Mutex::Locker lock(_formatterMutex);
                                _consoleFormatter = formatter ? formatter : defaultConsoleFormatter();
                        }
                        if(_terminating.value()) return;
                        _queue.emplace(CmdSetFormatter{std::move(formatter), true});
                }

                /**
                 * @brief Registers a listener that will receive every future log entry.
                 *
                 * Installation is processed on the worker thread, so by
                 * the time this call returns the listener is guaranteed
                 * to either see a given entry via @p replayCount or as
                 * a live notification — never both, never neither.
                 *
                 * The replay step delivers the last
                 * @c min(replayCount, history.size()) entries currently
                 * held in the in-memory history ring (see
                 * @ref setHistorySize) in chronological order before the
                 * subscription becomes live.
                 *
                 * @param listener      Callback invoked for each entry.
                 *                      Empty function is rejected and
                 *                      yields a zero handle.
                 * @param replayCount   Number of recent entries to
                 *                      deliver synchronously before
                 *                      subscribing.  Capped at the
                 *                      current history size.
                 * @return Non-zero handle on success, or @c 0 if the
                 *         listener was empty or the logger is
                 *         terminating.
                 */
                ListenerHandle installListener(LogListener listener, size_t replayCount = 0);

                /**
                 * @brief Removes a previously registered listener.
                 *
                 * Removal is processed on the worker thread; the call
                 * blocks until the listener has been unregistered, so
                 * it is safe to destroy any state captured by the
                 * listener once this method returns.  Passing an
                 * unknown handle is a no-op.
                 */
                void removeListener(ListenerHandle handle);

                /**
                 * @brief Sets the maximum number of entries kept for listener replay.
                 *
                 * The history ring is trimmed lazily as new entries
                 * arrive: shrinking the size won't drop excess entries
                 * until the next @ref log call processes them on the
                 * worker thread.  A size of zero disables history
                 * (listeners installed with @c replayCount > 0 simply
                 * see no replay).
                 */
                void setHistorySize(size_t n) { _historySize.setValue(n); }

                /** @brief Returns the configured history-ring size. */
                size_t historySize() const { return _historySize.value(); }

                /**
                 * @brief Blocks until all queued log commands have been processed.
                 * @param timeoutMs Maximum time to wait in milliseconds.  A value
                 *        of zero (the default) waits indefinitely.
                 * @return Error::Ok if the sync completed, Error::Timeout if the
                 *         timeout elapsed first.
                 */
                Error sync(unsigned int timeoutMs = 0) {
                        if(_terminating.value()) return Error::Ok;
                        auto p = std::make_shared<Promise<void>>();
                        Future<void> f = p->future();
                        _queue.emplace(CmdSync{std::move(p)});
                        if(timeoutMs == 0) {
                                f.waitForFinished();
                                return Error::Ok;
                        }
                        return f.waitForFinished(timeoutMs);
                }

        private:

                struct CmdSetThreadName {
                        uint64_t        threadId;
                        String          name;
                };

                struct CmdSetFile {
                        String          filename;
                };

                struct CmdSetFormatter {
                        LogFormatter    formatter;
                        bool            console;
                };

                struct CmdSync {
                        std::shared_ptr<Promise<void>> promise;
                };

                struct CmdInstallListener {
                        LogListener     listener;
                        size_t          replayCount;
                        std::shared_ptr<Promise<ListenerHandle>> promise;
                };

                struct CmdRemoveListener {
                        ListenerHandle  handle;
                        std::shared_ptr<Promise<void>> promise;
                };

                struct CmdTerminate {};

                using Command = std::variant<LogEntry, CmdSetThreadName, CmdSetFile,
                        CmdSetFormatter, CmdSync, CmdInstallListener,
                        CmdRemoveListener, CmdTerminate>;

                struct ListenerEntry {
                        ListenerHandle  handle;
                        LogListener     fn;
                };

                struct HistoryEntry {
                        LogEntry        entry;
                        String          threadName;
                };

                std::thread             _thread;
                Atomic<int>             _level;
                Atomic<bool>            _consoleLogging;
                Atomic<bool>            _terminating{false};
                Atomic<size_t>          _historySize{DefaultHistorySize};
                Atomic<uint64_t>        _nextListenerHandle{0};
                Queue<Command>          _queue;
                mutable Mutex           _formatterMutex;
                LogFormatter            _fileFormatter;
                LogFormatter            _consoleFormatter;
                Map<uint64_t, String>   _threadNames;
                List<ListenerEntry>     _listeners;     ///< Worker-thread only.
                Deque<HistoryEntry>     _history;       ///< Worker-thread only.

                void worker();
                void writeLog(const LogEntry &cmd, class FileIODevice *logFile);
                class FileIODevice *openLogFile(const String &filename, class FileIODevice *existing);

};

PROMEKI_NAMESPACE_END

