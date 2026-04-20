/**
 * @file      slot.tpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Template definitions for @ref promeki::Slot members that depend on
 * the full @ref promeki::Variant type.  Kept separate from @c slot.h
 * so the latter stays lightweight and does not pull @c variant.h into
 * every TU that touches @ref promeki::ObjectBase.
 *
 * Include this file from any TU that instantiates
 * @c Slot<Args...>::pack or @c Slot<Args...>::exec(const VariantList &).
 */

#pragma once

#include <promeki/slot.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

template <typename... Args>
VariantList Slot<Args...>::pack(Args... args) {
        return { Variant(RemoveConstAndRef<Args>(args))... };
}

template <typename... Args>
void Slot<Args...>::exec(const VariantList &variantList) {
        execFromVariantList(std::make_index_sequence<sizeof...(Args)>(), variantList);
}

template <typename... Args>
template <std::size_t... Idx>
void Slot<Args...>::execFromVariantList(std::index_sequence<Idx...>, const VariantList &variantList) {
        exec(unpackVariant<Idx>(variantList[Idx])...);
}

template <typename... Args>
template <std::size_t Idx>
decltype(auto) Slot<Args...>::unpackVariant(const Variant &variant) {
        using T = RemoveConstAndRef<std::tuple_element_t<Idx, std::tuple<Args...>>>;
        return variant.template get<T>();
}

PROMEKI_NAMESPACE_END
