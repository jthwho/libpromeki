/**
 * @file      backendweight.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Conventional selection weights for codec / IO backends.
 * @ingroup core
 *
 * Used as the @c weight field on every @c BackendRecord (encoders,
 * decoders, and any other multi-backend registry).  Higher weights win
 * when several backends register for the same logical slot and no
 * caller has pinned a specific backend.
 *
 * The bands are intentionally widely spaced so a backend can land
 * between two standard tiers (e.g. @c Vendored + 50 to outrank vendored
 * code without crossing into the system band).
 */
namespace BackendWeight {
        /** @brief Vendored / built-in backend (lowest of the standard bands). */
        constexpr int Vendored = 100;
        /** @brief System / host-installed backend (FFmpeg, OS, …). */
        constexpr int System   = 200;
        /** @brief Application-supplied backend; wins over library defaults. */
        constexpr int User     = 1000;
}

PROMEKI_NAMESPACE_END
