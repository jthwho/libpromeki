# TypeRegistry Pattern {#typeregistry}

Lightweight, extensible descriptors for immutable type data.

The TypeRegistry pattern provides a uniform way to define, look up,
and extend immutable descriptor records. It is used throughout
promeki for types whose identity is a fixed set of read-only
properties (primaries, transfer functions, memory operations, etc.)
and where users may need to register additional entries at runtime.

## Overview {#tr_overview}

A TypeRegistry class has four parts:

1. **Data struct** — an immutable record holding all properties for
   one entry (primaries, name, function pointers, etc.). Defined
   publicly in the header so callers can populate it for
   `registerData()`; the registry itself (and its populated entries)
   lives in the implementation file.

2. **ID enum** — an unscoped enum with named constants for each
   well-known entry. User-defined entries obtain new IDs from
   `registerType()`, which starts at the `UserDefined` sentinel.

3. **Registry** — a construct-on-first-use singleton that maps IDs
   to Data records. Well-known entries are populated in the registry
   constructor; user entries are added via `registerData()`.

4. **Wrapper class** — a trivial inline class that stores a single
   `const Data *` pointer. All accessors are inline and compile away
   to direct struct member access. The wrapper is the only type that
   should appear in APIs and member variables; naked IDs exist only
   for constructing a wrapper.

## Construction and Copying {#tr_construction}

Constructing a wrapper from an ID performs a single registry lookup
and caches the resulting `const Data *`. After construction, all
access is a pointer dereference — no lookup, no indirection beyond
the pointer itself.

Copying a wrapper copies one pointer. Comparing two wrappers
compares two pointers. Both are trivially cheap.

```cpp
// Construction from a well-known ID
ColorModel cm(ColorModel::sRGB);

// Copy is just a pointer copy
ColorModel cm2 = cm;

// Comparison is pointer comparison
assert(cm == cm2);
```

## Registering User-Defined Types {#tr_extension}

Users extend the registry in two steps:

1. Call `registerType()` to allocate a unique ID. This uses a
   lock-free atomic counter, making it thread-safe and suitable
   for use in static initializers.

2. Call `registerData()` with a populated Data struct whose `id` field
   is set to the new ID. After this call, constructing a wrapper
   from that ID resolves to the registered data.

```cpp
// Allocate a new ID
ColorModel::ID myID = ColorModel::registerType();

// Populate and register the data
ColorModel::Data myData;
myData.id   = myID;
myData.type = ColorModel::TypeRGB;
myData.name = "MyCustomRGB";
// ... fill in remaining fields ...
ColorModel::registerData(std::move(myData));

// Now usable as a normal ColorModel
ColorModel cm(myID);
assert(cm.name() == "MyCustomRGB");
```

## Design Guidelines {#tr_guidelines}

- **Never store a naked ID.** IDs exist only to construct the
  wrapper. Store and pass the wrapper instead — it is the same
  size as a pointer and avoids repeated lookups.

- **The wrapper is the API type.** Function parameters, return
  values, and member variables should use the wrapper class, not
  the ID enum. Pass wrappers by `const` reference:

  ```cpp
  // WRONG -- forces a registry lookup at every call site
  Image(size_t w, size_t h, PixelFormat::ID pd);

  // RIGHT -- the caller already has a resolved wrapper (or the
  // compiler inserts the implicit conversion once)
  Image(size_t w, size_t h, const PixelFormat &pd);
  ```

  Because every wrapper has an implicit constructor from its ID
  enum, changing a parameter from `ID` to `const Wrapper &` is
  source-compatible: callers that pass an enum constant like
  `PixelFormat::RGBA8_sRGB` compile unchanged.

- **Data is immutable.** Once registered, a Data record must not
  be modified. This guarantees that concurrent reads from multiple
  threads are safe without locking.

- **Inline everything.** All wrapper accessors should be inline
  in the header so the compiler can see through the abstraction.
  The wrapper should compile away to the same code as accessing
  the Data struct directly.

## ID Disambiguation Guards {#tr_disambiguation}

TypeRegistry ID enums are unscoped, so the compiler treats them as
integers in overload resolution. When a class has another
constructor whose first parameter is an integer-compatible type
(e.g. `uint8_t`), the ID may silently match the wrong overload.

In this situation, provide an explicit ID overload as a
**disambiguation guard** — a constructor that accepts the ID type
directly and forwards to the wrapper overload:

```cpp
// Primary constructor -- takes the wrapper
Color(const ColorModel &model, float c0, float c1, float c2, float c3 = 1.0f);

// Disambiguation guard -- prevents ColorModel::sRGB (an int-like
// enum) from matching Color(uint8_t, uint8_t, uint8_t, uint8_t)
Color(ColorModel::ID id, float c0, float c1, float c2, float c3 = 1.0f);
```

This is the **only** case where an ID should appear in a public
parameter list. Document the overload with a comment explaining
why it exists so it is not removed during future cleanup.

## Classes Using This Pattern {#tr_classes}

| Class | Data Struct | Description |
|-------|-------------|-------------|
| `ColorModel` | `ColorModel::Data` | Color model / color space descriptors |
| `MemSpace` | `MemSpace::Ops` | Memory space operation tables |
| `PixelMemLayout` | `PixelMemLayout::Data` | Pixel memory layout (components, bit depths, planes) |
| `PixelFormat` | `PixelFormat::Data` | Full pixel description (format + color model + ranges) |

**See also:** `ColorModel`, `MemSpace`, `PixelMemLayout`, `PixelFormat`.
