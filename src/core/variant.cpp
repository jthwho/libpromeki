/**
 * @file      variant.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Hosts the explicit template instantiations matching the `extern template`
 * declarations at the bottom of variant.h.  Centralizing them here keeps
 * consumer TUs from re-instantiating the ~250-line get<T>() std::visit
 * lambda and the ~35²-branch operator== for every translation unit that
 * touches Variant.  The bodies live in variant.tpp, which is deliberately
 * only included here so consumer TUs never parse them.
 */

#include <promeki/variant.tpp>

PROMEKI_NAMESPACE_BEGIN

#define X(name, type) type,
template class VariantImpl< PROMEKI_VARIANT_TYPES detail::VariantEnd >;
#undef X

#define X(name, type) \
        template type Variant::Base::get<type>(Error *err) const;
PROMEKI_VARIANT_TYPES
#undef X

PROMEKI_NAMESPACE_END

