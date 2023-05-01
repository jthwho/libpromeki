/*****************************************************************************
 * structdatabase.h
 * April 29, 2023
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

namespace promeki {

// Provides a structure database that can be initialized from a initialization list.  This allows
// you to initialize a structure database on startup.  The struct must contain an item named 'id'.
// This id will be used as the key in the map.  Also, the ID 0 must exist as this will be returned
// in the case an id is not found.
template<typename KeyType, typename StructType>
class StructDatabase {
        public:
                typedef std::map<KeyType, StructType> Map;

                StructDatabase() = default;

                StructDatabase(const std::initializer_list<StructType> &&list) {
                        load(std::move(list));
                }

                void add(const StructType &&val) {
                        map[val.id] = std::move(val);
                        return;
                }

                const StructType &get(const KeyType &id) const {
                        auto it = map.find(id);
                        if(it != map.end()) return it->second;
                        it = map.find(static_cast<KeyType>(0));
                        if(it != map.end()) return it->second;
                        throw std::runtime_error("StructDatabase is missing an invalid entry (id 0)");
                }

                void load(const std::initializer_list<StructType> &&list) {
                        for(auto &&item : list) add(std::move(item));
                        return;
                }

                const Map &database() const { return map; }
                Map &database() { return map; }

        private:
                Map map;
};

} // namespace promeki
