/**
 * @file      buildinfo.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Holds compile-time build information for the library.
 * @ingroup util
 */
typedef struct {
    const char * name;        ///< Project name.
    const char * version;     ///< Version string (e.g. "1.2.3").
    const char * repoident;   ///< Repository identifier (e.g. git commit hash).
    const char * date;        ///< Build date string (__DATE__).
    const char * time;        ///< Build time string (__TIME__).
    const char * hostname;    ///< Hostname of the build machine.
    const char * type;        ///< Build type (e.g. "Release", "Debug").
    int          betaVersion; ///< Beta version number, or 0 if not a beta build.
    int          rcVersion;   ///< Release candidate number, or 0 if not an RC build.
} BuildInfo;

/** @brief Returns a pointer to the global BuildInfo structure. */
const BuildInfo * getBuildInfo();

/** @brief Writes all build information fields to the log output. */
void logBuildInfo();

PROMEKI_NAMESPACE_END

