/*****************************************************************************
 * metadata.h
 * April 30, 2023
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

#include <map>
#include <promeki/variant.h>
#include <promeki/util.h>

#define PROMEKI_ENUM_METADATA_ID \
        X(Invalid) \
        X(Timecode) \
        X(Gamma)

namespace promeki {

class Metadata {
        public:
                #define X(name) name,
                enum ID { PROMEKI_ENUM_METADATA_ID };
                #undef X

                static const String &idName(ID id);

                template <typename T> void set(ID id, const T &value) {
                        d[id] = Variant(value);
                        return;
                }
                Variant get(ID id) const { return d.at(id); }
                bool contains(ID id) const { return d.find(id) != d.end(); }
                void remove(ID id) { d.erase(id); return; }
                void clear() { d.clear(); return; }
                size_t size() const { return d.size(); }

                template <typename Func> void forEach(Func &&func) const {
                        for(const auto &[id, value] : d) {
                                func(id, value);
                        }
                        return;
                }

        private:
                std::map<ID, Variant> d;
};

} // namespace promeki
