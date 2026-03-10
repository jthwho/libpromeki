# Coding Standards

This document describes the coding conventions and design patterns used throughout libpromeki. All new code should follow these standards. Existing code that deviates should be updated when it is being modified for other reasons.

## Design Philosophy

libpromeki provides Qt-inspired C++ core classes built on top of the C++ standard library. The goal is to offer friendlier, less verbose APIs than `std::` while retaining full interoperability. Key principles:

- **Value semantics**: All data classes are plain value types. The layer above decides whether to use `MyClass::Ptr` (SharedPtr) for shared ownership.
- **No raw ownership**: Memory is managed via `SharedPtr`, `Buffer`, or RAII. Raw pointers are only used for non-owning references (e.g., parent/child relationships in `ObjectBase`).
- **Error codes over exceptions**: Use the `Error` class and return values rather than throwing. Constructors never throw.
- **Minimal abstraction**: Wrap `std::` types to improve ergonomics, but don't add layers of indirection for their own sake.
- **Blocking calls must support timeouts**: Any function that can block the calling thread should accept a `timeoutMs` parameter (type `unsigned int`, in milliseconds). The default value should be `0`, meaning "wait indefinitely." When a timeout expires, the function should return `Error::Timeout`. This convention applies unless the specific use case makes millisecond-granularity timeouts inappropriate (e.g., a frame-accurate deadline might use a different unit).

---

## Object Categories {#object-categories}

libpromeki classes fall into two distinct categories with different ownership, copying, and lifetime rules.

### Data Objects

Data objects represent values or containers of values. They are the vast majority of classes in the library. Key traits:

- **Copyable by value.** They can be freely copied, assigned, passed by value, and stored in containers.
- **No identity.** Two data objects with the same contents are interchangeable. They have no inherent notion of "this specific instance."
- **Thread-safe when shared.** When managed by `SharedPtr`, the reference counting is atomic. Individual instances are not internally synchronized unless the class documents otherwise (e.g., `Queue`).

Data objects subdivide into two categories based on size, usage patterns, and sharing semantics. See also the @ref dataobjects "Data Object Categories" page.

| Category | Internal storage | SharedPtr support | Examples |
|---|---|---|---|
| **Simple** | Direct member variables | None — no `PROMEKI_SHARED_FINAL`, no `RefCount` overhead | `Point`, `Size2D`, `Rational`, `FourCC`, `UUID`, `Timecode`, `TimeStamp`, `XYZColor` |
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

### Choosing the Right Category

Ask: "Does this class represent a *value* or a *thing*?"

- A timecode, an image description, a list of strings — these are values. Use a data object.
- An audio processing node, a file watcher, a UI widget — these are things with identity. Derive from `ObjectBase`.

When in doubt, prefer a data object. Most classes in a library like this are data.

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
- Template type aliases at namespace level: `using Size2D = Size2DTemplate<size_t>;`

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

1. **`std::pair<T, Error>`** — Best for factory/parse methods where both a value and error status are needed:
   ```cpp
   static std::pair<Timecode, Error> fromString(const String &str);
   auto [tc, err] = Timecode::fromString(str);
   ```

2. **Direct `Error` return** — For operations that don't produce a value:
   ```cpp
   Error open(const String &path);
   ```

3. **`Error *` output parameter** — For conversion methods where error detail is optional:
   ```cpp
   int toInt(Error *err = nullptr) const;
   ```

4. **`bool *ok` output parameter** — For template/generic conversions:
   ```cpp
   template <typename T> T to(bool *ok = nullptr) const;
   ```

Avoid mixing patterns within the same class without good reason.

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
 * @return A pair of the dequeued element (default-constructed on
 *         timeout) and Error::Ok or Error::Timeout.
 */
std::pair<T, Error> pop(unsigned int timeoutMs = 0);
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

Each library (`core`, `proav`, `music`) has its own test executable (`unittest-core`, `unittest-proav`, `unittest-music`). Test executables link against their respective library and are registered with CTest. The `run-tests` custom target runs all tests as part of the normal build.

---

## Miscellaneous

- **C++ standard**: C++20.
- **Build system**: CMake 3.22+.
- **No `using namespace std;`** in any file.
- **Forward declare** in headers where possible to minimize include chains.
- **`#pragma once`** for all headers.
- **Avoid reserved identifiers**: Do not use double-underscore prefixes (`__Foo`) as these are reserved by the C++ standard. Use a `Detail` namespace or `Impl` suffix instead.
- **Explicit `return;`** at the end of void functions is the project convention — follow it for consistency.
