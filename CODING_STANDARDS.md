# Coding Standards

This document describes the coding conventions and design patterns used throughout libpromeki. All new code should follow these standards. Existing code that deviates should be updated when it is being modified for other reasons.

## Design Philosophy

libpromeki provides Qt-inspired C++ core classes built on top of the C++ standard library. The goal is to offer friendlier, less verbose APIs than `std::` while retaining full interoperability. Key principles:

- **Value semantics**: All data classes are plain value types. The layer above decides whether to use `MyClass::Ptr` (SharedPtr) for shared ownership.
- **No raw ownership**: Memory is managed via `SharedPtr`, `Buffer`, or RAII. Raw pointers are only used for non-owning references (e.g., parent/child relationships in `ObjectBase`).
- **Error codes over exceptions**: Use the `Error` class and return values rather than throwing. Constructors never throw.
- **Minimal abstraction**: Wrap `std::` types to improve ergonomics, but don't add layers of indirection for their own sake.
- **Fix the library, don't work around it**: When library types have missing operators, conversions, or other ergonomic gaps that force awkward usage at call sites, fix the library type rather than scattering workarounds through application code. For example, if `"literal" + MyType(...)` doesn't compile, add a free `operator+` overload — don't require callers to write `MyType("literal") + ...`.
- **Blocking calls must support timeouts**: Any function that can block the calling thread should accept a `timeoutMs` parameter (type `unsigned int`, in milliseconds). The default value should be `0`, meaning "wait indefinitely." When a timeout expires, the function should return `Error::Timeout`. This convention applies unless the specific use case makes millisecond-granularity timeouts inappropriate (e.g., a frame-accurate deadline might use a different unit).

---

## Object Categories {#object-categories}

libpromeki classes fall into two distinct categories with different ownership, copying, and lifetime rules.

### Data Objects

Data objects represent values or containers of values. They are the vast majority of classes in the library. Key traits:

- **Copyable by value.** They can be freely copied, assigned, passed by value, and stored in containers.
- **No identity.** Two data objects with the same contents are interchangeable. They have no inherent notion of "this specific instance."
- **Not internally thread-safe.** Individual instances are not synchronized unless the class documents otherwise (e.g., `Queue`). See the thread safety patterns below for how to safely share data across threads.

Data objects subdivide into two categories based on size, usage patterns, and sharing semantics. See also the @ref dataobjects "Data Object Categories" page.

| Category | Internal storage | SharedPtr support | Examples |
|---|---|---|---|
| **Simple** | Direct member variables | None — no `PROMEKI_SHARED_FINAL`, no `RefCount` overhead | `Point`, `Size2Du32`/`Size2Di32`, `Rect2Di32`, `Rational`, `FourCC`, `UUID`, `Timecode`, `TimeStamp`, `XYZColor` |
| **Shareable** | Direct member variables | `PROMEKI_SHARED_FINAL` + `using Ptr = SharedPtr<ClassName>` | `String`, `Buffer`, `AudioDesc`, `ImageDesc`, `VideoDesc`, `Metadata`, `JsonObject`, `JsonArray`, `List<T>`, `Map<K,V>`, `Set<K>`, `Array<T,N>`, `Image`, `Audio`, `Frame` |

**Simple** types are small and cheap to copy — no reference counting needed. **Shareable** types store data directly (no internal `SharedPtr<Data>`) but include `PROMEKI_SHARED_FINAL` so they *can* be managed by `SharedPtr` when shared ownership is needed. The layer above decides whether to use `MyClass::Ptr` for shared ownership. Media objects like `Image` and `Audio` hold buffers via `Buffer::Ptr` for zero-copy buffer sharing even when the object itself is copied.

### Functional Objects (ObjectBase)

Functional objects represent active entities with identity, behavior, and relationships. They derive from `ObjectBase`. Key traits:

- **Not copyable.** Each instance is unique. Copying is deleted.
- **Identity matters.** A functional object *is* a specific thing, not just a bag of data. Signals are connected to *this* object, children belong to *this* parent.
- **Parent/child ownership.** Lifetime is managed through a tree: destroying a parent destroys its children. Raw pointers are used for non-owning references within the tree.
- **Signals and slots.** Communication between functional objects uses the signal/slot system, not return values.
- **No SharedPtr.** These objects use `PROMEKI_OBJECT` / `PROMEKI_SHARED` macros for their own reflection and signal infrastructure, not for SharedPtr-style sharing.

Examples: `AudioBlock`, custom processing nodes, application-level objects.

### Utility Classes

Utility classes provide infrastructure services (synchronization, threading, async coordination) without being data objects or functional objects. They do not use `PROMEKI_SHARED_FINAL` and do not derive from `ObjectBase`. Key traits:

- **Not copyable, not movable.** They manage OS resources (mutexes, threads, condition variables) that cannot be meaningfully copied.
- **Often thread-safe.** Most utility classes are designed for concurrent access and document this in their Doxygen.
- **RAII lockers.** Synchronization utilities provide nested RAII locker classes (`Mutex::Locker`, `ReadWriteLock::ReadLocker`, `ReadWriteLock::WriteLocker`) that acquire on construction and release on destruction.

| Class | Wraps | Purpose |
|---|---|---|
| `Mutex` | `std::mutex` | Mutual exclusion with nested `Locker` |
| `ReadWriteLock` | `std::shared_mutex` | Reader-writer locking with `ReadLocker`/`WriteLocker` |
| `WaitCondition` | `std::condition_variable` | Thread signaling (works with `Mutex`) |
| `Atomic<T>` | `std::atomic<T>` | Lock-free atomic operations |
| `Future<T>` | `std::future<T>` | Asynchronous result retrieval |
| `Promise<T>` | `std::promise<T>` | Asynchronous result fulfillment |
| `ThreadPool` | — | Worker thread pool with task submission |
| `Queue<T>` | — | Thread-safe FIFO queue |

### Choosing the Right Category

Ask: "Does this class represent a *value*, a *thing*, or *infrastructure*?"

- A timecode, an image description, a list of strings — these are values. Use a data object.
- An audio processing node, a file watcher, a UI widget — these are things with identity. Derive from `ObjectBase`.
- A mutex, a thread pool, an atomic counter — these are infrastructure. Use a utility class (no `PROMEKI_SHARED_FINAL`, no `ObjectBase`).

When in doubt, prefer a data object. Most classes in a library like this are data.

### Sharing Data Objects Across Threads

Data objects are not internally thread-safe — concurrent reads and writes to the same instance require external synchronization. The standard pattern for sharing data between threads is to use the `Ptr` typedef (`SharedPtr`). The reference counting in `SharedPtr` is atomic, so passing a `Ptr` to another thread is safe. The pointed-to object itself is not synchronized, so the pattern relies on ownership handoff: once you pass a `Ptr` to another thread, you should not mutate the underlying object from the original thread.

**Pattern 1: Share a single data object via Ptr.**

When you need to pass one data object to another thread (e.g., pushing a `Frame::Ptr` through a pipeline, or emitting a signal with an `Image::Ptr`), wrap it in its `Ptr` type:

```cpp
// Producer thread
Frame::Ptr frame = Frame::Ptr::create(desc);
// ... fill frame data ...
link.pushFrame(frame);  // Ptr safely crosses thread boundary

// Consumer thread
auto [frame, err] = link.pullFrame();
// frame is now exclusively accessed by this thread
```

**Pattern 2: Build a composite structure and share via Ptr.**

When you need to share multiple related values across threads, compose them into a shareable data object and share that object via `Ptr`:

```cpp
class RenderJob {
        PROMEKI_SHARED_FINAL(RenderJob)
        public:
                using Ptr = SharedPtr<RenderJob>;
                // ... fields ...
        private:
                ImageDesc       _desc;
                Timecode        _startTC;
                List<String>    _layers;
};

// Submit to thread pool
RenderJob::Ptr job = RenderJob::Ptr::create(desc, tc, layers);
pool.submit([job]() {
        // job->desc(), job->startTC(), etc. — safe, no concurrent mutation
});
```

**Do not** protect data objects with a `Mutex` as a general practice — if you find yourself wrapping a data object in a mutex, you should instead restructure so that each thread works on its own copy or uses the `Ptr` handoff pattern. The exception is explicitly thread-safe classes like `Queue` that manage their own synchronization.

Simple data objects (no `PROMEKI_SHARED_FINAL`) are cheap to copy. When sharing simple types across threads, just copy them — no `Ptr` needed:

```cpp
Timecode tc = currentTimecode();
pool.submit([tc]() {
        // tc is a copy, safe to use
});
```

---

## File Layout

### Header Files

```cpp
/**
 * @file      filename.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <std-headers>
#include <promeki/dependency.h>

PROMEKI_NAMESPACE_BEGIN

// Forward declarations

class MyClass {
        public:
                // Types / enums
                // Static constants
                // Static methods
                // Constructors / destructor
                // Operators
                // Const accessors (getters)
                // Mutators (setters)
                // Other methods

        protected:
                // ...

        private:
                // Members
};

PROMEKI_NAMESPACE_END
```

### File Header Comment

Every file starts with a Doxygen `@file` block. Use `@copyright Howard Logic. All rights reserved.` and reference the license as: `See LICENSE file in the project root folder for license information.` with a trailing period and using "project root folder" (not "source root folder").

The `@author` tag is optional.

### Include Guards

Always use `#pragma once`. No `#ifndef` guards.

### Includes

Order:
1. C++ standard library headers (`<cstdint>`, `<vector>`, etc.)
2. Third-party headers (nlohmann/json, libvtc, etc.)
3. promeki headers (`<promeki/foo.h>`)

Use angle brackets for all includes: `<promeki/foo.h>`, not `"promeki/foo.h"`.

---

## Naming Conventions

### Classes and Types

- **PascalCase**: `String`, `Timecode`, `FrameRate`, `AudioBlock`, `ObjectBase`
- Type aliases: `using Iterator = ...;`, `using ConstIterator = ...;`
- Type alias naming: PascalCase — `RevIterator`, `ConstRevIterator`, `RationalType`
- Template type aliases at namespace level: `using Size2Du32 = Size2DTemplate<uint32_t>;`

### Convenience Type Aliases for Templates

When a template class is commonly instantiated with specific types, provide `using` aliases at namespace scope so callers never spell the template arguments directly. Every alias must include an explicit type suffix — there are no unsuffixed "default" aliases.

| Suffix | Underlying type | Example |
|---|---|---|
| `i8` | `int8_t` | `Point2Di8` |
| `u8` | `uint8_t` | `Size2Du8` |
| `i16` | `int16_t` | `Point2Di16` |
| `u16` | `uint16_t` | `Size2Du16` |
| `i32` | `int32_t` | `Point2Di32` = `Point<int32_t, 2>`, `Rect2Di32` = `Rect<int32_t>` |
| `u32` | `uint32_t` | `Size2Du32` = `Size2DTemplate<uint32_t>` |
| `i64` | `int64_t` | `Point2Di64` |
| `u64` | `uint64_t` | `Size2Du64` |
| `f` | `float` | `Size2Df` = `Size2DTemplate<float>` |
| `d` | `double` | `Size2Dd` = `Size2DTemplate<double>` |

Dimensionality is part of the base name; the type suffix always comes last (e.g. `Point3Di32`, `Line4Df`). Choose signed vs. unsigned based on the domain: coordinates and offsets are typically signed (`i32`), dimensions and sizes are typically unsigned (`u32`).

Always use the convenience alias in code, never the raw template instantiation. When adding a new template class, define aliases for all commonly used instantiations in the same header.

### Methods

- **camelCase** for all methods.
- **Getters**: bare noun, no `get` prefix — `width()`, `height()`, `size()`, `fps()`, `mode()`
- **Setters**: `set` prefix — `setWidth()`, `setHeight()`, `setParent()`
- **Predicates**: `is` prefix — `isValid()`, `isEmpty()`, `isDropFrame()`, `isNull()`
- **Boolean queries**: `has` prefix when checking for presence — `hasFormat()`, `hasParent()`
- **Count queries**: `*Count()` suffix — `compCount()`, `planeCount()`
- **Conversions**: `to*()` — `toString()`, `toDouble()`, `toFrameNumber()`, `toUpper()`
- **Static factories**: `from*()` or descriptive name — `fromString()`, `fromFrameNumber()`, `now()`, `number()`, `syserr()`

### Member Variables

- **Private members**: underscore prefix — `_width`, `_height`, `_mode`, `_parent`

### Enums

- Enum values: **PascalCase** — `Ok`, `Invalid`, `DropFrame`, `NotExist`
- Enum class or nested enum within the owning class.
- Well-known constant enums may use `PREFIX_Name` style for readability: `FPS_2997`, `FPS_Invalid`

### Standard Library Type Aliases

When a `std::` type appears in a class's **public API** — return types, parameter types, public members, or type aliases that users interact with — wrap it in a `using` alias inside the class so that library users never spell `std::` types directly:

```cpp
class Thread {
        public:
                using NativeHandle = std::thread::native_handle_type;

                NativeHandle nativeHandle() const;
                // ...
        private:
                // Private members may use std:: types directly when no
                // library wrapper exists (e.g., std::thread).
                std::thread     _thread;
};
```

Private members and implementation files may use `std::` types directly when no library wrapper exists, since they do not leak into the public interface.

### Prefer Library Wrappers Over Raw std:: Types

When a promeki wrapper exists for a `std::` type, always use the wrapper — even in private members and implementation files. This "eat your own dogfood" principle ensures consistency, exercises the library's own APIs, and catches usability gaps early.

| std:: type | promeki wrapper |
|---|---|
| `std::mutex` | `Mutex` |
| `std::shared_mutex` | `ReadWriteLock` |
| `std::condition_variable` | `WaitCondition` |
| `std::atomic<T>` | `Atomic<T>` |
| `std::future<T>` | `Future<T>` |
| `std::promise<T>` | `Promise<T>` |
| `std::pair<T, Error>` | `Result<T>` |
| `std::vector<T>` | `List<T>` |
| `std::map<K,V>` | `Map<K,V>` |
| `std::set<K>` | `Set<K>` |
| `std::string` | `String` |

Use the raw `std::` type only when no wrapper exists (e.g., `std::thread`, `std::packaged_task`) or when interfacing with third-party code that requires it.

### Macros

- All caps with `PROMEKI_` prefix: `PROMEKI_SHARED`, `PROMEKI_OBJECT`, `PROMEKI_SIGNAL`
- Utility macros: `PROMEKI_STRINGIFY`, `PROMEKI_ASSERT`, `PROMEKI_ARRAY_SIZE`

### Constants

- `static constexpr` inside the class.
- PascalCase or descriptive: `DefaultAlign`, `DefaultFormat`, `WhitespaceChars`, `npos`

---

## Indentation and Formatting

- **Indentation**: 8-space-wide tabs for all code.
- **Brace style**: Opening brace on the same line for control flow; class/function bodies use standard K&R style.
- **Access specifiers**: Indented one level from the class keyword (8 spaces).
- **Member declarations**: Indented two levels from the class keyword (16 spaces).
- **Alignment**: Align related member declarations with tabs/spaces for readability:
  ```cpp
  Mode            _mode;
  FlagsType       _flags  = 0;
  DigitType       _hour   = 0;
  ```
- **Single-line bodies**: Short getters/setters may be inline on one line:
  ```cpp
  bool isValid() const { return _valid; }
  uint32_t fps() const { return _format ? vtc_format_fps(_format) : 0; }
  ```
- **Return statements**: Void functions end with `return;` on a separate line (Qt convention).

---

## The SharedPtr / Copy-on-Write Pattern

SharedPtr provides reference-counted smart pointers with optional copy-on-write semantics (see [Object Categories](#object-categories) above).

### Shareable Data Objects Support SharedPtr

Every shareable data object includes the `PROMEKI_SHARED_FINAL` macro, which adds zero-cost `RefCount` and `_promeki_clone()` hooks so the object can be managed by `SharedPtr` without proxy indirection. Simple data objects do not include this macro — they are too small to benefit from reference counting.

Every shareable data object must provide the following type aliases in its `public:` section:

```cpp
class Image {
        PROMEKI_SHARED_FINAL(Image)
        public:
                using Ptr = SharedPtr<Image>;
                using List = promeki::List<Image>;
                using PtrList = promeki::List<Ptr>;
                // ...
};

// Usage: always MyClass::Ptr, never SharedPtr<MyClass>
Buffer::Ptr buf = Buffer::Ptr::create(1024);
Image::Ptr img = Image::Ptr::create(desc);
List<int>::Ptr sharedList = List<int>::Ptr::create();
```

- `Ptr` — SharedPtr alias for shared ownership.
- `List` — convenience alias for `promeki::List<MyClass>` (plain value list).
- `PtrList` — convenience alias for `promeki::List<Ptr>` (shared pointer list).

This convention makes shared ownership explicit at the call site and provides a consistent spelling throughout the codebase. Use `MyClass::Ptr` wherever you would have used a raw pointer for shared ownership.

### No Internal SharedPtr<Data>

No data objects use `SharedPtr<Data>` internally. All data objects store their data directly as member variables. The decision to share is made by the *composing* layer, not baked into each data object. If a caller needs shared ownership, they use `MyClass::Ptr` (i.e., `SharedPtr<MyClass>`) explicitly.

Media objects like `Image` and `Audio` hold their buffers via `Buffer::Ptr`, so pixel and sample data is shared across copies without deep copies. When the object itself is copied, only the descriptors and shared pointer references are duplicated — the actual buffer memory is not.

Do **not** use `SharedPtr<Data>` internally for any data object:
- Simple value types (Point, Size2D, Rational, UUID, etc.) — cheap to copy, no SharedPtr support needed.
- Shareable value types (String, AudioDesc, Image, Audio, etc.) — store data directly; use `Ptr` externally when sharing is needed.
- Template containers (List, Array, Map, Set) — wrap standard containers directly.
- Functional objects (ObjectBase-derived) — not copyable; use parent/child ownership.

### How to Implement (Shareable Classes)

Shareable classes store data directly as member variables and include `PROMEKI_SHARED_FINAL` so they can be managed by `SharedPtr` externally:

```cpp
class AudioDesc {
        PROMEKI_SHARED_FINAL(AudioDesc)
        public:
                using Ptr = SharedPtr<AudioDesc>;
                using List = promeki::List<AudioDesc>;
                using PtrList = promeki::List<Ptr>;
                // ... public API ...

        private:
                int         _sampleRate = 0;
                int         _channels   = 0;
                SampleFormat _format    = SampleFormat::Invalid;
};
```

No internal `SharedPtr<Data>`, no `d->field` / `d.modify()->field` pattern. Just direct member access. When shared ownership is needed, callers use `AudioDesc::Ptr`.

---

## Error Handling

### The Error Class

`Error` is an enum-based error class with codes like `Ok`, `Invalid`, `NotExist`, etc.

- `isOk()` / `isError()` for boolean checks
- `name()` / `desc()` for human-readable text
- `syserr()` to create from `errno`

### Return Patterns

Use one of these patterns consistently within a class. Preferred order:

1. **`Result<T>`** — Best for factory/parse methods where both a value and error status are needed. `Result<T>` is defined in `result.h` as `Pair<T, Error>` and supports structured bindings:
   ```cpp
   static Result<Timecode> fromString(const String &str);
   auto [tc, err] = Timecode::fromString(str);
   ```
   Use `Result<T>` (not `std::pair<T, Error>`) in all public APIs. The helper functions `makeResult(value)` and `makeError<T>(err)` simplify construction.

2. **Direct `Error` return** — For operations that don't produce a value:
   ```cpp
   Error open(const String &path);
   ```

3. **`Error *` output parameter** — For conversion methods where error detail is optional:
   ```cpp
   int toInt(Error *err = nullptr) const;
   ```

Avoid mixing patterns within the same class without good reason.

### Avoid `bool` for Error Reporting

Do not use `bool` return values or `bool *ok` output parameters to indicate success or failure. Use the `Error` class instead. `Error` provides specific, diagnosable failure reasons (`NoMem`, `NoPermission`, `Timeout`, etc.) whereas `bool` discards all context about *why* something failed, making debugging and error recovery harder. The only exception is pure predicates that answer a yes/no question (e.g., `isEmpty()`, `contains()`), which are not error reporting.

When wrapping system calls that set `errno`, use `Error::syserr()` to capture the specific failure:
```cpp
if(mlock(ptr, size) != 0) {
        Error err = Error::syserr();
        promekiWarn("mlock failed: %s", err.desc().cstr());
}
```

---

## Container and Wrapper Types

### String

- Wraps `std::string` as a plain shareable value type (no internal SharedPtr).
- Use `String` everywhere in the public API, not `std::string`.
- `stds()` provides access to the underlying `std::string` when needed.
- Implicit conversions to `std::string&`, `const char*` are provided for interop.

### List<T>

- Wraps `std::vector<T>` with Qt-inspired naming.
- Use `pushToBack()` / `popFromBack()` (not `push_back` / `pop_back`).
- Use `isEmpty()` (not `empty()`), `size()`, `contains()`.
- Supports `operator+=` for appending items or lists.
- Use `List<T>` for type aliases in public APIs (not `std::vector<T>` directly).

### Array<T, N>

- Wraps `std::array<T, N>` with arithmetic operators and utility methods.
- Use for fixed-size numerical data (color components, matrix rows, etc.).

### Buffer

- Manages raw memory with optional ownership and alignment.
- A shareable value type — use `Buffer::Ptr` (`SharedPtr<Buffer>`) for shared ownership.
- `Buffer::List` is `List<Buffer>` (plain buffer list); `Buffer::PtrList` is `List<Buffer::Ptr>` (shared pointer list). Media classes hold buffers via `Buffer::Ptr` for zero-copy sharing.
- Tied to `MemSpace` for allocation abstraction.

---

## TypeRegistry Types

ColorModel, MemSpace, PixelFormat, and PixelDesc use the [TypeRegistry pattern](@ref typeregistry). Each is a lightweight wrapper around a `const Data *` pointer, resolved once from an ID via a construct-on-first-use registry. Copying is a pointer copy; comparison is a pointer comparison.

### Pass the Wrapper, Not the ID

IDs exist only for two purposes: constructing a wrapper, and registering new types. **All function parameters, return values, and member variables must use the wrapper class**, not the ID enum. This avoids redundant registry lookups:

```cpp
// WRONG — every caller pays for a registry lookup
Image(size_t w, size_t h, PixelDesc::ID pd);

// RIGHT — caller passes a resolved wrapper (or the compiler
// inserts the implicit conversion from ID once automatically)
Image(size_t w, size_t h, const PixelDesc &pd);
```

Because every wrapper has an **implicit** constructor from its ID enum, changing a parameter from `ID` to `const Wrapper &` is source-compatible — callers that pass an enum constant like `PixelDesc::RGBA8_sRGB` compile unchanged.

Do **not** create `static const` wrapper objects for well-known IDs, as this introduces static initialization order uncertainty. Instead, pass the ID enum constant directly and let the implicit conversion happen at the call site.

### ID Disambiguation Guards

TypeRegistry ID enums are unscoped, so the compiler treats them as integer-compatible in overload resolution. When a class has another constructor whose leading parameters are integer-compatible types (e.g. `uint8_t`), the ID may silently match the wrong overload. In this case, provide an explicit ID overload as a **disambiguation guard**:

```cpp
// Primary constructor
Color(const ColorModel &model, float c0, float c1, float c2, float c3 = 1.0f);

// Disambiguation guard — prevents ColorModel::sRGB (int-like enum)
// from matching Color(uint8_t, uint8_t, uint8_t, uint8_t)
Color(ColorModel::ID id, float c0, float c1, float c2, float c3 = 1.0f);
```

This is the **only** case where an ID should appear in a public parameter list. Always document the guard with a comment explaining why it exists.

---

## Well-Known Enums

libpromeki uses the runtime-typed `Enum` class (`include/promeki/enum.h`) for any enumerated value that needs to round-trip through `Variant`, `VariantDatabase`, JSON config files, or user-facing CLI/GUI options. Every **well-known** enum — meaning one that is shared across subsystems or exposed as configuration — goes in a single header: `include/promeki/enums.h`.

### Use the `TypedEnum<Derived>` CRTP Pattern

All enums in `enums.h` inherit from `TypedEnum<Self>`. This gives the enum a compile-time type identity so function signatures can take a concrete `const VideoPattern &` (etc.) instead of a generic `const Enum &`, while still participating in every runtime Enum API via public inheritance.

```cpp
// include/promeki/enums.h
class VideoPattern : public TypedEnum<VideoPattern> {
        public:
                static inline const Enum::Type Type = Enum::registerType("VideoPattern",
                        {
                                { "ColorBars", 0 },
                                { "Ramp",      1 },
                                // ...
                        },
                        0);  // default: ColorBars

                using TypedEnum<VideoPattern>::TypedEnum;

                static const VideoPattern ColorBars;
                static const VideoPattern Ramp;
                // ...
};

inline const VideoPattern VideoPattern::ColorBars { 0 };
inline const VideoPattern VideoPattern::Ramp      { 1 };
```

Key elements:

- **`class`, not `struct`** — the wrapper is a real type, not a namespace for loose constants.
- **`static inline const Enum::Type Type`** defined *inside* the class body so it is initialized before the per-value constants that follow.
- **`using TypedEnum<Self>::TypedEnum;`** inherits the three base constructors (default / int / String name).
- **`static const` member declarations inside the class, `inline const` definitions outside** — declaring them inline inside the class does not work because the enclosing class is not yet complete when the brace initializer runs. Defining them outside the class body after it closes is mandatory.
- **No `Type` argument on the static definitions** — the inherited `TypedEnum(int)` pulls `Derived::Type` automatically, so `{ 0 }` is all that is needed.

### Where Well-Known Enums Live

Every enum that is used outside a single implementation file **must** live in `include/promeki/enums.h`. Do not scatter them across subsystem headers. Private, single-file enums (like a scoped `enum class` used only inside one `.cpp`) stay local — only enums that cross module boundaries or become config values belong in `enums.h`.

When adding a new well-known enum, also add a brief Doxygen comment explaining what subsystem it belongs to and which `MediaConfig` / `VariantDatabase` keys consume it.

### Function Signatures

Prefer concrete `const TypedEnum-derived &` parameters over `const Enum &` whenever the function only accepts one specific enum kind. This gives compile-time type safety at zero runtime cost:

```cpp
// PREFER — compile-time type check; VideoPattern cannot be passed here
String formatByteCount(const ByteCountStyle &style);

// Only use generic Enum when the function genuinely accepts any enum
// (serialization, equality, generic lookup helpers)
DataStream &operator<<(const Enum &e);
```

Because `TypedEnum<T>` inherits publicly from `Enum` and adds no members, passing a typed value to an API that takes `const Enum &` still works via implicit derived-to-base conversion.

### Backward Compatibility

Migrating an existing enum from the old `struct { static inline const Enum X{Type,N}; }` pattern to `TypedEnum<Self>` is source-compatible:

- `cfg.set(key, VideoPattern::ColorBars)` — still works; `std::variant`'s converting constructor picks the `Enum` alternative via derived-to-base conversion.
- `Enum e = VideoPattern::ColorBars;` — still works; slicing is safe because `TypedEnum` adds no data members.
- `Enum::lookup("VideoPattern::ColorBars")` — unchanged on the wire format.

---

## ObjectBase and Signals/Slots

Functional objects (see [Object Categories](#object-categories)) derive from `ObjectBase` to gain identity, parent/child ownership, and signal/slot communication.

- Use `PROMEKI_OBJECT(ClassName, ParentClassName)` macro in the public section.
- Define signals with `PROMEKI_SIGNAL(name, ArgTypes...)`.
- Define slots with `PROMEKI_SLOT(name, ArgTypes...)`.
- Connect with `ObjectBase::connect(&obj.signalNameSignal, &other.slotNameSlot)`.
- Functional objects are **not** copyable — they use pointer-based parent/child trees.
- Do **not** use `PROMEKI_SHARED_FINAL` on ObjectBase-derived classes. They have their own ownership model and are never managed by SharedPtr.

---

## Namespace

All code goes in the `promeki` namespace using macros:
```cpp
PROMEKI_NAMESPACE_BEGIN
// ...
PROMEKI_NAMESPACE_END
```

Do not use `using namespace promeki;` in headers.

---

## Stream Operator Support (TextStream / DataStream)

New data classes should provide free `operator<<` and `operator>>` overloads for `TextStream` and, where binary serialization makes sense, `DataStream`. These overloads live in the class's own header (not in `textstream.h` or `datastream.h`), following the same pattern Qt uses for placing `QTextStream`/`QDataStream` operators alongside each type.

### TextStream

Every data class that has a meaningful text representation should provide:

```cpp
// In myclass.h — forward-declare TextStream to avoid circular includes
class TextStream;

TextStream &operator<<(TextStream &stream, const MyClass &val);
TextStream &operator>>(TextStream &stream, MyClass &val);
```

The `operator<<` should produce a human-readable representation consistent with `toString()`. The `operator>>` should parse the same format when round-tripping makes sense; omit it when it doesn't (e.g., complex summary formats).

### DataStream

Data classes that participate in binary serialization (pipeline frames, object state, network protocols) should provide:

```cpp
// In myclass.h — forward-declare DataStream to avoid circular includes
class DataStream;

DataStream &operator<<(DataStream &stream, const MyClass &val);
DataStream &operator>>(DataStream &stream, MyClass &val);
```

These write/read the class fields in a well-defined order. See `devplan/core_streams.md` for the wire format conventions.

### When to Omit

- Utility classes (Mutex, Queue, etc.) do not need stream operators.
- ObjectBase-derived functional objects do not need stream operators (use saveState/loadState instead).
- Simple wrapper types where the underlying primitive's operator already suffices (e.g., `Atomic<T>`) do not need their own operators.

---

## Documentation (Doxygen)

All public API surfaces should be documented with Doxygen comments using the `/** ... */` style.

### Class Documentation

Every class should have a `@brief` line followed by a longer description when appropriate. Use `@tparam` for template parameters and `@note` for important caveats:

```cpp
/**
 * @brief Thread-safe FIFO queue.
 *
 * All public methods are safe to call concurrently from multiple
 * threads.  Internally synchronized with a mutex and condition
 * variable.
 *
 * @note waitForEmpty() indicates that all items have been removed
 * from the queue, not that they have been fully processed by the
 * consumer.
 *
 * @tparam T The element type stored in the queue.
 */
template <typename T>
class Queue { ... };
```

### Method Documentation

Every public method should have at minimum a `@brief` line. Document parameters, return values, and template parameters as applicable:

- `@param name` — describe each parameter. Use `@param[out]` or `@param[in,out]` for output/inout parameters.
- `@return` — describe the return value. For `Error` returns, state the possible error codes.
- `@tparam` — describe template parameters on templated methods.

```cpp
/**
 * @brief Removes and returns the front element, blocking until
 *        one is available or the timeout expires.
 * @param timeoutMs Maximum time to wait in milliseconds.  A value
 *        of zero (the default) waits indefinitely.
 * @return A Result containing the dequeued element and Error::Ok,
 *         or a default-constructed element and Error::Timeout.
 */
Result<T> pop(unsigned int timeoutMs = 0);
```

### Thread Safety Documentation

Every class must document its thread safety guarantees in the class-level Doxygen comment. Use one of these standard statements:

- **Thread-safe**: "All public methods are safe to call concurrently from multiple threads." — The class handles its own synchronization internally. Example: `Queue`.
- **Not thread-safe**: "This class is not thread-safe. External synchronization is required for concurrent access." — The default for most data objects and simple types.
- **Conditionally thread-safe**: "Distinct instances may be used concurrently. Concurrent access to a single instance requires external synchronization." — The common case for value types that are safe to copy across threads but not to mutate concurrently. Example: `List`, `Map`.
- **Thread-affine**: "This class must only be used from the thread that created it (or the thread it was moved to via `moveToThread()`)." — For ObjectBase-derived classes bound to an EventLoop.

When individual methods have different thread safety from the class default, document them at the method level. For example, a class that is generally not thread-safe but has a specific atomic query method should note that on the method.

```cpp
/**
 * @brief Manages multicast group membership.
 *
 * This class is thread-affine: all methods must be called from
 * the EventLoop thread this object belongs to.  Signals may be
 * connected from any thread.
 */
class MulticastManager : public ObjectBase { ... };
```

### What Not to Document

- Private members and implementation details do not require Doxygen comments (plain `//` comments are fine when needed).
- Trivial getters/setters that are self-explanatory from their name and signature may omit Doxygen, but prefer documenting them when there are non-obvious semantics.

---

## Testing

Tests use the [doctest](https://github.com/doctest/doctest) framework, vendored in `thirdparty/doctest/`.

### File Layout

- Test files live in `tests/` and are named after the class they test (e.g., `tests/string.cpp` for `String`).
- Each test file includes `<doctest/doctest.h>` (not a promeki wrapper).
- A shared `tests/doctest_main.cpp` defines the `main()` entry point with `DOCTEST_CONFIG_IMPLEMENT`. Individual test files should **not** define `main()`.
- Tests are compiled and run automatically during the build via CTest.

### Structure

Use `TEST_CASE` for each class or logical grouping, and `SUBCASE` for individual behaviors:

```cpp
#include <doctest/doctest.h>
#include <promeki/myclass.h>

using namespace promeki;

TEST_CASE("MyClass") {
        SUBCASE("default construction") {
                MyClass obj;
                CHECK(obj.isValid());
        }

        SUBCASE("fromString") {
                auto [result, err] = MyClass::fromString("hello");
                CHECK(err.isOk());
                CHECK(result.name() == "hello");
        }
}
```

### Assertions

Prefer these doctest macros:

- `CHECK(expr)` — non-fatal assertion (test continues on failure).
- `CHECK_FALSE(expr)` — check that an expression is false.
- `CHECK(val == doctest::Approx(expected))` — floating-point comparison.
- `CHECK(val == doctest::Approx(expected).epsilon(0.001))` — with custom tolerance.
- `REQUIRE(expr)` — fatal assertion (test stops on failure). Use sparingly, only when subsequent checks depend on this one passing.

### Build Integration

All library code is built into a single `promeki` shared library with one test executable (`unittest-promeki`). It is registered with CTest. The `run-tests` custom target runs all tests as part of the normal build.

---

## Logging

All logging goes through the `Logger` system via the convenience macros in `logger.h`. Follow these conventions so that log output is useful for diagnosing problems without access to a debugger.

### Use the Right Level

| Macro | When to use |
|---|---|
| `promekiDebug(fmt, ...)` | Per-module diagnostic output — gated on `PROMEKI_DEBUG` and compiled out in Release builds. Use liberally inside the library. |
| `promekiInfo(fmt, ...)` | Normal operational events (startup, shutdown, configuration applied). |
| `promekiWarn(fmt, ...)` | Recoverable problems that are worth investigating (fallbacks, unexpected but handled conditions). |
| `promekiErr(fmt, ...)` | Errors that affect correctness or represent failed operations. |

### Register Every Source File for Debug Logging

Any `.cpp` file that contains `promekiDebug()` calls must register a debug module name near the top, after the includes and inside the `promeki` namespace:

```cpp
#include <promeki/myclass.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MyClass)
```

This lets users activate debug output for individual modules at runtime via `PROMEKI_DEBUG=MyClass`.

### Capture All Relevant State

A log message should contain enough context to understand what happened without cross-referencing other output. Include the values of variables, arguments, and state that led to the message:

```cpp
// BAD — no context, impossible to diagnose
promekiWarn("Open failed");

// GOOD — captures the path, the error, and enough state to reproduce
promekiWarn("Failed to open '%s': %s", path.cstr(), err.desc().cstr());
```

### Identify the Object Instance

When logging from an instance method, prefix the message with the class name and `this` pointer so you can correlate log lines with a specific object, especially when multiple instances of the same class exist:

```cpp
promekiDebug("ThreadPool(%p): spawning thread %d (active %d, waiting %d, max %d)",
             (void *)this, _threadCount, _activeCount, _waitingCount,
             _maxThreadCount);

promekiDebug("MediaIO(%p): configure() called with %d keys",
             (void *)this, config.size());
```

The format is `ClassName(%p):` followed by the message. For classes that have a user-visible name, include that too:

```cpp
promekiDebug("Thread(%p '%s'): started, priority %d",
             (void *)this, _name.cstr(), _priority);
```

### Instrument the Library with promekiDebug

Use `promekiDebug()` generously throughout library code so the library can be debugged in-situ at runtime. Key places to add debug logging:

- **Construction and destruction** — log creation/teardown of heavyweight objects with their configuration.
- **State transitions** — log when an object changes state (started, stopped, resized, connected, disconnected).
- **Configuration changes** — log when settings are applied, with before and after values.
- **Error recovery** — log the fallback path taken when a recoverable error occurs.
- **Thread lifecycle** — log thread start, naming, and exit.

Because `promekiDebug()` is compiled out in Release builds and skipped at runtime unless the module is activated, there is no performance cost to having extensive instrumentation.

---

## Miscellaneous

- **C++ standard**: C++20.
- **Build system**: CMake 3.22+.
- **No `using namespace std;`** in any file.
- **Forward declare** in headers where possible to minimize include chains.
- **`#pragma once`** for all headers.
- **Avoid reserved identifiers**: Do not use double-underscore prefixes (`__Foo`) as these are reserved by the C++ standard. Use a `Detail` namespace or `Impl` suffix instead.
- **Explicit `return;`** at the end of void functions is the project convention — follow it for consistency.
