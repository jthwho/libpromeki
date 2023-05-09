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

PROMEKI_NAMESPACE_BEGIN

#define DEFINE_SPACE(item) \
        .id = MemSpace::item, \
        .name = PROMEKI_STRINGIFY(item)

static StructDatabase<MemSpace::ID, MemSpace::Ops> db = {
        {
                DEFINE_SPACE(System),
                .alloc = [](size_t bytes, size_t align) -> void * {
                        void *ret = std::aligned_alloc(align, bytes);
                        PROMEKI_ASSERT(ret != nullptr);
                        return ret;
                },
                .release = [](void *ptr) -> void {
                        std::free(ptr);
                        return;
                },
                .copy = [](MemSpace::ID id, void *to, const void *from, size_t bytes) -> bool {
                        PROMEKI_ASSERT(from != nullptr);
                        PROMEKI_ASSERT(to != nullptr);
                        bool ret = false;
                        switch(id) {
                                case MemSpace::System: 
                                        std::memcpy(to, from, bytes); 
                                        ret = true;
                                        break;
                                default:
                                        promekiErr("(%p -> %p, %llu bytes) Copy from memspace %d to system not allowed",
                                                from, to, (unsigned long long)bytes, id);
                                        ret = false;
                                        break;
                        }
                        return ret;
                },
                .set = [](void *to, size_t bytes, char value) -> bool {
                        PROMEKI_ASSERT(to != nullptr);
                        std::memset(to, value, bytes);
                        return true;
                }
        }
};


const MemSpace::Ops *MemSpace::lookup(ID id) {
        return &db.get(id);
}

PROMEKI_NAMESPACE_END

