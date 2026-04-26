/**
 * @file      regex.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <regex>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Regular expression wrapper around std::regex.
 * @ingroup strings
 *
 * Provides a simplified interface for pattern matching, searching, and
 * extracting matches from strings using standard C++ regular expressions.
 *
 * @par Thread Safety
 * A compiled RegEx is safe to share across threads for read-only
 * operations (@c match, @c search, @c replace) — the underlying
 * @c std::regex is internally const-safe.  Recompiling (assigning
 * a new pattern) requires external synchronization.
 *
 * @par Example
 * @code
 * RegEx re("(\\d+)x(\\d+)");
 * auto match = re.match("1920x1080");
 * if(match.hasMatch()) {
 *     String w = match.captured(1);  // "1920"
 *     String h = match.captured(2);  // "1080"
 * }
 * @endcode
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

                /** @brief Default constructor — produces an invalid empty RegEx. */
                RegEx() : _valid(false) {}

                /**
                 * @brief Constructs a RegEx from a String pattern.
                 *
                 * On invalid pattern the resulting RegEx is left @em invalid
                 * (see @c isValid()) rather than throwing.  Use
                 * @c RegEx::compile() to receive an explicit @ref Error.
                 *
                 * @param pattern The regular expression pattern.
                 * @param flags   Syntax option flags (default: DefaultFlags).
                 */
                RegEx(const String &pattern, Flag flags = DefaultFlags) : p(pattern) {
                        try {
                                d.assign(pattern.cstr(), flags);
                                _valid = true;
                        } catch (const std::regex_error &) {
                                _valid = false;
                        }
                }

                /**
                 * @brief Constructs a RegEx from a C-string pattern.
                 *
                 * On invalid pattern the resulting RegEx is left @em invalid
                 * (see @c isValid()) rather than throwing.  Use
                 * @c RegEx::compile() to receive an explicit @ref Error.
                 *
                 * @param pattern The regular expression pattern.
                 * @param flags   Syntax option flags (default: DefaultFlags).
                 */
                RegEx(const char *pattern, Flag flags = DefaultFlags) : p(pattern) {
                        try {
                                d.assign(pattern, flags);
                                _valid = true;
                        } catch (const std::regex_error &) {
                                _valid = false;
                        }
                }

                /**
                 * @brief Compiles a pattern, returning a Result so callers can
                 *        observe parse failures explicitly.
                 * @param pattern The regular expression pattern.
                 * @param flags   Syntax option flags (default: DefaultFlags).
                 * @return @c Result holding the compiled RegEx on success, or
                 *         @c Error::Invalid on a malformed pattern.
                 */
                static Result<RegEx> compile(const String &pattern, Flag flags = DefaultFlags) {
                        RegEx re;
                        try {
                                re.d.assign(pattern.cstr(), flags);
                                re._valid = true;
                                re.p = pattern;
                        } catch (const std::regex_error &) {
                                return makeError<RegEx>(Error::Invalid);
                        }
                        return makeResult(std::move(re));
                }

                /**
                 * @brief Assigns a new pattern to this regex.
                 *
                 * On invalid pattern the RegEx is left @em invalid; the
                 * previous pattern is replaced regardless.
                 *
                 * @param pattern The new regular expression pattern.
                 * @return Reference to this RegEx.
                 */
                RegEx &operator=(const String &pattern) {
                        p = pattern;
                        try {
                                d.assign(pattern.cstr());
                                _valid = true;
                        } catch (const std::regex_error &) {
                                _valid = false;
                        }
                        return *this;
                }

                /** @brief Returns true if the regex was compiled successfully. */
                bool isValid() const { return _valid; }

                /**
                 * @brief Returns the current pattern string.
                 * @return The regular expression pattern.
                 */
                String pattern() const { return p; }

                /**
                 * @brief Tests whether the entire string matches the pattern.
                 * @param str The string to test.
                 * @return True if the full string matches the regular expression;
                 *         always false when @c isValid() is false.
                 */
                bool match(const String &str) const {
                        if (!_valid) return false;
                        std::smatch m;
                        return std::regex_match(str.str(), m, d);
                }

                /**
                 * @brief Searches for the first occurrence of the pattern within the string.
                 * @param str The string to search in.
                 * @return True if any substring matches the regular expression;
                 *         always false when @c isValid() is false.
                 */
                bool search(const String &str) const {
                        if (!_valid) return false;
                        return std::regex_search(str.str(), d);
                }

                /**
                 * @brief Returns all non-overlapping matches of the pattern in the string.
                 * @param str The string to search in.
                 * @return A StringList containing every matching substring;
                 *         empty when @c isValid() is false.
                 */
                StringList matches(const String &str) const {
                        StringList matches;
                        if (!_valid) return matches;
                        std::smatch        match;
                        const std::string &s = str.str();
                        auto               pos = s.cbegin();
                        while (std::regex_search(pos, s.cend(), match, d)) {
                                matches += match.str();
                                pos = match.suffix().first;
                        }
                        return matches;
                }

        private:
                std::regex d;
                String     p;
                bool       _valid = false;
};

PROMEKI_NAMESPACE_END
