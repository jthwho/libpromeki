/**
 * @file      signal.tpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Template definitions for @ref promeki::Signal members that depend on
 * the full @ref promeki::Variant type.  Kept separate from @c signal.h
 * so the latter stays lightweight and does not pull @c variant.h into
 * every TU that touches @ref promeki::ObjectBase.
 *
 * Include this file from any TU that instantiates
 * @c Signal<Args...>::pack (currently only the cross-thread marshalling
 * path in @c objectbase.tpp and the dedicated Signal unit test).
 */

#pragma once

#include <promeki/signal.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

template <typename... Args>
VariantList Signal<Args...>::pack(Args... args) {
        return { Variant(RemoveConstAndRef<Args>(args))... };
}

PROMEKI_NAMESPACE_END
