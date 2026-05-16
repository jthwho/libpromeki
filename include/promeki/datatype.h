/**
 * @file      datatype.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Triggers one-time registration of the builtin Variant alternatives.
 *
 * Defined in @c variant.cpp; called lazily from @ref DataType::of<T> so
 * that any caller whose static initialization runs before variant.cpp's
 * own static-init sequence (e.g. @ref VariantSpec default values
 * declared at namespace scope) still observes the builtin DataType
 * records on first access.  The function is internally synchronized
 * and idempotent — second and subsequent calls are effectively free.
 */
void ensureBuiltinDataTypesRegistered();

/**
 * @brief Runtime metadata describing one C++ type as known to Variant and DataStream.
 * @ingroup util
 *
 * DataType uses the @ref typeregistry "TypeRegistry pattern": a lightweight
 * value-type handle wrapping a pointer to an immutable @ref Data record.
 * Each registered C++ type owns exactly one Data record, which carries:
 *
 * - a 16-bit @ref ID drawn from the same numbering space as
 *   @ref DataStream::TypeId — library types live in @c 0x0001 – @c 0x3FFF,
 *   user-defined types in @c 0x4000 – @c 0xFFFF (see @ref UserBegin /
 *   @ref UserEnd);
 * - a human-readable name (e.g. @c "String", @c "Timecode", @c "MyType");
 * - size / alignment information so callers can construct trailing-storage
 *   payloads without knowing the type statically;
 * - a @c std::type_index for the originating C++ type, so Variant's
 *   @c get<T>() / @c peek<T>() can map a static C++ type back to its
 *   registered ID;
 * - an @ref Ops table of function pointers (copy / move / destroy / equal /
 *   toString / fromString / writeStream / readStream) populated automatically
 *   by @ref registerType from whichever well-known operations the type
 *   provides — see the concept list under @ref Detail.
 *
 * A registered type is identified by a small integer; Variant stores that
 * integer (and a pointer to its trailing payload) rather than a closed
 * @c std::variant alternative list.  Adding a new type to the system is
 * therefore a one-time call to @ref registerType (typically via the
 * @ref PROMEKI_IMPLEMENT_DATATYPE macro at the bottom of the owning
 * header / .cpp) and does not require recompiling @c variant.h.
 *
 * @par DataStream interoperability
 * The @ref ID enum is a type-alias of @ref DataStream::TypeId, so the
 * 16-bit tag in a DataStream frame header is the same integer that
 * Variant uses internally.  This means a Variant holding @c MyType
 * serializes to the same wire bytes as a free @c operator<<(DataStream &,
 * const MyType &) — no extra dispatch layer, no parallel registry.
 *
 * @par Registration
 * Use the @ref PROMEKI_DECLARE_DATATYPE / @ref PROMEKI_IMPLEMENT_DATATYPE
 * macros for normal types whose @c Ops can be derived from well-known
 * operations.  Use @ref PROMEKI_IMPLEMENT_DATATYPE_ID when the wire-format
 * tag must be stable across builds (mandatory for library builtins,
 * recommended for user types that travel across persisted streams).
 *
 * @par Thread Safety
 * Distinct DataType instances may be used concurrently — each is just a
 * pointer to an immutable record.  The static registry (registerType,
 * byId, byName, of) is internally synchronized and safe to call from
 * any thread.
 *
 * @par Example
 * @code
 * // Header — declare that MyType participates in the registry.
 * class MyType {
 *     public:
 *         String toString() const;
 *         static Result<MyType> fromString(const String &s);
 *         bool   operator==(const MyType &o) const;
 *         // ...
 * };
 * PROMEKI_DECLARE_DATATYPE(MyType)
 *
 * // One .cpp — register it.  Ops are concept-detected automatically.
 * PROMEKI_IMPLEMENT_DATATYPE(MyType, "MyType")
 *
 * // Anywhere — query.
 * DataType dt = DataType::of<MyType>();
 * promekiInfo("registered %s with id 0x%04x", dt.name(), dt.id());
 * @endcode
 *
 * @see DataStream::TypeId for the wire-format tag namespace.
 * @see @ref typeregistry "TypeRegistry Pattern" for the design pattern.
 */
class DataType {
        public:
                /**
                 * @brief Stable 16-bit identifier for a registered type.
                 *
                 * Alias of @ref DataStream::TypeId so the Variant runtime
                 * tag and the DataStream wire-format tag are literally
                 * the same value.  Library builtins are pinned to the
                 * IDs listed in DataStream::TypeId; user-defined types
                 * occupy @c UserBegin – @c UserEnd.
                 */
                using ID = DataStream::TypeId;

                /**
                 * @brief Per-type function-pointer table populated at registration.
                 *
                 * Slots that have a corresponding well-known operation on
                 * the C++ type (e.g. @c operator==) are filled in
                 * automatically by @ref registerType via the concept
                 * detectors in @ref Detail.  Slots without a matching
                 * operation are left @c nullptr; consumers must check
                 * before calling.
                 *
                 * @par Slot semantics
                 * - @c defaultConstruct(p) — placement-new a default-constructed
                 *   T at @c p.  Populated when @c T is
                 *   default-constructible; null otherwise.  Used by
                 *   @ref Variant::createDefault and by paths that need
                 *   to materialise a typed value with no source bytes
                 *   in hand (e.g. DataStream read landing zone).
                 * - @c copyConstruct(dst,src) — placement-new a copy of
                 *   @c *src into the raw storage at @c dst.  Always
                 *   populated; types without a copy constructor cannot
                 *   be registered.
                 * - @c moveConstruct(dst,src) — placement-new a move-from
                 *   of @c *src into @c dst.  Always populated.
                 * - @c destroy(p) — invoke @c ~T() on the object at @c p.
                 *   Always populated.
                 * - @c equal(a,b) — return @c true when @c *a and @c *b
                 *   compare equal.  Populated when @c T has
                 *   @c operator==.
                 * - @c toString(p,err) — produce a String representation
                 *   of @c *p.  Populated when @c T has a member
                 *   @c toString().
                 * - @c fromString(s,out,err) — parse @c s into @c *out.
                 *   Auto-populated by @ref makeDefaultOps when @p T
                 *   satisfies @ref Detail::HasResultFromString (i.e.
                 *   provides @c static @c Result<T> @c T::fromString(const
                 *   @c String &)).  Types with a bespoke @c fromString shape
                 *   (e.g. an extra format argument) can opt in via
                 *   @ref Detail::HasFreeFromString.  Returns @c true on
                 *   success.
                 * - @c writeStream(s,p) — write @c *p to @c s using the
                 *   free @c operator<<(DataStream &, const T &).
                 *   Populated when that overload exists.
                 * - @c readStream(s,p) — read @c *p from @c s using the
                 *   free @c operator>>(DataStream &, T &).  Populated
                 *   when that overload exists.
                 */
                struct Ops {
                        void   (*defaultConstruct)(void *p)                 = nullptr;
                        void   (*copyConstruct)(void *dst, const void *src) = nullptr;
                        void   (*moveConstruct)(void *dst, void *src)       = nullptr;
                        void   (*destroy)(void *p)                          = nullptr;
                        bool   (*equal)(const void *a, const void *b)       = nullptr;
                        String (*toString)(const void *p, Error *err)       = nullptr;
                        bool   (*fromString)(const String &s, void *out, Error *err) = nullptr;
                        void   (*writeStream)(DataStream &s, const void *p) = nullptr;
                        void   (*readStream)(DataStream &s, void *p)        = nullptr;
                };

                /**
                 * @brief Immutable descriptor for one registered type.
                 *
                 * Lives in a static registry indexed by @c id (and
                 * cross-indexed by name and @c cppType).  Populated
                 * by @ref registerType and never mutated thereafter,
                 * so DataType handles can be copied and shared
                 * across threads without synchronization.
                 */
                struct Data {
                        ID              id      = DataType::NoType;
                        const char     *name    = "Invalid";
                        size_t          size    = 0;
                        size_t          align   = 1;
                        std::type_index cppType = std::type_index(typeid(void));
                        Ops             ops;
                };

                /**
                 * @brief Sentinel @ref ID meaning "no registered type".
                 *
                 * Distinct from @c DataStream::TypeInvalid (@c 0x0E), which is
                 * itself a valid registered wire-format tag denoting a Variant
                 * that explicitly holds an "invalid" value.  @c NoType (the
                 * zero value, never assigned to any registration) is the
                 * out-of-band marker used by:
                 *  - default-constructed @ref DataType handles;
                 *  - the @p preferredId argument to @ref registerType meaning
                 *    "auto-allocate from @ref UserBegin";
                 *  - @ref byId / @ref byName when no record matches.
                 */
                static constexpr ID NoType = static_cast<ID>(0);

                /** @brief First @ref ID value available for user-defined types. */
                static constexpr ID UserBegin = static_cast<ID>(DataStream::UserTypeIdBegin);

                /** @brief Largest legal @ref ID for user-defined types (inclusive). */
                static constexpr ID UserEnd = static_cast<ID>(DataStream::UserTypeIdEnd);

                /** @brief Default-constructs an invalid handle (@c isValid() returns false). */
                DataType() = default;

                /** @brief Wraps an existing immutable record; primarily for the registry to construct handles. */
                explicit DataType(const Data *d) : _data(d) { return; }

                /**
                 * @brief Looks up the registered type with @p id.
                 *
                 * Equivalent to @ref byId(id); leaves @c isValid() false
                 * when no type is registered under @p id.  This constructor
                 * is implicit so existing @c switch / comparison code
                 * that uses raw IDs continues to work unchanged once
                 * call-site parameters are migrated from @c ID to
                 * @c const DataType &.
                 *
                 * @param id  The wire-format tag to resolve.
                 */
                DataType(ID id);

                /** @brief Returns true when this handle refers to a registered type. */
                bool isValid() const { return _data != nullptr; }

                /** @brief Returns the type's stable @ref ID, or @c DataType::NoType for an invalid handle. */
                ID id() const { return _data != nullptr ? _data->id : DataType::NoType; }

                /** @brief Returns the registered name, or @c "Invalid" for an invalid handle. */
                const char *name() const { return _data != nullptr ? _data->name : "Invalid"; }

                /** @brief Returns @c sizeof(T) for the underlying C++ type, or @c 0 for an invalid handle. */
                size_t size() const { return _data != nullptr ? _data->size : 0; }

                /** @brief Returns @c alignof(T) for the underlying C++ type, or @c 1 for an invalid handle. */
                size_t alignment() const { return _data != nullptr ? _data->align : 1; }

                /** @brief Returns the type_index for the underlying C++ type (typeid(void) when invalid). */
                std::type_index cppType() const {
                        return _data != nullptr ? _data->cppType : std::type_index(typeid(void));
                }

                /**
                 * @brief Returns the function-pointer table.
                 *
                 * Aborts when called on an invalid handle (no Data is
                 * attached, so there are no ops to return).  Check
                 * @ref isValid first when you don't statically know
                 * the handle is good.
                 */
                const Ops &ops() const;

                /** @brief Returns the underlying immutable record, or @c nullptr for an invalid handle. */
                const Data *data() const { return _data; }

                /** @brief Two handles are equal iff they refer to the same registered record. */
                bool operator==(const DataType &o) const { return _data == o._data; }
                /** @brief Inequality counterpart of @ref operator==. */
                bool operator!=(const DataType &o) const { return _data != o._data; }

                /**
                 * @brief Registers @p T under @p name, populating its @ref Ops via concept detection.
                 *
                 * The returned handle is stable for the lifetime of the
                 * process and may be cached.  If @p preferredId is
                 * @c DataType::NoType (the default), an ID is
                 * auto-allocated from the user range starting at
                 * @ref UserBegin.  Otherwise the registration is pinned
                 * to @p preferredId — required for library builtins
                 * (so the wire format is stable) and recommended for
                 * any user type that needs DataStream wire stability.
                 *
                 * Registration is rejected (an invalid handle is
                 * returned) when:
                 *  - @p preferredId is already in use by a different
                 *    C++ type;
                 *  - @p T has already been registered under a different
                 *    name or ID;
                 *  - @p preferredId falls outside the legal user range
                 *    but does not correspond to a known library type.
                 *
                 * @tparam T          The C++ type to register.  Must be
                 *                    copyable, movable, and destructible.
                 * @param name        Human-readable type name; stored by
                 *                    pointer, so it must outlive the
                 *                    process (string literal is the norm).
                 * @param preferredId Either @c DataType::NoType
                 *                    (auto-allocate from @ref UserBegin)
                 *                    or a specific ID to pin.
                 * @return            A handle to the registered record,
                 *                    or an invalid handle on rejection.
                 */
                template <typename T>
                static DataType registerType(const char *name, ID preferredId = DataType::NoType);

                /**
                 * @brief Low-level registration entry point used by @ref registerType<T>.
                 *
                 * Callers normally use the templated overload, which
                 * derives @p size, @p align, @p cppType, and @p ops
                 * automatically.  This overload exists for test fixtures
                 * and any caller that wants to override the auto-derived
                 * ops table (e.g. to provide a custom @c toString).
                 *
                 * @param name        Human-readable type name; pointer-stored.
                 * @param ti          C++ type_index for the type.
                 * @param size        @c sizeof(T).
                 * @param align       @c alignof(T).
                 * @param ops         Populated function-pointer table.
                 * @param preferredId Either @c DataType::NoType
                 *                    or a pinned ID.
                 * @return            A handle to the registered record,
                 *                    or an invalid handle on rejection.
                 */
                static DataType registerType(const char *name, std::type_index ti, size_t size, size_t align,
                                             Ops ops, ID preferredId = DataType::NoType);

                /**
                 * @brief Returns the IDs of every registered type, in ascending order.
                 *
                 * Useful for generic round-trip tests, introspection
                 * tools, and any code that needs to enumerate the
                 * full type universe at runtime.  The snapshot is
                 * taken under the registry lock; subsequent
                 * registrations are not reflected.
                 */
                static List<ID> registeredIds();

                /** @brief Looks up the registered type with @p id; returns an invalid handle when absent. */
                static DataType byId(ID id);

                /** @brief Looks up by name; returns an invalid handle when absent.  Comparison is case-sensitive. */
                static DataType byName(const char *name);

                /**
                 * @brief Looks up the DataType for the given C++ type.
                 *
                 * Resolves @c std::type_index(typeid(T)) against the
                 * registry's C++-type index.  Requires @p T to have been
                 * registered via @ref registerType (typically through
                 * @ref PROMEKI_IMPLEMENT_DATATYPE).  Returns an invalid
                 * handle when no registration exists.
                 *
                 * Triggers @ref ensureBuiltinDataTypesRegistered before
                 * doing the lookup, so that consumers whose static
                 * initialization runs before @c variant.cpp's (e.g.
                 * VariantSpec-based config tables) still see the
                 * builtin types registered on their first access.
                 *
                 * @tparam T  The C++ type whose DataType should be returned.
                 */
                template <typename T> static DataType of() {
                        ensureBuiltinDataTypesRegistered();
                        return byCppType(std::type_index(typeid(T)));
                }

                /** @brief Looks up by C++ type_index; returns invalid handle when absent. */
                static DataType byCppType(std::type_index ti);

        private:
                const Data *_data = nullptr;
};

/** @brief Concept detectors and template helpers used by @ref DataType registration. */
namespace Detail {

template <typename T>
concept HasEqualityOp = requires(const T &a, const T &b) {
        { a == b } -> std::convertible_to<bool>;
};

template <typename T>
concept HasMemberToString = requires(const T &t) {
        { t.toString() } -> std::convertible_to<String>;
};

/**
 * @brief Detects @c static Result<T> T::fromString(const String &).
 *
 * The standard parse-method shape across the codebase (see
 * @ref CODING_STANDARDS.md "Error Handling — Return Patterns").  When
 * a type satisfies this concept, @ref makeDefaultOps populates the
 * @c fromString slot in the type's @ref Ops table so the converter
 * registry and other generic consumers can parse it without per-type
 * dispatch.
 *
 * Types whose @c fromString shape is bespoke (e.g.
 * @c DateTime::fromString with a mandatory format argument, or
 * TypeRegistry wrappers that publish @c lookup() instead) opt in via
 * the @ref HasFreeFromString trait specialization, exactly like
 * @ref HasFreeDataStreamWrite handles free DataStream operators.
 */
template <typename T>
concept HasResultFromString = requires(const String &s) {
        { T::fromString(s) } -> std::convertible_to<Result<T>>;
};

/**
 * @brief Specializable opt-in trait for types whose @c fromString lives
 *        outside the @c Result<T>(String) shape detected by
 *        @ref HasResultFromString.
 *
 * Specialize as @c std::true_type and provide a free overload
 * @c bool T_fromStringThunk(const String &, T &, Error *) in the same
 * namespace as @p T (or in @c promeki::Detail) — the registry will
 * route the @c fromString slot through that overload.  Use sparingly;
 * the conformant shape should always be preferred.
 */
template <typename T> struct HasFreeFromString : std::false_type {};

/** @brief True when @p T has any populated @c fromString path. */
template <typename T>
inline constexpr bool HasFromStringV = HasResultFromString<T> || HasFreeFromString<T>::value;

/**
 * @brief Detects an *exact-match* @c operator<<(DataStream &, const T &)
 *        without falling back to the @ref Variant converting constructor.
 *
 * Plain expression-based requires-clauses (@c s @c << @c v) silently
 * match the @c operator<<(DataStream&, const Variant&) overload via
 * @c T's implicit conversion to @c Variant, which would let the
 * default-ops factory generate a @c writeStream slot that recurses
 * straight back into itself.  The pointer-cast technique below picks
 * exactly one overload by signature, so a type without its own
 * operator yields a substitution failure and the slot stays @c nullptr.
 *
 * Supports four call shapes:
 *  - Member: @c DataStream::operator<<(const T &)
 *  - Member: @c DataStream::operator<<(T) (by-value, used by primitives)
 *  - Free:   @c operator<<(DataStream &, const T &) (promeki namespace)
 *  - Free:   @c operator<<(DataStream &, T) (rare; future-proofing)
 */
template <typename T, typename = void>
struct ExactDataStreamWrite : std::false_type {};

template <typename T>
struct ExactDataStreamWrite<T, std::void_t<decltype(static_cast<DataStream &(DataStream::*)(const T &)>(
                                          &DataStream::operator<<))>> : std::true_type {};

template <typename T>
struct ExactDataStreamWrite<T, std::void_t<decltype(static_cast<DataStream &(DataStream::*)(T)>(
                                          &DataStream::operator<<))>> : std::true_type {};

template <typename T>
inline constexpr bool ExactDataStreamWriteV = ExactDataStreamWrite<T>::value;

/**
 * @brief Detects an *exact-match* @c operator>>(DataStream &, T &)
 *        for the read direction.  Same rationale as
 *        @ref ExactDataStreamWrite — the @c Variant fallback would
 *        otherwise mask missing per-type read operators and recurse.
 */
template <typename T, typename = void>
struct ExactDataStreamRead : std::false_type {};

template <typename T>
struct ExactDataStreamRead<T, std::void_t<decltype(static_cast<DataStream &(DataStream::*)(T &)>(
                                          &DataStream::operator>>))>> : std::true_type {};

template <typename T>
inline constexpr bool ExactDataStreamReadV = ExactDataStreamRead<T>::value;

/**
 * @brief Trait specializable to mark a type as serializable through a
 *        free-function @c operator<<(DataStream &, const T &).
 *
 * Library code in @c variant.cpp specializes this for the types whose
 * DataStream operator lives outside of @c DataStream itself
 * (Size2DTemplate, Rational, VariantList, MasteringDisplay, ...).
 * Concept-based detection cannot match free operators safely because
 * the @c Variant converting constructor masks the substitution
 * failure, so the explicit specialization here is what tells
 * @ref makeDefaultOps that a free operator exists.
 */
template <typename T> struct HasFreeDataStreamWrite : std::false_type {};

/** @brief Read-direction counterpart to @ref HasFreeDataStreamWrite. */
template <typename T> struct HasFreeDataStreamRead : std::false_type {};

/** @brief True when @p T has any populated DataStream write operator. */
template <typename T>
inline constexpr bool HasDataStreamWriteV = ExactDataStreamWriteV<T> || HasFreeDataStreamWrite<T>::value;

/** @brief True when @p T has any populated DataStream read operator. */
template <typename T>
inline constexpr bool HasDataStreamReadV = ExactDataStreamReadV<T> || HasFreeDataStreamRead<T>::value;

/**
 * @brief Builds an Ops table for @p T, populating slots for whichever
 *        well-known operations @p T satisfies.
 *
 * The lifetime-management slots (copy / move / destroy) are always
 * populated — types missing them will fail to compile here, which is
 * the right behaviour: a non-copyable type cannot live inside a
 * Variant payload that may be CoW-cloned.  Optional slots (equal,
 * toString, fromString, writeStream, readStream) are populated only
 * when the corresponding concept is satisfied.
 */
template <typename T>
DataType::Ops makeDefaultOps() {
        DataType::Ops ops;
        if constexpr (std::is_default_constructible_v<T>) {
                ops.defaultConstruct = [](void *p) {
                        ::new (p) T();
                        return;
                };
        }
        ops.copyConstruct = [](void *dst, const void *src) {
                ::new (dst) T(*static_cast<const T *>(src));
                return;
        };
        ops.moveConstruct = [](void *dst, void *src) {
                ::new (dst) T(std::move(*static_cast<T *>(src)));
                return;
        };
        ops.destroy = [](void *p) {
                static_cast<T *>(p)->~T();
                return;
        };
        if constexpr (HasEqualityOp<T>) {
                ops.equal = [](const void *a, const void *b) -> bool {
                        return *static_cast<const T *>(a) == *static_cast<const T *>(b);
                };
        }
        if constexpr (HasMemberToString<T>) {
                ops.toString = [](const void *p, Error *err) -> String {
                        if (err != nullptr) *err = Error::Ok;
                        return static_cast<const T *>(p)->toString();
                };
        }
        if constexpr (HasResultFromString<T>) {
                ops.fromString = [](const String &s, void *out, Error *err) -> bool {
                        auto r = T::fromString(s);
                        if (r.second().isError()) {
                                if (err != nullptr) *err = r.second();
                                return false;
                        }
                        *static_cast<T *>(out) = r.first();
                        if (err != nullptr) *err = Error::Ok;
                        return true;
                };
        }
        if constexpr (HasDataStreamWriteV<T>) {
                ops.writeStream = [](DataStream &s, const void *p) {
                        s << *static_cast<const T *>(p);
                        return;
                };
        }
        if constexpr (HasDataStreamReadV<T>) {
                ops.readStream = [](DataStream &s, void *p) {
                        s >> *static_cast<T *>(p);
                        return;
                };
        }
        return ops;
}

} // namespace Detail

template <typename T>
DataType DataType::registerType(const char *name, ID preferredId) {
        return registerType(name, std::type_index(typeid(T)), sizeof(T), alignof(T),
                            Detail::makeDefaultOps<T>(), preferredId);
}

PROMEKI_NAMESPACE_END

/**
 * @brief Token-pasting helper used by the DataType registration macros.
 *
 * Two-level expansion is required so that @c __LINE__ is expanded
 * before the @c ## operator runs.
 */
#define PROMEKI_DATATYPE_CAT(a, b) PROMEKI_DATATYPE_CAT_INNER(a, b)
/** @brief Inner half of @ref PROMEKI_DATATYPE_CAT — do not use directly. */
#define PROMEKI_DATATYPE_CAT_INNER(a, b) a##b

/**
 * @brief Declares that the C++ type @p T participates in the DataType registry.
 *
 * Stage A's macro pair kept a per-type @c DataTypeRegistrar thunk so
 * that @c DataType::of<T> could dispatch through a typed function.
 * Stage B switched @c DataType::of<T> to a runtime @c type_index
 * lookup, so the thunk is no longer required and
 * @c PROMEKI_DECLARE_DATATYPE collapses to an empty macro — kept
 * around as an intent marker that future tooling (or a return to
 * the typed-thunk approach) can hook into without churning every
 * registering header.
 *
 * @param T  The C++ type to register.  Must be visible at the point
 *           of use (full definition, not just a forward declaration).
 */
#define PROMEKI_DECLARE_DATATYPE(T)

/**
 * @brief Registers @p T with an auto-allocated user @ref DataType::ID.
 *
 * Place in exactly one translation unit (typically the .cpp that
 * implements @p T), **outside** the @c promeki namespace.  The macro
 * emits an anonymous-namespace static @ref DataType variable whose
 * initialization runs the registration before @c main.  After that
 * point @c DataType::of<T>() returns the registered handle.
 *
 * The auto-allocated ID is drawn from @c [DataType::UserBegin,
 * DataType::UserEnd] and is **not stable across builds**.  Use
 * @ref PROMEKI_IMPLEMENT_DATATYPE_ID when the wire format must
 * survive a rebuild (typical for any type that travels through
 * persisted DataStreams).
 *
 * @param T     The C++ type.
 * @param NAME  String literal used as the type's human-readable name.
 *              Stored by pointer; must outlive the process.
 */
#define PROMEKI_IMPLEMENT_DATATYPE(T, NAME)                                                                            \
        namespace {                                                                                                    \
                [[maybe_unused]] const ::promeki::DataType                                                             \
                        PROMEKI_DATATYPE_CAT(_promeki_datatype_init_, __LINE__) =                                      \
                                ::promeki::DataType::registerType<T>(NAME);                                            \
        }

/**
 * @brief Registers @p T pinned to @p IDVAL for wire-format stability.
 *
 * Same as @ref PROMEKI_IMPLEMENT_DATATYPE but the @ref DataType::ID
 * is fixed rather than auto-allocated.  Required for library builtins
 * (so e.g. a Variant<bool> always serializes as tag @c 0x0B) and
 * recommended for user types whose payloads need to round-trip across
 * persisted streams.
 *
 * @param T      The C++ type.
 * @param NAME   String literal used as the type's human-readable name.
 * @param IDVAL  The pinned @ref DataType::ID value.
 */
#define PROMEKI_IMPLEMENT_DATATYPE_ID(T, NAME, IDVAL)                                                                  \
        namespace {                                                                                                    \
                [[maybe_unused]] const ::promeki::DataType                                                             \
                        PROMEKI_DATATYPE_CAT(_promeki_datatype_init_, __LINE__) =                                      \
                                ::promeki::DataType::registerType<T>(                                                  \
                                        NAME, static_cast<::promeki::DataType::ID>(IDVAL));                            \
        }

#endif // PROMEKI_ENABLE_CORE
