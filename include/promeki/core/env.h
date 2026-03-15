/**
 * @file      core/env.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdlib>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Provides access to process environment variables.
 *
 * Static utility class wrapping standard environment variable operations.
 * All methods return promeki::String for consistent integration with the
 * rest of the library.
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
};

PROMEKI_NAMESPACE_END
