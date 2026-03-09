/**
 * @file      stringlist.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

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

