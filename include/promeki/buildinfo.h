/**
 * @file      buildinfo.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Holds compile-time build information for the library.
 * @ingroup util
 *
 * @par Thread Safety
 * Trivially thread-safe.  The BuildInfo struct is populated at
 * compile time and is immutable; the free helpers are pure
 * formatters that read only that immutable state.  All accessors
 * are safe to call from any thread.
 */
typedef struct {
                const char *name;        ///< Project name.
                const char *version;     ///< Version string (e.g. "1.2.3").
                const char *repoident;   ///< Repository identifier (e.g. git commit hash).
                const char *date;        ///< Build date string (__DATE__).
                const char *time;        ///< Build time string (__TIME__).
                const char *hostname;    ///< Hostname of the build machine.
                const char *type;        ///< Build type (e.g. "Release", "Debug").
                int         betaVersion; ///< Beta version number, or 0 if not a beta build.
                int         rcVersion;   ///< Release candidate number, or 0 if not an RC build.
} BuildInfo;

/** @brief Returns a pointer to the global BuildInfo structure. */
const BuildInfo *getBuildInfo();

/** @brief Writes all build information fields to the log output. */
void logBuildInfo();

/**
 * @brief Returns the build identity as a human-readable string.
 *
 * Includes the project name, version, repo ident, build type,
 * build date/time, and build hostname.
 */
String buildInfoString();

/**
 * @brief Returns the platform and compiler as a human-readable string.
 *
 * Example: @c "Platform: Linux | Compiler: GCC 13.2.0 | C++: 202002"
 */
String buildPlatformString();

/**
 * @brief Returns enabled library features as a space-separated string.
 *
 * Example: @c "Features: NETWORK PROAV MUSIC PNG JPEG AUDIO CSC"
 */
String buildFeatureString();

/**
 * @brief Returns hardware and runtime info for the current process.
 *
 * Includes the CPU count and process ID.
 */
String runtimeInfoString();

/**
 * @brief Returns whether promekiDebug() logging is compiled in.
 */
String debugStatusString();

/**
 * @brief Returns all build, platform, feature, runtime, and debug
 *        status strings as a list of lines.
 *
 * Convenience for logging or display — each entry is one logical
 * line suitable for printing or enqueuing to the Logger.
 */
StringList buildInfoStrings();

PROMEKI_NAMESPACE_END
