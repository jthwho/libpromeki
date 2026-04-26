/**
 * @file      variant_fwd.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Variadic template that implements the Variant machinery.
 *
 * Only the declaration is exposed here so headers can refer to
 * @ref VariantImpl by pointer or reference without pulling in the
 * 40+ member-type headers required by the full definition in
 * @c variant.h.
 */
template <typename...> class VariantImpl;

/**
 * @brief Tagged union able to hold any of the supported Variant alternatives.
 *
 * Full definition in @c variant.h.  Declared here as an incomplete
 * type so widely-included headers can reference it without paying
 * the full instantiation cost.  Anyone who needs to construct,
 * copy, or invoke methods on a @ref Variant must include
 * @c promeki/variant.h.
 */
class Variant;

/**
 * @brief List of @ref Variant values used for type-erased argument marshalling.
 *
 * Full definition in @c variant.h.  Forward-declared here for the
 * same reasons as @ref Variant.
 */
class VariantList;

/**
 * @brief String-keyed map of @ref Variant values.
 *
 * Full definition in @c variant.h.  Forward-declared here for the
 * same reasons as @ref Variant — the full @c Map<String, Variant>
 * instantiation is heavy and shouldn't be paid for by every header
 * that simply needs to refer to the type.
 */
class VariantMap;

PROMEKI_NAMESPACE_END
