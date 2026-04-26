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
 * @brief Manages a list of strings.
 * @ingroup strings
 *
 * Derives from @c List<String> and adds string-specific helpers such as
 * @c join() and @c filter().  Overrides the base @c _promeki_clone() and
 * the @c Ptr alias so @c SharedPtr<StringList> behaves correctly under
 * copy-on-write — without these, copy-on-write would slice the StringList
 * back to a plain @c List<String> at the next @c modify() call.
 *
 * @par Thread Safety
 * Inherits @ref List: distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally
 * synchronized.
 */
class StringList : public List<String> {
        public:
                using List::List;
                using List::operator+=;
                using List::operator=;

                /** @brief Shared pointer type for StringList (overrides the base alias). */
                using Ptr = SharedPtr<StringList>;

                /**
                 * @brief Copy-on-write clone hook for StringList.
                 *
                 * Hides the base @c List<String>::_promeki_clone() so a
                 * @c SharedPtr<StringList>::modify() detaches into a real
                 * @c StringList, preserving the derived API and type identity.
                 */
                StringList *_promeki_clone() const { return new StringList(*this); }

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

};

PROMEKI_NAMESPACE_END

