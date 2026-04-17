/**
 * @file      logger.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <filesystem>
#include <doctest/doctest.h>
#include <promeki/logger.h>
#include <promeki/file.h>
#include <promeki/textstream.h>

using namespace promeki;

// ============================================================================
// levelToChar
// ============================================================================

TEST_CASE("Logger_levelToChar") {
        CHECK(Logger::levelToChar(Logger::Force) == ' ');
        CHECK(Logger::levelToChar(Logger::Debug) == 'D');
        CHECK(Logger::levelToChar(Logger::Info)  == 'I');
        CHECK(Logger::levelToChar(Logger::Warn)  == 'W');
        CHECK(Logger::levelToChar(Logger::Err)   == 'E');
}

// ============================================================================
// Log level filtering
// ============================================================================

TEST_CASE("Logger_LogLevelFiltering") {
        Logger &logger = Logger::defaultLogger();
        int savedLevel = logger.level();

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
        for(int i = 0; i < 10; i++) {
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
        bool savedConsole = logger.consoleLoggingEnabled();

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
        int savedLevel = logger.level();

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
        File infile(tmpPath);
        Error err = infile.open(IODevice::ReadOnly);
        REQUIRE(err.isOk());

        TextStream ts(&infile);
        String contents = ts.readAll();
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

        File infile(tmpPath);
        Error err = infile.open(IODevice::ReadOnly);
        REQUIRE(err.isOk());
        TextStream ts(&infile);
        String contents = ts.readAll();
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

        Logger::LogEntry entry{DateTime::now(), Logger::Info, "test.cpp", 42, 12345, "hello"};
        Logger::LogFormat fmt{&entry, nullptr};
        String fileResult = fileFmt(fmt);
        String consoleResult = consoleFmt(fmt);

        CHECK(fileResult.contains(" I ["));
        CHECK(fileResult.contains("hello"));
        CHECK(fileResult.contains("test.cpp:42"));

        CHECK(consoleResult.contains('I'));
        CHECK(consoleResult.contains("hello"));
}

// ============================================================================
// Debug channel registration
// ============================================================================

TEST_CASE("Logger_DebugRegistration") {
        // Register a debug channel and verify it doesn't crash
        bool enabled = false;
        bool ret = promekiRegisterDebug(&enabled, "TestChannel", PROMEKI_SOURCE_FILE, __LINE__);
        // Without PROMEKI_DEBUG env var set to include "TestChannel", should be false
        CHECK(ret == false);

        SUBCASE("Null enabler returns false") {
                bool nullRet = promekiRegisterDebug(nullptr, "NullTest", PROMEKI_SOURCE_FILE, __LINE__);
                CHECK(nullRet == false);
        }
}
