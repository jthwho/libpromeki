/**
 * @file      numnameseq.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <vector>
#include <promeki/namespace.h>
#include <promeki/numname.h>

PROMEKI_NAMESPACE_BEGIN

class StringList;

// Describes a NumName sequence
class NumNameSeq {
        public:
                using List = std::vector<NumNameSeq>;

                // Parses a list of num name objects from the string list.
                // Any objects that can't be num names will be left in the
                // input list.
                static List parseList(StringList &input);

                NumNameSeq() = default;
                NumNameSeq(const NumName &n, size_t h, size_t t) :
                        _name(n), _head(h), _tail(t) {}

                const NumName &name() const { return _name; }
                bool isValid() const { return _name.isValid(); }
                size_t length() const { return _tail - _head + 1; }
                size_t head() const { return _head; }
                size_t tail() const { return _tail; }
                
        private:
                NumName         _name;
                size_t          _head = 0;
                size_t          _tail = 0;
};

PROMEKI_NAMESPACE_END

