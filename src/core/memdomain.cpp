/**
 * @file      memdomain.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/memdomain.h>
#include <promeki/atomic.h>
#include <promeki/map.h>

PROMEKI_NAMESPACE_BEGIN

static Atomic<int> _nextDomain{MemDomain::UserDefined};

MemDomain::ID MemDomain::registerType() {
        return static_cast<ID>(_nextDomain.fetchAndAdd(1));
}

namespace {

struct MemDomainRegistry {
                Map<MemDomain::ID, MemDomain::Data> entries;

                MemDomainRegistry() {
                        // Built-in domains.  The order here is the
                        // order they appear in registeredIDs();
                        // callers should not depend on insertion order
                        // for correctness (Map keeps keys sorted) but
                        // tests over registered IDs benefit from a
                        // stable seed.
                        entries[MemDomain::Host] = MemDomain::Data{MemDomain::Host, String("Host")};
                        entries[MemDomain::CudaDevice] = MemDomain::Data{MemDomain::CudaDevice, String("CudaDevice")};
                        entries[MemDomain::FpgaDevice] = MemDomain::Data{MemDomain::FpgaDevice, String("FpgaDevice")};
                }
};

MemDomainRegistry &registry() {
        static MemDomainRegistry reg;
        return reg;
}

} // namespace

const MemDomain::Data *MemDomain::lookup(ID id) {
        auto &reg = registry();
        auto  it = reg.entries.find(id);
        if (it != reg.entries.end()) return &it->second;
        return &reg.entries[Host];
}

void MemDomain::registerData(Data &&data) {
        auto &reg = registry();
        reg.entries[data.id] = std::move(data);
}

MemDomain::IDList MemDomain::registeredIDs() {
        auto  &reg = registry();
        IDList ret;
        for (const auto &[id, data] : reg.entries) {
                ret.pushToBack(id);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
