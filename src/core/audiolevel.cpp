/**
 * @file      audiolevel.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <promeki/audiolevel.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

String AudioLevel::toString() const {
        if (isSilence()) return String("-inf dBFS");
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f dBFS", _dbfs);
        return String(buf);
}

PROMEKI_NAMESPACE_END
