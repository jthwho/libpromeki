/**
 * @file      stringregistry.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <promeki/stringregistry.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace detail {

        void stringRegistryCollisionAbort(const char *registryName, const String &existing, const String &incoming,
                                          uint64_t hash) {
                promekiErr("FATAL: StringRegistry<%s> hash collision: "
                           "\"%s\" and \"%s\" both hash to 0x%016llx. "
                           "Rename one of these identifiers.",
                           registryName, existing.cstr(), incoming.cstr(), static_cast<unsigned long long>(hash));
                promekiLogSync();
                std::abort();
        }

} // namespace detail

PROMEKI_NAMESPACE_END
