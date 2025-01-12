/*****************************************************************************
 * cmdlineparser.h
 * May 02, 2023
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

#pragma once

#include <variant>
#include <map>
#include <promeki/namespace.h>
#include <promeki/cmdlineparser.h>
#include <promeki/stringlist.h>
#include <promeki/string.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

class CmdLineParser {
        public:
                using OptionCallback = std::function<int()>;
                using OptionBoolCallback = std::function<int(bool)>;
                using OptionStringCallback = std::function<int(const String &)>;
                using OptionIntCallback = std::function<int(int)>;
                using OptionDoubleCallback = std::function<int(double)>;

                // Always add to the end of the list as the option variant index is used
                // to infer data type.
                using OptionCallbackVariant = std::variant<
                        OptionCallback, 
                        OptionBoolCallback,
                        OptionStringCallback, 
                        OptionIntCallback,
                        OptionDoubleCallback>;

                class Option {
                        public:
                                enum ArgType {
                                        ArgNone,
                                        ArgBool,
                                        ArgString,
                                        ArgInt,
                                        ArgDouble,
                                        ArgUnknown
                                };

                                char                    shortName = 0;
                                String                  longName;
                                String                  desc;
                                OptionCallbackVariant   callback;

                                Option() = default;
                                Option(char sn, const String &ln, const String &d, OptionCallbackVariant cb) : 
                                        shortName(sn), longName(ln), desc(d), callback(cb) {}
                                //Option(char sn, const String &ln, OptionBoolCallback cb) : shortName(sn), longName(ln), callback(cb) {}
                                //Option(char sn, const char *ln, OptionIntCallback cb) : shortName(sn), longName(ln), callback(cb) {}
                                //Option(char sn, const char *ln, OptionDoubleCallback cb) : shortName(sn), longName(ln), callback(cb) {}
                                //Option(char sn, const char *ln, OptionStringCallback cb) : shortName(sn), longName(ln), callback(cb) {}
                                
                                ArgType argType() const {
                                        if(std::holds_alternative<OptionCallback>(callback)) return ArgNone;
                                        if(std::holds_alternative<OptionBoolCallback>(callback)) return ArgBool;
                                        if(std::holds_alternative<OptionIntCallback>(callback)) return ArgInt;
                                        if(std::holds_alternative<OptionStringCallback>(callback)) return ArgString;
                                        if(std::holds_alternative<OptionDoubleCallback>(callback)) return ArgDouble;
                                        return ArgUnknown;
                                }

                };

                void registerOptions(const std::initializer_list<Option> &options) {
                        for(const auto &opt : options) {
                                if(opt.shortName != 0) {
                                        _optionsMap[String(1, opt.shortName)] = opt;
                                }
                                if(!opt.longName.isEmpty()) {
                                        _optionsMap[opt.longName] = opt;
                                }
                                _options.push_back(opt);
                        }
                }

                CmdLineParser() = default;

                // Clear the object, including the 
                void clear() {
                        _optionsMap.clear();
                        _options.clear();
                        _args.clear();
                        return;
                }

                // Helper function to make this easier to use w/ the main
                // function.
                int parseMain(int argc, char **argv) {
                        StringList args(argc, (const char **)argv);
                        args.remove(0);
                        return parse(args);
                }

                // Does the parsing.  Returns 0 on success or some other
                // number that you've returned from one of your option callbacks.
                int parse(StringList args);

                // Returns the arguments left after parsing out the options
                int argCount() const { return _args.size(); }

                // Returns an argument left after parsing
                const String &arg(int index) const { return _args[index]; }

                // Generates a string list with each line containing the help for
                // one option.  This should be suitable for printing to the screen
                StringList generateUsage() const;

        private:
                std::map<String, Option>        _optionsMap;
                std::vector<Option>             _options;
                StringList                      _args;

                static String optionFullName(bool optionName, const Option &option);
};

PROMEKI_NAMESPACE_END
