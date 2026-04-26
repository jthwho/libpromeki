/**
 * @file      stringregistry.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/map.h>
#include <promeki/list.h>
#include <promeki/readwritelock.h>
#include <promeki/fnv1a.h>

PROMEKI_NAMESPACE_BEGIN

namespace detail {

        /**
 * @brief Logs a fatal hash-collision diagnostic and aborts the process.
 *
 * Out-of-line so @ref StringRegistry does not need to pull in the
 * logger header (and its transitive dependencies) from every
 * translation unit that touches the registry.  Shared across every
 * registry instantiation; the caller threads the registry's name
 * through so the diagnostic can identify which registry collided.
 */
        [[noreturn]] void stringRegistryCollisionAbort(const char *registryName, const String &existing,
                                                       const String &incoming, uint64_t hash);

} // namespace detail

/**
 * @brief Thread-safe append-only registry that maps unique strings to integer IDs.
 * @ingroup util
 *
 * @tparam Name A compile-time string literal that identifies this
 *         registry.  Each unique name gets its own static registry
 *         instance, and the name is included in any hash-collision
 *         diagnostic so the responsible registry is obvious from
 *         the failure message.
 *
 * StringRegistry assigns a deterministic uint64_t ID to each string it
 * encounters, derived from a 64-bit FNV-1a hash of the name.  Well-known
 * IDs can therefore be materialized at compile time via
 * @ref Item::literal, making them usable in `constexpr` contexts
 * (switch labels, `static_assert`, template parameters, `constexpr-if`).
 *
 * Two registration paths share the backing map:
 *
 * - The "strict" path used by `declareID()` (well-known IDs declared at
 *   static-init time).  It returns the pure hash; a collision between
 *   two distinct well-known names is fatal and aborts the process
 *   before `main()` runs, since a silent collision would make
 *   @ref Item::literal disagree with the registered value.
 * - The "probe" path used by the runtime `Item(String)` /
 *   `Item(const char *)` constructors.  It linearly probes on
 *   collision so registration never fails for dynamically-named keys
 *   (JSON, user input, etc.).  For the common non-colliding case the
 *   probe terminates at its own hash slot on the first step, so the
 *   runtime cost matches a direct lookup.
 *
 * The nested @ref Item class provides a lightweight handle (a single
 * uint64_t) for working with registered strings.
 *
 * @warning IDs are stable across runs for the same name (the hash is
 *          deterministic), but they are not stable across renames.
 *          Serialization formats continue to use the name, not the ID.
 *
 * @par Example
 * @code
 * using MyRegistry = StringRegistry<"MyRegistry">;
 * using MyItem = MyRegistry::Item;
 *
 * // Runtime-dynamic name: registered for reverse lookup.
 * MyItem a("video.width");
 * MyItem b("video.height");
 * MyItem c("video.width"); // same ID as a
 * assert(a == c);
 * assert(a != b);
 * String name = a.name(); // "video.width"
 *
 * // Well-known constant, usable in constexpr contexts.
 * static constexpr MyItem Width = MyItem::literal("video.width");
 * static_assert(Width.id() == MyItem::literal("video.width").id());
 * @endcode
 */
template <CompiledString Name> class StringRegistry {
        public:
                /** @brief Sentinel value representing an invalid/unregistered ID. */
                static constexpr uint64_t InvalidID = UINT64_MAX;

                /**
                 * @brief Lightweight handle identifying a registered string.
                 *
                 * Item wraps a uint64_t ID and provides access to the underlying
                 * string via the owning registry.  Items are cheap to copy and
                 * can be compared by integer value.
                 */
                class Item {
                        public:
                                /** @brief Constructs an invalid Item with no associated name. */
                                constexpr Item() = default;

                                /**
                                 * @brief Constructs an Item, registering the name if not already known.
                                 *
                                 * Uses the probing registration path, so a hash collision
                                 * between two distinct dynamic names does not fail — the
                                 * colliding name is stored at the next free slot and
                                 * subsequent lookups follow the same probe.
                                 *
                                 * @param name The string to register.
                                 */
                                Item(const String &name) : _id(instance().findOrCreateProbe(name)) {}

                                /**
                                 * @brief Constructs an Item, registering the name if not already known.
                                 * @param name The string to register as a C-string.
                                 */
                                Item(const char *name) : _id(instance().findOrCreateProbe(String(name))) {}

                                /**
                                 * @brief Creates a compile-time Item from a name literal.
                                 *
                                 * Returns the pure FNV-1a hash of @p name without touching
                                 * the runtime registry.  Use this for well-known IDs that
                                 * need to participate in `constexpr` machinery (switch
                                 * labels, `static_assert`, template parameters).  The
                                 * resulting ID will match any Item later constructed from
                                 * the same name via @ref VariantDatabase::declareID, provided no hash
                                 * collision was flagged at static-init time.
                                 *
                                 * Note that `literal()` does not register the name for
                                 * reverse lookup; @ref name will return an empty string
                                 * unless the same name was also registered via a runtime
                                 * path.
                                 */
                                static constexpr Item literal(const char *name) {
                                        Item item;
                                        item._id = fnv1a(name);
                                        return item;
                                }

                                /**
                                 * @brief Looks up an Item by name without registering it.
                                 * @param name The string to look up.
                                 * @return The Item if found, or an invalid Item if not registered.
                                 */
                                static Item find(const String &name) {
                                        Item item;
                                        item._id = instance().findId(name);
                                        return item;
                                }

                                /**
                                 * @brief Constructs an Item from a raw integer ID.
                                 * @param id The raw ID value.
                                 * @return An Item wrapping the given ID.  No validation is performed.
                                 */
                                static constexpr Item fromId(uint64_t id) {
                                        Item item;
                                        item._id = id;
                                        return item;
                                }

                                /**
                                 * @brief Returns the integer ID for this item.
                                 * @return The ID, or InvalidID if this item is invalid.
                                 */
                                constexpr uint64_t id() const { return _id; }

                                /**
                                 * @brief Returns the name associated with this item.
                                 * @return The string, or an empty String if invalid or unregistered.
                                 */
                                String name() const { return instance().name(_id); }

                                /**
                                 * @brief Returns true if this Item refers to a valid registered name.
                                 * @return True if valid, false otherwise.
                                 */
                                constexpr bool isValid() const { return _id != InvalidID; }

                                /** @brief Equality comparison by ID. */
                                constexpr bool operator==(const Item &other) const { return _id == other._id; }

                                /** @brief Inequality comparison by ID. */
                                constexpr bool operator!=(const Item &other) const { return _id != other._id; }

                                /** @brief Less-than comparison by ID (for use in ordered containers). */
                                constexpr bool operator<(const Item &other) const { return _id < other._id; }

                        private:
                                uint64_t _id = InvalidID;
                };

                /** @brief Returns the singleton registry instance for this Tag type. */
                static StringRegistry &instance() {
                        static StringRegistry reg;
                        return reg;
                }

                /**
                 * @brief Looks up the ID for a previously registered string.
                 *
                 * Uses the same linear probe as @ref findOrCreateProbe so that
                 * lookups remain consistent with registrations in the presence
                 * of collisions.
                 *
                 * @param str The string to look up.
                 * @return The ID if found, or InvalidID if the string has not been registered.
                 */
                uint64_t findId(const String &str) const {
                        ReadWriteLock::ReadLocker lock(_lock);
                        uint64_t                  slot = fnv1a(str.cstr());
                        while (true) {
                                auto it = _names.find(slot);
                                if (it == _names.end()) return InvalidID;
                                if (it->second == str) return slot;
                                ++slot;
                        }
                }

                /**
                 * @brief Registers a well-known name strictly at its hash slot.
                 *
                 * This is the registration path used by `declareID()`.  A hash
                 * collision between two distinct well-known names is fatal:
                 * the function logs the offending pair and calls `std::abort()`
                 * so the problem is caught at static-init time rather than
                 * allowing @ref Item::literal to silently disagree with a
                 * probed slot.
                 *
                 * Idempotent: re-registering the same name at the same hash
                 * (the common case for headers included from multiple TUs)
                 * simply returns the existing slot.
                 *
                 * @param str The string to register.
                 * @return The hash ID for @p str.
                 */
                uint64_t findOrCreateStrict(const String &str) {
                        uint64_t h = fnv1a(str.cstr());
                        // Fast path: check under a read lock first.
                        {
                                ReadWriteLock::ReadLocker lock(_lock);
                                auto                      it = _names.find(h);
                                if (it != _names.end()) {
                                        if (it->second == str) return h;
                                        detail::stringRegistryCollisionAbort(Name.bytes(), it->second, str, h);
                                }
                        }
                        ReadWriteLock::WriteLocker lock(_lock);
                        auto                       it = _names.find(h);
                        if (it != _names.end()) {
                                if (it->second == str) return h;
                                detail::stringRegistryCollisionAbort(Name.bytes(), it->second, str, h);
                        }
                        _names.insert(h, str);
                        return h;
                }

                /**
                 * @brief Registers a name using linear probing on collision.
                 *
                 * Used by runtime-dynamic name registration (the `Item(String)`
                 * constructors).  Never fails: on collision the name is placed
                 * at the next free slot, and lookups follow the same probe.
                 *
                 * @param str The string to register.
                 * @return The ID assigned to @p str (the hash for a fresh
                 *         name, or a probed slot when its natural slot was
                 *         occupied).
                 */
                uint64_t findOrCreateProbe(const String &str) {
                        uint64_t start = fnv1a(str.cstr());
                        // Fast path: read-locked probe.
                        {
                                ReadWriteLock::ReadLocker lock(_lock);
                                uint64_t                  slot = start;
                                while (true) {
                                        auto it = _names.find(slot);
                                        if (it == _names.end()) break;
                                        if (it->second == str) return slot;
                                        ++slot;
                                }
                        }
                        // Slow path: acquire write lock and retry so we can
                        // safely insert if still missing.
                        ReadWriteLock::WriteLocker lock(_lock);
                        uint64_t                   slot = start;
                        while (true) {
                                auto it = _names.find(slot);
                                if (it == _names.end()) {
                                        _names.insert(slot, str);
                                        return slot;
                                }
                                if (it->second == str) return slot;
                                ++slot;
                        }
                }

                /**
                 * @brief Returns the string associated with an ID.
                 * @param id The ID to look up.
                 * @return The string if found, or an empty String if the ID is invalid.
                 */
                String name(uint64_t id) const {
                        ReadWriteLock::ReadLocker lock(_lock);
                        auto                      it = _names.find(id);
                        if (it == _names.end()) return String();
                        return it->second;
                }

                /**
                 * @brief Returns the number of registered strings.
                 * @return The number of entries in the registry.
                 */
                size_t count() const {
                        ReadWriteLock::ReadLocker lock(_lock);
                        return _names.size();
                }

                /**
                 * @brief Returns true if the registry contains the given string.
                 * @param str The string to check.
                 * @return True if the string has been registered.
                 */
                bool contains(const String &str) const { return findId(str) != InvalidID; }

        private:
                StringRegistry() = default;

                mutable ReadWriteLock _lock;
                Map<uint64_t, String> _names;
};

PROMEKI_NAMESPACE_END
