/*****************************************************************************
 * unittest.cpp
 * April 26, 2023
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

#include <promeki/unittest.h>
#include <promeki/logger.h>
#include <promeki/buildinfo.h>
#include <promeki/cmdlineparser.h>

using namespace promeki;

// Run the built in unit tests
int main(int argc, char **argv) {
        String filter = ".+";
        CmdLineParser cmdLine;
        cmdLine.registerOptions({
                CmdLineParser::Option('t', "test", 
                        CmdLineParser::OptionCallback([](){ 
                                promekiInfo("Test option"); 
                                return 0; 
                        })
                ),
                CmdLineParser::Option('b', "bool", 
                        CmdLineParser::OptionBoolCallback([](bool val) { 
                                promekiInfo("Test Bool %s option", val ? "true" : "false"); 
                                return 0; })
                ),
                CmdLineParser::Option('i', "int", 
                        CmdLineParser::OptionIntCallback([](int val) { 
                                promekiInfo("Test int %d option", val); 
                                return 0; })
                ),
                CmdLineParser::Option('d', "double", 
                        CmdLineParser::OptionDoubleCallback([](double val) { 
                                promekiInfo("Test Double %lf option", val); 
                                return 0; })
                ),
                CmdLineParser::Option('s', "string", 
                        CmdLineParser::OptionStringCallback([](const String &val) { 
                                promekiInfo("Test Bool %s option", val.cstr()); 
                                return 0; })
                )
        });

        int ret = cmdLine.parseMain(argc, argv);
        if(ret != 0) {
                promekiErr("Argument parsing failed with %d", ret);
                return ret;
        }
        if(cmdLine.argCount()) filter = cmdLine.arg(0);

        logBuildInfo(); 
        return runUnitTests(filter) ? 0 : 9999;
}

