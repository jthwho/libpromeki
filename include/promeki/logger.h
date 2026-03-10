/**
 * @file      logger.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <thread>
#include <atomic>
#include <future>
#include <iostream>
#include <fstream>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/queue.h>
#include <promeki/datetime.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

// Variadic macros for logging at various levels

#define PROMEKI_DEBUG(name) \
        namespace { \
                [[maybe_unused]] static const char *_promeki_debug_name = PROMEKI_STRINGIFY(name); \
                [[maybe_unused]] static bool _promeki_debug_enabled = \
                promekiRegisterDebug(&_promeki_debug_enabled, PROMEKI_STRINGIFY(name), __FILE__, __LINE__); \
        }

#define promekiLogImpl(level, format, ...) Logger::defaultLogger().log(level, __FILE__, __LINE__, String::sprintf(format, ##__VA_ARGS__))
#define promekiLog(level, format, ...) promekiLogImpl(level, format, ##__VA_ARGS__)
#define promekiLogSync() Logger::defaultLogger().sync()
#define promekiLogStackTrace(level) Logger::defaultLogger().log(level, __FILE__, __LINE__, promekiStackTrace())

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

                /** @brief Constructs a Logger and starts the worker thread. */
                Logger() : _level(Debug), _consoleLogging(true) {
                        _thread = std::thread(&Logger::worker, this);
                }

                /** @brief Destructor. Signals the worker thread to terminate and waits for it to finish. */
                ~Logger() {
                        // Signal the worker thread to terminate
                        Command cmd;
                        cmd.cmd = CmdTerminate;
                        _queue.push(cmd);

                        // Wait for the worker thread to finish
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
                        if(loglevel && loglevel < level()) return;
                        Command cmd;
                        cmd.cmd = CmdLog;
                        cmd.level = loglevel;
                        cmd.file = file;
                        cmd.line = line;
                        cmd.msg = msg;
                        cmd.ts = DateTime::now();
                        _queue.push(cmd);
                }

                /**
                 * @brief Enqueues multiple log messages with the same timestamp.
                 * @param loglevel The severity level of the messages.
                 * @param file     The source file name (typically __FILE__).
                 * @param line     The source line number (typically __LINE__).
                 * @param lines    A StringList where each entry becomes a separate log line.
                 */
                void log(LogLevel loglevel, const char *file, int line, const StringList &lines) {
                        if(loglevel && loglevel < level()) return;
                        DateTime ts = DateTime::now();
                        List<Command> cmdlist;
                        for(const auto &item : lines) {
                                Command cmd;
                                cmd.cmd = CmdLog;
                                cmd.level = loglevel;
                                cmd.file = file;
                                cmd.line = line;
                                cmd.msg = item;
                                cmd.ts = ts;
                                cmdlist.pushToBack(cmd);
                        }
                        _queue.push(cmdlist);
                        return;
                }

                /**
                 * @brief Sets the log output file.
                 * @param filename Path to the log file. The file is opened by the worker thread.
                 */
                void setLogFile(const String &filename) {
                        Command cmd;
                        cmd.cmd = CmdSetFile;
                        cmd.msg = filename;
                        _queue.push(cmd);
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
                 * @brief Blocks until all queued log commands have been processed.
                 * @param timeoutMs Maximum time to wait in milliseconds.  A value
                 *        of zero (the default) waits indefinitely.
                 * @return Error::Ok if the sync completed, Error::Timeout if the
                 *         timeout elapsed first.
                 */
                Error sync(unsigned int timeoutMs = 0) {
                        auto p = std::make_shared<std::promise<void>>();
                        auto f = p->get_future();
                        Command cmd;
                        cmd.cmd = CmdSync;
                        cmd.syncPromise = std::move(p);
                        _queue.push(std::move(cmd));
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
                enum CommandType {
                        CmdLog,
                        CmdSetFile,
                        CmdSync,
                        CmdTerminate
                };

                struct Command {
                        CommandType                             cmd;
                        int                                     level;
                        const char *                            file;
                        int                                     line;
                        String                                  msg;
                        DateTime                                ts;
                        std::shared_ptr<std::promise<void>>     syncPromise;
                };

                std::thread             _thread;
                std::atomic<int>        _level;
                std::atomic<bool>       _consoleLogging;
                std::ofstream           _file;
                Queue<Command>        _queue;

                void worker();
                void writeLog(const Command &cmd);
                void openLogFile(const Command &cmd);

};

PROMEKI_NAMESPACE_END

