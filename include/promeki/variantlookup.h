/**
 * @file      variantlookup.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdlib>
#include <functional>
#include <optional>
#include <type_traits>
#include <typeinfo>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/map.h>
#include <promeki/readwritelock.h>
#include <promeki/variant.h>
#include <promeki/variantspec.h>
#include <promeki/variantdatabase.h>
#include <promeki/error.h>
#include <promeki/fnv1a.h>
#include <promeki/stringregistry.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

namespace detail {

/**
 * @brief Trait that detects an opt-in polymorphic-dispatch hook on @c T.
 * @ingroup util
 *
 * When @c T exposes a member function
 * @code
 * std::optional<Variant> variantLookupResolve(const String &, Error *) const;
 * @endcode
 * the public @c VariantLookup<T>::resolve (and its @c assign / @c specFor
 * siblings) route through it before hitting the per-type registry, which
 * lets a class with a virtual-dispatch base (@ref MediaPayload being the
 * primary use case) reach the @em most-derived type's registry even when
 * the caller only holds a reference-to-base.  The concept is opt-in —
 * types without the hook stay on the direct-lookup fast path.
 */
template <typename T, typename = void>
struct HasVariantLookupDispatch : std::false_type {};

template <typename T>
struct HasVariantLookupDispatch<T, std::void_t<
        decltype(std::declval<const T &>().variantLookupResolve(
                std::declval<const String &>(),
                std::declval<Error *>()))>>
        : std::true_type {};

/**
 * @brief Convenience variable template for @ref HasVariantLookupDispatch.
 */
template <typename T>
inline constexpr bool hasVariantLookupDispatchV = HasVariantLookupDispatch<T>::value;

/**
 * @brief Parsed leading segment of a VariantLookup key.
 * @ingroup util
 *
 * The lookup grammar is:
 * @code
 * key     := segment ( '.' segment )*
 * segment := name ( '[' index ']' )?
 * @endcode
 *
 * @ref parseLeadingSegment consumes the first @c segment and, if
 * present, the trailing @c '.' so @ref rest holds just the remainder
 * (without any leading dot).
 */
struct VariantLookupSegment {
        /** @brief Name of this segment (no brackets, no dot). */
        String  name;
        /** @brief Everything after the first @c '.', empty if there was none. */
        String  rest;
        /** @brief Integer index if the segment carried a @c '[N]' suffix. */
        size_t  index    = 0;
        /** @brief True when the segment had a @c '[N]' suffix. */
        bool    hasIndex = false;
        /** @brief True when the segment was followed by a @c '.' and a non-empty remainder. */
        bool    hasRest  = false;
};

/**
 * @brief Parses the leading @c segment of a VariantLookup key.
 *
 * Accepts names drawn from @c [A-Za-z0-9_@], an optional
 * @c '[N]' suffix with a non-empty decimal index, and a
 * dot-separated remainder.  Returns false with
 * @c Error::ParseFailed on empty input, empty names,
 * empty indices, unterminated brackets, trailing dots, or any
 * other structural anomaly.  On success, @c err (when provided)
 * is set to @c Error::Ok.
 *
 * @param key  The full remaining key.
 * @param out  Populated with the parsed segment on success.
 * @param err  Optional error output.
 * @return     True on success, false on parse failure.
 */
inline bool parseLeadingSegment(const String &key,
                                VariantLookupSegment &out,
                                Error *err = nullptr) {
        if(err != nullptr) *err = Error::Ok;
        const size_t len = key.byteCount();
        if(len == 0) {
                if(err != nullptr) *err = Error::ParseFailed;
                return false;
        }
        const char *s = key.cstr();
        size_t i = 0;
        auto isNameChar = [](char c) -> bool {
                return (c >= 'A' && c <= 'Z')
                    || (c >= 'a' && c <= 'z')
                    || (c >= '0' && c <= '9')
                    || c == '_' || c == '@';
        };
        while(i < len && isNameChar(s[i])) ++i;
        if(i == 0) {
                if(err != nullptr) *err = Error::ParseFailed;
                return false;
        }
        out.name = String(s, i);
        out.hasIndex = false;
        out.hasRest = false;
        out.rest = String();
        out.index = 0;

        if(i < len && s[i] == '[') {
                ++i;
                const size_t numStart = i;
                while(i < len && s[i] >= '0' && s[i] <= '9') ++i;
                if(i == numStart) {
                        if(err != nullptr) *err = Error::ParseFailed;
                        return false;
                }
                if(i >= len || s[i] != ']') {
                        if(err != nullptr) *err = Error::ParseFailed;
                        return false;
                }
                char *endp = nullptr;
                out.index = static_cast<size_t>(
                        std::strtoull(s + numStart, &endp, 10));
                out.hasIndex = true;
                ++i;
        }

        if(i == len) return true;

        if(s[i] != '.') {
                if(err != nullptr) *err = Error::ParseFailed;
                return false;
        }
        ++i;
        if(i >= len) {
                if(err != nullptr) *err = Error::ParseFailed;
                return false;
        }
        out.rest = String(s + i, len - i);
        out.hasRest = true;
        return true;
}

} // namespace detail

/**
 * @brief Type-parameterised registry of named accessors on @c T.
 * @ingroup util
 *
 * @tparam T The target type this lookup resolves against.
 *
 * Generalises the pseudo-metadata lookup that Frame, Image and Audio
 * all implement by hand.  Each well-known key registers a small
 * @c std::function against one of four handler kinds, and a two-line
 * @ref resolve /  @ref assign dispatch pair replaces the
 * @c resolvePseudoKey + @c resolveKey + subscript-parsing
 * boilerplate per type.
 *
 * @par Grammar
 * @code
 * key     := segment ( '.' segment )*
 * segment := name ( '[' index ']' )?
 * @endcode
 *
 * @par Handler kinds
 *  - @b scalar             — terminal @c "Name" producing a @ref Variant.
 *  - @b indexedScalar      — terminal @c "Name[N]" producing a @ref Variant.
 *  - @b child<U>           — composition @c "Name.rest" delegating to
 *    @c VariantLookup<U>::resolve on a borrowed @c const @c U&.
 *  - @b indexedChild<U>    — composition @c "Name[N].rest" delegating to
 *    @c VariantLookup<U>::resolve on a borrowed @c const @c U&.
 *  - @b database<Name>     — composition @c "Prefix.DbKey" resolving
 *    @c DbKey through a borrowed @ref VariantDatabase.
 *
 * @par Inheritance cascade
 * A registration may declare a single C++ base via
 * @ref Registrar::inheritsFrom "inheritsFrom<Base>()".  When a lookup on
 * @c T cannot match the leading segment, the dispatcher upcasts the
 * instance to @c Base and delegates to @c VariantLookup<Base>::resolveDirect
 * (and peer assign / specFor).  This composes transitively, so a
 * derived class only registers what it adds and inherits everything
 * from its base chain up to the root.
 *
 * @par Polymorphic dispatch
 * Types that opt in via a member function
 * @code
 * std::optional<Variant> variantLookupResolve(const String &, Error *) const;
 * bool                   variantLookupAssign(const String &, const Variant &, Error *);
 * const VariantSpec *    variantLookupSpecFor(const String &, Error *) const;
 * @endcode
 * get polymorphic dispatch for free — the public @ref resolve /
 * @ref assign / @ref specFor entry points route through the virtual
 * first so a caller holding a reference-to-base lands on the @em most
 * derived type's registry, and the upward cascade then fills in any
 * keys the derived class inherited from its base.  Use
 * @ref PROMEKI_VARIANTLOOKUP_DISPATCH(Self) inside the derived
 * @c class body to implement these overrides uniformly.  Types that
 * do not opt in stay on the direct-lookup fast path with no vtable
 * hit.
 *
 * Each handler optionally accepts a setter / mutable accessor.  Omitting
 * it marks the path read-only, causing @ref assign to fail with
 * @c Error::ReadOnly.  Terminal handlers also accept an optional
 * @ref VariantSpec pointer describing the value's declared type; the
 * spec surfaces through @ref specFor for callers that need to coerce
 * string literals to the key's native type (query compilers,
 * introspectors, configuration tools).
 *
 * @par Lookup keys
 * Internally every registered name is hashed to a @c uint64_t via
 * 64-bit FNV-1a and stored in hash-keyed maps so @ref resolve /
 * @ref assign / @ref specFor avoid per-call string comparisons.  A
 * per-@c T reverse map preserves names for introspection.  The
 * @ref Key nested type is a lightweight handle around the same
 * @c uint64_t ID with a @c constexpr @ref Key::literal factory, so
 * callers that already know which terminal they want can skip the
 * name round-trip with a typed overload.
 *
 * @par Registration
 * Handlers register at static-init time via the @ref PROMEKI_LOOKUP_REGISTER
 * macro and the fluent @ref Registrar builder:
 * @code
 * PROMEKI_LOOKUP_REGISTER(Image)
 *         .scalar("Width",  [](const Image &i){ return Variant(i.desc().width()); })
 *         .scalar("Height", [](const Image &i){ return Variant(i.desc().height()); })
 *         .database<"Metadata">("Meta",
 *                 [](const Image &i) -> const VariantDatabase<"Metadata">* {
 *                         return &i.desc().metadata();
 *                 });
 * @endcode
 *
 * @par Dispatch
 * @code
 * Error err;
 * auto v = VariantLookup<Image>::resolve(img, "Meta.Timecode", &err);
 * @endcode
 *
 * @par Error codes
 *  - @c Error::Ok          — resolution / assignment succeeded.
 *  - @c Error::IdNotFound  — no handler matched the leading segment.
 *  - @c Error::ParseFailed — malformed key (empty name, unterminated
 *    bracket, trailing dot, etc.).
 *  - @c Error::OutOfRange  — subscripted child / indexed scalar got an
 *    out-of-range index.
 *  - @c Error::ReadOnly    — @ref assign hit a handler without a setter
 *    or a read-only traversal.
 *  - @c Error::ConversionFailed — setter rejected the supplied value.
 *
 * @par Thread Safety
 * Fully thread-safe.  The per-type registry is internally guarded by a
 * @c ReadWriteLock: registrations take the write lock, lookups take a
 * shared read lock.  Registrations are expected at static-init time;
 * thereafter @c resolve / @c assign / @c specFor may be called concurrently
 * from any thread, on any number of instances of @c T, without external
 * synchronization (the @c T instance itself is borrowed by reference, so
 * the caller is still responsible for synchronizing accesses to that
 * object's mutable state).
 *
 * @todo FIXME(compiled-path) — @ref resolve currently re-parses and
 *       re-hashes every segment on every call.  A @c VariantLookup::Path
 *       type could pre-resolve a full dotted key such as
 *       @c "Video[0].Meta.Timecode" into a sequence of integer IDs
 *       plus kind-transition tags at compile / parse time, so hot
 *       evaluators (VariantQuery, format templates) do integer-only
 *       lookups at run time.  The path has to remember its kind
 *       transitions (indexedChild → database, …) and each hop's
 *       target type, so this is a non-trivial follow-up.
 */
template <typename T>
class VariantLookup {
        public:
                // ============================================================
                // Key
                // ============================================================

                /**
                 * @brief Lightweight handle for a single registered key name.
                 *
                 * A @ref Key wraps the 64-bit FNV-1a hash of a registered
                 * name, so lookups that already know which terminal they
                 * want skip the per-call string hash.  Use @ref literal
                 * to build a @c constexpr handle from a name literal (no
                 * registry touch; the hash matches any name later
                 * registered via @ref Registrar::scalar etc.).  Use
                 * @ref find to look up a name that may or may not be
                 * registered without inserting it.  The regular
                 * @c Key(String) constructor is intentionally omitted
                 * because VariantLookup's registration path always flows
                 * through the @ref Registrar macros; callers should use
                 * @ref literal (compile-time) or @ref find (runtime) to
                 * avoid accidentally creating stray Key entries.
                 */
                class Key {
                        public:
                                /** @brief Constructs an invalid Key. */
                                constexpr Key() = default;

                                /**
                                 * @brief Builds a Key from a name literal.
                                 *
                                 * Pure FNV-1a hash; does not touch the
                                 * runtime registry.  The resulting @ref id
                                 * matches any name later registered via
                                 * the @ref Registrar, so @ref literal
                                 * pairs of Keys can be used in
                                 * @c constexpr machinery (switch labels,
                                 * @c static_assert, non-type template
                                 * parameters).
                                 */
                                static constexpr Key literal(const char *name) {
                                        return Key(fnv1a(name));
                                }

                                /**
                                 * @brief Looks up a Key by name without registering it.
                                 *
                                 * Returns an invalid Key when the name has
                                 * not been registered with any handler on
                                 * @c T.
                                 */
                                static Key find(const String &name) {
                                        const Registry &r = registry();
                                        ReadWriteLock::ReadLocker lock(r.lock);
                                        uint64_t id = r.findId(name);
                                        return id == Invalid ? Key() : Key(id);
                                }

                                /**
                                 * @brief Builds a Key from a raw integer ID.
                                 *
                                 * No validation is performed; primarily
                                 * used internally by @ref resolve.
                                 */
                                static constexpr Key fromId(uint64_t id) { return Key(id); }

                                /** @brief Returns the integer ID. */
                                constexpr uint64_t id() const { return _id; }

                                /**
                                 * @brief Returns the registered name, or
                                 *        empty when the name was never
                                 *        registered via a runtime path.
                                 *
                                 * @ref literal does not register the
                                 * name; callers that want a reverse
                                 * lookup from a @ref literal key must
                                 * register the name through the
                                 * @ref Registrar first.
                                 */
                                String name() const {
                                        const Registry &r = registry();
                                        ReadWriteLock::ReadLocker lock(r.lock);
                                        auto it = r.names.find(_id);
                                        if(it == r.names.end()) return String();
                                        return it->second;
                                }

                                /** @brief True when this handle refers to a registered key. */
                                constexpr bool isValid() const { return _id != Invalid; }

                                constexpr bool operator==(const Key &other) const { return _id == other._id; }
                                constexpr bool operator!=(const Key &other) const { return _id != other._id; }
                                constexpr bool operator<(const Key &other) const { return _id < other._id; }

                        private:
                                static constexpr uint64_t Invalid = UINT64_MAX;
                                uint64_t _id = Invalid;

                                constexpr explicit Key(uint64_t id) : _id(id) {}

                                friend class VariantLookup<T>;
                };

                // ============================================================
                // Handler signatures
                // ============================================================

                /** @brief Terminal scalar getter. */
                using ScalarGet        = std::function<std::optional<Variant>(const T &)>;
                /**
                 * @brief Terminal scalar setter.
                 *
                 * Returns @c Error::Ok on success or a specific error
                 * code on failure (e.g. @c Error::ConversionFailed for
                 * a value of the wrong type, @c Error::OutOfRange for
                 * a numeric bounds violation).  @ref assign propagates
                 * the returned code to the caller unchanged.
                 */
                using ScalarSet        = std::function<Error(T &, const Variant &)>;
                /** @brief Terminal indexed scalar getter. */
                using IndexedScalarGet = std::function<std::optional<Variant>(const T &, size_t)>;
                /** @brief Terminal indexed scalar setter; see @ref ScalarSet for error semantics. */
                using IndexedScalarSet = std::function<Error(T &, size_t, const Variant &)>;

                /** @brief Erased composition getter (child or database). */
                using ComposeResolve   = std::function<std::optional<Variant>(const T &, const String &, Error *)>;
                /** @brief Erased composition setter (child or database). */
                using ComposeAssign    = std::function<bool(T &, const String &, const Variant &, Error *)>;

                /** @brief Erased indexed composition getter. */
                using IndexedCompResolve = std::function<std::optional<Variant>(const T &, size_t, const String &, Error *)>;
                /** @brief Erased indexed composition setter. */
                using IndexedCompAssign  = std::function<bool(T &, size_t, const String &, const Variant &, Error *)>;

                /**
                 * @brief Erased spec resolver for a composed handler.
                 *
                 * Maps a trailing key-path fragment (everything after the
                 * composition's leading segment) to a @ref VariantSpec
                 * pointer, or @c nullptr when no spec is declared.  For a
                 * @ref Registrar::child "child<U>" handler this forwards
                 * to @c VariantLookup<U>::specFor(rest); for a
                 * @ref Registrar::database "database<DbName>" handler it
                 * forwards to @c VariantDatabase<DbName>::specFor(rest).
                 */
                using ComposeSpecFor   = std::function<const VariantSpec *(const String &, Error *)>;

                // ============================================================
                // Registrar (fluent builder)
                // ============================================================

                /**
                 * @brief Fluent builder returned by @ref registrar.
                 *
                 * All methods side-effect into the per-@c T static registry
                 * and return a fresh @ref Registrar so registrations can
                 * be chained.  The instance itself is stateless; it exists
                 * only so users can write the @c PROMEKI_LOOKUP_REGISTER
                 * block as a single expression.
                 *
                 * @note Registration methods are marked @c const because
                 *       @c Registrar carries no per-instance state — the
                 *       @c const here denotes "no @c Registrar members
                 *       are modified", not "no side effects".  Each call
                 *       takes a write lock on the per-@c T static registry
                 *       and inserts the handler there.
                 */
                class Registrar {
                        public:
                                /** @brief Registers a read-only scalar. */
                                Registrar scalar(const String &name, ScalarGet get,
                                                 const VariantSpec *spec = nullptr) const {
                                        Registry &r = registry();
                                        ReadWriteLock::WriteLocker lock(r.lock);
                                        uint64_t id = r.declareName(name);
                                        r.scalars.insert(id, ScalarEntry{ std::move(get), ScalarSet(), spec });
                                        return *this;
                                }

                                /** @brief Registers a read-write scalar. */
                                Registrar scalar(const String &name, ScalarGet get, ScalarSet set,
                                                 const VariantSpec *spec = nullptr) const {
                                        Registry &r = registry();
                                        ReadWriteLock::WriteLocker lock(r.lock);
                                        uint64_t id = r.declareName(name);
                                        r.scalars.insert(id, ScalarEntry{ std::move(get), std::move(set), spec });
                                        return *this;
                                }

                                /** @brief Registers a read-only indexed scalar. */
                                Registrar indexedScalar(const String &name, IndexedScalarGet get,
                                                        const VariantSpec *spec = nullptr) const {
                                        Registry &r = registry();
                                        ReadWriteLock::WriteLocker lock(r.lock);
                                        uint64_t id = r.declareName(name);
                                        r.indexedScalars.insert(id,
                                                IndexedScalarEntry{ std::move(get), IndexedScalarSet(), spec });
                                        return *this;
                                }

                                /** @brief Registers a read-write indexed scalar. */
                                Registrar indexedScalar(const String &name, IndexedScalarGet get, IndexedScalarSet set,
                                                        const VariantSpec *spec = nullptr) const {
                                        Registry &r = registry();
                                        ReadWriteLock::WriteLocker lock(r.lock);
                                        uint64_t id = r.declareName(name);
                                        r.indexedScalars.insert(id,
                                                IndexedScalarEntry{ std::move(get), std::move(set), spec });
                                        return *this;
                                }

                                /**
                                 * @brief Registers a child composition (no index).
                                 *
                                 * Looking up @c "Name.rest" calls @p get on the instance,
                                 * then forwards @c rest to @c VariantLookup<U>::resolve.
                                 * A null @p get result yields @c Error::IdNotFound.
                                 * @ref specFor on the same path delegates to
                                 * @c VariantLookup<U>::specFor(rest), so @c U's
                                 * declared types flow up transparently.
                                 *
                                 * @tparam U      Child type.  Must have its own
                                 *                @c VariantLookup<U> registrations for
                                 *                the @c rest portion to resolve.
                                 * @param name    Segment name.
                                 * @param get     Const accessor returning a borrowed @c U*.
                                 * @param getMut  Optional mutable accessor for @ref assign.
                                 *                When null the path is read-only.
                                 */
                                template <typename U>
                                Registrar child(const String &name,
                                                std::function<const U *(const T &)> get,
                                                std::function<U *(T &)> getMut = nullptr) const {
                                        ComposeResolve resolveFn =
                                                [get](const T &t, const String &rest, Error *err) -> std::optional<Variant> {
                                                        const U *u = get(t);
                                                        if(u == nullptr) {
                                                                if(err != nullptr) *err = Error::IdNotFound;
                                                                return std::nullopt;
                                                        }
                                                        return VariantLookup<U>::resolve(*u, rest, err);
                                                };
                                        ComposeAssign assignFn;
                                        if(getMut) {
                                                assignFn = [getMut](T &t, const String &rest, const Variant &v, Error *err) -> bool {
                                                        U *u = getMut(t);
                                                        if(u == nullptr) {
                                                                if(err != nullptr) *err = Error::IdNotFound;
                                                                return false;
                                                        }
                                                        return VariantLookup<U>::assign(*u, rest, v, err);
                                                };
                                        }
                                        ComposeSpecFor specFn =
                                                [](const String &rest, Error *err) -> const VariantSpec * {
                                                        return VariantLookup<U>::specFor(rest, err);
                                                };
                                        std::function<StringList(const T &, const String &)> dumpFn =
                                                [get](const T &t, const String &indent) -> StringList {
                                                        const U *u = get(t);
                                                        if(u == nullptr) return StringList();
                                                        return VariantLookup<U>::dump(*u, indent);
                                                };
                                        Registry &r = registry();
                                        ReadWriteLock::WriteLocker lock(r.lock);
                                        uint64_t id = r.declareName(name);
                                        r.children.insert(id,
                                                ComposeEntry{ std::move(resolveFn), std::move(assignFn),
                                                              std::move(specFn), std::move(dumpFn) });
                                        return *this;
                                }

                                /**
                                 * @brief Registers an indexed child composition.
                                 *
                                 * Looking up @c "Name[N].rest" calls @p get on the
                                 * instance with @c N, then forwards @c rest to
                                 * @c VariantLookup<U>::resolve.  A null @p get
                                 * result yields @c Error::OutOfRange.  Spec lookup
                                 * ignores the index (spec is a type-level property)
                                 * and delegates to @c VariantLookup<U>::specFor.
                                 */
                                template <typename U>
                                Registrar indexedChild(const String &name,
                                                       std::function<const U *(const T &, size_t)> get,
                                                       std::function<U *(T &, size_t)> getMut = nullptr) const {
                                        IndexedCompResolve resolveFn =
                                                [get](const T &t, size_t idx, const String &rest, Error *err) -> std::optional<Variant> {
                                                        const U *u = get(t, idx);
                                                        if(u == nullptr) {
                                                                if(err != nullptr) *err = Error::OutOfRange;
                                                                return std::nullopt;
                                                        }
                                                        return VariantLookup<U>::resolve(*u, rest, err);
                                                };
                                        IndexedCompAssign assignFn;
                                        if(getMut) {
                                                assignFn = [getMut](T &t, size_t idx, const String &rest, const Variant &v, Error *err) -> bool {
                                                        U *u = getMut(t, idx);
                                                        if(u == nullptr) {
                                                                if(err != nullptr) *err = Error::OutOfRange;
                                                                return false;
                                                        }
                                                        return VariantLookup<U>::assign(*u, rest, v, err);
                                                };
                                        }
                                        ComposeSpecFor specFn =
                                                [](const String &rest, Error *err) -> const VariantSpec * {
                                                        return VariantLookup<U>::specFor(rest, err);
                                                };
                                        std::function<StringList(const T &, const String &)> dumpFn =
                                                [get](const T &t, const String &indent) -> StringList {
                                                        // Probe 0..N, stopping at the first
                                                        // null returned from @c get.  Each
                                                        // slice gets its own @c "[i]:"
                                                        // header so multi-slice sections
                                                        // don't smoosh into one flat block
                                                        // in @ref dump output.
                                                        StringList out;
                                                        const String childIndent = indent + "  ";
                                                        for(size_t i = 0; ; ++i) {
                                                                const U *u = get(t, i);
                                                                if(u == nullptr) break;
                                                                StringList sub = VariantLookup<U>::dump(*u, childIndent);
                                                                out.pushToBack(indent + String::sprintf("[%zu]:", i));
                                                                for(const String &l : sub) out.pushToBack(l);
                                                        }
                                                        return out;
                                                };
                                        Registry &r = registry();
                                        ReadWriteLock::WriteLocker lock(r.lock);
                                        uint64_t id = r.declareName(name);
                                        r.indexedChildren.insert(id,
                                                IndexedCompEntry{ std::move(resolveFn), std::move(assignFn),
                                                                  std::move(specFn), std::move(dumpFn) });
                                        return *this;
                                }

                                /**
                                 * @brief Registers an indexed child where
                                 *        the child target is constructed by
                                 *        value on demand.
                                 *
                                 * Companion to @ref indexedChild for types
                                 * that can be cheaply materialised from the
                                 * parent but aren't stored anywhere
                                 * long-term — the textbook case is a
                                 * lightweight proxy like
                                 * @ref BufferView::Entry that only holds a
                                 * parent pointer and an index.  @p get
                                 * returns a @c std::optional<U> by value;
                                 * @ref resolve borrows the temporary long
                                 * enough to run
                                 * @c VariantLookup<U>::resolve on it.
                                 *
                                 * Read-only: the by-value shape rules out
                                 * assign — there is no stable target to
                                 * hold a mutation.  Consumers that need to
                                 * mutate must use the pointer-returning
                                 * @ref indexedChild overload.
                                 *
                                 * @tparam U    The child type.  Must be
                                 *              registered with its own
                                 *              @c VariantLookup<U>.
                                 * @param name  Segment name (e.g. @c "Buffer").
                                 * @param get   Computes the child on demand;
                                 *              return @c std::nullopt to
                                 *              signal out-of-range.  The
                                 *              @ref dump walker stops at
                                 *              the first @c nullopt.
                                 */
                                template <typename U>
                                Registrar indexedChildByValue(const String &name,
                                                              std::function<std::optional<U>(const T &, size_t)> get) const {
                                        IndexedCompResolve resolveFn =
                                                [get](const T &t, size_t idx, const String &rest, Error *err) -> std::optional<Variant> {
                                                        auto u = get(t, idx);
                                                        if(!u.has_value()) {
                                                                if(err != nullptr) *err = Error::OutOfRange;
                                                                return std::nullopt;
                                                        }
                                                        return VariantLookup<U>::resolve(*u, rest, err);
                                                };
                                        // No assign — by-value temporaries have no backing store.
                                        IndexedCompAssign assignFn;
                                        ComposeSpecFor specFn =
                                                [](const String &rest, Error *err) -> const VariantSpec * {
                                                        return VariantLookup<U>::specFor(rest, err);
                                                };
                                        std::function<StringList(const T &, const String &)> dumpFn =
                                                [get](const T &t, const String &indent) -> StringList {
                                                        // Matches the pointer-variant
                                                        // @ref indexedChild dump format —
                                                        // one @c "[i]:" header per slice,
                                                        // scalars indented underneath.
                                                        StringList out;
                                                        const String childIndent = indent + "  ";
                                                        for(size_t i = 0; ; ++i) {
                                                                auto u = get(t, i);
                                                                if(!u.has_value()) break;
                                                                StringList sub = VariantLookup<U>::dump(*u, childIndent);
                                                                out.pushToBack(indent + String::sprintf("[%zu]:", i));
                                                                for(const String &l : sub) out.pushToBack(l);
                                                        }
                                                        return out;
                                                };
                                        Registry &r = registry();
                                        ReadWriteLock::WriteLocker lock(r.lock);
                                        uint64_t id = r.declareName(name);
                                        r.indexedChildren.insert(id,
                                                IndexedCompEntry{ std::move(resolveFn), std::move(assignFn),
                                                                  std::move(specFn), std::move(dumpFn) });
                                        return *this;
                                }

                                /**
                                 * @brief Registers a prefixed database binding.
                                 *
                                 * Looking up @c "Prefix.DbKey" calls @p get on the
                                 * instance then resolves @c DbKey through the
                                 * returned @ref VariantDatabase.  The remainder
                                 * is treated opaquely: it is handed to
                                 * @c VariantDatabase<DbName>::ID::find without
                                 * further parsing.  A null @p get result yields
                                 * @c Error::IdNotFound; an unregistered key
                                 * also yields @c Error::IdNotFound.  Spec lookup
                                 * delegates to
                                 * @c VariantDatabase<DbName>::specFor(rest), so
                                 * declared types for keys like
                                 * @c Meta.Timecode surface uniformly.
                                 *
                                 * @tparam DbName The compile-time name of the
                                 *                target @ref VariantDatabase.
                                 *                The binding is typed so the
                                 *                right per-@c DbName StringRegistry
                                 *                is consulted.
                                 */
                                template <CompiledString DbName>
                                Registrar database(const String &prefix,
                                                   std::function<const VariantDatabase<DbName> *(const T &)> get,
                                                   std::function<VariantDatabase<DbName> *(T &)> getMut = nullptr) const {
                                        ComposeResolve resolveFn =
                                                [get](const T &t, const String &rest, Error *err) -> std::optional<Variant> {
                                                        const VariantDatabase<DbName> *db = get(t);
                                                        if(db == nullptr) {
                                                                if(err != nullptr) *err = Error::IdNotFound;
                                                                return std::nullopt;
                                                        }
                                                        auto id = VariantDatabase<DbName>::ID::find(rest);
                                                        if(!id.isValid() || !db->contains(id)) {
                                                                if(err != nullptr) *err = Error::IdNotFound;
                                                                return std::nullopt;
                                                        }
                                                        if(err != nullptr) *err = Error::Ok;
                                                        return db->get(id);
                                                };
                                        ComposeAssign assignFn;
                                        if(getMut) {
                                                assignFn = [getMut](T &t, const String &rest, const Variant &v, Error *err) -> bool {
                                                        VariantDatabase<DbName> *db = getMut(t);
                                                        if(db == nullptr) {
                                                                if(err != nullptr) *err = Error::IdNotFound;
                                                                return false;
                                                        }
                                                        typename VariantDatabase<DbName>::ID id(rest);
                                                        if(!db->set(id, v)) {
                                                                if(err != nullptr) *err = Error::ConversionFailed;
                                                                return false;
                                                        }
                                                        if(err != nullptr) *err = Error::Ok;
                                                        return true;
                                                };
                                        }
                                        ComposeSpecFor specFn =
                                                [](const String &rest, Error *err) -> const VariantSpec * {
                                                        const VariantSpec *sp = VariantDatabase<DbName>::specFor(rest);
                                                        if(sp == nullptr && err != nullptr) *err = Error::IdNotFound;
                                                        return sp;
                                                };
                                        std::function<StringList(const T &, const String &)> dumpFn =
                                                [get](const T &t, const String &indent) -> StringList {
                                                        const VariantDatabase<DbName> *db = get(t);
                                                        StringList out;
                                                        if(db == nullptr) return out;
                                                        // Match Metadata::dump formatting
                                                        // so db-like bindings render the
                                                        // same shape regardless of which
                                                        // concrete VariantDatabase is
                                                        // bound — consumers reading the
                                                        // output don't have to
                                                        // special-case the Meta prefix.
                                                        db->forEach([&out, &indent](
                                                                        typename VariantDatabase<DbName>::ID id,
                                                                        const Variant &value) {
                                                                String s = indent;
                                                                s += id.name();
                                                                s += " [";
                                                                s += value.typeName();
                                                                s += "]: ";
                                                                s += value.format(String());
                                                                out.pushToBack(s);
                                                        });
                                                        return out;
                                                };
                                        Registry &r = registry();
                                        ReadWriteLock::WriteLocker lock(r.lock);
                                        uint64_t id = r.declareName(prefix);
                                        r.databases.insert(id,
                                                ComposeEntry{ std::move(resolveFn), std::move(assignFn),
                                                              std::move(specFn), std::move(dumpFn) });
                                        return *this;
                                }

                                /**
                                 * @brief Declares @c T's C++ base for lookup cascade.
                                 *
                                 * When a lookup on @c T misses every handler,
                                 * the dispatcher upcasts @c instance to
                                 * @c Base and delegates to
                                 * @c VariantLookup<Base>::resolveDirect (or
                                 * the @c assign / @c specFor peer) so the
                                 * derived class only registers what it
                                 * adds.  Composes transitively — the
                                 * @c Base may itself declare
                                 * @c inheritsFrom<SuperBase>().
                                 *
                                 * @tparam Base A public base of @c T.  The
                                 *              @c static_assert enforces
                                 *              @c std::is_base_of_v<Base,T>
                                 *              so misuse is caught at
                                 *              static-init time.
                                 */
                                template <typename Base>
                                Registrar inheritsFrom() const {
                                        static_assert(std::is_base_of_v<Base, T>,
                                                "VariantLookup::inheritsFrom<Base>(): "
                                                "Base must be a public base of T");
                                        static_assert(!std::is_same_v<Base, T>,
                                                "VariantLookup::inheritsFrom<Base>(): "
                                                "Base must differ from T");
                                        Registry &r = registry();
                                        ReadWriteLock::WriteLocker lock(r.lock);
                                        r.inherit.resolve =
                                                [](const T &t, const String &key, Error *err)
                                                        -> std::optional<Variant> {
                                                        return VariantLookup<Base>::resolveDirect(
                                                                static_cast<const Base &>(t), key, err);
                                                };
                                        r.inherit.assign =
                                                [](T &t, const String &key, const Variant &v, Error *err)
                                                        -> bool {
                                                        return VariantLookup<Base>::assignDirect(
                                                                static_cast<Base &>(t), key, v, err);
                                                };
                                        r.inherit.specFor =
                                                [](const String &key, Error *err)
                                                        -> const VariantSpec * {
                                                        return VariantLookup<Base>::specForDirect(key, err);
                                                };
                                        r.inherit.resolveByKey =
                                                [](const T &t, uint64_t id, Error *err)
                                                        -> std::optional<Variant> {
                                                        return VariantLookup<Base>::resolveKeyIdDirect(
                                                                static_cast<const Base &>(t), id, err);
                                                };
                                        r.inherit.assignByKey =
                                                [](T &t, uint64_t id, const Variant &v, Error *err)
                                                        -> bool {
                                                        return VariantLookup<Base>::assignKeyIdDirect(
                                                                static_cast<Base &>(t), id, v, err);
                                                };
                                        r.inherit.specForByKey =
                                                [](uint64_t id, Error *err)
                                                        -> const VariantSpec * {
                                                        return VariantLookup<Base>::specForKeyIdDirect(id, err);
                                                };
                                        r.inherit.resolveIndexedByKey =
                                                [](const T &t, uint64_t id, size_t index, Error *err)
                                                        -> std::optional<Variant> {
                                                        return VariantLookup<Base>::resolveIndexedKeyIdDirect(
                                                                static_cast<const Base &>(t), id, index, err);
                                                };
                                        r.inherit.assignIndexedByKey =
                                                [](T &t, uint64_t id, size_t index, const Variant &v, Error *err)
                                                        -> bool {
                                                        return VariantLookup<Base>::assignIndexedKeyIdDirect(
                                                                static_cast<Base &>(t), id, index, v, err);
                                                };
                                        r.inherit.forEachScalar =
                                                [](const std::function<void(const String &)> &fn) {
                                                        VariantLookup<Base>::forEachScalar(fn);
                                                };
                                        r.inherit.registeredScalars =
                                                []() { return VariantLookup<Base>::registeredScalars(); };
                                        r.inherit.registeredIndexedScalars =
                                                []() { return VariantLookup<Base>::registeredIndexedScalars(); };
                                        r.inherit.registeredChildren =
                                                []() { return VariantLookup<Base>::registeredChildren(); };
                                        r.inherit.registeredIndexedChildren =
                                                []() { return VariantLookup<Base>::registeredIndexedChildren(); };
                                        r.inherit.registeredDatabases =
                                                []() { return VariantLookup<Base>::registeredDatabases(); };
                                        r.inherit.dumpComposites =
                                                [](const T &t, const String &indent) -> StringList {
                                                        return VariantLookup<Base>::dumpComposites(
                                                                static_cast<const Base &>(t), indent);
                                                };
                                        return *this;
                                }
                };

                /** @brief Returns a fresh @ref Registrar for chain-style registration. */
                static Registrar registrar() { return Registrar{}; }

                // ============================================================
                // Dispatch
                // ============================================================

                /**
                 * @brief Resolves @p key against @p instance.
                 *
                 * The public entry point.  When @c T exposes the opt-in
                 * @c variantLookupResolve virtual hook (see
                 * @ref detail::HasVariantLookupDispatch), the call is
                 * forwarded through the hook so the @em most-derived
                 * type's registry is consulted first — the concrete
                 * override then delegates to @ref resolveDirect on its
                 * own @c VariantLookup, and the upward cascade
                 * (@ref Registrar::inheritsFrom) fills in any keys
                 * inherited from the base chain.  Types without the
                 * hook stay on the direct-lookup fast path.
                 *
                 * @param instance The target instance.
                 * @param key      The lookup key.
                 * @param err      Optional error output.
                 * @return The resolved @ref Variant, or @c std::nullopt on
                 *         any failure.
                 */
                static std::optional<Variant> resolve(const T &instance, const String &key, Error *err = nullptr) {
                        if constexpr (detail::hasVariantLookupDispatchV<T>) {
                                return instance.variantLookupResolve(key, err);
                        } else {
                                return resolveDirect(instance, key, err);
                        }
                }

                /**
                 * @brief Non-dispatching sibling of @ref resolve.
                 *
                 * Walks only @c T's own registry and, on
                 * @c Error::IdNotFound, the single @ref Registrar::inheritsFrom
                 * fallback if one was declared.  Never re-enters the
                 * virtual hook, so overrides of
                 * @c variantLookupResolve can safely delegate here
                 * without risking infinite recursion.
                 */
                static std::optional<Variant> resolveDirect(const T &instance, const String &key, Error *err = nullptr) {
                        if(err != nullptr) *err = Error::Ok;
                        detail::VariantLookupSegment seg;
                        if(!detail::parseLeadingSegment(key, seg, err)) return std::nullopt;

                        const Registry &r = registry();
                        ReadWriteLock::ReadLocker lock(r.lock);
                        const uint64_t id = r.findId(seg.name);
                        if(id == Registry::Invalid) {
                                // Cascade to inherited registry before
                                // reporting IdNotFound.  The lock is
                                // held but the inherit fallback touches
                                // VariantLookup<Base>'s registry, which
                                // has its own lock — no deadlock since
                                // each Registry's lock is independent.
                                if(r.inherit.resolve) {
                                        return r.inherit.resolve(instance, key, err);
                                }
                                if(err != nullptr) *err = Error::IdNotFound;
                                return std::nullopt;
                        }

                        if(!seg.hasRest) {
                                if(seg.hasIndex) {
                                        auto it = r.indexedScalars.find(id);
                                        if(it == r.indexedScalars.end()) {
                                                if(r.inherit.resolve) {
                                                        return r.inherit.resolve(instance, key, err);
                                                }
                                                if(err != nullptr) *err = Error::IdNotFound;
                                                return std::nullopt;
                                        }
                                        auto v = it->second.get(instance, seg.index);
                                        if(!v.has_value()) {
                                                if(err != nullptr) *err = Error::OutOfRange;
                                        }
                                        return v;
                                }
                                auto it = r.scalars.find(id);
                                if(it == r.scalars.end()) {
                                        if(r.inherit.resolve) {
                                                return r.inherit.resolve(instance, key, err);
                                        }
                                        if(err != nullptr) *err = Error::IdNotFound;
                                        return std::nullopt;
                                }
                                auto v = it->second.get(instance);
                                if(!v.has_value()) {
                                        if(err != nullptr) *err = Error::IdNotFound;
                                }
                                return v;
                        }

                        if(seg.hasIndex) {
                                auto it = r.indexedChildren.find(id);
                                if(it == r.indexedChildren.end()) {
                                        if(r.inherit.resolve) {
                                                return r.inherit.resolve(instance, key, err);
                                        }
                                        if(err != nullptr) *err = Error::IdNotFound;
                                        return std::nullopt;
                                }
                                return it->second.resolve(instance, seg.index, seg.rest, err);
                        }

                        auto itChild = r.children.find(id);
                        if(itChild != r.children.end()) {
                                return itChild->second.resolve(instance, seg.rest, err);
                        }
                        auto itDb = r.databases.find(id);
                        if(itDb != r.databases.end()) {
                                return itDb->second.resolve(instance, seg.rest, err);
                        }
                        if(r.inherit.resolve) {
                                return r.inherit.resolve(instance, key, err);
                        }
                        if(err != nullptr) *err = Error::IdNotFound;
                        return std::nullopt;
                }

                /**
                 * @brief Resolves a terminal scalar by @ref Key.
                 *
                 * Dispatches through @c variantLookupResolveKey when
                 * @c T opts in, otherwise delegates to
                 * @ref resolveKeyIdDirect on the scalar-only fast path.
                 * Composed handlers and indexed scalars are unreachable
                 * via this overload — use the @c String or indexed
                 * overloads for those.
                 */
                static std::optional<Variant> resolve(const T &instance, Key key, Error *err = nullptr) {
                        if constexpr (detail::hasVariantLookupDispatchV<T>) {
                                // Name lookup via Key is still handy
                                // for dispatching types — route it
                                // through the name path (most callers
                                // will have the name the Key wraps) so
                                // the virtual hook applies uniformly.
                                String name = key.name();
                                if(!name.isEmpty()) {
                                        return instance.variantLookupResolve(name, err);
                                }
                                return resolveKeyIdDirect(instance, key.id(), err);
                        } else {
                                return resolveKeyIdDirect(instance, key.id(), err);
                        }
                }

                /**
                 * @brief Resolves a terminal indexed scalar by @ref Key.
                 *
                 * Typed shortcut matching the @c "Name[index]" shape
                 * for callers that already hold a @ref Key.
                 */
                static std::optional<Variant> resolve(const T &instance, Key key, size_t index, Error *err = nullptr) {
                        return resolveIndexedKeyIdDirect(instance, key.id(), index, err);
                }

                /**
                 * @brief Internal Key-id resolver used by the @ref Key
                 *        overloads and the inherit cascade.
                 *
                 * Walks only the terminal-scalar map of @c T and, on
                 * miss, the @ref Registrar::inheritsFrom fallback so a
                 * Key registered on a base resolves through a derived
                 * instance.
                 */
                static std::optional<Variant> resolveKeyIdDirect(const T &instance, uint64_t id, Error *err = nullptr) {
                        if(err != nullptr) *err = Error::Ok;
                        const Registry &r = registry();
                        ReadWriteLock::ReadLocker lock(r.lock);
                        auto it = r.scalars.find(id);
                        if(it == r.scalars.end()) {
                                if(r.inherit.resolveByKey) {
                                        return r.inherit.resolveByKey(instance, id, err);
                                }
                                if(err != nullptr) *err = Error::IdNotFound;
                                return std::nullopt;
                        }
                        auto v = it->second.get(instance);
                        if(!v.has_value() && err != nullptr) *err = Error::IdNotFound;
                        return v;
                }

                /**
                 * @brief Internal indexed-Key resolver used by the
                 *        indexed @ref Key overload and the inherit
                 *        cascade.
                 */
                static std::optional<Variant> resolveIndexedKeyIdDirect(const T &instance, uint64_t id, size_t index, Error *err = nullptr) {
                        if(err != nullptr) *err = Error::Ok;
                        const Registry &r = registry();
                        ReadWriteLock::ReadLocker lock(r.lock);
                        auto it = r.indexedScalars.find(id);
                        if(it == r.indexedScalars.end()) {
                                if(r.inherit.resolveIndexedByKey) {
                                        return r.inherit.resolveIndexedByKey(instance, id, index, err);
                                }
                                if(err != nullptr) *err = Error::IdNotFound;
                                return std::nullopt;
                        }
                        auto v = it->second.get(instance, index);
                        if(!v.has_value() && err != nullptr) *err = Error::OutOfRange;
                        return v;
                }

                /**
                 * @brief Assigns @p value under @p key into @p instance.
                 *
                 * Mirror of @ref resolve for mutation.  Fails with
                 * @c Error::ReadOnly when the matched handler has no
                 * setter, and with @c Error::ConversionFailed when the
                 * setter rejects the supplied value.  Dispatches
                 * through the @c variantLookupAssign hook when @c T
                 * opts in, otherwise delegates to @ref assignDirect.
                 *
                 * @param instance The target instance (mutable).
                 * @param key      The lookup key.
                 * @param value    The new value.
                 * @param err      Optional error output.
                 * @return True on success, false on any failure.
                 */
                static bool assign(T &instance, const String &key, const Variant &value, Error *err = nullptr) {
                        if constexpr (detail::hasVariantLookupDispatchV<T>) {
                                return instance.variantLookupAssign(key, value, err);
                        } else {
                                return assignDirect(instance, key, value, err);
                        }
                }

                /**
                 * @brief Non-dispatching sibling of @ref assign.
                 *
                 * Walks @c T's own registry, then the single
                 * @ref Registrar::inheritsFrom fallback.  Safe to call
                 * from inside @c variantLookupAssign overrides without
                 * risking recursion.
                 */
                static bool assignDirect(T &instance, const String &key, const Variant &value, Error *err = nullptr) {
                        if(err != nullptr) *err = Error::Ok;
                        detail::VariantLookupSegment seg;
                        if(!detail::parseLeadingSegment(key, seg, err)) return false;

                        const Registry &r = registry();
                        ReadWriteLock::ReadLocker lock(r.lock);
                        const uint64_t id = r.findId(seg.name);
                        if(id == Registry::Invalid) {
                                if(r.inherit.assign) {
                                        return r.inherit.assign(instance, key, value, err);
                                }
                                if(err != nullptr) *err = Error::IdNotFound;
                                return false;
                        }

                        if(!seg.hasRest) {
                                if(seg.hasIndex) {
                                        auto it = r.indexedScalars.find(id);
                                        if(it == r.indexedScalars.end()) {
                                                if(r.inherit.assign) {
                                                        return r.inherit.assign(instance, key, value, err);
                                                }
                                                if(err != nullptr) *err = Error::IdNotFound;
                                                return false;
                                        }
                                        if(!it->second.set) {
                                                if(err != nullptr) *err = Error::ReadOnly;
                                                return false;
                                        }
                                        Error setErr = it->second.set(instance, seg.index, value);
                                        if(setErr.isError()) {
                                                if(err != nullptr) *err = setErr;
                                                return false;
                                        }
                                        return true;
                                }
                                auto it = r.scalars.find(id);
                                if(it == r.scalars.end()) {
                                        if(r.inherit.assign) {
                                                return r.inherit.assign(instance, key, value, err);
                                        }
                                        if(err != nullptr) *err = Error::IdNotFound;
                                        return false;
                                }
                                if(!it->second.set) {
                                        if(err != nullptr) *err = Error::ReadOnly;
                                        return false;
                                }
                                Error setErr = it->second.set(instance, value);
                                if(setErr.isError()) {
                                        if(err != nullptr) *err = setErr;
                                        return false;
                                }
                                return true;
                        }

                        if(seg.hasIndex) {
                                auto it = r.indexedChildren.find(id);
                                if(it == r.indexedChildren.end()) {
                                        if(r.inherit.assign) {
                                                return r.inherit.assign(instance, key, value, err);
                                        }
                                        if(err != nullptr) *err = Error::IdNotFound;
                                        return false;
                                }
                                if(!it->second.assign) {
                                        if(err != nullptr) *err = Error::ReadOnly;
                                        return false;
                                }
                                return it->second.assign(instance, seg.index, seg.rest, value, err);
                        }

                        auto itChild = r.children.find(id);
                        if(itChild != r.children.end()) {
                                if(!itChild->second.assign) {
                                        if(err != nullptr) *err = Error::ReadOnly;
                                        return false;
                                }
                                return itChild->second.assign(instance, seg.rest, value, err);
                        }
                        auto itDb = r.databases.find(id);
                        if(itDb != r.databases.end()) {
                                if(!itDb->second.assign) {
                                        if(err != nullptr) *err = Error::ReadOnly;
                                        return false;
                                }
                                return itDb->second.assign(instance, seg.rest, value, err);
                        }
                        if(r.inherit.assign) {
                                return r.inherit.assign(instance, key, value, err);
                        }
                        if(err != nullptr) *err = Error::IdNotFound;
                        return false;
                }

                /**
                 * @brief Assigns @p value to a terminal scalar by @ref Key.
                 */
                static bool assign(T &instance, Key key, const Variant &value, Error *err = nullptr) {
                        if constexpr (detail::hasVariantLookupDispatchV<T>) {
                                String name = key.name();
                                if(!name.isEmpty()) {
                                        return instance.variantLookupAssign(name, value, err);
                                }
                                return assignKeyIdDirect(instance, key.id(), value, err);
                        } else {
                                return assignKeyIdDirect(instance, key.id(), value, err);
                        }
                }

                /**
                 * @brief Assigns @p value to a terminal indexed scalar by @ref Key.
                 */
                static bool assign(T &instance, Key key, size_t index, const Variant &value, Error *err = nullptr) {
                        return assignIndexedKeyIdDirect(instance, key.id(), index, value, err);
                }

                /**
                 * @brief Internal Key-id setter used by the @ref Key
                 *        overload and the inherit cascade.
                 */
                static bool assignKeyIdDirect(T &instance, uint64_t id, const Variant &value, Error *err = nullptr) {
                        if(err != nullptr) *err = Error::Ok;
                        const Registry &r = registry();
                        ReadWriteLock::ReadLocker lock(r.lock);
                        auto it = r.scalars.find(id);
                        if(it == r.scalars.end()) {
                                if(r.inherit.assignByKey) {
                                        return r.inherit.assignByKey(instance, id, value, err);
                                }
                                if(err != nullptr) *err = Error::IdNotFound;
                                return false;
                        }
                        if(!it->second.set) {
                                if(err != nullptr) *err = Error::ReadOnly;
                                return false;
                        }
                        Error setErr = it->second.set(instance, value);
                        if(setErr.isError()) {
                                if(err != nullptr) *err = setErr;
                                return false;
                        }
                        return true;
                }

                /**
                 * @brief Internal indexed-Key setter used by the
                 *        indexed @ref Key overload and the inherit
                 *        cascade.
                 */
                static bool assignIndexedKeyIdDirect(T &instance, uint64_t id, size_t index, const Variant &value, Error *err = nullptr) {
                        if(err != nullptr) *err = Error::Ok;
                        const Registry &r = registry();
                        ReadWriteLock::ReadLocker lock(r.lock);
                        auto it = r.indexedScalars.find(id);
                        if(it == r.indexedScalars.end()) {
                                if(r.inherit.assignIndexedByKey) {
                                        return r.inherit.assignIndexedByKey(instance, id, index, value, err);
                                }
                                if(err != nullptr) *err = Error::IdNotFound;
                                return false;
                        }
                        if(!it->second.set) {
                                if(err != nullptr) *err = Error::ReadOnly;
                                return false;
                        }
                        Error setErr = it->second.set(instance, index, value);
                        if(setErr.isError()) {
                                if(err != nullptr) *err = setErr;
                                return false;
                        }
                        return true;
                }

                // ============================================================
                // Spec discovery
                // ============================================================

                /**
                 * @brief Returns the declared @ref VariantSpec for @p key, or nullptr.
                 *
                 * Walks @p key with the same segment parser @ref resolve
                 * uses, but stops at the matched handler and returns the
                 * spec the handler was registered with (for terminal
                 * scalars / indexed scalars) or the spec recursively
                 * reported by the composed target (for @ref Registrar::child
                 * "child", @ref Registrar::indexedChild "indexedChild",
                 * and @ref Registrar::database "database").  Dispatches
                 * through the @c variantLookupSpecFor hook when @c T
                 * opts in, otherwise delegates to @ref specForDirect.
                 *
                 * @param key The full dotted key path.
                 * @param err Optional error output.  @c Error::Ok when a
                 *            handler matched (even if the handler has no
                 *            spec); @c Error::IdNotFound when no handler
                 *            matched or the terminal spec is missing;
                 *            @c Error::ParseFailed on malformed keys.
                 * @return The declared spec, or @c nullptr when none was
                 *         registered or the path cannot be resolved.
                 */
                static const VariantSpec *specFor(const String &key, Error *err = nullptr) {
                        if constexpr (detail::hasVariantLookupDispatchV<T>) {
                                // specFor dispatch needs an instance to
                                // land on the concrete type's
                                // VariantLookup — but specFor is a
                                // type-level query with no instance in
                                // hand.  Fall through to the static
                                // chain rooted at T; the cascade then
                                // climbs up through declared base
                                // classes, which is the correct answer
                                // for any key inherited from the base
                                // chain.  Keys registered strictly on
                                // a sibling subclass are
                                // type-dependent and out of scope for
                                // an instance-less spec lookup.
                                return specForDirect(key, err);
                        } else {
                                return specForDirect(key, err);
                        }
                }

                /**
                 * @brief Non-dispatching sibling of @ref specFor.
                 */
                static const VariantSpec *specForDirect(const String &key, Error *err = nullptr) {
                        if(err != nullptr) *err = Error::Ok;
                        detail::VariantLookupSegment seg;
                        if(!detail::parseLeadingSegment(key, seg, err)) return nullptr;

                        const Registry &r = registry();
                        ReadWriteLock::ReadLocker lock(r.lock);
                        const uint64_t id = r.findId(seg.name);
                        if(id == Registry::Invalid) {
                                if(r.inherit.specFor) {
                                        return r.inherit.specFor(key, err);
                                }
                                if(err != nullptr) *err = Error::IdNotFound;
                                return nullptr;
                        }

                        if(!seg.hasRest) {
                                if(seg.hasIndex) {
                                        auto it = r.indexedScalars.find(id);
                                        if(it == r.indexedScalars.end()) {
                                                if(r.inherit.specFor) {
                                                        return r.inherit.specFor(key, err);
                                                }
                                                if(err != nullptr) *err = Error::IdNotFound;
                                                return nullptr;
                                        }
                                        return it->second.spec;
                                }
                                auto it = r.scalars.find(id);
                                if(it == r.scalars.end()) {
                                        if(r.inherit.specFor) {
                                                return r.inherit.specFor(key, err);
                                        }
                                        if(err != nullptr) *err = Error::IdNotFound;
                                        return nullptr;
                                }
                                return it->second.spec;
                        }

                        if(seg.hasIndex) {
                                auto it = r.indexedChildren.find(id);
                                if(it == r.indexedChildren.end()) {
                                        if(r.inherit.specFor) {
                                                return r.inherit.specFor(key, err);
                                        }
                                        if(err != nullptr) *err = Error::IdNotFound;
                                        return nullptr;
                                }
                                return it->second.specFor(seg.rest, err);
                        }

                        auto itChild = r.children.find(id);
                        if(itChild != r.children.end()) {
                                return itChild->second.specFor(seg.rest, err);
                        }
                        auto itDb = r.databases.find(id);
                        if(itDb != r.databases.end()) {
                                return itDb->second.specFor(seg.rest, err);
                        }
                        if(r.inherit.specFor) {
                                return r.inherit.specFor(key, err);
                        }
                        if(err != nullptr) *err = Error::IdNotFound;
                        return nullptr;
                }

                /**
                 * @brief Returns the declared spec for a terminal @ref Key.
                 *
                 * Consults the terminal-scalar and indexed-scalar
                 * registries only, with inherit-cascade fallback via
                 * @ref specForKeyIdDirect; composed handlers need the
                 * @c String overload because their spec depends on the
                 * trailing path.
                 */
                static const VariantSpec *specFor(Key key, Error *err = nullptr) {
                        return specForKeyIdDirect(key.id(), err);
                }

                /**
                 * @brief Internal Key-id spec lookup used by the
                 *        @ref Key overload and the inherit cascade.
                 */
                static const VariantSpec *specForKeyIdDirect(uint64_t id, Error *err = nullptr) {
                        if(err != nullptr) *err = Error::Ok;
                        const Registry &r = registry();
                        ReadWriteLock::ReadLocker lock(r.lock);
                        auto it = r.scalars.find(id);
                        if(it != r.scalars.end()) return it->second.spec;
                        auto itIdx = r.indexedScalars.find(id);
                        if(itIdx != r.indexedScalars.end()) return itIdx->second.spec;
                        if(r.inherit.specForByKey) {
                                return r.inherit.specForByKey(id, err);
                        }
                        if(err != nullptr) *err = Error::IdNotFound;
                        return nullptr;
                }

                // ============================================================
                // Introspection
                // ============================================================

                /**
                 * @brief Returns the names of all registered terminal scalars.
                 *
                 * Includes scalars registered on any declared
                 * @ref Registrar::inheritsFrom base — the combined list
                 * is de-duplicated and sorted for stable diagnostics.
                 */
                static StringList registeredScalars() {
                        return mergeWithInherited(collectNames(registry().scalars),
                                registry().inherit.registeredScalars);
                }

                /** @brief Returns the names of all registered indexed scalars, inherit-aware. */
                static StringList registeredIndexedScalars() {
                        return mergeWithInherited(collectNames(registry().indexedScalars),
                                registry().inherit.registeredIndexedScalars);
                }

                /** @brief Returns the names of all registered child compositions, inherit-aware. */
                static StringList registeredChildren() {
                        return mergeWithInherited(collectNames(registry().children),
                                registry().inherit.registeredChildren);
                }

                /** @brief Returns the names of all registered indexed child compositions, inherit-aware. */
                static StringList registeredIndexedChildren() {
                        return mergeWithInherited(collectNames(registry().indexedChildren),
                                registry().inherit.registeredIndexedChildren);
                }

                /** @brief Returns the prefixes of all registered database bindings, inherit-aware. */
                static StringList registeredDatabases() {
                        return mergeWithInherited(collectNames(registry().databases),
                                registry().inherit.registeredDatabases);
                }

                /**
                 * @brief Iterates over the registered terminal scalars.
                 *
                 * @tparam Fn Callable with signature @c void(const String &name).
                 * @param fn Invoked once per scalar name.  Names are
                 *           emitted in lexicographic order for stable
                 *           dump output.
                 *
                 * @note Snapshots the registered names under a read
                 *       lock, releases the lock, then calls @p fn
                 *       outside the lock so @p fn is free to call
                 *       back into @ref resolve / @ref assign / etc.
                 *       This is safe under the documented
                 *       "registrations happen at static-init" contract;
                 *       concurrent registration during iteration is
                 *       undefined.
                 */
                template <typename Fn>
                static void forEachScalar(Fn &&fn) {
                        const Registry &r = registry();
                        StringList names;
                        std::function<void(const std::function<void(const String &)> &)> inheritForEach;
                        {
                                ReadWriteLock::ReadLocker lock(r.lock);
                                for(auto it = r.scalars.cbegin(); it != r.scalars.cend(); ++it) {
                                        auto n = r.names.find(it->first);
                                        if(n != r.names.end()) names.pushToBack(n->second);
                                }
                                inheritForEach = r.inherit.forEachScalar;
                        }
                        // Pull inherited names in through the fallback —
                        // this recurses transparently because
                        // @c VariantLookup<Base>::forEachScalar also
                        // consults its own inherit chain.
                        if(inheritForEach) {
                                inheritForEach([&names](const String &n) {
                                        names.pushToBack(n);
                                });
                        }
                        names = names.sort();
                        // De-duplicate — a derived class may shadow a
                        // base's scalar with the same name; emit only
                        // once so dumps don't print the key twice.
                        String prev;
                        for(const String &name : names) {
                                if(!prev.isEmpty() && name == prev) continue;
                                fn(name);
                                prev = name;
                        }
                }

                // ============================================================
                // Recursive dump
                // ============================================================

                /**
                 * @brief Returns a human-readable dump of every
                 *        reachable key on @p instance.
                 *
                 * Produces one line per terminal scalar (resolved
                 * through the polymorphic-dispatch @ref resolve so the
                 * most-derived registry wins), then each child
                 * composition as an @c "<Name>:" header followed by
                 * the indented dump of the child's type, then each
                 * database binding as an @c "<Prefix>:" header
                 * followed by its entries in @c "<Key> [<Type>]:
                 * <value>" form.  The inherit cascade is walked
                 * transparently — a derived lookup's dump includes
                 * every level of the hierarchy without duplicating
                 * names (@ref forEachScalar de-duplicates shadowed
                 * scalars).
                 *
                 * @param instance The target instance.
                 * @param indent   Leading whitespace applied to every
                 *                 emitted line.  Sub-levels receive
                 *                 @p indent @c + @c "  ".
                 * @return A @ref StringList of dump lines, one per
                 *         emitted key or header.  Empty when the
                 *         registry is empty.
                 */
                static StringList dump(const T &instance, const String &indent = String()) {
                        StringList out;
                        // Scalars — forEachScalar handles the cascade
                        // and de-duplication, resolve() handles the
                        // polymorphic-dispatch to the concrete type.
                        // Each line includes the Variant's type name
                        // in brackets so scalar output matches the
                        // @ref Metadata::dump / @ref VariantDatabase
                        // dump format (@c "<Key> [<Type>]: <Value>"),
                        // letting introspection consumers parse every
                        // line uniformly without branching on
                        // scalar-vs-db-entry.
                        forEachScalar([&out, &instance, &indent](const String &name) {
                                auto v = VariantLookup<T>::resolve(instance, name);
                                if(v.has_value()) {
                                        out.pushToBack(indent + name + " [" + v->typeName() + "]: " + v->format(String()));
                                }
                        });
                        // Composites — children, indexed children, and
                        // databases — cascade via dumpComposites so a
                        // base class's Desc / Meta bindings land in
                        // the derived dump too.
                        StringList composites = dumpComposites(instance, indent);
                        for(const String &l : composites) out.pushToBack(l);
                        return out;
                }

                /**
                 * @brief Non-scalar half of @ref dump — walks
                 *        children, indexed children, and databases.
                 *
                 * Split out so the inherit cascade can stitch a
                 * derived type's composites onto the base's without
                 * re-emitting scalars (those already cascade through
                 * @ref forEachScalar).
                 */
                static StringList dumpComposites(const T &instance, const String &indent = String()) {
                        StringList out;

                        // Snapshot per-entry dump lambdas + names under
                        // the read lock so we can drop the lock before
                        // recursing into VariantLookup<U>::dump (which
                        // takes its own VariantLookup<U> registry lock;
                        // holding two locks invites hidden deadlocks as
                        // the hierarchy grows).
                        struct Item {
                                String name;
                                std::function<StringList(const T &, const String &)> dump;
                        };
                        List<Item> children;
                        List<Item> indexedChildren;
                        List<Item> databases;
                        std::function<StringList(const T &, const String &)> inheritFn;
                        {
                                const Registry &r = registry();
                                ReadWriteLock::ReadLocker lock(r.lock);
                                for(auto it = r.children.cbegin(); it != r.children.cend(); ++it) {
                                        if(!it->second.dump) continue;
                                        auto n = r.names.find(it->first);
                                        if(n == r.names.end()) continue;
                                        children.pushToBack(Item{ n->second, it->second.dump });
                                }
                                for(auto it = r.indexedChildren.cbegin(); it != r.indexedChildren.cend(); ++it) {
                                        if(!it->second.dump) continue;
                                        auto n = r.names.find(it->first);
                                        if(n == r.names.end()) continue;
                                        indexedChildren.pushToBack(Item{ n->second, it->second.dump });
                                }
                                for(auto it = r.databases.cbegin(); it != r.databases.cend(); ++it) {
                                        if(!it->second.dump) continue;
                                        auto n = r.names.find(it->first);
                                        if(n == r.names.end()) continue;
                                        databases.pushToBack(Item{ n->second, it->second.dump });
                                }
                                inheritFn = r.inherit.dumpComposites;
                        }

                        const String sub = indent + "  ";

                        auto emit = [&out, &indent, &sub](
                                        const String &name, const StringList &lines) {
                                if(lines.isEmpty()) return;
                                out.pushToBack(indent + name + ":");
                                for(const String &l : lines) out.pushToBack(l);
                        };

                        for(const Item &c : children) {
                                emit(c.name, c.dump(instance, sub));
                        }
                        for(const Item &c : indexedChildren) {
                                emit(c.name, c.dump(instance, sub));
                        }
                        for(const Item &d : databases) {
                                emit(d.name, d.dump(instance, sub));
                        }

                        // Pick up composites declared on the inherited
                        // base so a derived dump is truly transitive.
                        if(inheritFn) {
                                StringList inherited = inheritFn(instance, indent);
                                for(const String &l : inherited) out.pushToBack(l);
                        }
                        return out;
                }

                // ============================================================
                // Template-string formatting
                // ============================================================

                /**
                 * @brief Substitutes @c {Key[:spec]} placeholders against @p instance.
                 *
                 * Walks @p tmpl character by character.  Each balanced
                 * @c {…} token is split on the first @c ':' into a key
                 * name and an optional format spec; the key is resolved
                 * via @ref resolve and the returned @ref Variant is
                 * rendered via @ref Variant::format.  Literal braces
                 * are escaped as @c "{{" and @c "}}", matching the
                 * @c std::format convention.
                 *
                 * When @ref resolve returns @c std::nullopt, the optional
                 * @p resolver callback is consulted with @c (keyName, spec).
                 * A returned @c std::optional<String> holding a value is
                 * substituted as-is.  A returned @c std::nullopt (or an
                 * absent resolver) leaves the key unresolved: the output
                 * gets the literal text @c "[UNKNOWN KEY: <name>]" and
                 * @p err is set to @c Error::IdNotFound.
                 *
                 * @tparam Resolver Callable with signature
                 *                  @c std::optional<String>(const String &, const String &).
                 *                  Pass @c nullptr to skip the fallback.
                 * @param instance The target instance.
                 * @param tmpl     Template string with @c {Key[:spec]} placeholders.
                 * @param resolver Optional fallback resolver.
                 * @param err      Optional error output, set to
                 *                 @c Error::Ok on full resolution or
                 *                 @c Error::IdNotFound if any key fell
                 *                 through unresolved.
                 */
                template <typename Resolver>
                static String format(const T &instance, const String &tmpl,
                                     Resolver &&resolver, Error *err = nullptr) {
                        if(err != nullptr) *err = Error::Ok;
                        std::string out;
                        out.reserve(tmpl.byteCount());
                        const char *src = tmpl.cstr();
                        const size_t len = tmpl.byteCount();
                        bool sawUnresolved = false;
                        size_t i = 0;
                        while(i < len) {
                                char c = src[i];
                                if(c == '{') {
                                        if(i + 1 < len && src[i + 1] == '{') {
                                                out.push_back('{');
                                                i += 2;
                                                continue;
                                        }
                                        size_t end = i + 1;
                                        while(end < len && src[end] != '}') ++end;
                                        if(end >= len) {
                                                out.append(src + i, len - i);
                                                break;
                                        }
                                        std::string_view body(src + i + 1, end - (i + 1));
                                        size_t colon = body.find(':');
                                        std::string_view keyView = (colon == std::string_view::npos)
                                                ? body : body.substr(0, colon);
                                        std::string_view specView = (colon == std::string_view::npos)
                                                ? std::string_view() : body.substr(colon + 1);
                                        String keyName(keyView.data(), keyView.size());
                                        String specStr(specView.data(), specView.size());
                                        auto v = resolve(instance, keyName);
                                        if(v.has_value()) {
                                                String rendered = v->format(specStr);
                                                out.append(rendered.cstr(), rendered.byteCount());
                                        } else {
                                                std::optional<String> resolved;
                                                if constexpr (!std::is_same_v<std::decay_t<Resolver>, std::nullptr_t>) {
                                                        resolved = resolver(keyName, specStr);
                                                }
                                                if(resolved.has_value()) {
                                                        const String &r = *resolved;
                                                        out.append(r.cstr(), r.byteCount());
                                                } else {
                                                        sawUnresolved = true;
                                                        out += "[UNKNOWN KEY: ";
                                                        out.append(keyView.data(), keyView.size());
                                                        out += ']';
                                                }
                                        }
                                        i = end + 1;
                                } else if(c == '}') {
                                        if(i + 1 < len && src[i + 1] == '}') {
                                                out.push_back('}');
                                                i += 2;
                                        } else {
                                                out.push_back('}');
                                                ++i;
                                        }
                                } else {
                                        out.push_back(c);
                                        ++i;
                                }
                        }
                        if(sawUnresolved && err != nullptr) *err = Error::IdNotFound;
                        return String(std::move(out));
                }

                /**
                 * @brief Convenience overload of @ref format with no resolver callback.
                 *
                 * Equivalent to calling the resolver overload with @c nullptr.
                 * Missing keys produce @c "[UNKNOWN KEY: <name>]" in the
                 * output and set @p err to @c Error::IdNotFound.
                 */
                static String format(const T &instance, const String &tmpl, Error *err = nullptr) {
                        return format(instance, tmpl, nullptr, err);
                }

        private:
                struct ScalarEntry {
                        ScalarGet           get;
                        ScalarSet           set;
                        const VariantSpec * spec = nullptr;
                };
                struct IndexedScalarEntry {
                        IndexedScalarGet    get;
                        IndexedScalarSet    set;
                        const VariantSpec * spec = nullptr;
                };
                struct ComposeEntry {
                        ComposeResolve   resolve;
                        ComposeAssign    assign;
                        ComposeSpecFor   specFor;
                        /**
                         * @brief Optional "dump this composition" hook.
                         *
                         * Drives @ref dump.  For a @ref Registrar::child
                         * "child" the lambda forwards to
                         * @c VariantLookup<U>::dump on the borrowed
                         * child.  For a @ref Registrar::database
                         * "database" it iterates the bound
                         * @ref VariantDatabase and formats each entry
                         * the same way @c Metadata::dump does.  Left
                         * empty when the entry pre-dates the dump
                         * feature; in that case @ref dump skips it.
                         */
                        std::function<StringList(const T &, const String &)> dump;
                };
                struct IndexedCompEntry {
                        IndexedCompResolve resolve;
                        IndexedCompAssign  assign;
                        ComposeSpecFor     specFor;
                        /**
                         * @brief Optional "dump all elements" hook.
                         *
                         * Invoked by @ref dump to walk an indexed
                         * child composition by probing indices 0..N
                         * until @c get returns @c nullptr.  The
                         * lambda stops at the first missing index,
                         * which yields a compact "print everything
                         * present, ignore absent slots" behaviour
                         * without needing a separate size oracle.
                         */
                        std::function<StringList(const T &, const String &)> dump;
                };

                /**
                 * @brief Cascade fallback for @ref Registrar::inheritsFrom.
                 *
                 * Each member is either empty (no inheritance declared)
                 * or forwards to @c VariantLookup<Base>::*Direct with the
                 * instance upcast to @c Base.  @ref resolveDirect and
                 * peers consult these after their own maps miss, so a
                 * derived class gets its base's keys for free without
                 * re-registering.
                 */
                struct InheritEntry {
                        std::function<std::optional<Variant>(const T &, const String &, Error *)> resolve;
                        std::function<bool(T &, const String &, const Variant &, Error *)>        assign;
                        std::function<const VariantSpec *(const String &, Error *)>               specFor;
                        std::function<std::optional<Variant>(const T &, uint64_t, Error *)>       resolveByKey;
                        std::function<bool(T &, uint64_t, const Variant &, Error *)>              assignByKey;
                        std::function<const VariantSpec *(uint64_t, Error *)>                     specForByKey;
                        std::function<std::optional<Variant>(const T &, uint64_t, size_t, Error *)> resolveIndexedByKey;
                        std::function<bool(T &, uint64_t, size_t, const Variant &, Error *)>      assignIndexedByKey;
                        std::function<void(const std::function<void(const String &)> &)>         forEachScalar;
                        std::function<StringList()>                                               registeredScalars;
                        std::function<StringList()>                                               registeredIndexedScalars;
                        std::function<StringList()>                                               registeredChildren;
                        std::function<StringList()>                                               registeredIndexedChildren;
                        std::function<StringList()>                                               registeredDatabases;
                        /**
                         * @brief Inherit-cascade hook for @ref dump.
                         *
                         * Returns every line that @c VariantLookup<Base>::dumpComposites
                         * would emit on the upcast instance, so
                         * @ref dump can walk up through child and
                         * database bindings declared on the base
                         * without re-emitting scalars (those already
                         * cascade via @ref forEachScalar).
                         */
                        std::function<StringList(const T &, const String &)>                     dumpComposites;
                };

                /**
                 * @brief Per-type storage for the VariantLookup handler set.
                 *
                 * All handlers hash their registered name through
                 * @ref fnv1a and key into the kind-specific map on the
                 * resulting @c uint64_t.  The @ref names map preserves
                 * the string form for introspection and key-equality
                 * diagnostics.  Registration uses a strict-hash path
                 * (like @ref VariantDatabase::declareID): a hash
                 * collision between two distinct names aborts at
                 * static-init time rather than silently diverging.
                 */
                struct Registry {
                        static constexpr uint64_t Invalid = UINT64_MAX;

                        Map<uint64_t, String>           names;
                        Map<uint64_t, ScalarEntry>      scalars;
                        Map<uint64_t, IndexedScalarEntry> indexedScalars;
                        Map<uint64_t, ComposeEntry>     children;
                        Map<uint64_t, IndexedCompEntry> indexedChildren;
                        Map<uint64_t, ComposeEntry>     databases;
                        InheritEntry                    inherit;
                        mutable ReadWriteLock           lock;

                        /**
                         * @brief Registers @p name and returns its integer ID.
                         *
                         * Caller must hold @ref lock for writing.  The
                         * name is stored in @ref names so introspection
                         * and @ref Key::name can find it later.  A hash
                         * collision with a previously-registered
                         * different name logs and aborts — the same
                         * safety guarantee @ref VariantDatabase uses for
                         * well-known IDs.
                         */
                        uint64_t declareName(const String &name) {
                                const uint64_t h = fnv1a(name.cstr());
                                auto it = names.find(h);
                                if(it != names.end()) {
                                        if(it->second == name) return h;
                                        detail::stringRegistryCollisionAbort(
                                                typeid(T).name(),
                                                it->second, name, h);
                                }
                                names.insert(h, name);
                                return h;
                        }

                        /**
                         * @brief Looks up @p name without registering it.
                         *
                         * Returns @ref Invalid when the name has never
                         * been registered with any kind-map on @c T.
                         * Caller must hold @ref lock (read lock is
                         * sufficient).
                         */
                        uint64_t findId(const String &name) const {
                                const uint64_t h = fnv1a(name.cstr());
                                auto it = names.find(h);
                                if(it == names.end()) return Invalid;
                                if(it->second != name) return Invalid;
                                return h;
                        }
                };

                static Registry &registry() {
                        static Registry r;
                        return r;
                }

                /**
                 * @brief Helper that collects registered names out of a kind-map.
                 *
                 * Iterates the supplied map under the shared read lock
                 * and joins each integer ID with its stored name from
                 * @ref Registry::names.  Names are returned
                 * sorted so callers get stable output for dumps and
                 * diagnostics.
                 */
                template <typename MapT>
                static StringList collectNames(const MapT &m) {
                        const Registry &r = registry();
                        ReadWriteLock::ReadLocker lock(r.lock);
                        StringList out;
                        for(auto it = m.cbegin(); it != m.cend(); ++it) {
                                auto n = r.names.find(it->first);
                                if(n != r.names.end()) out.pushToBack(n->second);
                        }
                        out = out.sort();
                        return out;
                }

                /**
                 * @brief Merges an own-registry name list with one
                 *        supplied by the inherit cascade.
                 *
                 * Keeps the combined list sorted and de-duplicated so
                 * a name registered on both derived and base appears
                 * once in introspection output.
                 */
                static StringList mergeWithInherited(StringList ownNames,
                                                     const std::function<StringList()> &inheritFn) {
                        if(!inheritFn) return ownNames;
                        StringList inherited = inheritFn();
                        for(const String &n : inherited) ownNames.pushToBack(n);
                        ownNames = ownNames.sort();
                        StringList out;
                        String prev;
                        for(const String &n : ownNames) {
                                if(!prev.isEmpty() && n == prev) continue;
                                out.pushToBack(n);
                                prev = n;
                        }
                        return out;
                }
};

PROMEKI_NAMESPACE_END

/**
 * @brief Registers VariantLookup handlers for a type.
 * @ingroup util
 *
 * Expands to a uniquely-named @c static @c inline constant that captures
 * the final @ref promeki::VariantLookup::Registrar "Registrar" value from
 * a fluent chain.  The side effects (inserting handlers into the per-@c T
 * registry) happen during static-init, so the macro must appear at
 * namespace or class scope.
 *
 * @note Must be invoked from inside @c PROMEKI_NAMESPACE_BEGIN /
 *       @c PROMEKI_NAMESPACE_END (the usual case for @c .cpp files that
 *       register against their own types).  Invocation at global scope
 *       places the generated registration variable in the global
 *       namespace, which works but pollutes the global scope with
 *       implementation-detail names.
 *
 * @par Example
 * @code
 * PROMEKI_LOOKUP_REGISTER(Image)
 *         .scalar("Width",  [](const Image &i){ return Variant(i.desc().width()); })
 *         .scalar("Height", [](const Image &i){ return Variant(i.desc().height()); })
 *         .database<"Metadata">("Meta",
 *                 [](const Image &i){ return &i.desc().metadata(); });
 * @endcode
 */
#define PROMEKI_LOOKUP_REGISTER(Type)                                   \
        [[maybe_unused]] static inline const                            \
        ::promeki::VariantLookup<Type>::Registrar                       \
        PROMEKI_CONCAT(_promeki_lookup_reg_, __COUNTER__) =             \
                ::promeki::VariantLookup<Type>::registrar()
