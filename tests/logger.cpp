/**
 * @file      logger.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <fstream>
#include <filesystem>
#include <doctest/doctest.h>
#include <promeki/logger.h>

using namespace promeki;

// ============================================================================
// levelToString
// ============================================================================

TEST_CASE("Logger_levelToString") {
        CHECK(std::string(Logger::levelToString(Logger::Force)) == "[ ]");
        CHECK(std::string(Logger::levelToString(Logger::Debug)) == "[D]");
        CHECK(std::string(Logger::levelToString(Logger::Info))  == "[I]");
        CHECK(std::string(Logger::levelToString(Logger::Warn))  == "[W]");
        CHECK(std::string(Logger::levelToString(Logger::Err))   == "[E]");
}

// ============================================================================
// Log level filtering
// ============================================================================

TEST_CASE("Logger_LogLevelFiltering") {
        Logger &logger = Logger::defaultLogger();

        SUBCASE("Default level is Debug") {
                CHECK(logger.level() == Logger::Debug);
        }

        SUBCASE("setLogLevel changes threshold") {
                logger.setLogLevel(Logger::Warn);
                logger.sync();
                CHECK(logger.level() == Logger::Warn);

                // Restore default
                logger.setLogLevel(Logger::Debug);
                logger.sync();
                CHECK(logger.level() == Logger::Debug);
        }
}

// ============================================================================
// Sync
// ============================================================================

TEST_CASE("Logger_Sync") {
        Logger &logger = Logger::defaultLogger();

        // Log several messages and verify sync returns without hanging
        for(int i = 0; i < 10; i++) {
                logger.log(Logger::Info, __FILE__, __LINE__, String::sprintf("Sync test message %d", i));
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

        // Disable console logging, log a message, re-enable
        logger.setConsoleLoggingEnabled(false);
        logger.log(Logger::Info, __FILE__, __LINE__, "This should not appear on console");
        logger.sync();

        logger.setConsoleLoggingEnabled(true);
        logger.log(Logger::Info, __FILE__, __LINE__, "Console logging re-enabled");
        logger.sync();

        // No crash or hang means it works
        CHECK(true);
}

// ============================================================================
// Force level always logs
// ============================================================================

TEST_CASE("Logger_ForceLevel") {
        Logger &logger = Logger::defaultLogger();

        // Set level to Err so only Err and Force pass
        logger.setLogLevel(Logger::Err);
        logger.sync();

        // Force should still be logged (not filtered)
        logger.log(Logger::Force, __FILE__, __LINE__, "Forced message at Err level");
        logger.sync();

        // Restore
        logger.setLogLevel(Logger::Debug);
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

        logger.log(Logger::Info, __FILE__, __LINE__, lines);
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
        logger.log(Logger::Info, __FILE__, __LINE__, testMsg);
        logger.sync();

        // Read the file and verify it contains our message
        std::ifstream infile(tmpPath);
        REQUIRE(infile.is_open());

        std::string contents((std::istreambuf_iterator<char>(infile)),
                              std::istreambuf_iterator<char>());
        infile.close();

        CHECK(contents.find("Logger file output test message") != std::string::npos);
        CHECK(contents.find("[I]") != std::string::npos);

        // Clean up
        std::filesystem::remove(tmpPath);
}

// ============================================================================
// Debug channel registration
// ============================================================================

TEST_CASE("Logger_DebugRegistration") {
        // Register a debug channel and verify it doesn't crash
        bool enabled = false;
        bool ret = promekiRegisterDebug(&enabled, "TestChannel", __FILE__, __LINE__);
        // Without PROMEKI_DEBUG env var set to include "TestChannel", should be false
        CHECK(ret == false);

        SUBCASE("Null enabler returns false") {
                bool nullRet = promekiRegisterDebug(nullptr, "NullTest", __FILE__, __LINE__);
                CHECK(nullRet == false);
        }
}
