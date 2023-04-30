/*****************************************************************************
 * memspace.cpp
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

#include <cstdlib>
#include <cstring>
#include <promeki/memspace.h>
#include <promeki/logger.h>
#include <promeki/structdatabase.h>
#include <promeki/util.h>

namespace promeki {

#define DEFINE_SPACE(item) \
        .id = item, \
        .name = PROMEKI_STRINGIFY(item)

static StructDatabase<MemSpace::ID, MemSpace::Ops> db = {
        {
                DEFINE_SPACE(MemSpace::System),
                .alloc = [](size_t bytes, size_t align) -> void * {
                        return std::aligned_alloc(align, bytes);
                },
                .release = [](void *ptr) -> void {
                        std::free(ptr);
                        return;
                },
                .copy = [](MemSpace::ID id, void *to, const void *from, size_t bytes) -> bool {
                        bool ret = false;
                        switch(id) {
                                case MemSpace::System: 
                                        std::memcpy(to, from, bytes); 
                                        ret = true;
                                        break;
                                default:
                                        // Do Nothing
                                        break;
                        }
                        return ret;
                },
                .set = [](void *to, size_t bytes, char value) -> bool {
                        std::memset(to, value, bytes);
                        return true;
                }
        }
};


const MemSpace::Ops *MemSpace::lookup(ID id) {
        return &db.get(id);
}

} // namespace promeki

