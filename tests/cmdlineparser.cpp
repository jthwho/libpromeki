/**
 * @file      cmdlineparser.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/cmdlineparser.h>

using namespace promeki;

TEST_CASE("CmdLineParser: default construction") {
        CmdLineParser parser;
        CHECK(parser.argCount() == 0);
}

TEST_CASE("CmdLineParser: register and parse short option") {
        CmdLineParser parser;
        bool flagSet = false;
        parser.registerOptions({
                {'v', "verbose", "Enable verbose mode", CmdLineParser::OptionCallback([&]() {
                        flagSet = true;
                        return 0;
                })}
        });
        StringList args = {"-v"};
        int ret = parser.parse(args);
        CHECK(ret == 0);
        CHECK(flagSet);
}

TEST_CASE("CmdLineParser: register and parse long option") {
        CmdLineParser parser;
        bool flagSet = false;
        parser.registerOptions({
                {'v', "verbose", "Enable verbose mode", CmdLineParser::OptionCallback([&]() {
                        flagSet = true;
                        return 0;
                })}
        });
        StringList args = {"--verbose"};
        int ret = parser.parse(args);
        CHECK(ret == 0);
        CHECK(flagSet);
}

TEST_CASE("CmdLineParser: string option") {
        CmdLineParser parser;
        String capturedValue;
        parser.registerOptions({
                {'o', "output", "Output file", CmdLineParser::OptionStringCallback([&](const String &val) {
                        capturedValue = val;
                        return 0;
                })}
        });
        StringList args = {"--output", "file.txt"};
        int ret = parser.parse(args);
        CHECK(ret == 0);
        CHECK(capturedValue == "file.txt");
}

TEST_CASE("CmdLineParser: int option") {
        CmdLineParser parser;
        int capturedValue = 0;
        parser.registerOptions({
                {'n', "count", "Count", CmdLineParser::OptionIntCallback([&](int val) {
                        capturedValue = val;
                        return 0;
                })}
        });
        StringList args = {"-n", "42"};
        int ret = parser.parse(args);
        CHECK(ret == 0);
        CHECK(capturedValue == 42);
}

TEST_CASE("CmdLineParser: remaining args") {
        CmdLineParser parser;
        parser.registerOptions({
                {'v', "verbose", "Verbose", CmdLineParser::OptionCallback([&]() {
                        return 0;
                })}
        });
        StringList args = {"-v", "arg1", "arg2"};
        int ret = parser.parse(args);
        CHECK(ret == 0);
        CHECK(parser.argCount() == 2);
        CHECK(parser.arg(0) == "arg1");
        CHECK(parser.arg(1) == "arg2");
}

TEST_CASE("CmdLineParser: clear") {
        CmdLineParser parser;
        parser.registerOptions({
                {'v', "verbose", "Verbose", CmdLineParser::OptionCallback([&]() { return 0; })}
        });
        parser.clear();
        CHECK(parser.argCount() == 0);
}

TEST_CASE("CmdLineParser: generateUsage") {
        CmdLineParser parser;
        parser.registerOptions({
                {'v', "verbose", "Enable verbose mode", CmdLineParser::OptionCallback([&]() { return 0; })},
                {'o', "output", "Output file", CmdLineParser::OptionStringCallback([&](const String &) { return 0; })}
        });
        StringList usage = parser.generateUsage();
        CHECK(usage.size() > 0);
}

TEST_CASE("CmdLineParser: Option argType") {
        CmdLineParser::Option opt;
        opt.callback = CmdLineParser::OptionCallback([]() { return 0; });
        CHECK(opt.argType() == CmdLineParser::Option::ArgNone);

        opt.callback = CmdLineParser::OptionStringCallback([](const String &) { return 0; });
        CHECK(opt.argType() == CmdLineParser::Option::ArgString);

        opt.callback = CmdLineParser::OptionIntCallback([](int) { return 0; });
        CHECK(opt.argType() == CmdLineParser::Option::ArgInt);

        opt.callback = CmdLineParser::OptionDoubleCallback([](double) { return 0; });
        CHECK(opt.argType() == CmdLineParser::Option::ArgDouble);
}
