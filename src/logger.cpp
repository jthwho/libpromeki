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

#include <map>
#include <sstream>
#include <cstdlib>
#include <promeki/logger.h>
#include <promeki/ansistream.h>
#include <promeki/fileinfo.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

struct DebugDatabaseItem {
        bool    *enabler;
        String  name;
        String  file;
        int     line;
};

using DebugDatabaseItemList = List<DebugDatabaseItem>;

using DebugDatabase = std::map<String, DebugDatabaseItemList>;

static DebugDatabase &debugDatabase() {
        static DebugDatabase ret;
        return ret;
}

static bool checkForEnvDebugEnable(const String &name) {
        static bool done = false;
        static StringList list;
        if(!done) {
                done = true;
                const char *enval = std::getenv("PROMEKI_DEBUG");
                if(enval == nullptr) return false;
                list = String(enval).split(",");
                if(!list.isEmpty()) {
                        promekiInfo("Env PROMEKI_DEBUG: %s", list.join(", ").cstr());
                }
        }
        return list.contains(name);
}

bool promekiRegisterDebug(bool *enabler, const char *name, const char *file, int line) {
        if(enabler == nullptr) {
                promekiWarn("Got a null enabler");
                return false;
        }
        DebugDatabase &db = debugDatabase();
        bool ret = checkForEnvDebugEnable(name);
        db[name] += { enabler, name, file, line };
        return ret;
}

Logger &Logger::defaultLogger() {
        static Logger ret;
        return ret;
}

const char *Logger::levelToString(int level) {
        const char *ret;
        switch(level) {
                case Force: ret = "[ ]"; break;
                case Debug: ret = "[D]"; break;
                case Info:  ret = "[I]"; break;
                case Warn:  ret = "[W]"; break;
                case Err:   ret = "[E]"; break;
                default:    ret = "[?]"; break;
        }
        return ret;
}

void Logger::worker() {
        bool running = true;
        size_t cmdct = 0;

        while(running) {
                Command cmd = _queue.pop();
                cmdct++;
                switch(cmd.cmd) {
                        case CmdLog:
                                writeLog(cmd);
                                break;
                        case CmdSetFile: 
                                openLogFile(cmd);
                                break;
                        case CmdTerminate:
                                running = false;
                                cmd.level = Force;
                                cmd.msg = String::sprintf("Logger %p terminated, %llu total commands", this, (unsigned long long)cmdct);
                                cmd.file = "LOGGER";
                                cmd.line = 0;
                                cmd.ts = DateTime::now();
                                writeLog(cmd);
                                break;
                }
        }
        _file.close();
}

void Logger::writeLine(const String &str) {
        std::cout << str << std::endl;
        return;
}

void Logger::writeLog(const Command &cmd) {
        const char *level = levelToString(cmd.level);

        AnsiStream term(std::cout);
        term.setAnsiEnabled(AnsiStream::stdoutSupportsANSI());
        bool hasSrc = false;
        String srcLocation;
        if(cmd.file != nullptr) {
            srcLocation = FileInfo(cmd.file).fileName();
            srcLocation += ':';
            srcLocation += String::dec(cmd.line);
            hasSrc = true;
        }

        String ts = cmd.ts.toString("%F %T.3");

        if(_file.is_open()) {
                _file << ts;
                if(hasSrc) {
                    _file << ' ';
                    _file << srcLocation;
                }
                _file << ' ';
                _file << level;
                _file << ' ';
                _file << cmd.msg;
        }
        if(_consoleLogging) {
                term.setForeground(AnsiStream::Cyan);
                term << ts;
                if(hasSrc) {
                    term << ' ';
                    term << srcLocation;
                }
                switch(cmd.level) {
                        case Warn: term.setForeground(AnsiStream::Yellow); break;
                        case Err:  term.setForeground(AnsiStream::Red); break;
                        default:   term.setForeground(AnsiStream::Green); break;
                }
                term << ' ';
                term << level;
                term << ' ';
                term.setForeground(AnsiStream::White);
                term << cmd.msg;
                term.reset();
                term << std::endl;
        }
        return;
}

void Logger::openLogFile(const Command &cmd) {
        return;
}

PROMEKI_NAMESPACE_END

