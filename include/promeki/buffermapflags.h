/**
 * @file      buffermapflags.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Access mode requested when mapping a Buffer to a domain.
 * @ingroup util
 *
 * Backends use the requested flags to decide whether a write-back is
 * required on @c Buffer::mapRelease.  A read-only mapping of a
 * device-resident buffer can skip the upload phase; a write-only
 * mapping can skip the download phase.
 */
enum class MapFlags {
        None      = 0, ///< No access requested (used by tests / sentinel paths).
        Read      = 1, ///< Read access.
        Write     = 2, ///< Write access.  Mapped data may be modified by the caller.
        ReadWrite = Read | Write ///< Both read and write access.
};

/** @brief Bitwise OR for MapFlags. */
inline MapFlags operator|(MapFlags a, MapFlags b) {
        return static_cast<MapFlags>(static_cast<int>(a) | static_cast<int>(b));
}

/** @brief Bitwise AND for MapFlags. */
inline MapFlags operator&(MapFlags a, MapFlags b) {
        return static_cast<MapFlags>(static_cast<int>(a) & static_cast<int>(b));
}

/**
 * @brief Returns true if @p value has @p flag set.
 *
 * Convenience predicate so call sites do not have to spell out the
 * cast every time.
 */
inline bool hasMapFlag(MapFlags value, MapFlags flag) {
        return (static_cast<int>(value) & static_cast<int>(flag)) != 0;
}

PROMEKI_NAMESPACE_END
