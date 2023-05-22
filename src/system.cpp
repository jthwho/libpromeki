/*****************************************************************************
 * system.cpp
 * May 17, 2023
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

#include <memory>
#include <cxxabi.h>
#include <promeki/system.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

String System::demangleSymbol(const char *val, bool useCache) {
        int status = 0;
        std::unique_ptr<char, void(*)(void*)> result(
                abi::__cxa_demangle(val, nullptr, nullptr, &status),
                std::free
        );
        return (status == 0) ? result.get() : val;
}

PROMEKI_NAMESPACE_END

