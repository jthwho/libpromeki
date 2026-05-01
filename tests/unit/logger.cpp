/**
 * @file      logger.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <filesystem>
#include <atomic>
#include <doctest/doctest.h>
#include <promeki/logger.h>
#include <promeki/file.h>
#include <promeki/list.h>
#include <promeki/mutex.h>
#include <promeki/textstream.h>

using namespace promeki;

// ============================================================================
// levelToChar
// ============================================================================

TEST_CASE("Logger_levelToChar") {
        CHECK(Logger::levelToChar(Logger::Force) == ' ');
        CHECK(Logger::levelToChar(Logger::Debug) == 'D');
        CHECK(Logger::levelToChar(Logger::Info) == 'I');
        CHECK(Logger::levelToChar(Logger::Warn) == 'W');
        CHECK(Logger::levelToChar(Logger::Err) == 'E');
}

// ============================================================================
// Log level filtering
// ============================================================================

TEST_CASE("Logger_LogLevelFiltering") {
        Logger &logger = Logger::defaultLogger();
        int     savedLevel = logger.level();

        SUBCASE("setLogLevel changes threshold") {
                logger.setLogLevel(Logger::Warn);
                logger.sync();
                CHECK(logger.level() == Logger::Warn);
        }

        logger.setLogLevel(static_cast<Logger::LogLevel>(savedLevel));
        logger.sync();
}

// ============================================================================
// Sync
// ============================================================================

TEST_CASE("Logger_Sync") {
        Logger &logger = Logger::defaultLogger();

        // Log several messages and verify sync returns without hanging
        for (int i = 0; i < 10; i++) {
                logger.log(Logger::Info, PROMEKI_SOURCE_FILE, __LINE__, String::sprintf("Sync test message %d", i));
        }
        logger.sync();

        // If we get here, sync worked correctly
        CHECK(true);
}

// ============================================================================
// Console logging toggle
// ============================================================================

TEST_CASE("Logger_ConsoleLoggingToggle") {
        Logger &logger = Logger::defaultLogger();
        bool    savedConsole = logger.consoleLoggingEnabled();

        logger.setConsoleLoggingEnabled(false);
        CHECK_FALSE(logger.consoleLoggingEnabled());
        logger.log(Logger::Info, PROMEKI_SOURCE_FILE, __LINE__, "This should not appear on console");
        logger.sync();

        logger.setConsoleLoggingEnabled(true);
        CHECK(logger.consoleLoggingEnabled());
        logger.sync();

        logger.setConsoleLoggingEnabled(savedConsole);
}

// ============================================================================
// Force level always logs
// ============================================================================

TEST_CASE("Logger_ForceLevel") {
        Logger &logger = Logger::defaultLogger();
        int     savedLevel = logger.level();

        logger.setLogLevel(Logger::Err);
        logger.sync();

        logger.log(Logger::Force, PROMEKI_SOURCE_FILE, __LINE__, "Forced message at Err level");
        logger.sync();

        logger.setLogLevel(static_cast<Logger::LogLevel>(savedLevel));
        logger.sync();
        CHECK(true);
}

// ============================================================================
// Multi-line logging (StringList overload)
// ============================================================================

TEST_CASE("Logger_MultiLineLogging") {
        Logger &logger = Logger::defaultLogger();

        StringList lines;
        lines += "Line 1 of multi-line log";
        lines += "Line 2 of multi-line log";
        lines += "Line 3 of multi-line log";

        logger.log(Logger::Info, PROMEKI_SOURCE_FILE, __LINE__, lines);
        logger.sync();

        // No crash or hang means it works
        CHECK(true);
}

// ============================================================================
// Log file output
// ============================================================================

TEST_CASE("Logger_LogFileOutput") {
        Logger &logger = Logger::defaultLogger();

        std::string tmpPath = std::filesystem::temp_directory_path().string() + "/promeki_logger_test.log";

        // Remove any existing file
        std::filesystem::remove(tmpPath);

        logger.setLogFile(tmpPath);
        logger.sync();

        // Log a known message
        String testMsg = "Logger file output test message";
        logger.log(Logger::Info, PROMEKI_SOURCE_FILE, __LINE__, testMsg);
        logger.sync();

        // Read the file and verify it contains our message
        File  infile(tmpPath);
        Error err = infile.open(IODevice::ReadOnly);
        REQUIRE(err.isOk());

        TextStream ts(&infile);
        String     contents = ts.readAll();
        infile.close();

        CHECK(contents.find("Logger file output test message") != String::npos);
        CHECK(contents.find(" I [") != String::npos);

        // Clean up
        std::filesystem::remove(tmpPath);
}

// ============================================================================
// Custom formatters
// ============================================================================

TEST_CASE("Logger_CustomFileFormatter") {
        Logger &logger = Logger::defaultLogger();

        std::string tmpPath = std::filesystem::temp_directory_path().string() + "/promeki_logger_formatter_test.log";
        std::filesystem::remove(tmpPath);

        // Set a custom file formatter that uses a different format
        logger.setFileFormatter([](const Logger::LogFormat &fmt) -> String {
                return String::sprintf("CUSTOM|%c|%s", Logger::levelToChar(fmt.entry->level), fmt.entry->msg.cstr());
        });
        logger.setLogFile(tmpPath);
        logger.sync();

        logger.log(Logger::Warn, PROMEKI_SOURCE_FILE, __LINE__, "custom formatter test");
        logger.sync();

        File  infile(tmpPath);
        Error err = infile.open(IODevice::ReadOnly);
        REQUIRE(err.isOk());
        TextStream ts(&infile);
        String     contents = ts.readAll();
        infile.close();

        CHECK(contents.find("CUSTOM|W|custom formatter test") != String::npos);

        // Restore default by passing empty function
        logger.setFileFormatter({});
        logger.sync();

        std::filesystem::remove(tmpPath);
}

TEST_CASE("Logger_CustomConsoleFormatter") {
        Logger &logger = Logger::defaultLogger();

        // Save the current console formatter
        auto saved = logger.consoleFormatter();

        // Set a custom console formatter and verify no crash
        logger.setConsoleFormatter([](const Logger::LogFormat &fmt) -> String {
                return String::sprintf("[CONSOLE] %c %s", Logger::levelToChar(fmt.entry->level), fmt.entry->msg.cstr());
        });

        logger.log(Logger::Info, PROMEKI_SOURCE_FILE, __LINE__, "custom console formatter test");
        logger.sync();

        // Restore previous formatter
        logger.setConsoleFormatter(saved);
        logger.sync();

        CHECK(true);
}

TEST_CASE("Logger_DefaultFormatters") {
        // Verify the default formatters can be obtained and called
        auto fileFmt = Logger::defaultFileFormatter();
        auto consoleFmt = Logger::defaultConsoleFormatter();

        Logger::LogEntry  entry{DateTime::now(), Logger::Info, "test.cpp", 42, 12345, "hello"};
        Logger::LogFormat fmt{&entry, nullptr};
        String            fileResult = fileFmt(fmt);
        String            consoleResult = consoleFmt(fmt);

        CHECK(fileResult.contains(" I ["));
        CHECK(fileResult.contains("hello"));
        CHECK(fileResult.contains("test.cpp:42"));

        CHECK(consoleResult.contains('I'));
        CHECK(consoleResult.contains("hello"));
}

// ============================================================================
// Debug channel registration
// ============================================================================

// ============================================================================
// Listener API
// ============================================================================

namespace {

        struct CapturedEntry {
                        Logger::LogLevel level;
                        String           msg;
                        String           threadName;
        };

        class ListenerSink {
                public:
                        void capture(const Logger::LogEntry &entry, const String &threadName) {
                                Mutex::Locker lock(_mutex);
                                _entries.pushToBack(CapturedEntry{entry.level, entry.msg, threadName});
                        }

                        List<CapturedEntry> snapshot() const {
                                Mutex::Locker lock(_mutex);
                                return _entries;
                        }

                        size_t size() const {
                                Mutex::Locker lock(_mutex);
                                return _entries.size();
                        }

                private:
                        mutable Mutex       _mutex;
                        List<CapturedEntry> _entries;
        };

} // namespace

TEST_CASE("Logger_ListenerReceivesLiveEntries") {
        Logger      &logger = Logger::defaultLogger();
        ListenerSink sink;

        Logger::ListenerHandle handle = logger.installListener(
                [&sink](const Logger::LogEntry &entry, const String &threadName) { sink.capture(entry, threadName); });
        REQUIRE(handle != 0);

        logger.log(Logger::Info, "listener_test.cpp", 100, "first");
        logger.log(Logger::Warn, "listener_test.cpp", 101, "second");
        logger.sync();

        auto entries = sink.snapshot();
        REQUIRE(entries.size() == 2);
        CHECK(entries[0].level == Logger::Info);
        CHECK(entries[0].msg == "first");
        CHECK(entries[1].level == Logger::Warn);
        CHECK(entries[1].msg == "second");

        logger.removeListener(handle);
}

TEST_CASE("Logger_RemovedListenerStopsReceivingEntries") {
        Logger      &logger = Logger::defaultLogger();
        ListenerSink sink;

        Logger::ListenerHandle handle = logger.installListener(
                [&sink](const Logger::LogEntry &entry, const String &threadName) { sink.capture(entry, threadName); });
        REQUIRE(handle != 0);

        logger.log(Logger::Info, "listener_test.cpp", 200, "kept");
        logger.sync();
        REQUIRE(sink.size() == 1);

        logger.removeListener(handle);

        logger.log(Logger::Info, "listener_test.cpp", 201, "dropped");
        logger.sync();
        CHECK(sink.size() == 1); // No new entries delivered.
}

TEST_CASE("Logger_InstallListenerRejectsEmptyAndZeroHandles") {
        Logger &logger = Logger::defaultLogger();

        Logger::LogListener empty;
        CHECK(logger.installListener(empty) == 0);

        // Removing an unknown / zero handle is a no-op.
        logger.removeListener(0);
        logger.removeListener(0xdeadbeefULL);
}

TEST_CASE("Logger_HistoryReplayOnInstall") {
        Logger &logger = Logger::defaultLogger();
        size_t  savedSize = logger.historySize();
        logger.setHistorySize(64);

        // Drain anything still in flight, then prime history with known entries.
        logger.sync();

        for (int i = 0; i < 5; i++) {
                logger.log(Logger::Info, "history_test.cpp", 300 + i, String::sprintf("history-%d", i));
        }
        logger.sync();

        ListenerSink           sink;
        Logger::ListenerHandle handle = logger.installListener(
                [&sink](const Logger::LogEntry &entry, const String &threadName) { sink.capture(entry, threadName); },
                /*replayCount=*/3);
        REQUIRE(handle != 0);

        // Replay is delivered synchronously inside installListener, so by the
        // time it returns the sink must already hold the last 3 entries.
        auto entries = sink.snapshot();
        REQUIRE(entries.size() == 3);
        CHECK(entries[0].msg == "history-2");
        CHECK(entries[1].msg == "history-3");
        CHECK(entries[2].msg == "history-4");

        logger.removeListener(handle);
        logger.setHistorySize(savedSize);
}

TEST_CASE("Logger_HistoryReplayCountClamped") {
        Logger &logger = Logger::defaultLogger();
        size_t  savedSize = logger.historySize();
        logger.setHistorySize(64);
        logger.sync();

        // Reset history by shrinking to zero then back: entries get dropped on
        // the next log() that arrives with size 0.
        logger.setHistorySize(0);
        logger.log(Logger::Info, "clamp_test.cpp", 400, "drain");
        logger.sync();
        logger.setHistorySize(64);

        for (int i = 0; i < 2; i++) {
                logger.log(Logger::Info, "clamp_test.cpp", 401 + i, String::sprintf("clamp-%d", i));
        }
        logger.sync();

        ListenerSink           sink;
        Logger::ListenerHandle handle = logger.installListener(
                [&sink](const Logger::LogEntry &entry, const String &threadName) { sink.capture(entry, threadName); },
                /*replayCount=*/100); // Way more than what's stored.
        REQUIRE(handle != 0);

        auto entries = sink.snapshot();
        // Two known entries above; the drain message may or may not still be
        // present depending on test ordering, so just assert we got the two
        // known ones at the end.
        REQUIRE(entries.size() >= 2);
        CHECK(entries[entries.size() - 2].msg == "clamp-0");
        CHECK(entries[entries.size() - 1].msg == "clamp-1");

        logger.removeListener(handle);
        logger.setHistorySize(savedSize);
}

TEST_CASE("Logger_HistoryRingTrimsToConfiguredSize") {
        Logger &logger = Logger::defaultLogger();
        size_t  savedSize = logger.historySize();
        logger.setHistorySize(3);
        logger.sync();

        for (int i = 0; i < 10; i++) {
                logger.log(Logger::Info, "trim_test.cpp", 500 + i, String::sprintf("trim-%d", i));
        }
        logger.sync();

        ListenerSink           sink;
        Logger::ListenerHandle handle = logger.installListener(
                [&sink](const Logger::LogEntry &entry, const String &threadName) { sink.capture(entry, threadName); },
                /*replayCount=*/100);
        REQUIRE(handle != 0);

        auto entries = sink.snapshot();
        REQUIRE(entries.size() == 3);
        CHECK(entries[0].msg == "trim-7");
        CHECK(entries[1].msg == "trim-8");
        CHECK(entries[2].msg == "trim-9");

        logger.removeListener(handle);
        logger.setHistorySize(savedSize);
}

TEST_CASE("Logger_HistorySizeZeroDisablesReplay") {
        Logger &logger = Logger::defaultLogger();
        size_t  savedSize = logger.historySize();
        logger.setHistorySize(0);
        logger.sync();

        for (int i = 0; i < 5; i++) {
                logger.log(Logger::Info, "zero_test.cpp", 600 + i, "should-not-replay");
        }
        logger.sync();

        ListenerSink           sink;
        Logger::ListenerHandle handle = logger.installListener(
                [&sink](const Logger::LogEntry &entry, const String &threadName) { sink.capture(entry, threadName); },
                /*replayCount=*/100);
        REQUIRE(handle != 0);

        // Nothing replayed, but live delivery still works.
        CHECK(sink.size() == 0);

        logger.log(Logger::Info, "zero_test.cpp", 700, "live");
        logger.sync();
        CHECK(sink.size() == 1);

        logger.removeListener(handle);
        logger.setHistorySize(savedSize);
}

TEST_CASE("Logger_MultipleListenersAllReceiveEntries") {
        Logger &logger = Logger::defaultLogger();
        logger.sync();

        ListenerSink a;
        ListenerSink b;

        Logger::ListenerHandle ha = logger.installListener(
                [&a](const Logger::LogEntry &entry, const String &threadName) { a.capture(entry, threadName); });
        Logger::ListenerHandle hb = logger.installListener(
                [&b](const Logger::LogEntry &entry, const String &threadName) { b.capture(entry, threadName); });
        REQUIRE(ha != 0);
        REQUIRE(hb != 0);
        CHECK(ha != hb);

        logger.log(Logger::Info, "multi_test.cpp", 800, "shared");
        logger.sync();

        CHECK(a.size() == 1);
        CHECK(b.size() == 1);

        logger.removeListener(ha);
        logger.removeListener(hb);
}

// ============================================================================
// Debug channel registration
// ============================================================================

TEST_CASE("Logger_DebugRegistration") {
        // The registry stores the enabler pointer for the life of the
        // process, so it must outlive this test frame.  PROMEKI_DEBUG
        // uses a file-scope static; we mirror that here.
        static bool enabled = false;
        bool        ret = promekiRegisterDebug(&enabled, "TestChannel", PROMEKI_SOURCE_FILE, __LINE__);
        // Without PROMEKI_DEBUG env var set to include "TestChannel", should be false
        CHECK(ret == false);

        SUBCASE("Null enabler returns false") {
                bool nullRet = promekiRegisterDebug(nullptr, "NullTest", PROMEKI_SOURCE_FILE, __LINE__);
                CHECK(nullRet == false);
        }
}
