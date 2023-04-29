/*****************************************************************************
 * logger.h
 * April 28, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <thread>
#include <atomic>
#include <iostream>
#include <fstream>
#include <promeki/string.h>
#include <promeki/tsqueue.h>
#include <promeki/datetime.h>

namespace promeki {

// Variadic macros for logging at various levels

#define promekiLogImpl(level, format, ...) Logger::defaultLogger().log(level, __FILE__, __LINE__, String::sprintf(format, ##__VA_ARGS__))
#define promekiLog(level, format, ...) promekiLogImpl(level, format, ##__VA_ARGS__)

#define promekiDebug(format, ...) promekiLog(Logger::LogLevel::Debug, format, ##__VA_ARGS__)
#define promekiInfo(format, ...)  promekiLog(Logger::LogLevel::Info,  format, ##__VA_ARGS__)
#define promekiWarn(format, ...)  promekiLog(Logger::LogLevel::Warn,  format, ##__VA_ARGS__)
#define promekiErr(format, ...)   promekiLog(Logger::LogLevel::Err,   format, ##__VA_ARGS__)

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
                static const char *levelToString(int level);

                Logger() : _level(Info), _consoleLogging(true) {
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

                void setLogFile(const std::string &filename) {
                        Command cmd;
                        cmd.cmd = CmdSetFile;
                        cmd.msg = filename;
                        _queue.push(cmd);
                }

                void setLogLevel(LogLevel level) {
                        _level = level;
                        log(Force, "LOGGER", 0, String("Logging Level Chanaged to %1").arg(level));
                        return;
                }

                void setConsoleLoggingEnabled(bool val) {
                        _consoleLogging = val;
                        return;
                }

        private:
                enum CommandType {
                        CmdLog,
                        CmdSetFile,
                        CmdTerminate
                };

                struct Command {
                        CommandType     cmd;
                        int             level;
                        const char *    file;
                        int             line;
                        String          msg;
                        DateTime        ts;
                };

                std::thread             _thread;
                std::atomic<int>        _level;
                std::atomic<bool>       _consoleLogging;
                std::ofstream           _file;
                TSQueue<Command>        _queue;

                void worker();
                void writeLine(const String &msg);
                void writeLog(const Command &cmd);
                void openLogFile(const Command &cmd);

};

} // namespace promeki

