/**
 * @file      core/regex.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <regex>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Regular expression wrapper around std::regex.
 *
 * Provides a simplified interface for pattern matching, searching, and
 * extracting matches from strings using standard C++ regular expressions.
 */
class RegEx {
        public:
                /** @brief Syntax option flags for controlling regex behavior. */
                using Flag = std::regex_constants::syntax_option_type;

                /** @brief Case-insensitive matching. */
                static constexpr Flag IgnoreCase = std::regex::icase;

                /** @brief Treat all sub-expressions as non-marking; no matches are stored. */
                static constexpr Flag NoSubs = std::regex::nosubs;

                /** @brief Optimize the regex for faster matching at the cost of slower construction. */
                static constexpr Flag Optimize = std::regex::optimize;

                /** @brief Make character ranges like "[a-b]" locale sensitive. */
                static constexpr Flag Collate = std::regex::collate;

                /** @brief Use the Modified ECMAScript regular expression grammar. */
                static constexpr Flag ECMAScript = std::regex::ECMAScript;

                /** @brief Use the basic POSIX regular expression grammar. */
                static constexpr Flag Basic = std::regex::basic;

                /** @brief Use the extended POSIX regular expression grammar. */
                static constexpr Flag Extended = std::regex::extended;

                /** @brief Use the awk POSIX regular expression grammar. */
                static constexpr Flag Awk = std::regex::awk;

                /** @brief Use the grep POSIX regular expression grammar. */
                static constexpr Flag Grep = std::regex::grep;

                /** @brief Use the egrep (grep -E) POSIX regular expression grammar. */
                static constexpr Flag EGrep = std::regex::egrep;

                /** @brief Default flags: ECMAScript grammar with optimization enabled. */
                static constexpr Flag DefaultFlags = ECMAScript | Optimize;

                /**
                 * @brief Constructs a RegEx from a String pattern.
                 * @param pattern The regular expression pattern.
                 * @param flags   Syntax option flags (default: DefaultFlags).
                 */
                RegEx(const String &pattern, Flag flags = DefaultFlags) : d(pattern.cstr(), flags), p(pattern) {}

                /**
                 * @brief Constructs a RegEx from a C string pattern.
                 * @param pattern The regular expression pattern.
                 * @param flags   Syntax option flags (default: DefaultFlags).
                 */
                RegEx(const char *pattern, Flag flags = DefaultFlags) : d(pattern, flags), p(pattern) {}

                /**
                 * @brief Assigns a new pattern to this regex.
                 * @param pattern The new regular expression pattern.
                 * @return Reference to this RegEx.
                 */
                RegEx &operator=(const String &pattern) {
                        d = pattern.cstr();
                        p = pattern;
                        return *this;
                }

                /**
                 * @brief Returns the current pattern string.
                 * @return The regular expression pattern.
                 */
                String pattern() const {
                        return p;
                }

                /**
                 * @brief Tests whether the entire string matches the pattern.
                 * @param str The string to test.
                 * @return True if the full string matches the regular expression.
                 */
                bool match(const String &str) const {
                        std::smatch m;
                        return std::regex_match(str.str(), m, d);
                }

                /**
                 * @brief Searches for the first occurrence of the pattern within the string.
                 * @param str The string to search in.
                 * @return True if any substring matches the regular expression.
                 */
                bool search(const String &str) const {
                        return std::regex_search(str.str(), d);
                }

                /**
                 * @brief Returns all non-overlapping matches of the pattern in the string.
                 * @param str The string to search in.
                 * @return A StringList containing every matching substring.
                 */
                StringList matches(const String& str) const {
                        StringList matches;
                        std::smatch match;
                        const std::string &s = str.str();
                        auto pos = s.cbegin();
                        while(std::regex_search(pos, s.cend(), match, d)) {
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

