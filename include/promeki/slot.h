/** 
 * @file slot.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the source root folder for license information.
 */

#pragma once

#include <algorithm>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

template <typename... Args>
class Slot {
        public:
                using Function = std::function<void(Args...)>;

                Slot(const Function &func, void *owner = nullptr, const char *prototype = nullptr, int id = -1) : 
                        _owner(owner), _prototype(prototype), _id(id), _function(func) {}

                void *owner() const { return _owner; }

                const char *prototype() const { return _prototype; }

                int id() const { return _id; }
                void setID(int val) { _id = val; }

                template <typename T> struct removeConstAndRef {
                        using type = std::remove_const_t<std::remove_reference_t<T>>;
                };

                template <typename T> using RemoveConstAndRef = typename removeConstAndRef<T>::type;

                static VariantList pack(Args... args) {
                        return { Variant(RemoveConstAndRef<Args>(args))... };
                }

                void exec(Args... args) {
                        _function(args...);
                        return;
                }

                void exec(const VariantList &variantList) {
                        execFromVariantList(std::make_index_sequence<sizeof...(Args)>(), variantList);
                }

        private:
                void            *_owner = nullptr;
                const char      *_prototype = nullptr;
                int             _id = -1;
                Function        _function;

                template <std::size_t... Idx> void execFromVariantList(std::index_sequence<Idx...>, const VariantList &variantList) {
                        exec(unpackVariant<Idx>(variantList[Idx])...);
                }

                template <std::size_t Idx> decltype(auto) unpackVariant(const Variant &variant) {
                        using T = RemoveConstAndRef<std::tuple_element_t<Idx, std::tuple<Args...>>>;
                        return variant.get<T>();
                }
};

PROMEKI_NAMESPACE_END

