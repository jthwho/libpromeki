/*****************************************************************************
 * cmdlineparser.cpp
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

#include <promeki/cmdlineparser.h>
#include <promeki/error.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

int CmdLineParser::parse(StringList args) {
        _args.clear(); // Clear the args.
        bool optionParsingEnabled = true;
        for(auto arg = args.begin(); arg != args.end(); ++arg) {
                if(!optionParsingEnabled) {
                        _args += *arg;
                        continue;
                }
                if(*arg == "--") {
                        optionParsingEnabled = false;
                        continue;
                }
                if(!arg->startsWith('-') || arg->size() == 1) {
                        _args += *arg;
                        continue;
                }
                if((*arg)[1] != '-') {
                        // In the case of a short name that has an option, split them,
                        // update the option, and add the option arg to the list
                        if(arg->size() > 2) {
                                String a = arg->substr(0, 2);
                                String o = arg->substr(2);
                                *arg = a;
                                arg = args.insert(arg + 1, o);
                                --arg;
                        }
                }
                String optstr = (*arg)[1] == '-' ? arg->substr(2) : String(1, (*arg)[1]);
                auto optionIt = _optionsMap.find(optstr);
                if(optionIt == _optionsMap.end()) {
                        promekiErr("Command line option '%s' isn't valid", optstr.cstr());
                        return 9911;
                }
                const Option &option = optionIt->second;
                Option::ArgType argType = option.argType();
                int ret;
                if(argType == Option::ArgNone) {
                        ret = std::get<OptionCallback>(option.callback)();
                } else {
                        if(arg + 1 == args.end()) {
                                promekiErr("Command line option '%s' requires an option", optstr.cstr());
                                return 9912;
                        }
                        const String &optarg = *(arg + 1);
                        if(argType == Option::ArgBool) {
                                Error err;
                                bool val = optarg.toBool(&err);
                                if(err.isError()) {
                                        promekiErr("Command line option '%s' requires a bool option", optstr.cstr());
                                        return 9913;
                                }
                                ret = std::get<OptionBoolCallback>(option.callback)(val);

                        } else if(argType == Option::ArgInt) {
                                Error err;
                                int val = optarg.toInt(&err);
                                if(err.isError()) {
                                        promekiErr("Command line option '%s' requires an int option", optstr.cstr());
                                        return 9914;
                                }
                                ret = std::get<OptionIntCallback>(option.callback)(val);

                        } else if(argType == Option::ArgDouble) {
                                Error err;
                                double val = optarg.toDouble(&err);
                                if(err.isError()) {
                                        promekiErr("Command line option '%s' requires a double option", optstr.cstr());
                                        return 9914;
                                }
                                ret = std::get<OptionDoubleCallback>(option.callback)(val);

                        } else if(argType == Option::ArgString) {
                                ret = std::get<OptionStringCallback>(option.callback)(optarg);

                        } else {
                                promekiErr("libpromeki bug! Command line option '%s' has an unsupported option type", optstr.cstr());
                                return 9915;
                        }
                        ++arg; // Skip the argument
                        if(ret) return ret;
                }
        }
        return 0;
}

String CmdLineParser::optionFullName(bool shortName, const Option &option) {
        String ret;
        if(shortName) {
                if(option.shortName == 0) return String();
                ret += '-';
                ret += option.shortName;
        } else {
                if(option.longName.isEmpty()) return String();
                ret += "--";
                ret += option.longName;
        }
        switch(option.argType()) {
                case Option::ArgBool: ret += " <bool>"; break;
                case Option::ArgInt: ret += " <int>"; break;
                case Option::ArgDouble: ret += " <float>"; break;
                case Option::ArgString: ret += " <string>"; break;
        }
        return ret;
}

StringList CmdLineParser::generateUsage() const {
        StringList list;
        int shortSpace = 0;
        int longSpace = 0;
        // First, walk through all the items and get the largest strings
        // so we'll know how to format everything
        for(const auto &item : _options) {
                String shortName = optionFullName(true, item);
                String longName = optionFullName(false, item);
                if(shortName.size() > shortSpace) shortSpace = shortName.size();
                if(longName.size() > longSpace) longSpace = longName.size();
        }
        int totalSpace = shortSpace + longSpace;
        if(shortSpace && longSpace) totalSpace += 2; // the ", " between long and short items.

        // Now, assemble the list
        for(const auto &item : _options) {
                String name;
                if(item.shortName != 0) {
                        name += optionFullName(true, item);
                } else {
                        if(longSpace) name += String(shortSpace, ' ');
                }
                if(!item.longName.isEmpty()) {
                        if(shortSpace) name += item.shortName ? ", " : "  ";
                        name += optionFullName(false, item);
                }
                name += String(totalSpace - name.size(), ' ');
                String line = name + " " + item.desc;
                list += line;
        }
        return list;
}

PROMEKI_NAMESPACE_END

