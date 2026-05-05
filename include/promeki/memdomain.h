/**
 * @file      memdomain.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Coarse classification of memory address spaces.
 * @ingroup util
 *
 * MemDomain identifies the @em class of a memory address space —
 * Host, CudaDevice, FpgaDevice, etc. — independently of the specific
 * @ref MemSpace within that class.  Multiple MemSpaces can belong to
 * the same MemDomain (e.g. SystemHost, SystemSecure, and
 * CudaPinnedHost all live in MemDomain::Host), and a Buffer can be
 * mapped for access from any registered domain via
 * @c Buffer::mapAcquire.
 *
 * @par TypeRegistry
 * Mirrors the @ref MemSpace registration pattern: well-known IDs are
 * pre-populated; user code can register additional domains with
 * @ref registerType followed by @ref registerData.  The wrapper is a
 * lightweight handle to an immutable @ref Data record looked up by
 * ID.
 *
 * @par Thread Safety
 * Fully thread-safe.  The registry is built once at startup and read
 * freely thereafter; the wrapper is a value type carrying a single
 * pointer.
 */
class MemDomain {
        public:
                /**
                 * @brief Identifies a memory domain.
                 *
                 * Well-known domains have named enumerators.
                 * User-defined domains obtain IDs from
                 * @ref registerType, which begins at @ref UserDefined.
                 */
                enum ID {
                        Host = 0,        ///< Host CPU memory (system RAM, including page-locked / secure variants).
                        CudaDevice = 1,  ///< CUDA device memory (GPU VRAM).
                        FpgaDevice = 2,  ///< FPGA on-board memory or buffer-index space.
                        Default = Host,  ///< Alias for Host.
                        UserDefined = 1024 ///< First ID available for user-registered domains.
                };

                /** @brief List of MemDomain IDs. */
                using IDList = ::promeki::List<ID>;

                /**
                 * @brief Immutable per-domain record stored in the registry.
                 *
                 * Plain-data: an ID and a human-readable name.
                 * Behavior lives on the @ref BufferImpl subclasses, not
                 * on the domain.
                 */
                struct Data {
                                ID     id;   ///< The domain identifier.
                                String name; ///< Human-readable name of the domain.
                };

                /**
                 * @brief Allocates and returns a new user-domain ID.
                 * @return A unique ID value at or above @ref UserDefined.
                 */
                static ID registerType();

                /**
                 * @brief Registers a Data record in the domain registry.
                 *
                 * After this call, constructing a MemDomain from
                 * @c data.id will resolve to the registered record.
                 *
                 * @param data Populated record with @c id set to a value from @ref registerType.
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns a list of every registered domain ID.
                 * @return A list including built-in and user-registered IDs.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Constructs a MemDomain wrapper for the given ID.
                 * @param id The domain ID (default: @ref Default).
                 */
                inline MemDomain(ID id = Default);

                /** @brief Returns the human-readable domain name. */
                const String &name() const { return d->name; }

                /** @brief Returns the domain ID. */
                ID id() const { return d->id; }

                /** @brief Equality (identity-based — every wrapper for a given ID shares one Data record). */
                bool operator==(const MemDomain &o) const { return d == o.d; }

                /** @brief Inequality. */
                bool operator!=(const MemDomain &o) const { return d != o.d; }

        private:
                const Data        *d = nullptr;
                static const Data *lookup(ID id);
};

inline MemDomain::MemDomain(ID id) : d(lookup(id)) {}

PROMEKI_NAMESPACE_END
