/**
 * @file      logger.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

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

static uint64_t cachedThreadId() {
        static thread_local uint64_t id = 0;
        if(id == 0) id = Thread::currentNativeId();
        return id;
}

void Logger::setThreadName(const String &name) {
        defaultLogger()._queue.emplace(CmdSetThreadName{cachedThreadId(), name});
}

void Logger::log(LogLevel loglevel, const char *file, int line, const String &msg) {
        _queue.emplace(LogEntry{DateTime::now(), loglevel, file, line, cachedThreadId(), msg});
}

void Logger::log(LogLevel loglevel, const char *file, int line, const StringList &lines) {
        uint64_t id = cachedThreadId();
        DateTime ts = DateTime::now();
        List<Command> cmdlist;
        for(const auto &item : lines) {
                cmdlist.pushToBack(LogEntry{ts, loglevel, file, line, id, item});
        }
        _queue.push(std::move(cmdlist));
}

Logger &Logger::defaultLogger() {
        static Logger ret;
        return ret;
}

char Logger::levelToChar(LogLevel level) {
        char ret = ' ';
        switch(level) {
                case Force: ret = ' '; break;
                case Debug: ret = 'D'; break;
                case Info:  ret = 'I'; break;
                case Warn:  ret = 'W'; break;
                case Err:   ret = 'E'; break;
                default:    ret = '?'; break;
        }
        return ret;
}

Logger::LogFormatter Logger::defaultFileFormatter() {
        return [](const LogFormat &fmt) -> String {
                const LogEntry &entry = *fmt.entry;
                char lvl = levelToChar(entry.level);
                String result = entry.ts.toString("%F %T.3");
                if(entry.file != nullptr) {
                        result += ' ';
                        result += entry.file;
                        result += ':';
                        result += String::dec(entry.line);
                }
                result += ' ';
                result += lvl;
                result += " [";
                if(fmt.threadName != nullptr) {
                        result += *fmt.threadName;
                } else {
                        result += String::dec(entry.threadId);
                }
                result += "] ";
                result += entry.msg;
                return result;
        };
}

Logger::LogFormatter Logger::defaultConsoleFormatter() {
        return [](const LogFormat &fmt) -> String {
                const LogEntry &entry = *fmt.entry;
                bool ansi = AnsiStream::stdoutSupportsANSI();
                char lvl = levelToChar(entry.level);

                // Build source:line and pad to a fixed column width
                String source;
                if(entry.file != nullptr) {
                        source = String::sprintf("%s:%d", entry.file, entry.line);
                }

                String thread = fmt.threadName != nullptr ?
                        *fmt.threadName : String::dec(entry.threadId);

                String result;
                if(ansi) {
                    switch(entry.level) {
                        case Warn: result += "\033[1;33m"; break;
                        case Err:  result += "\033[1;31m"; break;
                        default:   result += "\033[1;32m"; break;
                    }
                }
                result += lvl;
                result += ' ';
                if(ansi) result += "\033[0;36m"; // Dim cyan
                result += entry.ts.toString("%T.3");
                if(ansi) result += "\033[0m";
                result += ' ';
                result += String::sprintf("%-20s", source.cstr());
                result += ' ';
                if(ansi) result += "\033[0;35m";
                result += String::sprintf("%-10s", thread.cstr());
                result += ' ';
                if(ansi) result += "\033[0m";
                result += entry.msg;
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
                        if constexpr (std::is_same_v<T, LogEntry>) {
                                writeLog(arg);
                        } else if constexpr (std::is_same_v<T, CmdSetThreadName>) {
                                _threadNames[arg.threadId] = arg.name;
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
                                LogEntry logentry{DateTime::now(), Debug, "LOGGER", 0,
                                        cachedThreadId(),
                                        String::sprintf("Logger %p terminated, %llu total commands",
                                                this, (unsigned long long)cmdct)};
                                writeLog(logentry);
                        }
                }, cmd);
        }
        _file.close();
        delete self;
}

void Logger::writeLog(const LogEntry &entry) {
        auto it = _threadNames.find(entry.threadId);
        LogFormat fmt{&entry, it != _threadNames.end() ? &it->second : nullptr};
        if(_file.is_open()) {
                _file << _fileFormatter(fmt) << std::endl;
        }
        if(_consoleLogging) {
                std::cout << _consoleFormatter(fmt) << "\033[0m" << std::endl;
        }
}

void Logger::openLogFile(const String &filename) {
        if(_file.is_open()) _file.close();
        _file.open(filename.stds(), std::ios::out | std::ios::app);
        if(!_file.is_open()) {
                LogEntry entry{DateTime::now(), Err, "LOGGER", 0,
                        cachedThreadId(),
                        String::sprintf("Failed to open log file: %s", filename.cstr())};
                writeLog(entry);
        }
}

PROMEKI_NAMESPACE_END

