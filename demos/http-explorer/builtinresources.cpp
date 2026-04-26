/**
 * @file      http-explorer/builtinresources.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Static-init registration of the demo's built-in cirf resource set.
 * The generated header @c http_explorer_demo_resources.h is produced
 * by the @c cirf_add_resources() call in this demo's
 * @c CMakeLists.txt; the include path is added to this target only.
 *
 * The mount prefix is @c "demo" so lookups for @c ":/demo/explorer/foo"
 * strip @c "demo" before walking the root, matching the @c "explorer/foo"
 * path declared in resources.json.  A non-empty prefix is required
 * because the library itself already mounts resources at the empty
 * prefix (under @c :/.PROMEKI/...) — multiple roots at the same
 * prefix collide.
 *
 * Touching this object also acts as the link anchor that pulls the
 * generated @c http_explorer_demo_resources.c (and therefore the
 * embedded asset bytes) into the final binary, since the resource
 * archive is statically linked.
 */

#include <promeki/resource.h>
#include "http_explorer_demo_resources.h"

PROMEKI_REGISTER_RESOURCES(http_explorer_demo_resources, "demo")
