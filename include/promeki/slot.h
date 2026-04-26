/** 
 * @file      slot.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <tuple>
#include <promeki/namespace.h>
#include <promeki/variant_fwd.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Type-safe callback slot that wraps a callable with optional ownership tracking.
 * @ingroup events
 *
 * Slot provides a mechanism for storing and invoking callbacks with a fixed
 * argument signature. It supports direct invocation with typed arguments as
 * well as deferred invocation from a VariantList, enabling type-erased
 * signal/slot communication.
 *
 * @par Thread Safety
 * Thread-affine — a Slot belongs to its owning ObjectBase and
 * should be invoked from that thread.  @ref Signal handles the
 * cross-thread routing for connections that need it.
 *
 * @tparam Args The argument types that the slot's callable accepts.
 */
template <typename... Args> class Slot {
        public:
                /** @brief Callable type that the slot wraps. */
                using Function = std::function<void(Args...)>;

                /**
                 * @brief Constructs a Slot with the given callable and optional metadata.
                 * @param func      The callable to invoke when the slot is executed.
                 * @param owner     Optional pointer to the object that owns this slot.
                 * @param prototype Optional human-readable prototype string for introspection.
                 * @param id        Optional numeric identifier for the slot, defaults to -1 (unset).
                 */
                Slot(const Function &func, void *owner = nullptr, const char *prototype = nullptr, int id = -1)
                    : _owner(owner), _prototype(prototype), _id(id), _function(func) {}

                /** @brief Returns the owner pointer associated with this slot. */
                void *owner() const { return _owner; }

                /** @brief Returns the prototype string associated with this slot. */
                const char *prototype() const { return _prototype; }

                /** @brief Returns the numeric identifier for this slot. */
                int id() const { return _id; }

                /** @brief Sets the numeric identifier for this slot. */
                void setID(int val) { _id = val; }

                /**
                 * @brief Helper trait that strips const qualification and references from a type.
                 * @tparam T The type to transform.
                 */
                template <typename T> struct removeConstAndRef {
                                using type = std::remove_const_t<std::remove_reference_t<T>>;
                };

                /** @brief Convenience alias for removeConstAndRef. */
                template <typename T> using RemoveConstAndRef = typename removeConstAndRef<T>::type;

                /**
                 * @brief Packs the given arguments into a VariantList.
                 *
                 * Each argument is converted to a Variant after stripping const and
                 * reference qualifiers. This is useful for serializing a set of
                 * arguments for later deferred invocation via exec(const VariantList &).
                 *
                 * Defined in @c slot.tpp so @c slot.h does not pull in the
                 * full @c variant.h.  Include @c promeki/slot.tpp from any TU
                 * that calls pack().
                 *
                 * @param args The arguments to pack.
                 * @return A VariantList containing each argument as a Variant.
                 */
                static VariantList pack(Args... args);

                /**
                 * @brief Executes the slot's callable with the given typed arguments.
                 * @param args The arguments to forward to the callable.
                 */
                void exec(Args... args) {
                        _function(args...);
                        return;
                }

                /**
                 * @brief Executes the slot's callable by unpacking arguments from a VariantList.
                 *
                 * Each element in the VariantList is converted back to the corresponding
                 * typed argument and forwarded to the underlying callable.
                 *
                 * Defined in @c slot.tpp so @c slot.h does not pull in the
                 * full @c variant.h.  Include @c promeki/slot.tpp from any TU
                 * that calls @c exec(const VariantList &).
                 *
                 * @param variantList The list of Variant values to unpack as arguments.
                 */
                void exec(const VariantList &variantList);

        private:
                void       *_owner = nullptr;
                const char *_prototype = nullptr;
                int         _id = -1;
                Function    _function;

                template <std::size_t... Idx>
                void execFromVariantList(std::index_sequence<Idx...>, const VariantList &variantList);

                template <std::size_t Idx> decltype(auto) unpackVariant(const Variant &variant);
};

PROMEKI_NAMESPACE_END
