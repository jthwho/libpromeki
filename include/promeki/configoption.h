/**
 * @file      configoption.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/variantdatabase.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Configuration database mapping named options to Variant values.
 * @ingroup util
 *
 * Config is a VariantDatabase instantiation with its own ID namespace.
 * Use Config::ID to create or look up configuration option identifiers.
 *
 * @par Example
 * @code
 * Config cfg;
 * Config::ID width("video.width");
 * Config::ID height("video.height");
 *
 * cfg.set(width, 1920);
 * cfg.set(height, 1080);
 *
 * int w = cfg.get(width).get<int32_t>(); // 1920
 * @endcode
 */
using Config = VariantDatabase<"Config">;

PROMEKI_NAMESPACE_END
