/**
 * @file      unittest.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <promeki/unittest.h>
#include <promeki/logger.h>
#include <promeki/buildinfo.h>
#include <promeki/cmdlineparser.h>

using namespace promeki;

void outputUsage(const CmdLineParser &p) {
        StringList lines = p.generateUsage();
        for(const auto &line : lines) {
                std::cout << line << std::endl;
        }
        exit(0);
        return;
}

// Run the built in unit tests
int main(int argc, char **argv) {
        String filter = ".+";
        CmdLineParser cmdLine;
        cmdLine.registerOptions({
                CmdLineParser::Option('f', "filter", "regex match to select which tests should run",
                        CmdLineParser::OptionStringCallback([&](const String &val) { 
                                filter = val;
                                return 0; })
                ),
                CmdLineParser::Option('h', "", "show the usage",
                        CmdLineParser::OptionCallback([&]() { 
                                outputUsage(cmdLine);
                                return 0; })
                )

        });

        int ret = cmdLine.parseMain(argc, argv);
        if(ret != 0) {
                promekiErr("Argument parsing failed with %d", ret);
                return ret;
        }

        logBuildInfo(); 
        return runUnitTests(filter) ? 0 : 9999;
}

