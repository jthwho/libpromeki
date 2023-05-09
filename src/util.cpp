/*****************************************************************************
 * util.cpp
 * May 03, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <random>
#include <algorithm>
#include <execinfo.h>
#include <cxxabi.h>
#include <promeki/util.h>
#include <promeki/error.h>
#include <promeki/stringlist.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

static Error fallbackRand(uint8_t *data, size_t bytes) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for(int i = 0; i < bytes; i++) data[i] = dis(gen);
        return Error();
}

Error promekiRand(uint8_t *data, size_t bytes) {
        // FIXME: Move over platform specific RNG stuff
        return fallbackRand(data, bytes);
}

StringList promekiStackTrace(bool demangle) {
        StringList ret;
        const int max_frames = 100;
        void* frames[max_frames];
        int framect = backtrace(frames, max_frames);
        char** symbols = backtrace_symbols(frames, framect);
        String lastFile;
        for(int i = 0; i < framect; i++) {
                if(demangle) {
                        char c, *p = symbols[i];
                        int state = 0;
                        String segment[4];
                        // Parse the backtrace_symbol into the various bits of information
                        while((c = *p++) != 0) {
                                switch(state) {
                                        case 0: // Filename (and possibly a line number)
                                                if(c == '(') {
                                                        state = 1;
                                                        continue;
                                                }
                                                break;
                                        case 1: // Mangled function
                                                if(c == '+') {
                                                        state = 2;
                                                        continue;
                                                }
                                                if(c == ')') {
                                                        state = 3;
                                                        continue;
                                                }
                                                break;
                                        case 2: // Offset
                                                if(c == ')') {
                                                        state = 3;
                                                        continue;
                                                }
                                                break;
                                        default:
                                                /* Do Nothing */
                                                break;
                                }
                                segment[state] += c;
                        }
                        // Attempt to demangle the mangled name.
                        int status;
                        char *demangled = abi::__cxa_demangle(segment[1].cstr(), nullptr, nullptr, &status);
                        if(demangled) {
                                segment[1] = demangled;
                                std::free(demangled);
                        }
                        if(lastFile != segment[0]) {
                                ret += String("In: %1").arg(segment[0]);
                                lastFile = segment[0];
                        }
                        String str = String::sprintf("  %d:", i);
                        str += segment[3];
                        str += " ";
                        str += segment[1];
                        if(!segment[2].isEmpty()) {
                                str += " +";
                                str += segment[2];
                        }
                        ret += str;
                } else {
                        ret += symbols[i];
                }
        }
        free(symbols);
        return ret;
}

PROMEKI_NAMESPACE_END

