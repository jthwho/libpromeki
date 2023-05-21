/*****************************************************************************
 * stringlist.h
 * May 02, 2023
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

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/** 
 * @brief Manages a list of strings
 */
class StringList : public List<String> {
        public:
                using List::List;
                using List::operator+=;
                using List::operator=;

                StringList(size_t ct, const char **list) {
                        reserve(ct);
                        for(size_t i = 0; i < ct; ++i) pushToBack(list[i]);
                }

                String join(const String& delimiter) const {
                        String result;
                        for(auto it = constBegin(); it != constEnd(); ++it) {
                                result += *it;
                                if(it + 1 != constEnd()) result += delimiter;
                        }
                        return result;
                } 

};

PROMEKI_NAMESPACE_END

