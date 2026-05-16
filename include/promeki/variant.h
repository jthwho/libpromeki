/**
 * @file      variant.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */


#pragma once


#if PROMEKI_ENABLE_CORE
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <nlohmann/json_fwd.hpp>
#include <promeki/namespace.h>
#include <promeki/config.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/function.h>
#include <promeki/sharedptr.h>
#include <promeki/datatype.h>
#include <promeki/enum.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/pair.h>
#include <promeki/uniqueptr.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

class VariantList;
class VariantMap;

/**
 * @brief Heap-allocated payload backing one @ref Variant.
 *
 * Lives behind a @ref SharedPtr so copying a Variant is a refcount
 * bump and mutating a Variant performs copy-on-write.  The structure
 * is internal — callers should not instantiate one directly; the
 * @c Detail thunks in variant.cpp are the only legal construction
 * site.  Member layout is exposed in this header (rather than living
 * out of line in variant.cpp) so @c SharedPtr<VariantBox>'s
 * @c IsSharedObject SFINAE check sees @c _promeki_refct without
 * requiring every consumer of variant.h to include variant.cpp.
 *
 * The payload bytes live in trailing storage at @c payloadOffset
 * past this header — allocated, cloned, and destroyed by the
 * static helpers defined alongside the registered @ref DataType ops.
 */
struct VariantBox {
                RefCount              _promeki_refct;
                const DataType::Data *typeData = nullptr;

                /** @brief Returns a pointer to the trailing payload bytes (mutable overload). */
                void *payload();

                /** @brief Returns a pointer to the trailing payload bytes (const overload). */
                const void *payload() const;

                /**
                 * @brief Custom @c SharedPtr clone hook.
                 *
                 * Standard SharedPtr cloning would do @c new VariantBox(*this),
                 * which slices the trailing payload.  This override allocates a
                 * fresh, properly-sized box and forwards to the registered
                 * @c copyConstruct op to duplicate the payload bytes.  Defined
                 * out of line in variant.cpp.
                 */
                VariantBox *_promeki_clone() const;

                /** @brief Returns the byte offset from this header to the first payload byte. */
                static constexpr size_t payloadOffset(size_t typeAlign) {
                        const size_t headerEnd = sizeof(VariantBox);
                        const size_t alignment = typeAlign == 0 ? 1 : typeAlign;
                        return (headerEnd + alignment - 1) & ~(alignment - 1);
                }

                /** @brief Total byte size needed to hold this header plus the payload for @p td. */
                static size_t totalSize(const DataType::Data *td) {
                        return payloadOffset(td->align) + td->size;
                }

                /**
                 * @brief Allocates a new VariantBox sized for @p td, copy-constructing the payload from @p src.
                 *
                 * @p src may be null when the caller wants to populate
                 * the payload manually (e.g. DataStream read).  Returns
                 * @c nullptr if @p td is null.  Defined in variant.cpp.
                 */
                static VariantBox *allocate(const DataType::Data *td, const void *src);

                /**
                 * @brief Custom @c operator @c delete pair invoked by
                 *        @c SharedPtr's @c delete @c _data path.
                 *
                 * Routes through the registered destroy op before
                 * releasing the over-aligned allocation, since the
                 * runtime cannot determine the original allocation
                 * size or alignment on its own.  Defined in variant.cpp.
                 */
                static void operator delete(void *p, std::size_t sz) noexcept;
};

/**
 * @brief Type-safe tagged value that can hold any registered @ref DataType.
 * @ingroup util
 *
 * Variant is the project-wide replacement for @c std::any /
 * @c std::variant — a value type that can carry any of the types
 * registered in the @ref DataType registry, with automatic conversion,
 * JSON / DataStream round-tripping, and structural equality.
 *
 * Internally a Variant is a single @c SharedPtr<VariantBox> handle —
 * one machine word — so a default-constructed Variant is essentially
 * free, copy is a refcount bump, and mutation transparently performs
 * copy-on-write through the registry's @c copyConstruct op.  The
 * runtime type tag and the @ref DataStream wire-format tag are the
 * same integer (@ref Type), so a Variant holding a @ref UUID
 * serializes to the same bytes as a free @c operator<<(DataStream &,
 * const UUID &) — no extra dispatch layer, no parallel registry.
 *
 * @par Adding a new variant alternative
 * Any C++ type registered via @ref PROMEKI_IMPLEMENT_DATATYPE is
 * automatically usable as a Variant payload — no edit to this header
 * required.  See @ref DataType for the registration mechanics.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  Concurrent access to
 * a single instance must be externally synchronized; the internal
 * @c SharedPtr is itself thread-safe for refcount operations, but a
 * mutating call on a Variant raced with another access on the same
 * Variant is undefined.
 *
 * @par Example
 * @code
 * Variant v = 42;                    // implicit converting constructor
 * String  s = v.get<String>();       // "42"
 * v.set(String("hello"));
 * Variant::Type t = v.type();        // Variant::TypeString
 * if (const String *str = v.peek<String>()) {
 *     promekiInfo("variant holds: %s", str->cstr());
 * }
 * @endcode
 */
class Variant {
        public:
                /**
                 * @brief Runtime tag identifying which @ref DataType a Variant holds.
                 *
                 * Alias of @ref DataStream::TypeId, so the Variant runtime
                 * tag and the DataStream wire-format tag are literally the
                 * same value.  Each registered type pins a stable integer
                 * here (library types in @c 0x0001 – @c 0x3FFF, user types
                 * in @c 0x4000 – @c 0xFFFF).
                 */
                using Type = DataStream::TypeId;

                // ------------------------------------------------------------------
                // Source-compatibility aliases.
                //
                // The legacy std::variant-backed Variant exposed an enum with
                // names like `TypeBool`, `TypeU8`, `TypeS32`, etc.  The unified
                // ID enum on DataStream uses `TypeUInt8`, `TypeInt32`, ... so
                // callers that switch on `Variant::TypeU8` still resolve to the
                // right integer without a mass sed.  Every existing well-known
                // type that the old Variant exposed has a constant here.
                // ------------------------------------------------------------------
                static constexpr Type TypeInvalid          = DataStream::TypeInvalid;
                static constexpr Type TypeBool             = DataStream::TypeBool;
                static constexpr Type TypeU8               = DataStream::TypeUInt8;
                static constexpr Type TypeS8               = DataStream::TypeInt8;
                static constexpr Type TypeU16              = DataStream::TypeUInt16;
                static constexpr Type TypeS16              = DataStream::TypeInt16;
                static constexpr Type TypeU32              = DataStream::TypeUInt32;
                static constexpr Type TypeS32              = DataStream::TypeInt32;
                static constexpr Type TypeU64              = DataStream::TypeUInt64;
                static constexpr Type TypeS64              = DataStream::TypeInt64;
                static constexpr Type TypeFloat            = DataStream::TypeFloat;
                static constexpr Type TypeDouble           = DataStream::TypeDouble;
                static constexpr Type TypeString           = DataStream::TypeString;
                static constexpr Type TypeBuffer           = DataStream::TypeBuffer;
                static constexpr Type TypeDateTime         = DataStream::TypeDateTime;
                static constexpr Type TypeTimeStamp        = DataStream::TypeTimeStamp;
                static constexpr Type TypeMediaTimeStamp   = DataStream::TypeMediaTimeStamp;
                static constexpr Type TypeFrameNumber      = DataStream::TypeFrameNumber;
                static constexpr Type TypeFrameCount       = DataStream::TypeFrameCount;
                static constexpr Type TypeMediaDuration    = DataStream::TypeMediaDuration;
                static constexpr Type TypeDuration         = DataStream::TypeDuration;
                static constexpr Type TypeSize2D           = DataStream::TypeSize2D;
                static constexpr Type TypeUUID             = DataStream::TypeUUID;
                static constexpr Type TypeUMID             = DataStream::TypeUMID;
                static constexpr Type TypeTimecode         = DataStream::TypeTimecode;
                static constexpr Type TypeRational         = DataStream::TypeRational;
                static constexpr Type TypeFrameRate        = DataStream::TypeFrameRate;
                static constexpr Type TypeVideoFormat      = DataStream::TypeVideoFormat;
                static constexpr Type TypeStringList       = DataStream::TypeStringList;
                static constexpr Type TypeColor            = DataStream::TypeColor;
                static constexpr Type TypeColorModel       = DataStream::TypeColorModel;
                static constexpr Type TypeMemSpace         = DataStream::TypeMemSpace;
                static constexpr Type TypePixelMemLayout   = DataStream::TypePixelMemLayout;
                static constexpr Type TypePixelFormat      = DataStream::TypePixelFormat;
                static constexpr Type TypeVideoCodec       = DataStream::TypeVideoCodec;
                static constexpr Type TypeAudioCodec       = DataStream::TypeAudioCodec;
                static constexpr Type TypeAudioFormat      = DataStream::TypeAudioFormat;
                static constexpr Type TypeAncFormat        = DataStream::TypeAncFormat;
                static constexpr Type TypeAudioStreamDesc  = DataStream::TypeAudioStreamDesc;
                static constexpr Type TypeAudioChannelMap  = DataStream::TypeAudioChannelMap;
                static constexpr Type TypeAudioMarkerList  = DataStream::TypeAudioMarkerList;
                static constexpr Type TypeEnum             = DataStream::TypeEnum;
                static constexpr Type TypeEnumList         = DataStream::TypeEnumList;
                static constexpr Type TypeMasteringDisplay = DataStream::TypeMasteringDisplay;
                static constexpr Type TypeContentLightLevel = DataStream::TypeContentLightLevel;
                static constexpr Type TypeUrl              = DataStream::TypeUrl;
                static constexpr Type TypeWindowedStat     = DataStream::TypeWindowedStat;
                static constexpr Type TypeVariantList      = DataStream::TypeVariantList;
                static constexpr Type TypeVariantMap       = DataStream::TypeVariantMap;
                static constexpr Type TypeXmlDocument      = DataStream::TypeXmlDocument;
                static constexpr Type TypeCea708Cdp        = DataStream::TypeCea708Cdp;
                static constexpr Type TypeCea708Service    = DataStream::TypeCea708Service;
                static constexpr Type TypeCea708DtvccPacket = DataStream::TypeCea708DtvccPacket;
                static constexpr Type TypeCea608           = DataStream::TypeCea608;
                static constexpr Type TypeSubtitle         = DataStream::TypeSubtitle;
                static constexpr Type TypeHdrStaticMetadata = DataStream::TypeHdrStaticMetadata;
                static constexpr Type TypeSocketAddress    = DataStream::TypeSocketAddress;
                static constexpr Type TypeSdpSession       = DataStream::TypeSdpSession;
                static constexpr Type TypeMacAddress       = DataStream::TypeMacAddress;
                static constexpr Type TypeEUI64            = DataStream::TypeEUI64;
                static constexpr Type TypeSslContext       = DataStream::TypeSslContext;

                /**
                 * @brief Returns the human-readable type name for the given Type tag.
                 *
                 * Looks the tag up in the @ref DataType registry; returns
                 * @c "Invalid" when @p id is @c TypeInvalid or otherwise
                 * unregistered.  The returned pointer is stable for the
                 * lifetime of the process (it is the @c name field of the
                 * registered Data record).
                 */
                static const char *typeName(Type id);

                /**
                 * @brief Constructs a Variant from a JSON value, inferring the best native type.
                 *
                 * Mapping rules:
                 *  - @c null            → invalid Variant
                 *  - @c boolean         → @c bool
                 *  - unsigned integer   → @c uint64_t
                 *  - signed integer     → @c int64_t
                 *  - floating-point     → @c double
                 *  - string             → @ref String
                 *  - array              → @ref VariantList (recursive)
                 *  - object             → @ref VariantMap  (recursive)
                 */
                static Variant fromJson(const nlohmann::json &val);

                /**
                 * @brief Reads a Variant of type @p dt from @p stream.
                 *
                 * Allocates a default-constructed payload of @p dt and
                 * forwards to its registered @c readStream op.  The
                 * stream must be positioned at the start of a frame
                 * whose tag matches @c dt.id() — the per-type read
                 * operator consumes that frame.  Returns an invalid
                 * Variant when @p dt is invalid, has no @c readStream
                 * slot, or has no @c defaultConstruct slot.
                 *
                 * Primarily intended for @c DataStream's tag-dispatch
                 * read path, which uses this to materialise
                 * user-registered Variant alternatives without a
                 * hard-coded switch case.  Direct callers should
                 * normally use @c DataStream::operator>>.
                 */
                static Variant readFromStream(DataStream &stream, const DataType &dt);

                /**
                 * @brief Returns a Variant holding a default-constructed value of @p dt.
                 *
                 * Uses @c dt.ops().defaultConstruct to materialise the
                 * payload in place — no copy.  Returns an invalid
                 * Variant when @p dt is itself invalid or when the
                 * registered type isn't default-constructible (the
                 * @c defaultConstruct slot is null).
                 *
                 * Useful for generic deserialization paths and for
                 * @ref VariantSpec-style defaults where the C++ type
                 * is only known at runtime.
                 *
                 * @param dt  Target type handle.
                 */
                static Variant createDefault(const DataType &dt);

                // ------------------------------------------------------------------
                // Converter registry
                //
                // The registry maps a (FromType, ToType) pair to a function that
                // takes a Variant holding @c FromType and returns a Variant holding
                // @c ToType.  The library populates the builtin cross-product at
                // first-use; user code registers its own pairs through the public
                // API below to make @ref get<T>(), @ref convertTo, and
                // @ref operator== aware of cross-type conversions involving
                // user-defined @ref DataType alternatives.
                // ------------------------------------------------------------------

                /**
                 * @brief Type-erased converter signature: Variant in, Variant out.
                 *
                 * The function receives @p src already validated to be a
                 * non-invalid Variant whose runtime type matches the
                 * registered @c from tag.  It should peek the source
                 * payload, run the typed conversion, and return a Variant
                 * holding the @c to type.  On failure it should set
                 * @c *err and return an invalid Variant.
                 */
                using ConverterFn = Variant (*)(const Variant &src, Error *err);

                /**
                 * @brief Registers a converter from @p from to @p to.
                 *
                 * Overwrites any previously-registered converter for the
                 * same (@p from, @p to) pair.  Thread-safe.  The registry
                 * is process-lifetime — there is no unregister.
                 *
                 * Most callers should prefer the typed
                 * @ref registerConverter<auto> overload, which deduces
                 * the From/To types and generates the peek/wrap thunk
                 * automatically.
                 *
                 * @param from  Source @ref Type tag (must be a registered DataType).
                 * @param to    Destination @ref Type tag (must be a registered DataType).
                 * @param fn    Converter function pointer; must not be null.
                 */
                static void registerConverter(Type from, Type to, ConverterFn fn);

                /**
                 * @brief Typed convenience overload — registers a converter for @p Fn.
                 *
                 * Deduces the @c From and @c To types from @p Fn's
                 * signature, registers the (From, To) pair, and generates
                 * a thunk that handles the @c peek<From>() / wrap-in-Variant
                 * boilerplate.  @p Fn must be a free-function (or
                 * non-capturing-lambda) pointer with signature
                 * @c To(const From&, Error*) — both types must be
                 * registered in the @ref DataType registry.
                 *
                 * @code
                 * static String myTypeToString(const MyType &val, Error *err);
                 * Variant::registerConverter<&myTypeToString>();
                 * @endcode
                 */
                template <auto Fn> static void registerConverter();

                /**
                 * @brief Returns the registered converter for (@p from, @p to), or @c nullptr.
                 *
                 * Useful for tooling and introspection; runtime conversion
                 * paths should call @ref convertTo or @ref get<T> instead,
                 * both of which handle the invalid-Variant and
                 * unregistered-target cases for you.
                 */
                static ConverterFn findConverter(Type from, Type to);

                /** @brief Default-constructs an invalid (empty) Variant. */
                Variant() = default;

                /** @brief Copy-constructs (refcount bump on the underlying VariantBox). */
                Variant(const Variant &) = default;

                /** @brief Move-constructs (steals the underlying box; @p other becomes invalid). */
                Variant(Variant &&) noexcept = default;

                /** @brief Copy-assigns (refcount bump on the underlying VariantBox). */
                Variant &operator=(const Variant &) = default;

                /** @brief Move-assigns (steals the underlying box; @p other becomes invalid). */
                Variant &operator=(Variant &&) noexcept = default;

                /** @brief Destructor. */
                ~Variant() = default;

                /**
                 * @brief Implicit converting constructor for any registered type (copy path).
                 *
                 * Equivalent to @code Variant v; v.set(value); @endcode.
                 *
                 * Constrained so the converting constructor only matches when
                 * @p T resolves to a registered @ref DataType — otherwise an
                 * unrelated single-arg call would shadow the copy/move
                 * constructors and yield an opaque template error.
                 *
                 * @tparam T  The C++ type of @p value.  Must be registered.
                 * @param value  The value to store.
                 */
                template <typename T,
                          typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Variant> &&
                                                      !std::is_same_v<std::decay_t<T>, VariantBox>>>
                Variant(const T &value) {
                        setValue<T>(value);
                        return;
                }

                /**
                 * @brief Implicit converting constructor for any registered type (move path).
                 *
                 * Same shape as the copy converting constructor but routes
                 * through the registered @c moveConstruct op, so e.g.
                 * @code Variant v(std::move(bigString)); @endcode
                 * relocates the payload bytes instead of deep-copying.
                 * Constrained to rvalues only via the
                 * @c !std::is_lvalue_reference_v constraint, leaving the
                 * @c const T& overload to handle lvalues.
                 */
                template <typename T,
                          typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Variant> &&
                                                      !std::is_same_v<std::decay_t<T>, VariantBox> &&
                                                      !std::is_lvalue_reference_v<T>>>
                Variant(T &&value) {
                        setValueMove<std::remove_cv_t<std::remove_reference_t<T>>>(std::move(value));
                        return;
                }

                /** @brief Returns true when the Variant holds a registered value. */
                bool isValid() const { return _box.isValid(); }

                /**
                 * @brief Replaces the held value with @p value (copy path).
                 *
                 * The new value's type must be registered in the @ref DataType
                 * registry.  Calling @c set with an unregistered C++ type
                 * leaves the Variant in its invalid state and logs a warning.
                 *
                 * @tparam T  The C++ type of @p value.  Must be registered.
                 * @param value  The value to store.
                 */
                template <typename T> void set(const T &value) {
                        setValue<T>(value);
                        return;
                }

                /**
                 * @brief Replaces the held value with @p value (move path).
                 *
                 * Routes through the registered @c moveConstruct op so
                 * @code v.set(std::move(bigString)); @endcode relocates
                 * the payload bytes rather than deep-copying.  Constrained
                 * to rvalues only; lvalues fall through to the
                 * @c set(const T&) overload.
                 */
                template <typename T,
                          typename = std::enable_if_t<!std::is_lvalue_reference_v<T> &&
                                                      !std::is_same_v<std::decay_t<T>, Variant>>>
                void set(T &&value) {
                        setValueMove<std::remove_cv_t<std::remove_reference_t<T>>>(std::move(value));
                        return;
                }

                /**
                 * @brief Returns the held value as @p To, converting if necessary.
                 *
                 * The conversion path:
                 *  - If the held type matches @c To exactly, returns by value.
                 *  - Otherwise, looks up a registered converter @c (currentId, toId)
                 *    in the @ref DataType registry and applies it.
                 *  - Returns a default-constructed @c To and sets @p err to
                 *    @c Error::Invalid when no conversion is available.
                 *
                 * @tparam To  Target C++ type.
                 * @param err  Optional pointer to receive @c Error::Ok on
                 *             success or @c Error::Invalid on failure.
                 */
                template <typename To> To get(Error *err = nullptr) const;

                /**
                 * @brief Borrows a pointer to the held value when its type is exactly @p T.
                 *
                 * Returns a non-owning pointer into the Variant's payload
                 * when @p T matches the registered type, or @c nullptr
                 * otherwise.  Cheap (no copy), and especially valuable for
                 * heavy alternatives such as @ref VariantList,
                 * @ref VariantMap, @ref String, and @ref Buffer where
                 * repeated @c get<T>() calls during a tree walk would
                 * deep-copy each container.
                 *
                 * @tparam T  Any registered Variant alternative.
                 * @return    Pointer to the stored value, or @c nullptr.
                 */
                template <typename T> const T *peek() const noexcept;

                /** @brief Returns the runtime type tag, or @c TypeInvalid for an empty Variant. */
                Type type() const;

                /** @brief Returns the human-readable name of the held type. */
                const char *typeName() const { return typeName(type()); }

                /** @brief Returns the @ref DataType handle for the held type, or an invalid handle. */
                DataType dataType() const;

                /**
                 * @brief Returns a const pointer to the held value's raw payload bytes.
                 *
                 * Primarily intended for the registry-driven DataStream
                 * write path and other infrastructure that has the
                 * @ref DataType::Ops in hand and needs to feed the
                 * payload to a function-pointer slot.  Callers that
                 * know the C++ type statically should prefer
                 * @ref peek<T>().
                 *
                 * @return Pointer into the payload region of the
                 *         underlying box, or @c nullptr if the
                 *         Variant is invalid.
                 */
                const void *payloadPtr() const;

                /**
                 * @brief Converts complex types to their @ref String representation, leaving simple types unchanged.
                 *
                 * Numeric types and the invalid alternative are returned
                 * as-is.  Every other Variant alternative is converted
                 * to @ref String via @c get<String>().
                 */
                Variant toStandardType() const;

                /**
                 * @brief Formats the held value using a type-specific format spec.
                 *
                 * Builds the format string @c "{:<spec>}" and routes it to
                 * @c std::vformat with the held value, so the type's own
                 * @c std::formatter specialization is invoked.  Rich types
                 * use their native style keywords; primitives use the
                 * standard @c std::format spec grammar.
                 *
                 * An empty @p spec short-circuits to @c get<String>().
                 * Variants whose alternatives have no @c std::formatter
                 * fall back to formatting the spec against the value's
                 * String form, so width / fill / alignment still work.
                 *
                 * @param spec  Type-specific format spec, without the
                 *              surrounding @c {} or leading @c ':'.
                 * @param err   Optional error output, @c Error::Ok on
                 *              success or @c Error::Invalid on a bad spec.
                 */
                String format(const String &spec, Error *err = nullptr) const;

                /**
                 * @brief Resolves the held value to an Enum of the given type.
                 *
                 * Accepts:
                 *  - @c TypeInvalid → returns @c Enum(enumType) (registered default).
                 *  - @c TypeEnum holding the right type → returned directly.
                 *  - @c TypeString @c "Type::Value", @c "Value", or a signed decimal.
                 *  - Any integer Type → wrapped as @c Enum(enumType, int).
                 *
                 * @param enumType  Target Enum type handle.
                 * @param err       Optional error output:
                 *                  @c Error::InvalidArgument if @p enumType
                 *                  is invalid, @c Error::Invalid on other
                 *                  conversion failure, @c Error::Ok on
                 *                  success.
                 */
                Enum asEnum(Enum::Type enumType, Error *err = nullptr) const;

                /**
                 * @brief Returns true iff both Variants hold values that compare equal.
                 *
                 * Three-tier comparison:
                 *  1. Same type → uses the type's @c ops.equal.
                 *  2. Cross-type numeric (including bool) → promotes to a
                 *     common representation and compares.
                 *  3. Cross-type convertible → attempts conversion in
                 *     either direction; returns true if either succeeds
                 *     and the converted values compare equal.
                 */
                bool operator==(const Variant &other) const;

                /** @brief Inequality counterpart of @ref operator==. */
                bool operator!=(const Variant &other) const { return !(*this == other); }

                /**
                 * @brief Returns this Variant converted to type @p to via the registry.
                 *
                 * Looks up @c (type(), to) in the converter registry and
                 * applies it.  Returns an invalid Variant when this
                 * Variant is itself invalid, when no converter is
                 * registered for the pair, or when the converter itself
                 * fails (in which case @p *err carries the converter's
                 * error code).
                 *
                 * Typed callers should normally use @ref get<T> instead;
                 * this entry point exists for runtime-typed code that
                 * only knows the target as a @ref Type tag.
                 */
                Variant convertTo(Type to, Error *err = nullptr) const;

        private:
                /** @brief Refcounted handle to the trailing-payload box. */
                using BoxPtr = SharedPtr<VariantBox>;

                template <typename T> void setValue(const T &value);
                template <typename T> void setValueMove(T &&value);

                BoxPtr _box;
};

/**
 * @brief Heterogeneous list of @ref Variant values.
 * @ingroup util
 *
 * @ref VariantList is a Variant alternative — i.e. a Variant can hold
 * a VariantList directly, enabling JSON-shaped trees and recursive
 * payloads to flow through the standard Variant pipeline.  To break
 * the size recursion (a VariantList contains Variants which can
 * themselves be VariantLists), the underlying @c List<Variant>
 * lives behind a @ref UniquePtr handle; @c sizeof(VariantList) is
 * one pointer regardless of how the contained Variants nest.
 *
 * Public API forwards the heavily-used @c List<Variant> operations
 * (size, indexing, push, clear, range-for) directly.  For operations
 * not surfaced here, callers can borrow the underlying list via
 * @ref list().
 *
 * @par Element iteration
 * Iteration is exposed via raw @c Variant @c * pointers — the underlying
 * @c List<Variant> storage is contiguous, so range-for and
 * pointer arithmetic both work.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 */
class VariantList {
        public:
                /** @brief Underlying storage type. */
                using ItemList = List<Variant>;
                /** @brief Mutable forward iterator. */
                using Iterator = Variant *;
                /** @brief Const forward iterator. */
                using ConstIterator = const Variant *;

                VariantList();
                VariantList(std::initializer_list<Variant> il);
                explicit VariantList(const ItemList &other);
                explicit VariantList(ItemList &&other);

                VariantList(const VariantList &other);
                VariantList(VariantList &&other) noexcept;
                ~VariantList();

                VariantList &operator=(const VariantList &other);
                VariantList &operator=(VariantList &&other) noexcept;

                size_t size() const;
                bool   isEmpty() const;
                void   clear();
                void   reserve(size_t capacity);

                Variant       &operator[](size_t index);
                const Variant &operator[](size_t index) const;
                Variant       &at(size_t index);
                const Variant &at(size_t index) const;

                void pushToBack(const Variant &v);
                void pushToBack(Variant &&v);
                void popBack();

                Variant       *data();
                const Variant *data() const;

                Iterator      begin();
                Iterator      end();
                ConstIterator begin() const;
                ConstIterator end() const;
                ConstIterator cbegin() const;
                ConstIterator cend() const;

                ItemList       &list();
                const ItemList &list() const;

                bool operator==(const VariantList &other) const;
                bool operator!=(const VariantList &other) const { return !(*this == other); }

                String toJsonString() const;
                static VariantList fromJsonString(const String &json, Error *err = nullptr);

                /**
                 * @brief Result-shaped alias of @ref fromJsonString.
                 *
                 * Mirrors the project-wide @c Result<T> @c fromString
                 * convention so the @ref DataType registry can auto-detect
                 * the inverse of @ref toJsonString() via
                 * @ref Detail::HasResultFromString.
                 */
                static Result<VariantList> fromString(const String &json) {
                        Error       e;
                        VariantList v = fromJsonString(json, &e);
                        if (e.isError()) return makeError<VariantList>(e);
                        return makeResult(std::move(v));
                }

        private:
                UniquePtr<ItemList> _list;
};

/**
 * @brief Heterogeneous string-keyed map of @ref Variant values.
 * @ingroup util
 *
 * Same shape as @ref VariantList but keyed by @ref String.  Used
 * for JSON-shaped objects, RPC argument bags, dynamic config
 * payloads.  As with VariantList, the underlying @c Map<String,
 * Variant> lives behind a @ref UniquePtr handle.
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref VariantList.
 */
class VariantMap {
        public:
                /** @brief Underlying storage type. */
                using EntryMap = Map<String, Variant>;
                /** @brief Pair shape accepted by initializer-list construction. */
                using EntryPair = Pair<String, Variant>;
                /** @brief Callback signature for @ref forEach. */
                using ForEachFn = Function<void(const String &, const Variant &)>;

                VariantMap();
                VariantMap(std::initializer_list<EntryPair> il);
                explicit VariantMap(const EntryMap &other);
                explicit VariantMap(EntryMap &&other);

                VariantMap(const VariantMap &other);
                VariantMap(VariantMap &&other) noexcept;
                ~VariantMap();

                VariantMap &operator=(const VariantMap &other);
                VariantMap &operator=(VariantMap &&other) noexcept;

                size_t size() const;
                bool   isEmpty() const;
                void   clear();
                bool   contains(const String &key) const;

                void insert(const String &key, const Variant &value);
                void insert(const String &key, Variant &&value);
                bool remove(const String &key);

                Variant value(const String &key) const;
                Variant value(const String &key, const Variant &defaultValue) const;

                Variant       *find(const String &key);
                const Variant *find(const String &key) const;

                StringList keys() const;
                void       forEach(ForEachFn fn) const;

                EntryMap       &map();
                const EntryMap &map() const;

                bool operator==(const VariantMap &other) const;
                bool operator!=(const VariantMap &other) const { return !(*this == other); }

                String toJsonString() const;
                static VariantMap fromJsonString(const String &json, Error *err = nullptr);

                /**
                 * @brief Result-shaped alias of @ref fromJsonString.
                 *
                 * Mirrors the project-wide @c Result<T> @c fromString
                 * convention so the @ref DataType registry can auto-detect
                 * the inverse of @ref toJsonString() via
                 * @ref Detail::HasResultFromString.
                 */
                static Result<VariantMap> fromString(const String &json) {
                        Error      e;
                        VariantMap v = fromJsonString(json, &e);
                        if (e.isError()) return makeError<VariantMap>(e);
                        return makeResult(std::move(v));
                }

        private:
                UniquePtr<EntryMap> _map;
};

// Inline template definitions for Variant::set / setValue / peek /
// registerConverter must live in the header so consumer TUs can
// instantiate them for their own types.

namespace Detail {

/**
 * @brief Internal: allocates a new VariantBox copy-constructed from @p value.
 *
 * Defined in @c variant.cpp.  @p typeData must refer to a registered
 * type whose @c cppType matches @c std::type_index(typeid(T)); the
 * lambda passed to PROMEKI_IMPLEMENT_DATATYPE-style registration
 * does that match-check at call time.  When @p typeData is null the
 * function returns a null SharedPtr (Variant becomes invalid).
 */
SharedPtr<VariantBox> makeVariantBox(const DataType::Data *typeData, const void *value);

/**
 * @brief Internal: allocates a new VariantBox move-constructed from @p value.
 *
 * Same contract as @ref makeVariantBox but routes through the
 * registered @c moveConstruct op so the payload bytes are relocated
 * rather than deep-copied.  Falls back to copy-construction when the
 * type record has no @c moveConstruct slot.
 */
SharedPtr<VariantBox> makeVariantBoxMove(const DataType::Data *typeData, void *value);

} // namespace Detail

template <typename T> void Variant::setValue(const T &value) {
        // Preserve the legacy std::variant-based behaviour where
        // TypedEnum<X>-derived types slice to Enum on assignment.  The
        // registry only carries the Enum entry, so derived types route
        // through it explicitly rather than producing an invalid
        // Variant.
        if constexpr (std::is_base_of_v<Enum, T> && !std::is_same_v<T, Enum>) {
                const Enum &sliced = static_cast<const Enum &>(value);
                _box = Detail::makeVariantBox(DataType::of<Enum>().data(), &sliced);
                return;
        } else {
                const DataType dt = DataType::of<T>();
                if (dt.isValid()) {
                        _box = Detail::makeVariantBox(dt.data(), &value);
                        return;
                }
                // Fallback: replicate legacy std::variant overload
                // resolution for the common case where @p T is
                // convertible to one of the registered types but is
                // not itself registered — string literals, char
                // arrays, @c std::string, etc.  String is the
                // dominant "implicit landing" alternative.
                if constexpr (!std::is_same_v<T, String> &&
                              std::is_constructible_v<String, const T &>) {
                        String s(value);
                        _box = Detail::makeVariantBox(DataType::of<String>().data(), &s);
                        return;
                }
                _box.clear();
                return;
        }
}

template <typename T> void Variant::setValueMove(T &&value) {
        // T is already a value type by the time this is instantiated
        // (the public set / converting ctor strip references), so the
        // typedef below is just for symmetry with @ref setValue.  The
        // Enum slicing path mirrors the copy variant: TypedEnum<X> →
        // Enum payload via the registry's Enum entry.
        using U = std::remove_cv_t<std::remove_reference_t<T>>;
        if constexpr (std::is_base_of_v<Enum, U> && !std::is_same_v<U, Enum>) {
                Enum sliced = static_cast<Enum &&>(value);
                _box = Detail::makeVariantBoxMove(DataType::of<Enum>().data(), &sliced);
                return;
        } else {
                const DataType dt = DataType::of<U>();
                if (dt.isValid()) {
                        U tmp(std::move(value));
                        _box = Detail::makeVariantBoxMove(dt.data(), &tmp);
                        return;
                }
                // Same String-convertible fallback as the copy path.
                if constexpr (!std::is_same_v<U, String> &&
                              std::is_constructible_v<String, U &&>) {
                        String s(std::move(value));
                        _box = Detail::makeVariantBoxMove(DataType::of<String>().data(), &s);
                        return;
                }
                _box.clear();
                return;
        }
}

template <typename T> const T *Variant::peek() const noexcept {
        if (_box.isNull()) return nullptr;
        const DataType::Data *td = _box->typeData;
        if (td == nullptr) return nullptr;
        if (td->cppType != std::type_index(typeid(T))) return nullptr;
        return static_cast<const T *>(_box->payload());
}

template <typename To> To Variant::get(Error *err) const {
        if (err != nullptr) *err = Error::Ok;
        if (const To *direct = peek<To>()) return *direct;

        const DataType targetDt = DataType::of<To>();
        if (!targetDt.isValid()) {
                if (err != nullptr) *err = Error::Invalid;
                return To{};
        }
        Error convErr;
        Variant out = convertTo(targetDt.id(), &convErr);
        if (convErr.isError()) {
                if (err != nullptr) *err = convErr;
                return To{};
        }
        if (const To *converted = out.peek<To>()) return *converted;
        if (err != nullptr) *err = Error::Invalid;
        return To{};
}

namespace Detail {

/**
 * @brief Function-pointer signature deducer used by @ref Variant::registerConverter<auto>.
 *
 * Specialized below for @c To(*)(const From&, Error*); other signatures
 * are intentionally not matched so the typed @c registerConverter
 * overload rejects them with a hard error rather than silently
 * registering nothing.
 */
template <typename Fn> struct ConverterFnTraits;

template <typename To, typename From> struct ConverterFnTraits<To (*)(const From &, Error *)> {
                using FromType = From;
                using ToType   = To;
};

} // namespace Detail

template <auto Fn> void Variant::registerConverter() {
        using Traits = Detail::ConverterFnTraits<decltype(Fn)>;
        using From   = typename Traits::FromType;
        using To     = typename Traits::ToType;
        registerConverter(DataType::of<From>().id(), DataType::of<To>().id(),
                          +[](const Variant &v, Error *err) -> Variant {
                                  const From *p = v.peek<From>();
                                  if (p == nullptr) {
                                          if (err != nullptr) *err = Error::Invalid;
                                          return Variant();
                                  }
                                  return Variant(Fn(*p, err));
                          });
}

class DataStream;

/** @brief Writes a VariantList to a DataStream. */
DataStream &operator<<(DataStream &stream, const VariantList &list);
/** @brief Reads a VariantList from a DataStream. */
DataStream &operator>>(DataStream &stream, VariantList &list);
/** @brief Writes a VariantMap to a DataStream. */
DataStream &operator<<(DataStream &stream, const VariantMap &map);
/** @brief Reads a VariantMap from a DataStream. */
DataStream &operator>>(DataStream &stream, VariantMap &map);

/**
 * @brief Resolves a dotted/indexed path against a @ref Variant tree.
 * @ingroup util
 *
 * Walks @p root following the same key grammar used by
 * @ref VariantLookup (@c "segment ( '.' segment )*", where
 * @c "segment := name ( '[' index ']' )?"), descending through nested
 * @ref VariantMap and @ref VariantList alternatives as needed.
 *
 * @par Errors
 *  - @c Error::IdNotFound — a key was missing from a VariantMap.
 *  - @c Error::OutOfRange — a list index was beyond the VariantList size.
 *  - @c Error::ParseFailed — the path itself was malformed.
 *  - @c Error::Invalid     — descended into a Variant that wasn't
 *                            VariantMap / VariantList when the path
 *                            still had segments to walk.
 */
Variant promekiResolveVariantPath(const Variant &root, const String &path, Error *err = nullptr);

PROMEKI_NAMESPACE_END

/**
 * @brief @c std::formatter specialization for @ref promeki::Variant.
 *
 * Routes the held value through @c Variant::get<String>(), which
 * already knows how to convert every Variant alternative to a String.
 * Standard string format specifiers (width, fill, alignment) are
 * inherited from @c std::formatter<std::string_view>.
 */
template <> struct std::formatter<promeki::Variant> : std::formatter<std::string_view> {
                using Base = std::formatter<std::string_view>;
                template <typename FormatContext>
                auto format(const promeki::Variant &v, FormatContext &ctx) const {
                        promeki::String s = v.get<promeki::String>();
                        return Base::format(std::string_view(s.cstr(), s.byteCount()), ctx);
                }
};

#endif // PROMEKI_ENABLE_CORE
