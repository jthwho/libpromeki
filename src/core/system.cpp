/**
 * @file      system.cpp
 * @copyright Jason Howard. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <cxxabi.h>
#include <promeki/array.h>
#include <promeki/system.h>
#include <promeki/string.h>

#include <cstring>
#include <cerrno>
#ifdef PROMEKI_PLATFORM_WINDOWS
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#endif


PROMEKI_NAMESPACE_BEGIN

String System::hostname() {
        Array<char, HOST_NAME_MAX> hostname;
        return gethostname(hostname.data(), hostname.size()) == 0 ? hostname.data() : String();
}

String System::demangleSymbol(const char *val, bool useCache) {
        int    status = 0;
        char  *demangled = abi::__cxa_demangle(val, nullptr, nullptr, &status);
        String out = (status == 0 && demangled != nullptr) ? String(demangled) : String(val);
        std::free(demangled);
        return out;
}

PROMEKI_NAMESPACE_END
