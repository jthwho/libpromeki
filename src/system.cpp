/**
 * @file      system.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <memory>
#include <cxxabi.h>
#include <promeki/system.h>
#include <promeki/string.h>

#include <iostream>
 #include <array>
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
    std::array<char, HOST_NAME_MAX> hostname;
     return gethostname(hostname.data(), hostname.size()) == 0 ? hostname.data() : String();
}

String System::demangleSymbol(const char *val, bool useCache) {
        int status = 0;
        std::unique_ptr<char, void(*)(void*)> result(
                abi::__cxa_demangle(val, nullptr, nullptr, &status),
                std::free
        );
        return (status == 0) ? result.get() : val;
}

PROMEKI_NAMESPACE_END

