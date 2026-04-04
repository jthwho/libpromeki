/**
 * @file      env.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/env.h>
#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <cstdlib>
#elif defined(PROMEKI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

PROMEKI_NAMESPACE_BEGIN

bool Env::set(const char *name, const String &value, bool overwrite) {
#if defined(PROMEKI_PLATFORM_POSIX)
        return setenv(name, value.cstr(), overwrite ? 1 : 0) == 0;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        if(!overwrite && std::getenv(name) != nullptr) return true;
        return SetEnvironmentVariableA(name, value.cstr()) != 0;
#else
        return false;
#endif
}

bool Env::unset(const char *name) {
#if defined(PROMEKI_PLATFORM_POSIX)
        return unsetenv(name) == 0;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        return SetEnvironmentVariableA(name, nullptr) != 0;
#else
        return false;
#endif
}

PROMEKI_NAMESPACE_END
