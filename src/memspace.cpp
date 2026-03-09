/**
 * @file      memspace.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <cstring>
#include <promeki/memspace.h>
#include <promeki/logger.h>
#include <promeki/structdatabase.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MemSpace)

#define DEFINE_SPACE(item) \
        .id = MemSpace::item, \
        .name = PROMEKI_STRINGIFY(item)

static StructDatabase<MemSpace::ID, MemSpace::Ops> db = {
        {
                DEFINE_SPACE(System),
                .alloc = [](size_t bytes, size_t align) -> void * {
                        void *ret = std::aligned_alloc(align, bytes);
                        PROMEKI_ASSERT(ret != nullptr);
                        promekiDebug("%p: system allocate %d, align %d", ret, (int)bytes, (int)align);
                        return ret;
                },
                .release = [](void *ptr) -> void {
                        promekiDebug("%p: system free", ptr);
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

