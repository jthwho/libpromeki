/**
 * @file      memspace.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <cstring>
#include <promeki/core/memspace.h>
#include <promeki/core/securemem.h>
#include <promeki/core/error.h>
#include <promeki/core/logger.h>
#include <promeki/core/structdatabase.h>
#include <promeki/core/util.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MemSpace)

#define DEFINE_SPACE(item) \
        .id = MemSpace::item, \
        .name = PROMEKI_STRINGIFY(item)

static StructDatabase<MemSpace::ID, MemSpace::Ops> db = {
        {
                DEFINE_SPACE(System),
                .isHostAccessible = [](const MemAllocation &) -> bool { return true; },
                .alloc = [](MemAllocation &a) -> void {
                        size_t allocSize = (a.size + a.align - 1) & ~(a.align - 1);
                        a.ptr = std::aligned_alloc(a.align, allocSize);
                        PROMEKI_ASSERT(a.ptr != nullptr);
                        promekiDebug("%p: system allocate %d (aligned %d), align %d", a.ptr, (int)a.size, (int)allocSize, (int)a.align);
                },
                .release = [](MemAllocation &a) -> void {
                        promekiDebug("%p: system free", a.ptr);
                        std::free(a.ptr);
                },
                .copy = [](const MemAllocation &src, const MemAllocation &dst, size_t bytes) -> bool {
                        switch(dst.ms.id()) {
                                case MemSpace::System:
                                case MemSpace::SystemSecure:
                                        std::memcpy(dst.ptr, src.ptr, bytes);
                                        return true;
                                default:
                                        promekiErr("(%p -> %p, %llu bytes) Copy from System to memspace %d not supported",
                                                src.ptr, dst.ptr, (unsigned long long)bytes, dst.ms.id());
                                        return false;
                        }
                },
                .fill = [](void *ptr, size_t bytes, char value) -> Error {
                        PROMEKI_ASSERT(ptr != nullptr);
                        std::memset(ptr, value, bytes);
                        return Error::Ok;
                }
        },
        {
                DEFINE_SPACE(SystemSecure),
                .isHostAccessible = [](const MemAllocation &) -> bool { return true; },
                .alloc = [](MemAllocation &a) -> void {
                        size_t allocSize = (a.size + a.align - 1) & ~(a.align - 1);
                        a.ptr = std::aligned_alloc(a.align, allocSize);
                        PROMEKI_ASSERT(a.ptr != nullptr);
                        promekiDebug("%p: secure allocate %d (aligned %d), align %d", a.ptr, (int)a.size, (int)allocSize, (int)a.align);
                        Error err = promeki::secureLock(a.ptr, allocSize);
                        if(err.isError()) {
                                promekiDebug("%p: secureLock failed (%s), buffer may be swapped to disk",
                                        a.ptr, err.desc().cstr());
                        }
                },
                .release = [](MemAllocation &a) -> void {
                        promekiDebug("%p: secure free", a.ptr);
                        promeki::secureZero(a.ptr, a.size);
                        Error err = promeki::secureUnlock(a.ptr, a.size);
                        if(err.isError()) {
                                promekiDebug("%p: secureUnlock failed (%s)", a.ptr, err.desc().cstr());
                        }
                        std::free(a.ptr);
                },
                .copy = [](const MemAllocation &src, const MemAllocation &dst, size_t bytes) -> bool {
                        switch(dst.ms.id()) {
                                case MemSpace::System:
                                case MemSpace::SystemSecure:
                                        std::memcpy(dst.ptr, src.ptr, bytes);
                                        return true;
                                default:
                                        promekiErr("(%p -> %p, %llu bytes) Copy from SystemSecure to memspace %d not supported",
                                                src.ptr, dst.ptr, (unsigned long long)bytes, dst.ms.id());
                                        return false;
                        }
                },
                .fill = [](void *ptr, size_t bytes, char value) -> Error {
                        PROMEKI_ASSERT(ptr != nullptr);
                        std::memset(ptr, value, bytes);
                        return Error::Ok;
                }
        }
};


const MemSpace::Ops *MemSpace::lookup(ID id) {
        return &db.get(id);
}

PROMEKI_NAMESPACE_END
