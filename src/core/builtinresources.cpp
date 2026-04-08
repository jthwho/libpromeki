/**
 * @file      builtinresources.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Static-init registration of libpromeki's built-in cirf resource set.
 * The generated header @c promeki_resources.h is produced by the
 * @c cirf_add_resources(promeki_resources ...) call in
 * @c CMakeLists.txt and lives in the build directory; the
 * corresponding include path is added to the @c promeki target only
 * when @c PROMEKI_ENABLE_CIRF is on.
 *
 * The mount prefix is empty because the cirf JSON config already
 * stores the resources under the virtual path @c .PROMEKI/..., so
 * lookups for @c ":/.PROMEKI/foo" walk the root directly with
 * @c ".PROMEKI/foo" as the search key.
 *
 * Touching this object also acts as the link anchor that pulls the
 * generated @c promeki_resources.c (and therefore the embedded data
 * arrays) into the final shared library when @c promeki_resources is
 * statically linked into @c libpromeki.so.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_CIRF

#include <promeki/resource.h>
#include "promeki_resources.h"

PROMEKI_REGISTER_RESOURCES(promeki_resources, "")

#endif
