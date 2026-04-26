/**
 * @file      env.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/env.h>
#include <promeki/regex.h>
#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <cstdlib>
extern "C" char **environ;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
#include <windows.h>
#endif

PROMEKI_NAMESPACE_BEGIN

bool Env::set(const char *name, const String &value, bool overwrite) {
#if defined(PROMEKI_PLATFORM_POSIX)
        return setenv(name, value.cstr(), overwrite ? 1 : 0) == 0;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        if (!overwrite && std::getenv(name) != nullptr) return true;
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

Map<String, String> Env::list() {
        Map<String, String> result;
#if defined(PROMEKI_PLATFORM_POSIX)
        for (char **ep = environ; ep != nullptr && *ep != nullptr; ++ep) {
                const char *entry = *ep;
                const char *eq = entry;
                while (*eq != '\0' && *eq != '=') ++eq;
                if (*eq == '=') {
                        result.insert(String(entry, static_cast<size_t>(eq - entry)), String(eq + 1));
                }
        }
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        char *block = GetEnvironmentStringsA();
        if (block != nullptr) {
                for (const char *p = block; *p != '\0';) {
                        const char *entry = p;
                        const char *eq = entry;
                        while (*eq != '\0' && *eq != '=') ++eq;
                        if (*eq == '=') {
                                result.insert(String(entry, static_cast<size_t>(eq - entry)), String(eq + 1));
                        }
                        while (*p != '\0') ++p;
                        ++p;
                }
                FreeEnvironmentStringsA(block);
        }
#endif
        return result;
}

Map<String, String> Env::list(const RegEx &filter) {
        Map<String, String> all = list();
        Map<String, String> result;
        all.forEach([&](const String &key, const String &value) {
                if (filter.search(key)) result.insert(key, value);
        });
        return result;
}

PROMEKI_NAMESPACE_END
