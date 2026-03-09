# libpromeki Coding Standards

This document describes the coding conventions and design patterns used throughout libpromeki. All new code should follow these standards. Existing code that deviates should be updated when it is being modified for other reasons.

## Design Philosophy

libpromeki provides Qt-inspired C++ core classes built on top of the C++ standard library. The goal is to offer friendlier, less verbose APIs than `std::` while retaining full interoperability. Key principles:

- **Value semantics with copy-on-write**: Classes that hold data use `SharedPtr<Data>` internally so copies are O(1) and mutations only deep-copy when shared.
- **No raw ownership**: Memory is managed via `SharedPtr`, `Buffer`, or RAII. Raw pointers are only used for non-owning references (e.g., parent/child relationships in `ObjectBase`).
- **Error codes over exceptions**: Use the `Error` class and return values rather than throwing. Constructors never throw.
- **Minimal abstraction**: Wrap `std::` types to improve ergonomics, but don't add layers of indirection for their own sake.
- **Blocking calls must support timeouts**: Any function that can block the calling thread should accept a `timeoutMs` parameter (type `unsigned int`, in milliseconds). The default value should be `0`, meaning "wait indefinitely." When a timeout expires, the function should return `Error::Timeout`. This convention applies unless the specific use case makes millisecond-granularity timeouts inappropriate (e.g., a frame-accurate deadline might use a different unit).

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
                // Inner Data class (if SharedPtr pattern)
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
- **SharedPtr data member**: simply `d` — `SharedPtr<Data> d;`
- **Inner Data class fields**: no prefix, camelCase — `desc`, `planeList`, `imageList`

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

This is a central design decision. Classes that hold non-trivial data should use the SharedPtr pattern for implicit sharing with copy-on-write semantics.

### When to Use SharedPtr

Use it for classes that:
- Hold dynamically allocated or non-trivial data (images, buffers, strings, audio)
- Are frequently copied by value
- Benefit from O(1) copies with deferred deep-copy on mutation

Do **not** use it for:
- Small value types (Point, Size2D, Rational, FourCC) — these are cheap to copy directly.
- Template containers (List, Array) — these wrap `std::vector`/`std::array` directly.
- ObjectBase-derived objects — these use parent/child ownership instead.

### How to Implement

1. Declare a private inner `Data` class with the `PROMEKI_SHARED_FINAL(Data)` macro:
   ```cpp
   class MyClass {
           public:
                   // ... public API ...

           private:
                   class Data {
                           PROMEKI_SHARED_FINAL(Data)
                           public:
                                   SomeType field1;
                                   OtherType field2;

                                   Data() = default;
                                   Data(const Data &o) = default;
                   };

                   SharedPtr<Data> d;
   };
   ```

2. Default constructor creates a valid (but empty) shared data:
   ```cpp
   MyClass() : d(SharedPtr<Data>::create()) { }
   ```

3. **Const access** uses `d->field` (no copy triggered):
   ```cpp
   int width() const { return d->desc.width(); }
   ```

4. **Mutable access** uses `d.modify()->field` (triggers deep copy if shared):
   ```cpp
   void setWidth(int w) { d.modify()->desc.setWidth(w); }
   ```

5. Expose `referenceCount()` for debugging:
   ```cpp
   int referenceCount() const { return d.referenceCount(); }
   ```

### Copy-on-Write Disabled

For types where COW semantics are wrong (e.g., `Buffer` manages external memory), disable COW:
```cpp
SharedPtr<Data, false> d;  // Reference counting only, no COW
```

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

- Wraps `std::string` with SharedPtr COW.
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
- Uses `SharedPtr<Data, false>` (ref counting without COW).
- Tied to `MemSpace` for allocation abstraction.

---

## ObjectBase and Signals/Slots

Classes that need signals/slots and parent-child relationships derive from `ObjectBase`.

- Use `PROMEKI_OBJECT(ClassName, ParentClassName)` macro in the public section.
- Define signals with `PROMEKI_SIGNAL(name, ArgTypes...)`.
- Define slots with `PROMEKI_SLOT(name, ArgTypes...)`.
- Connect with `ObjectBase::connect(&obj.signalNameSignal, &other.slotNameSlot)`.
- ObjectBase objects are **not** copyable — they use pointer-based parent/child trees.

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
