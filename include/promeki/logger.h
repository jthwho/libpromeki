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

class Logger {
        public:
                enum LogLevel {
                        Force   = 0,      // Forced messages are always logged
                        Debug   = 1,
                        Info    = 2,
                        Warn    = 3,
                        Err     = 4
                };

                static Logger &defaultLogger();
                static const char *levelToString(LogLevel level);

                Logger() : _level(Debug), _consoleLogging(true) {
                        _thread = std::thread(&Logger::worker, this);
                }

                ~Logger() {
                        // Signal the worker thread to terminate
                        Command cmd;
                        cmd.cmd = CmdTerminate;
                        _queue.push(cmd);

                        // Wait for the worker thread to finish
                        _thread.join();
                }

                int level() const {
                        return _level;
                }

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

                void log(LogLevel loglevel, const char *file, int line, const StringList &lines) {
                        if(loglevel && loglevel < level()) return;
                        DateTime ts = DateTime::now();
                        std::vector<Command> cmdlist;
                        for(const auto &item : lines) {
                                Command cmd;
                                cmd.cmd = CmdLog;
                                cmd.level = loglevel;
                                cmd.file = file;
                                cmd.line = line;
                                cmd.msg = item;
                                cmd.ts = ts;
                                cmdlist.push_back(cmd);
                        }
                        _queue.push(cmdlist);
                        return;
                }

                void setLogFile(const std::string &filename) {
                        Command cmd;
                        cmd.cmd = CmdSetFile;
                        cmd.msg = filename;
                        _queue.push(cmd);
                }

                void setLogLevel(LogLevel level) {
                        _level = level;
                        log(Force, "LOGGER", 0, String::sprintf("Logging Level Changed to %d", level));
                        return;
                }

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

