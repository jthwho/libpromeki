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

                /**
                 * @brief Constructs a StringList from a C-style string array.
                 * @param ct   Number of strings in the array.
                 * @param list Array of C-string pointers.
                 */
                StringList(size_t ct, const char **list) {
                        reserve(ct);
                        for(size_t i = 0; i < ct; ++i) pushToBack(list[i]);
                }

                /**
                 * @brief Joins all strings using the given delimiter.
                 * @param delimiter The separator to place between each string.
                 * @return A single String with all elements joined.
                 */
                String join(const String& delimiter) const {
                        String result;
                        for(auto it = constBegin(); it != constEnd(); ++it) {
                                result += *it;
                                if(it + 1 != constEnd()) result += delimiter;
                        }
                        return result;
                }

                /**
                 * @brief Returns a new StringList containing only strings that match the predicate.
                 * @param func A callable that takes a const String & and returns true to keep the item.
                 * @return A new StringList with matching strings.
                 */
                StringList filter(TestFunc func) const {
                        StringList result;
                        for(const auto &item : *this) {
                                if(func(item)) result.pushToBack(item);
                        }
                        return result;
                }

                /**
                 * @brief Returns the index of the first occurrence of a string, or -1 if not found.
                 * @param val The string to search for.
                 * @return The zero-based index, or -1 if not found.
                 */
                int indexOf(const String &val) const {
                        for(size_t i = 0; i < size(); ++i) {
                                if((*this)[i] == val) return static_cast<int>(i);
                        }
                        return -1;
                }

};

PROMEKI_NAMESPACE_END

