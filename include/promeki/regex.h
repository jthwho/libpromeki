/*****************************************************************************
 * regex.h
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

#include <regex>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

class RegEx {
        public:
                using Flag = std::regex_constants::syntax_option_type;

                // Character matching should be performed without regard to case.
                static constexpr Flag IgnoreCase = std::regex::icase;
                // When performing matches, all marked sub-expressions (expr) are 
                // treated as non-marking sub-expressions (?:expr). No matches 
                // are stored in the supplied std::regex_match structure and 
                // mark_count() is zero.
                static constexpr Flag NoSubs = std::regex::nosubs;

                // Instructs the regular expression engine to make matching faster, 
                // with the potential cost of making construction slower. For 
                // example, this might mean converting a non-deterministic FSA to 
                // a deterministic FSA.
                static constexpr Flag Optimize = std::regex::optimize;

                // Character ranges of the form "[a-b]" will be locale sensitive.
                static constexpr Flag Collate = std::regex::collate;

                // Use the Modified ECMAScript regular expression grammar.
                static constexpr Flag ECMAScript = std::regex::ECMAScript;

                // Use the basic POSIX regular expression grammar (grammar documentation).
                static constexpr Flag Basic = std::regex::basic;

                // Use the extended POSIX regular expression grammar (grammar documentation).
                static constexpr Flag Extended = std::regex::extended;

                // Use the regular expression grammar used by the awk 
                // utility in POSIX (grammar documentation).
                static constexpr Flag Awk = std::regex::awk;

                // Use the regular expression grammar used by the grep utility 
                // in POSIX. This is effectively the same as the basic option 
                // with the addition of newline '\n' as an alternation separator.
                static constexpr Flag Grep = std::regex::grep;

                // Use the regular expression grammar used by the grep utility, 
                // with the -E option, in POSIX. This is effectively the same 
                // as the extended option with the addition of newline '\n' as 
                // an alternation separator in addition to '|'.
                static constexpr Flag EGrep = std::regex::egrep;

                static constexpr Flag DefaultFlags = ECMAScript | Optimize;

                RegEx(const String &pattern, Flag flags = DefaultFlags) : d(pattern.cstr(), flags), p(pattern) {}
                RegEx(const char *pattern, Flag flags = DefaultFlags) : d(pattern, flags), p(pattern) {}

                RegEx &operator=(const String &pattern) {
                        d = pattern.cstr();
                        p = pattern;
                        return *this;
                }

                String pattern() const {
                        return p;
                }

                bool match(const String &str) const {
                        std::smatch m;
                        return std::regex_match(str.stds(), m, d);
                }

                bool search(const String &str) const {
                        return std::regex_search(str.stds(), d);
                }

                StringList matches(const String& str) const {
                        StringList matches;
                        std::smatch match;
                        auto pos = str.cbegin();
                        while(std::regex_search(pos, str.cend(), match, d)) {
                                matches += match.str();
                                pos = match.suffix().first;
                        }
                        return matches;
                }

        private:
                std::regex      d;
                String          p;
};

PROMEKI_NAMESPACE_END

