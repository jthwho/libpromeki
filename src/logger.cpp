/**
 * @file      logger.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <map>
#include <sstream>
#include <cstdlib>
#include <promeki/logger.h>
#include <promeki/thread.h>
#include <promeki/ansistream.h>
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

struct ThreadLogInfo {
        uint64_t        id = 0;
        String          name;
};

static ThreadLogInfo &threadLogInfo() {
        static thread_local ThreadLogInfo info;
        if(info.id == 0) {
                info.id = Thread::currentNativeId();
                Thread *t = Thread::currentThread();
                if(t != nullptr) info.name = t->name();
        }
        return info;
}

static const ThreadLogInfo &cachedThreadLogInfo() {
        return threadLogInfo();
}

void Logger::setThreadName(const String &name) {
        threadLogInfo().name = name;
}

void Logger::log(LogLevel loglevel, const char *file, int line, const String &msg) {
        const ThreadLogInfo &ti = cachedThreadLogInfo();
        _queue.emplace(CmdLog{loglevel, file, line, msg, DateTime::now(), ti.id, &ti.name});
}

void Logger::log(LogLevel loglevel, const char *file, int line, const StringList &lines) {
        const ThreadLogInfo &ti = cachedThreadLogInfo();
        DateTime ts = DateTime::now();
        List<Command> cmdlist;
        for(const auto &item : lines) {
                cmdlist.pushToBack(CmdLog{loglevel, file, line, item, ts, ti.id, &ti.name});
        }
        _queue.push(std::move(cmdlist));
}

Logger &Logger::defaultLogger() {
        static Logger ret;
        return ret;
}

const char *Logger::levelToString(LogLevel level) {
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

Logger::LogFormatter Logger::defaultFileFormatter() {
        return [](const DateTime &ts, LogLevel level, const char *file, int line,
                  uint64_t threadId, const String &threadName, const String &msg) -> String {
                const char *lvl = levelToString(level);
                String result = ts.toString("%F %T.3");
                if(file != nullptr) {
                        result += ' ';
                        result += file;
                        result += ':';
                        result += String::dec(line);
                }
                result += ' ';
                result += lvl;
                result += " [";
                if(!threadName.isEmpty()) {
                        result += threadName;
                } else {
                        result += String::dec(threadId);
                }
                result += "] ";
                result += msg;
                return result;
        };
}

Logger::LogFormatter Logger::defaultConsoleFormatter() {
        return [](const DateTime &ts, LogLevel level, const char *file, int line,
                  uint64_t threadId, const String &threadName, const String &msg) -> String {
                bool ansi = AnsiStream::stdoutSupportsANSI();
                const char *lvl = levelToString(level);

                String result;
                if(ansi) result += "\033[1;36m"; // Cyan
                result += ts.toString("%F %T.3");
                if(file != nullptr) {
                        result += ' ';
                        result += file;
                        result += ':';
                        result += String::dec(line);
                }
                if(ansi) {
                        switch(level) {
                                case Warn: result += "\033[1;33m"; break; // Yellow
                                case Err:  result += "\033[1;31m"; break; // Red
                                default:   result += "\033[1;32m"; break; // Green
                        }
                }
                result += ' ';
                result += lvl;
                if(ansi) result += "\033[0m"; // Reset
                result += " [";
                if(ansi) result += "\033[0;35m"; // Magenta
                if(!threadName.isEmpty()) {
                        result += threadName;
                } else {
                        result += String::dec(threadId);
                }
                if(ansi) result += "\033[0m"; // Reset
                result += "] ";
                result += msg;
                return result;
        };
}

void Logger::worker() {
        Thread *self = Thread::adoptCurrentThread();
        self->setName("logger");

        bool running = true;
        size_t cmdct = 0;

        while(running) {
                auto [cmd, err] = _queue.pop();
                cmdct++;
                std::visit([&](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, CmdLog>) {
                                writeLog(arg);
                        } else if constexpr (std::is_same_v<T, CmdSetFile>) {
                                openLogFile(arg.filename);
                        } else if constexpr (std::is_same_v<T, CmdSetFormatter>) {
                                if(arg.console) {
                                        _consoleFormatter = arg.formatter ? arg.formatter : defaultConsoleFormatter();
                                } else {
                                        _fileFormatter = arg.formatter ? arg.formatter : defaultFileFormatter();
                                }
                        } else if constexpr (std::is_same_v<T, CmdSync>) {
                                arg.promise->set_value();
                        } else if constexpr (std::is_same_v<T, CmdTerminate>) {
                                running = false;
                                const ThreadLogInfo &ti = cachedThreadLogInfo();
                                CmdLog logcmd{Debug, "LOGGER", 0,
                                        String::sprintf("Logger %p terminated, %llu total commands",
                                                this, (unsigned long long)cmdct),
                                        DateTime::now(),
                                        ti.id, &ti.name};
                                writeLog(logcmd);
                        }
                }, cmd);
        }
        _file.close();
        delete self;
}

void Logger::writeLog(const CmdLog &cmd) {
        static const String empty;
        const String &tname = cmd.threadName != nullptr ? *cmd.threadName : empty;
        if(_file.is_open()) {
                _file << _fileFormatter(cmd.ts, cmd.level, cmd.file, cmd.line,
                        cmd.threadId, tname, cmd.msg) << std::endl;
        }
        if(_consoleLogging) {
                std::cout << _consoleFormatter(cmd.ts, cmd.level, cmd.file, cmd.line,
                        cmd.threadId, tname, cmd.msg) << "\033[0m" << std::endl;
        }
}

void Logger::openLogFile(const String &filename) {
        if(_file.is_open()) _file.close();
        _file.open(filename.stds(), std::ios::out | std::ios::app);
        if(!_file.is_open()) {
                const ThreadLogInfo &ti = cachedThreadLogInfo();
                CmdLog cmd{Err, "LOGGER", 0,
                        String::sprintf("Failed to open log file: %s", filename.cstr()),
                        DateTime::now(),
                        ti.id, &ti.name};
                writeLog(cmd);
        }
}

PROMEKI_NAMESPACE_END

