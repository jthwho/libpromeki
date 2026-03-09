/**
 * @file      buildinfo.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

typedef struct {
    const char * name;
    const char * version;
    const char * repoident;
    const char * date;
    const char * time;
    const char * hostname;
    const char * type;
    int          betaVersion;
    int          rcVersion;
} BuildInfo;

const BuildInfo * getBuildInfo();

// Writes all the build info to the log output
void logBuildInfo();

PROMEKI_NAMESPACE_END

