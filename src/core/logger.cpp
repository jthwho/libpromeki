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
#include <promeki/buildinfo.h>

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

static StringList &debugEnvList() {
        static StringList list;
        return list;
}

static bool &debugEnvListMode() {
        static bool mode = false;
        return mode;
}

static void printEnvDebugReport() {
        DebugDatabase &db = debugDatabase();
        if(debugEnvListMode()) {
                promekiInfo("PROMEKI_DEBUG=list, available debug items:");
                for(const auto &[name, items] : db) {
                        for(const auto &item : items) {
                                promekiInfo("  %s (%s:%d)",
                                        name.cstr(), item.file.cstr(), item.line);
                        }
                }
                return;
        }
        const StringList &list = debugEnvList();
        if(list.isEmpty()) return;
#ifndef PROMEKI_DEBUG_ENABLE
        promekiWarn("PROMEKI_DEBUG is set but promekiDebug() messages are "
                    "compiled out. Rebuild with "
                    "-DCMAKE_BUILD_TYPE=DevRelease or Debug.");
#endif
        for(const auto &name : list) {
                if(!db.contains(name)) {
                        promekiWarn("PROMEKI_DEBUG item '%s' is not registered "
                                    "in the debug database", name.cstr());
                }
        }
}

namespace {
        // Static destructor runs at program exit, by which time every
        // PROMEKI_DEBUG() macro has registered, so the report sees
        // the complete database.  Construction order (in
        // ensureEnvDebugReporter) forces the Logger to be built
        // first, guaranteeing LIFO destruction tears the reporter
        // down while the Logger is still alive.
        struct EnvDebugReporter {
                ~EnvDebugReporter() { printEnvDebugReport(); }
        };
}

static void ensureEnvDebugReporter() {
        (void)Logger::defaultLogger();
        static EnvDebugReporter instance;
        (void)instance;
}

static bool checkForEnvDebugEnable(const String &name) {
        static bool done = false;
        StringList &list = debugEnvList();
        if(!done) {
                done = true;
                String enval = Env::get("PROMEKI_DEBUG");
                if(enval.isEmpty()) return false;
                StringList parsed = enval.split(",");
                if(parsed.size() == 1 && parsed.front() == "list") {
                        debugEnvListMode() = true;
                } else {
                        list = std::move(parsed);
                }
                ensureEnvDebugReporter();
                promekiInfo("PROMEKI_DEBUG=%s", enval.cstr());
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
        Logger &log = defaultLogger();
        if(log._terminating.value()) return;
        log._queue.emplace(CmdSetThreadName{cachedThreadId(), name});
}

void Logger::log(LogLevel loglevel, const char *file, int line, const String &msg) {
        if(_terminating.value()) return;
        _queue.emplace(LogEntry{DateTime::now(), loglevel, file, line, cachedThreadId(), msg});
}

void Logger::log(LogLevel loglevel, const char *file, int line, const StringList &lines) {
        if(_terminating.value()) return;
        uint64_t id = cachedThreadId();
        DateTime ts = DateTime::now();
        List<Command> cmdlist;
        for(const auto &item : lines) {
                cmdlist.pushToBack(LogEntry{ts, loglevel, file, line, id, item});
        }
        _queue.push(std::move(cmdlist));
}

Logger::ListenerHandle Logger::installListener(LogListener listener, size_t replayCount) {
        if(!listener) return 0;
        if(_terminating.value()) return 0;
        auto promise = std::make_shared<Promise<ListenerHandle>>();
        Future<ListenerHandle> future = promise->future();
        _queue.emplace(CmdInstallListener{std::move(listener), replayCount, promise});
        return future.result().first();
}

void Logger::removeListener(ListenerHandle handle) {
        if(handle == 0) return;
        if(_terminating.value()) return;
        auto promise = std::make_shared<Promise<void>>();
        Future<void> future = promise->future();
        _queue.emplace(CmdRemoveListener{handle, promise});
        future.waitForFinished();
}

Logger::~Logger() {
        // Drain any commands already enqueued, then close the gate
        // so subsequent public log()/setX() calls become no-ops, then
        // tell the worker to exit and join it.
        sync();
        _terminating.setValue(true);
        _queue.emplace(CmdTerminate{});
        _thread.join();
}

Logger &Logger::defaultLogger() {
        static Logger ret;
        return ret;
}

Logger::DebugChannel::List Logger::debugChannels() {
        DebugChannel::List out;
        const DebugDatabase &db = debugDatabase();
        for(const auto &[name, items] : db) {
                for(const auto &item : items) {
                        DebugChannel ch;
                        ch.name    = name;
                        ch.file    = item.file;
                        ch.line    = item.line;
                        ch.enabled = item.enabler != nullptr ? *item.enabler : false;
                        out.pushToBack(ch);
                }
        }
        return out;
}

bool Logger::setDebugChannel(const String &name, bool enabled) {
        DebugDatabase &db = debugDatabase();
        auto it = db.find(name);
        if(it == db.end()) return false;
        for(auto &item : it->second) {
                if(item.enabler != nullptr) *item.enabler = enabled;
        }
        return true;
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
                static const int maxSourceLen = 25;
                String lineno = String::number(entry.line);
                String file = String(entry.file).left(maxSourceLen - lineno.length() - 1);
                String source = file + ":" + lineno;

                String thread = fmt.threadName != nullptr ?
                        *fmt.threadName : String::dec(entry.threadId);

                String result;
                if(ansi) result += "\033[0;36m"; // Dim cyan
                result += entry.ts.toString("%T.3");
                if(ansi) result += "\033[0m";
                result += ' ';
                result += String::sprintf("%-25s", source.cstr());
                result += ' ';
                if(ansi) result += "\033[0;35m";
                result += String::sprintf("%-10s", thread.cstr());
                result += ' ';
                if(ansi) {
                    switch(entry.level) {
                        case Warn: result += "\033[1;33m"; break;
                        case Err:  result += "\033[1;31m"; break;
                        default:   result += "\033[1;32m"; break;
                    }
                }
                result += lvl;
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

#ifdef PROMEKI_DEBUG_ENABLE
        // When PROMEKI_DEBUG is set, write a startup banner before
        // processing any queued messages so log files always begin
        // with the build/platform context needed for diagnosis.
        if(!Env::get("PROMEKI_DEBUG").isEmpty()) {
                uint64_t tid = cachedThreadId();
                _threadNames[tid] = "logger";
                DateTime now = DateTime::now();
                for(const auto &line : buildInfoStrings()) {
                        writeLog(LogEntry{now, Info, "LOGGER", 0, tid, line}, logFile);
                }
        }
#endif

        bool running = true;
        size_t cmdct = 0;

        while(running) {
                auto [cmd, err] = _queue.pop();
                cmdct++;
                std::visit([&](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, LogEntry>) {
                                writeLog(arg, logFile);
                                auto it = _threadNames.find(arg.threadId);
                                String tname = it != _threadNames.end() ? it->second : String();
                                // Append to the history ring, trimming to the
                                // currently configured size.  Trimming is done
                                // here (rather than on setHistorySize) so the
                                // size knob can be changed from any thread
                                // without taking a lock.
                                size_t cap = _historySize.value();
                                if(cap > 0) {
                                        _history.pushToBack(HistoryEntry{arg, tname});
                                        while(_history.size() > cap) _history.popFromFront();
                                } else if(_history.size() > 0) {
                                        _history.clear();
                                }
                                // Fan out to any registered listeners.
                                for(auto &lst : _listeners) {
                                        lst.fn(arg, tname);
                                }
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
                        } else if constexpr (std::is_same_v<T, CmdInstallListener>) {
                                // Replay the tail of the history ring to the
                                // new listener, then register it — both happen
                                // on this worker thread so no entry can slip
                                // between the replay and the live subscription.
                                size_t replay = arg.replayCount;
                                if(replay > _history.size()) replay = _history.size();
                                size_t start = _history.size() - replay;
                                for(size_t i = start; i < _history.size(); i++) {
                                        const HistoryEntry &h = _history[i];
                                        arg.listener(h.entry, h.threadName);
                                }
                                ListenerHandle handle =
                                        _nextListenerHandle.fetchAndAdd(1) + 1;
                                _listeners.pushToBack(ListenerEntry{handle, std::move(arg.listener)});
                                arg.promise->setValue(handle);
                        } else if constexpr (std::is_same_v<T, CmdRemoveListener>) {
                                for(auto it = _listeners.begin(); it != _listeners.end(); ++it) {
                                        if(it->handle == arg.handle) {
                                                _listeners.remove(it);
                                                break;
                                        }
                                }
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
