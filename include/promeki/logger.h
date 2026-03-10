/**
 * @file      logger.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <mutex>
#include <thread>
#include <atomic>
#include <future>
#include <variant>
#include <iostream>
#include <fstream>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/queue.h>
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
#define promekiDebug(format, ...) if(_promeki_debug_enabled) { promekiLog(Logger::LogLevel::Debug, format, ##__VA_ARGS__); }
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
 *
 * All log messages are enqueued and written by a dedicated worker thread.
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

                /**
                 * @brief Function type for formatting log messages.
                 *
                 * A formatter receives the timestamp, log level, source file, source line,
                 * and message text, and returns a fully formatted string ready for output.
                 */
                using LogFormatter = std::function<String(const DateTime &ts, LogLevel level,
                        const char *file, int line, const String &msg)>;

                /**
                 * @brief Returns the singleton default Logger instance.
                 * @return A reference to the default Logger.
                 */
                static Logger &defaultLogger();

                /**
                 * @brief Converts a LogLevel to its string representation.
                 * @param level The log level to convert.
                 * @return A C string such as "DEBUG", "INFO", "WARN", or "ERR".
                 */
                static const char *levelToString(LogLevel level);

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
                Logger() : _level(Info), _consoleLogging(true),
                        _fileFormatter(defaultFileFormatter()),
                        _consoleFormatter(defaultConsoleFormatter()) {
                        _thread = std::thread(&Logger::worker, this);
                }

                /** @brief Destructor. Signals the worker thread to terminate and waits for it to finish. */
                ~Logger() {
                        _queue.emplace(CmdTerminate{});
                        _thread.join();
                }

                /**
                 * @brief Returns the current minimum log level.
                 * @return The log level as an integer.
                 */
                int level() const {
                        return _level;
                }

                /**
                 * @brief Enqueues a single log message.
                 * @param loglevel The severity level of the message.
                 * @param file     The source file name (typically __FILE__).
                 * @param line     The source line number (typically __LINE__).
                 * @param msg      The log message text.
                 */
                void log(LogLevel loglevel, const char *file, int line, const String &msg) {
                        _queue.emplace(CmdLog{loglevel, file, line, msg, DateTime::now()});
                }

                /**
                 * @brief Enqueues multiple log messages with the same timestamp.
                 * @param loglevel The severity level of the messages.
                 * @param file     The source file name (typically __FILE__).
                 * @param line     The source line number (typically __LINE__).
                 * @param lines    A StringList where each entry becomes a separate log line.
                 */
                void log(LogLevel loglevel, const char *file, int line, const StringList &lines) {
                        DateTime ts = DateTime::now();
                        List<Command> cmdlist;
                        for(const auto &item : lines) {
                                cmdlist.pushToBack(CmdLog{loglevel, file, line, item, ts});
                        }
                        _queue.push(std::move(cmdlist));
                }

                /**
                 * @brief Sets the log output file.
                 * @param filename Path to the log file. The file is opened by the worker thread.
                 */
                void setLogFile(const String &filename) {
                        _queue.emplace(CmdSetFile{filename});
                }

                /**
                 * @brief Changes the minimum log level.
                 * @param level The new minimum log level. Messages below this are discarded.
                 */
                void setLogLevel(LogLevel level) {
                        _level = level;
                        log(Force, "LOGGER", 0, String::sprintf("Logging Level Changed to %d", level));
                        return;
                }

                /**
                 * @brief Enables or disables console (stderr) log output.
                 * @param val true to enable console logging, false to disable.
                 */
                void setConsoleLoggingEnabled(bool val) {
                        _consoleLogging = val;
                        return;
                }

                /**
                 * @brief Returns the current file log formatter.
                 */
                LogFormatter fileFormatter() const {
                        std::lock_guard<std::mutex> lock(_formatterMutex);
                        return _fileFormatter;
                }

                /**
                 * @brief Returns the current console log formatter.
                 */
                LogFormatter consoleFormatter() const {
                        std::lock_guard<std::mutex> lock(_formatterMutex);
                        return _consoleFormatter;
                }

                /**
                 * @brief Sets a custom formatter for file log output.
                 * @param formatter The formatter function. Pass an empty std::function
                 *        to restore the default.
                 */
                void setFileFormatter(LogFormatter formatter) {
                        {
                                std::lock_guard<std::mutex> lock(_formatterMutex);
                                _fileFormatter = formatter ? formatter : defaultFileFormatter();
                        }
                        _queue.emplace(CmdSetFormatter{std::move(formatter), false});
                }

                /**
                 * @brief Sets a custom formatter for console log output.
                 * @param formatter The formatter function. Pass an empty std::function
                 *        to restore the default.
                 */
                void setConsoleFormatter(LogFormatter formatter) {
                        {
                                std::lock_guard<std::mutex> lock(_formatterMutex);
                                _consoleFormatter = formatter ? formatter : defaultConsoleFormatter();
                        }
                        _queue.emplace(CmdSetFormatter{std::move(formatter), true});
                }

                /**
                 * @brief Blocks until all queued log commands have been processed.
                 * @param timeoutMs Maximum time to wait in milliseconds.  A value
                 *        of zero (the default) waits indefinitely.
                 * @return Error::Ok if the sync completed, Error::Timeout if the
                 *         timeout elapsed first.
                 */
                Error sync(unsigned int timeoutMs = 0) {
                        auto p = std::make_shared<std::promise<void>>();
                        auto f = p->get_future();
                        _queue.emplace(CmdSync{std::move(p)});
                        if(timeoutMs == 0) {
                                f.wait();
                                return Error::Ok;
                        }
                        if(f.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::ready) {
                                return Error::Ok;
                        }
                        return Error::Timeout;
                }

        private:
                struct CmdLog {
                        LogLevel        level;
                        const char *    file;
                        int             line;
                        String          msg;
                        DateTime        ts;
                };

                struct CmdSetFile {
                        String          filename;
                };

                struct CmdSetFormatter {
                        LogFormatter    formatter;
                        bool            console;
                };

                struct CmdSync {
                        std::shared_ptr<std::promise<void>> promise;
                };

                struct CmdTerminate {};

                using Command = std::variant<CmdLog, CmdSetFile, CmdSetFormatter, CmdSync, CmdTerminate>;

                std::thread             _thread;
                std::atomic<int>        _level;
                std::atomic<bool>       _consoleLogging;
                std::ofstream           _file;
                Queue<Command>          _queue;
                mutable std::mutex      _formatterMutex;
                LogFormatter            _fileFormatter;
                LogFormatter            _consoleFormatter;

                void worker();
                void writeLog(const CmdLog &cmd);
                void openLogFile(const String &filename);

};

PROMEKI_NAMESPACE_END

