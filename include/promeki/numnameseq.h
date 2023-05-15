/*****************************************************************************
 * numnameseq.h
 * May 09, 2023
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

