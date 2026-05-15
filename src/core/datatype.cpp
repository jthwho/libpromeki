/**
 * @file      datatype.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <promeki/datatype.h>
#include <promeki/hashmap.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(DataType)

namespace {

/**
 * @brief Process-lifetime registry backing @ref DataType.
 *
 * Three parallel indexes keep every form of lookup O(1) or
 * O(log n):
 *  - @c byId      — wire-format tag → record (Variant payloads,
 *                   DataStream frames, persisted IDs).
 *  - @c byCppType — std::type_index → record (C++ template
 *                   dispatch, @c DataType::of<T>).
 *  - @c byName    — string name → record (config-file lookup,
 *                   logging, debug tools).
 *
 * Data records are heap-allocated by @c registerType and never
 * freed.  The pointers handed out via @ref DataType are stable
 * for the lifetime of the process, so DataType handles can be
 * copied and shared across threads without synchronization.
 */
struct Registry {
        Mutex                                              mutex;
        Map<DataType::ID, const DataType::Data *>          byId;
        HashMap<std::type_index, const DataType::Data *>   byCppType;
        Map<String, const DataType::Data *>                byName;
        DataType::ID                                       nextUserId = DataType::UserBegin;
};

/**
 * @brief Construct-on-first-use accessor for the registry.
 *
 * Using a function-local static avoids the static-init-order
 * fiasco — registrations triggered by @c PROMEKI_IMPLEMENT_DATATYPE
 * in other translation units run @em before @c main and need a
 * working registry, regardless of TU initialization order.
 */
Registry &registry() {
        static Registry r;
        return r;
}

} // anonymous namespace

DataType::DataType(ID id) {
        Registry         &r = registry();
        Mutex::Locker     lock(r.mutex);
        auto              it = r.byId.find(id);
        _data = (it != r.byId.end()) ? it->second : nullptr;
        return;
}

const DataType::Ops &DataType::ops() const {
        if (_data == nullptr) {
                promekiErr("DataType::ops() called on invalid handle");
                std::abort();
        }
        return _data->ops;
}

DataType DataType::registerType(const char *name, std::type_index ti, size_t size, size_t align,
                                Ops ops, ID preferredId) {
        if (name == nullptr || *name == '\0') {
                promekiWarn("DataType::registerType: empty name");
                return DataType();
        }
        Registry      &r = registry();
        Mutex::Locker  lock(r.mutex);

        // Reject re-registration of the same C++ type.  This catches
        // duplicate PROMEKI_IMPLEMENT_DATATYPE calls in two TUs and is
        // far easier to diagnose than the alternative (last writer wins,
        // silent on the wire).
        if (auto it = r.byCppType.find(ti); it != r.byCppType.end()) {
                promekiWarn("DataType::registerType: type already registered as '%s' (id 0x%04x); "
                            "attempted to register as '%s'",
                            it->second->name, static_cast<unsigned>(it->second->id), name);
                return DataType(it->second);
        }

        // Resolve the ID — either pin to preferredId or auto-allocate
        // from the user range.  Pinned IDs must not collide with
        // anything already registered; auto-allocation walks forward
        // until it finds a free slot, skipping anything (typically a
        // library builtin) that has already claimed an ID below
        // UserBegin's running cursor.
        ID id = DataType::NoType;
        if (preferredId != DataType::NoType) {
                if (auto it = r.byId.find(preferredId); it != r.byId.end()) {
                        promekiWarn("DataType::registerType: id 0x%04x already in use by '%s'; "
                                    "rejected registration of '%s'",
                                    static_cast<unsigned>(preferredId), it->second->name, name);
                        return DataType();
                }
                id = preferredId;
        } else {
                ID candidate = r.nextUserId;
                while (candidate <= UserEnd && r.byId.find(candidate) != r.byId.end()) {
                        candidate = static_cast<ID>(static_cast<uint16_t>(candidate) + 1);
                }
                if (candidate > UserEnd) {
                        promekiErr("DataType::registerType: exhausted user ID range registering '%s'", name);
                        return DataType();
                }
                id = candidate;
                r.nextUserId = static_cast<ID>(static_cast<uint16_t>(candidate) + 1);
        }

        // Also reject duplicate names within the same range — same
        // diagnostic motivation as the type_index check.
        if (auto it = r.byName.find(String(name)); it != r.byName.end()) {
                promekiWarn("DataType::registerType: name '%s' already in use by id 0x%04x; "
                            "rejecting registration",
                            name, static_cast<unsigned>(it->second->id));
                return DataType();
        }

        Data *d = new Data;
        d->id      = id;
        d->name    = name;
        d->size    = size;
        d->align   = align;
        d->cppType = ti;
        d->ops     = std::move(ops);

        r.byId.insert(id, d);
        r.byCppType.insert(ti, d);
        r.byName.insert(String(name), d);

        promekiDebug("DataType: registered '%s' id 0x%04x size %zu align %zu", name,
                     static_cast<unsigned>(id), size, align);
        return DataType(d);
}

List<DataType::ID> DataType::registeredIds() {
        Registry      &r = registry();
        Mutex::Locker  lock(r.mutex);
        List<ID>       out;
        out.reserve(r.byId.size());
        for (auto it = r.byId.cbegin(); it != r.byId.cend(); ++it) {
                out.pushToBack(it->first);
        }
        return out;
}

DataType DataType::byId(ID id) {
        if (id == DataType::NoType) return DataType();
        Registry      &r = registry();
        Mutex::Locker  lock(r.mutex);
        auto           it = r.byId.find(id);
        return (it != r.byId.end()) ? DataType(it->second) : DataType();
}

DataType DataType::byCppType(std::type_index ti) {
        Registry      &r = registry();
        Mutex::Locker  lock(r.mutex);
        auto           it = r.byCppType.find(ti);
        return (it != r.byCppType.end()) ? DataType(it->second) : DataType();
}

DataType DataType::byName(const char *name) {
        if (name == nullptr || *name == '\0') return DataType();
        Registry      &r = registry();
        Mutex::Locker  lock(r.mutex);
        auto           it = r.byName.find(String(name));
        return (it != r.byName.end()) ? DataType(it->second) : DataType();
}

PROMEKI_NAMESPACE_END
