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

namespace promeki {

int CmdLineParser::parse(const StringList &args) {
        _args.clear(); // Clear the args.
        bool optionParsingEnabled = true;
        for(size_t i = 0; i < args.size(); ++i) {
                const String &arg = args[i];
                if(!optionParsingEnabled) {
                        _args += arg;
                        continue;
                }
                if(arg == "--") {
                        optionParsingEnabled = false;
                        continue;
                }
                if(!arg.startsWith('-') || arg.size() == 1) {
                        _args += arg;
                        continue;
                }
                String optstr = arg[1] == '-' ? arg.substr(2) : String(1, arg[1]);
                auto optionIt = _options.find(optstr);
                if(optionIt == _options.end()) {
                        promekiErr("Command line option '%s' isn't valid", optstr.cstr());
                        return 9911;
                }
                const Option &option = optionIt->second;
                Option::ArgType argType = option.argType();
                int ret;
                if(argType == Option::ArgNone) {
                        ret = std::get<OptionCallback>(option.callback)();
                } else {
                        if(i == args.size() - 1) {
                                promekiErr("Command line option '%s' requires an option", optstr.cstr());
                                return 9912;
                        }
                        const String &optarg = args[i + 1];
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
                        i++; // Skip the argument
                        if(ret) return ret;
                }
        }
        return 0;
}

} // namespace promeki

