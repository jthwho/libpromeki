/**
 * @file      namespace.h
 * @copyright Jason Howard. All rights reserved.
 * @ingroup util
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
/** @def PROMEKI_NAMESPACE_BEGIN
 *  @brief Starts a promeki namespace block.
 */
#define PROMEKI_NAMESPACE_BEGIN namespace promeki {

/** @def PROMEKI_NAMESPACE_END
 *  @brief Ends a promeki namespace block.
 */
#define PROMEKI_NAMESPACE_END }

#endif // PROMEKI_ENABLE_CORE
