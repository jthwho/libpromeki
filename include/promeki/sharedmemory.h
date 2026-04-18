/**
 * @file      sharedmemory.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Cross-platform named shared memory region.
 * @ingroup util
 *
 * Wraps a named region of shared memory that can be shared between
 * processes.  One side creates the region (the @em owner) and the
 * other side(s) open it by name.  When the owner closes or destroys
 * its @ref SharedMemory, the region is unlinked from the system's
 * name table; existing mappings remain valid until each holder
 * unmaps it.
 *
 * On POSIX the region is backed by @c shm_open (typically surfaced
 * under @c /dev/shm on Linux).  On other platforms support may be
 * unavailable — see @ref isSupported.
 *
 * @par Naming
 * POSIX shared-memory names must begin with @c "/" and must not
 * contain any further path separators.  A leading slash is added
 * automatically if missing; any embedded slash is rejected as
 * @c Error::Invalid.  Maximum length is platform-defined (@c SHM_NAME_MAX
 * on BSDs; usually @c NAME_MAX on Linux).
 *
 * @par Cross-user access
 * The owner passes a file mode to @ref create (default @c 0600 —
 * owner only).  For cross-user access, use wider permissions such as
 * @c 0660 and set @c groupName to a group both parties belong to.
 * If @c groupName is non-empty, the underlying object is @c chown'd
 * to that group immediately after creation; failures propagate.
 *
 * @par Cleanup
 * If the owning process crashes, the region name is orphaned and
 * remains in the system's name table until it is reused, explicitly
 * unlinked (for example by an admin's @c "rm /dev/shm/foo"), or the
 * system reboots.  Holders that map the orphaned region continue to
 * see a valid mapping until they unmap it.
 *
 * @par Example
 * @code
 * // Producer side.
 * SharedMemory shm;
 * Error err = shm.create("/my-region", 4096);
 * void *p = shm.data();
 * // ... write into p ...
 *
 * // Consumer side (potentially in another process).
 * SharedMemory mapped;
 * err = mapped.open("/my-region", SharedMemory::ReadOnly);
 * const void *q = mapped.data();
 * // ... read from q ...
 * @endcode
 */
class SharedMemory {
        public:
                /** @brief Default file mode for newly created regions (owner rw). */
                static constexpr uint32_t DefaultPermissions = 0600;

                /** @brief Access mode for @ref open. */
                enum Access {
                        ReadOnly,       ///< @brief Map the region read-only.
                        ReadWrite       ///< @brief Map the region read-write.
                };

                /**
                 * @brief Returns true if shared memory is supported on the current platform.
                 * @return True when create/open can succeed, false otherwise.
                 */
                static bool isSupported();

                /**
                 * @brief Removes a shared memory name from the system table.
                 *
                 * Intended for higher-level code that has independently
                 * determined a prior owner is no longer live and needs
                 * to recycle the name before a fresh @ref create.
                 * Equivalent to @c shm_unlink on POSIX.  Returns
                 * @c Error::Ok when the name is gone afterwards
                 * (including when it never existed) and an error only
                 * if the unlink itself failed for a non-ENOENT reason.
                 *
                 * Existing mappings in this or other processes remain
                 * valid until each holder unmaps them — only the name
                 * is removed.
                 *
                 * @param name  Region name (leading @c "/" added if missing).
                 * @return @c Error::Ok on success, or an error.
                 */
                static Error unlink(const String &name);

                /** @brief Constructs an empty, invalid @ref SharedMemory. */
                SharedMemory();

                /** @brief Destructor — unmaps and, if owner, unlinks. */
                ~SharedMemory();

                SharedMemory(const SharedMemory &) = delete;
                SharedMemory &operator=(const SharedMemory &) = delete;

                /** @brief Move constructor — transfers ownership of the region. */
                SharedMemory(SharedMemory &&other) noexcept;

                /** @brief Move assignment — closes @c *this then transfers ownership. */
                SharedMemory &operator=(SharedMemory &&other) noexcept;

                /**
                 * @brief Creates a new shared memory region and maps it read-write.
                 *
                 * Fails with @c Error::Exists if a region with the same
                 * name is already present in the system name table.
                 * Fails with @c Error::Invalid if @p name contains an
                 * embedded slash or @p size is zero.
                 *
                 * @param name       Region name (leading @c "/" added if missing).
                 * @param size       Region size in bytes (must be > 0).
                 * @param mode       POSIX file mode for the object.
                 * @param groupName  Optional group to @c chown the object to (empty = skip).
                 * @return @c Error::Ok on success, or an error.
                 */
                Error create(const String &name, size_t size,
                             uint32_t mode = DefaultPermissions,
                             const String &groupName = String());

                /**
                 * @brief Opens an existing shared memory region by name.
                 *
                 * Fails with @c Error::NotExist if the region is not present.
                 * Fails with @c Error::PermissionDenied if the caller cannot
                 * open the object with the requested access.
                 *
                 * @param name    Region name (leading @c "/" added if missing).
                 * @param access  Mapping access mode (default @c ReadOnly).
                 * @return @c Error::Ok on success, or an error.
                 */
                Error open(const String &name, Access access = ReadOnly);

                /**
                 * @brief Unmaps the region and, if owner, unlinks the name.
                 *
                 * Safe to call on an invalid or already-closed object.
                 */
                void close();

                /** @brief Returns true if the region is mapped. */
                bool isValid() const { return _data != nullptr; }

                /** @brief Returns true if this instance owns the region. */
                bool isOwner() const { return _owner; }

                /** @brief Returns the mapped address (null when invalid). */
                void *data() { return _data; }

                /** @brief Returns the mapped address (null when invalid). */
                const void *data() const { return _data; }

                /** @brief Returns the region size in bytes. */
                size_t size() const { return _size; }

                /** @brief Returns the canonical (slash-prefixed) region name. */
                const String &name() const { return _name; }

                /** @brief Returns the current mapping access mode. */
                Access access() const { return _access; }

        private:
                String          _name;
                void            *_data   = nullptr;
                size_t          _size    = 0;
                intptr_t        _handle  = -1;  // fd (POSIX) or HANDLE (Windows)
                bool            _owner   = false;
                Access          _access  = ReadOnly;
};

PROMEKI_NAMESPACE_END
