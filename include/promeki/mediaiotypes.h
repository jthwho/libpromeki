/**
 * @file      mediaiotypes.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/variantdatabase.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// MediaIO supporting types — used by both the MediaIO public API
// (mediaio.h) and the command hierarchy (mediaiocommand.h).  Lives in its
// own header so the dependency tree stays acyclic:
//
//   mediaiotypes.h  ←  mediaio.h
//        ↑                ↑
//        +-- mediaiocommand.h
//
// neither mediaio.h nor mediaiocommand.h needs to include the other for
// these types — both reach them through this header.
// ============================================================================

/**
 * @brief Seek-mode hint for seekToFrame().
 *
 * Use the @c MediaIO::Seek* constants (e.g. @c MediaIO::SeekExact) at
 * call sites — the free enum exists so commands defined before
 * MediaIO can reference the type.  Most backends interpret these to
 * decide whether to land on an exact frame or the nearest keyframe.
 * Default lets the task pick the most efficient mode (Exact for
 * sample-accurate sources, KeyframeBefore for compressed streams with
 * B-frames, etc.).
 */
enum MediaIOSeekMode {
        MediaIO_SeekDefault = 0,     ///< @brief Backend picks (resolved per task).
        MediaIO_SeekExact,           ///< @brief Land on the exact requested frame.
        MediaIO_SeekNearestKeyframe, ///< @brief Land on the nearest keyframe in either direction.
        MediaIO_SeekKeyframeBefore,  ///< @brief Land on the closest keyframe at or before.
        MediaIO_SeekKeyframeAfter    ///< @brief Land on the closest keyframe at or after.
};

/**
 * @brief Parameter / result container for MediaIO parameterized commands.
 * @ingroup mediaio_user
 *
 * A distinct VariantDatabase type for backend-specific parameterized
 * command payloads.  Has its own StringRegistry so param keys don't
 * collide with config or stats keys.  Has no predefined keys — the
 * key set is entirely backend-defined.  Backends that want to expose
 * named parameters typically define static const IDs on their task
 * class.
 */
using MediaIOParams = VariantDatabase<"MediaIOParams">;

/**
 * @brief Parameterized command ID type.
 * @ingroup mediaio_user
 */
using MediaIOParamsID = MediaIOParams::ID;

PROMEKI_NAMESPACE_END
