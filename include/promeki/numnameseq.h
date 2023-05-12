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

// Describes a NumName sequence
class NumNameSeq {
        public:
                using List = std::vector<NumNameSeq>;

                NumNameSeq() = default;
                NumNameSeq(const NumName &n, size_t o, size_t l) :
                        _name(n), _offset(o), _length(l) {}

                const NumName &name() const { return _name; }
                bool isValid() const { return _length > 0; }
                size_t offset() const { return _offset; }
                size_t length() const { return _length; }
                size_t head() const { return _offset; }
                size_t tail() const { return _offset + _length; }
                
        private:
                NumName         _name;
                size_t          _offset = 0;
                size_t          _length = 0;
};

PROMEKI_NAMESPACE_END

