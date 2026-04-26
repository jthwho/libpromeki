/**
 * @file      env.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdlib>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/map.h>

PROMEKI_NAMESPACE_BEGIN
class RegEx;
PROMEKI_NAMESPACE_END

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Provides access to process environment variables.
 * @ingroup util
 *
 * Static utility class wrapping standard environment variable operations.
 * All methods return promeki::String for consistent integration with the
 * rest of the library.
 *
 * @par Thread Safety
 * The static read accessors (@c get, @c contains) are safe to call
 * from any thread.  @c set / @c unset modify the process
 * environment via @c setenv / @c unsetenv, which on POSIX is not
 * thread-safe with respect to other threads reading the
 * environment — synchronize externally if mutating the environment
 * after worker threads have started.
 *
 * @par Example
 * @code
 * String home = Env::get("HOME");
 * Env::set("MY_APP_DEBUG", "1");
 * bool exists = Env::contains("PATH");
 * @endcode
 */
class Env {
        public:
                /**
                 * @brief Returns the value of an environment variable.
                 * @param name The variable name.
                 * @return The value as a String, or an empty String if not set.
                 */
                static String get(const char *name) {
                        const char *val = std::getenv(name);
                        return val ? String(val) : String();
                }

                /**
                 * @brief Returns the value of an environment variable with a default.
                 * @param name The variable name.
                 * @param defaultValue Value to return if the variable is not set.
                 * @return The value as a String, or defaultValue if not set.
                 */
                static String get(const char *name, const String &defaultValue) {
                        const char *val = std::getenv(name);
                        return val ? String(val) : defaultValue;
                }

                /**
                 * @brief Returns true if the environment variable is set.
                 * @param name The variable name.
                 * @return true if the variable exists in the environment.
                 */
                static bool isSet(const char *name) {
                        return std::getenv(name) != nullptr;
                }

                /**
                 * @brief Sets an environment variable.
                 * @param name The variable name.
                 * @param value The value to set.
                 * @param overwrite If true, overwrite an existing value.
                 * @return true on success.
                 */
                static bool set(const char *name, const String &value, bool overwrite = true);

                /**
                 * @brief Removes an environment variable.
                 * @param name The variable name.
                 * @return true on success.
                 */
                static bool unset(const char *name);

                /**
                 * @brief Returns all environment variables as a name/value map.
                 * @return A Map of every variable in the process environment.
                 */
                static Map<String, String> list();

                /**
                 * @brief Returns environment variables whose names match a regex.
                 *
                 * Iterates the entire process environment and returns only
                 * those variables whose name is matched by @p filter (using
                 * RegEx::search, so partial matches count).
                 *
                 * @par Example
                 * @code
                 * // All vars starting with PROMEKI_OPT_
                 * auto opts = Env::list(RegEx("^PROMEKI_OPT_"));
                 * @endcode
                 *
                 * @param filter Regex applied to the variable name.
                 * @return A Map of matching variables.
                 */
                static Map<String, String> list(const RegEx &filter);
};

PROMEKI_NAMESPACE_END
