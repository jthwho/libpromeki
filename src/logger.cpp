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

#include <sstream>
#include <promeki/logger.h>
#include <promeki/ansistream.h>
#include <promeki/fileinfo.h>

namespace promeki {

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

        while(running) {
                Command cmd = _queue.pop();
                switch(cmd.cmd) {
                        case CmdLog:
                                writeLog(cmd);
                                break;
                        case CmdSetFile: 
                                openLogFile(cmd);
                                break;
                        case CmdTerminate:
                                running = false;
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
        String srcLocation = FileInfo(cmd.file).fileName();
        srcLocation += ':';
        srcLocation += String::dec(cmd.line);

        String ts = cmd.ts.toString("%F %T.3");

        if(_file.is_open()) {
                _file << ts;
                _file << ' ';
                _file << srcLocation;
                _file << ' ';
                _file << level;
                _file << ' ';
                _file << cmd.msg;
        }
        if(_consoleLogging) {
                term.setForeground(AnsiStream::Cyan);
                term << ts;
                term << ' ';
                term << srcLocation;
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

} // namespace promeki

