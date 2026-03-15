/**
 * @file      core/cmdlineparser.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <variant>
#include <promeki/core/namespace.h>
#include <promeki/core/stringlist.h>
#include <promeki/core/string.h>
#include <promeki/core/list.h>
#include <promeki/core/map.h>
#include <promeki/core/logger.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Command-line argument parser with callback-driven option handling.
 *
 * Supports short (-x) and long (--name) options with typed arguments.
 * Each option is associated with a callback that is invoked when the option
 * is encountered during parsing. Non-option arguments are collected and
 * accessible after parsing completes.
 */
class CmdLineParser {
        public:
                /** @brief Callback for options that take no argument. */
                using OptionCallback = std::function<int()>;

                /** @brief Callback for options that take a boolean argument. */
                using OptionBoolCallback = std::function<int(bool)>;

                /** @brief Callback for options that take a string argument. */
                using OptionStringCallback = std::function<int(const String &)>;

                /** @brief Callback for options that take an integer argument. */
                using OptionIntCallback = std::function<int(int)>;

                /** @brief Callback for options that take a double argument. */
                using OptionDoubleCallback = std::function<int(double)>;

                /**
                 * @brief Variant type holding any of the supported option callbacks.
                 *
                 * Always add new callback types to the end, as the variant index
                 * is used to infer the argument data type.
                 */
                using OptionCallbackVariant = std::variant<
                        OptionCallback,
                        OptionBoolCallback,
                        OptionStringCallback,
                        OptionIntCallback,
                        OptionDoubleCallback>;

                /**
                 * @brief Describes a single command-line option.
                 *
                 * Each option has an optional single-character short name, an optional
                 * long name, a description for help text, and a typed callback.
                 */
                class Option {
                        public:
                                /** @brief Enumerates the argument types an option can accept. */
                                enum ArgType {
                                        ArgNone,        ///< No argument required.
                                        ArgBool,        ///< Boolean argument.
                                        ArgString,      ///< String argument.
                                        ArgInt,         ///< Integer argument.
                                        ArgDouble,      ///< Double argument.
                                        ArgUnknown      ///< Unknown or unsupported type.
                                };

                                char                    shortName = 0;  ///< Single-character short option name, or 0 if none.
                                String                  longName;       ///< Long option name, or empty if none.
                                String                  desc;           ///< Description shown in usage/help text.
                                OptionCallbackVariant   callback;       ///< Typed callback invoked when this option is parsed.

                                /** @brief Default constructor. */
                                Option() = default;

                                /**
                                 * @brief Constructs an option with all fields.
                                 * @param sn Short name character (0 for none).
                                 * @param ln Long name string.
                                 * @param d  Description for help text.
                                 * @param cb Typed callback variant.
                                 */
                                Option(char sn, const String &ln, const String &d, OptionCallbackVariant cb) :
                                        shortName(sn), longName(ln), desc(d), callback(cb) {}

                                /**
                                 * @brief Returns the argument type inferred from the callback variant.
                                 * @return The ArgType corresponding to the held callback type.
                                 */
                                ArgType argType() const {
                                        if(std::holds_alternative<OptionCallback>(callback)) return ArgNone;
                                        if(std::holds_alternative<OptionBoolCallback>(callback)) return ArgBool;
                                        if(std::holds_alternative<OptionIntCallback>(callback)) return ArgInt;
                                        if(std::holds_alternative<OptionStringCallback>(callback)) return ArgString;
                                        if(std::holds_alternative<OptionDoubleCallback>(callback)) return ArgDouble;
                                        return ArgUnknown;
                                }

                };

                /**
                 * @brief Registers a list of options with the parser.
                 *
                 * Each option is indexed by its short and/or long name for lookup
                 * during parsing.
                 *
                 * @param options An initializer list of Option objects to register.
                 */
                void registerOptions(const std::initializer_list<Option> &options) {
                        for(const auto &opt : options) {
                                if(opt.shortName != 0) {
                                        _optionsMap[String(1, opt.shortName)] = opt;
                                }
                                if(!opt.longName.isEmpty()) {
                                        _optionsMap[opt.longName] = opt;
                                }
                                _options.pushToBack(opt);
                        }
                }

                /** @brief Default constructor. */
                CmdLineParser() = default;

                /** @brief Clears all registered options, mappings, and collected arguments. */
                void clear() {
                        _optionsMap.clear();
                        _options.clear();
                        _args.clear();
                        return;
                }

                /**
                 * @brief Parses command-line arguments from main().
                 *
                 * Wraps parse() for convenience, converting argc/argv and stripping
                 * the program name (argv[0]).
                 *
                 * @param argc Argument count from main().
                 * @param argv Argument vector from main().
                 * @return 0 on success, or a non-zero value returned by an option callback.
                 */
                int parseMain(int argc, char **argv) {
                        StringList args(argc, (const char **)argv);
                        args.remove(0);
                        return parse(args);
                }

                /**
                 * @brief Parses the given argument list against registered options.
                 *
                 * Options are consumed and their callbacks invoked. Remaining non-option
                 * arguments are stored and accessible via arg() and argCount().
                 *
                 * @param args The argument list to parse.
                 * @return 0 on success, or a non-zero value returned by an option callback.
                 */
                int parse(StringList args);

                /**
                 * @brief Returns the number of non-option arguments remaining after parsing.
                 * @return The argument count.
                 */
                int argCount() const { return _args.size(); }

                /**
                 * @brief Returns a non-option argument by index.
                 * @param index The zero-based argument index.
                 * @return A const reference to the argument string.
                 */
                const String &arg(int index) const { return _args[index]; }

                /**
                 * @brief Generates usage/help text for all registered options.
                 * @return A StringList where each entry describes one option.
                 */
                StringList generateUsage() const;

        private:
                Map<String, Option>             _optionsMap;    ///< Lookup map from option name to Option.
                List<Option>                    _options;       ///< Ordered list of all registered options.
                StringList                      _args;          ///< Non-option arguments collected after parsing.

                /**
                 * @brief Builds a display name string for an option.
                 * @param optionName If true, formats as an option name (with dashes).
                 * @param option     The option to format.
                 * @return The formatted option name string.
                 */
                static String optionFullName(bool optionName, const Option &option);
};

PROMEKI_NAMESPACE_END
