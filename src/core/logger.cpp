/**
 * @file      logger.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/logger.h>
#include <promeki/thread.h>
#include <promeki/ansistream.h>
#include <promeki/list.h>
#include <promeki/fileiodevice.h>
#include <promeki/env.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(Logger)

struct DebugDatabaseItem {
        bool    *enabler;
        String  name;
        String  file;
        int     line;
};

using DebugDatabaseItemList = List<DebugDatabaseItem>;

using DebugDatabase = Map<String, DebugDatabaseItemList>;

static DebugDatabase &debugDatabase() {
        static DebugDatabase ret;
        return ret;
}

static bool checkForEnvDebugEnable(const String &name) {
        static bool done = false;
        static StringList list;
        if(!done) {
                done = true;
                String enval = Env::get("PROMEKI_DEBUG");
                if(enval.isEmpty()) return false;
                list = enval.split(",");
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

Logger::Logger() : _level(Info), _consoleLogging(true),
        _fileFormatter(defaultFileFormatter()),
        _consoleFormatter(defaultConsoleFormatter()) {
        // Force stdio singletons to initialize before this Logger,
        // ensuring they outlive the Logger at static destruction time.
        FileIODevice::stdoutDevice();
        _thread = std::thread(&Logger::worker, this);
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

        FileIODevice *logFile = nullptr;
        bool running = true;
        size_t cmdct = 0;

        while(running) {
                auto [cmd, err] = _queue.pop();
                cmdct++;
                std::visit([&](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, LogEntry>) {
                                writeLog(arg, logFile);
                        } else if constexpr (std::is_same_v<T, CmdSetThreadName>) {
                                _threadNames[arg.threadId] = arg.name;
                        } else if constexpr (std::is_same_v<T, CmdSetFile>) {
                                logFile = openLogFile(arg.filename, logFile);
                        } else if constexpr (std::is_same_v<T, CmdSetFormatter>) {
                                if(arg.console) {
                                        _consoleFormatter = arg.formatter ? arg.formatter : defaultConsoleFormatter();
                                } else {
                                        _fileFormatter = arg.formatter ? arg.formatter : defaultFileFormatter();
                                }
                        } else if constexpr (std::is_same_v<T, CmdSync>) {
                                arg.promise->setValue();
                        } else if constexpr (std::is_same_v<T, CmdTerminate>) {
                                running = false;
                                if(_promeki_debug_enabled) {
                                        LogEntry logentry{DateTime::now(), Debug, "LOGGER", 0,
                                                cachedThreadId(),
                                                String::sprintf("Logger %p terminated, %llu total commands",
                                                        this, (unsigned long long)cmdct)};
                                        writeLog(logentry, logFile);
                                }
                        }
                }, cmd);
        }
        delete logFile;
        delete self;
}

void Logger::writeLog(const LogEntry &entry, FileIODevice *logFile) {
        auto it = _threadNames.find(entry.threadId);
        LogFormat fmt{&entry, it != _threadNames.end() ? &it->second : nullptr};
        if(logFile != nullptr && logFile->isOpen()) {
                String line = _fileFormatter(fmt) + "\n";
                logFile->write(line.cstr(), static_cast<int64_t>(line.length()));
                logFile->flush();
        }
        if(_consoleLogging.value()) {
                FileIODevice *out = FileIODevice::stdoutDevice();
                String line = _consoleFormatter(fmt) + "\033[0m\n";
                out->write(line.cstr(), static_cast<int64_t>(line.length()));
                out->flush();
        }
}

FileIODevice *Logger::openLogFile(const String &filename, FileIODevice *existing) {
        if(existing != nullptr) {
                existing->close();
                delete existing;
        }
        auto *dev = new FileIODevice(filename);
        Error err = dev->open(IODevice::Append);
        if(err.isError()) {
                LogEntry entry{DateTime::now(), Err, "LOGGER", 0,
                        cachedThreadId(),
                        String::sprintf("Failed to open log file: %s", filename.cstr())};
                writeLog(entry, nullptr);
                delete dev;
                return nullptr;
        }
        return dev;
}

PROMEKI_NAMESPACE_END
