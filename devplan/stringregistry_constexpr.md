# StringRegistry: compile-time ID registration via FNV-1a hashing

## Context

`StringRegistry<Tag>` currently assigns sequential `uint32_t` IDs at registration time, so well-known IDs declared as

```cpp
static inline const ID Title = declareID("Title", spec);
```

are technically *static initializers*, not compile-time constants. Their integer values depend on the order in which TUs touch the registry, which means the ID cannot be used in `constexpr` contexts (switch labels, `static_assert`, template parameters, constexpr-if) and is not stable across runs.

We want well-known IDs to be true `constexpr` values, derived purely from the name at compile time. The name→ID mapping becomes deterministic, and IDs gain the ability to participate in compile-time machinery while the runtime registry remains for reverse lookup (`id.name()`), serialization, and the `VariantDatabase` spec store.

## Design

### Hashing and Item layout

- **Hash algorithm**: FNV-1a 64-bit, computed by a `constexpr` free function `hashName(const char *)`.
- **Item storage**: `_id` becomes `uint64_t` (was `uint32_t`). Item remains a single scalar (one 8-byte word).
- **Invalid sentinel**: `StringRegistry<Tag>::InvalidID = UINT64_MAX`.
- **Stability**: IDs are now deterministic across runs. The `@warning` note in the existing header about "must not be persisted" is replaced with a note that IDs ARE stable across runs for the same name (but not across renames).

### Item API

```cpp
class Item {
    public:
        Item() = default;

        // Runtime construction: hashes the name, registers it for reverse
        // lookup, probes on collision so registration never fails.
        Item(const String &name);
        Item(const char *name);

        // Compile-time construction for well-known IDs. Pure hash; no
        // registration. Intended for use with declareID (which also
        // registers via the runtime path) or where no reverse lookup
        // is needed.
        static constexpr Item literal(const char *name) {
            Item item;
            item._id = hashName(name);
            return item;
        }

        static constexpr Item fromId(uint64_t id) { Item i; i._id = id; return i; }
        constexpr uint64_t id() const { return _id; }
        constexpr bool isValid() const { return _id != InvalidID; }
        constexpr bool operator==(const Item &o) const { return _id == o._id; }
        constexpr bool operator!=(const Item &o) const { return _id != o._id; }
        constexpr bool operator<(const Item &o)  const { return _id <  o._id; }

        static Item find(const String &name);   // lookup-only, InvalidID if missing

    private:
        uint64_t _id = InvalidID;
};
```

### Registry internals

Replace the existing `Map<String, uint32_t> _map` + `List<String> _names` pair with a single:

```cpp
Map<uint64_t, String> _names;  // hash (or probed slot) -> canonical name
```

Two registration entry points share this map:

- `findOrCreateStrict(const String &name)` — used by `declareID`:
    - Compute `h = hashName(name)`.
    - If `_names` has no entry for `h`, insert `{h, name}` and return `h`.
    - If `_names[h] == name`, return `h` (idempotent — expected for well-known IDs referenced from multiple TUs).
    - Otherwise: hash collision between two distinct well-known names. This is fatal: `promekiErr()` a message naming both strings and the hash, then `PROMEKI_ABORT` (unconditional, not debug-only). A colliding well-known ID produces silent drift from `Item::literal` and must never be allowed to run.

- `findOrCreateProbe(const String &name)` — used by the runtime `Item(string)` / `Item(const char *)` constructors:
    - Compute `h = hashName(name)`.
    - Linear probe `h, h+1, h+2, ...` until finding either `_names[slot] == name` (existing) or an empty slot (insert and return). Registration never fails.
    - `findId(name)` and `Item::find(name)` use the same probe so lookups stay consistent with registrations.
    - Note: for a name that has *not* yet collided, the probe terminates at its own hash on the first step, so the common-case runtime cost is identical to a direct lookup.

- `name(uint64_t id) const` → map lookup on `_names`; empty `String()` on miss.
- `contains(const String &str) const` → delegates to `findId`.

Both entry points run under the existing `ReadWriteLock` with the same fast-path-read / slow-path-write pattern the file already uses.

### Why two entry points

- `Item::literal("X").id()` is a pure compile-time hash. `declareID("X", spec)` must agree with it so `switch` / `static_assert` / template-param uses work. A collision here would cause silent semantic drift, so we abort instead.
- Runtime-dynamic names (JSON-deserialized backend parameters, user-entered keys, etc.) have no compile-time counterpart to drift against. Probing gives us never-fails registration at negligible cost, and internal consistency is preserved because lookup uses the same probe.

### Collision reality check

64-bit FNV-1a over ~200 well-known IDs has collision probability ~10⁻¹⁶; over the tens of thousands of dynamically-registered names a long-running process might see, still ~10⁻¹². The strict path is not expected to abort in practice; it exists as a safety rail that turns a potential silent bug into an immediate, actionable crash at startup.

### Example error message (strict path)

```
FATAL: StringRegistry<MetadataTag> hash collision:
  "FrameRate" and "Some_Other_Key" both hash to 0xABCD1234EFGH5678.
  Rename one of these identifiers.
Aborted.
```

The program dies before `main()` runs on the first binary that links both colliding headers.

## Files to modify

**Core change:**
- `include/promeki/stringregistry.h` — everything above. New `hashName()` helper; Item widens to `uint64_t`; `InvalidID` widens; `_names` replaces `_map`+`_names`; two registration entry points; `Item::literal` factory; `fromId` and ops become `constexpr`.

**Call-site updates (uint32_t → uint64_t):**
- `include/promeki/variantdatabase.h` — `SpecRegistry::_specs` becomes `Map<uint64_t, VariantSpec>`; `_data` becomes `Map<uint64_t, Variant>`; `registeredSpecs()` return type; `writeTo(DataStream)` / `ids()` uses uint64_t cast where it emits; `ID::fromId` calls already pass through.
- Other files that capture `.id()` into a local `uint32_t`. The initial audit flagged ~30 candidates (`src/proav/mediaiotask_*.cpp`, `src/core/clockdomain.cpp`, `src/core/datastream.cpp`, bench/util code, tests). Most pass IDs through the `ID` / `Item` type rather than raw integers, so the audit is mechanical: each site either widens to `uint64_t` or stays on `.id()` without reassignment.

**Docs:**
- Update the `@warning` block in `stringregistry.h` (and the matching one in `variantdatabase.h`) to describe the new ID-stability guarantee and the strict-vs-probe registration paths.
- Update `@par Example` to show `static constexpr ID X = ID::literal("X")` usage for well-known IDs that don't need a spec, and keep the existing `declareID` example for spec-bearing IDs.

**Tests:**
- `tests/stringregistry.cpp` — add:
    - `Item::literal(name)` returns the same ID as `Item(name)` on a non-colliding name.
    - `Item::literal(name)` is usable inside a `constexpr` context / `static_assert`.
    - `Item::fromId(hashName("foo")) == Item::literal("foo")`.
    - `find()` still returns invalid for a never-registered name even after `Item::literal` has been invoked on it (literal doesn't register).
    - Roundtrip: register via `Item("foo")`, confirm `Item::literal("foo").id() == Item("foo").id()`.
- `tests/variantdatabase.cpp` and `tests/metadata.cpp` — existing cases continue to pass; add one `static_assert(Metadata::Title.id() == Metadata::ID::literal("Title").id())` to lock in the compile-time equivalence.
- No new test for the strict-abort path (untestable from doctest without forking a process); instead the code path is exercised by construction via a TU-local negative test that's `#if 0`-guarded and documented in the source.

## Non-goals

- No change to `declareID()` signature.
- No change to JSON / DataStream / TextStream wire formats: all serialize by *name*, so the uint32→uint64 ID widening is invisible on the wire.
- No centralized "well-known ID manifest" macro — declarations remain co-located with their specs, matching the existing metadata.h style.
- No compile-time cross-TU collision check. Cross-TU collisions are caught at static-init time by the strict-abort path.

## Verification

1. `build` — clean build across the whole tree with zero warnings (per CODING_STANDARDS / user preference).
2. `build unittest-promeki && ./bin/unittest-promeki` — all pre-existing tests pass; new StringRegistry cases cover the consteval path.
3. `static_assert(Metadata::Title.id() == Metadata::ID::literal("Title").id())` compiles — proves the runtime `declareID` path and the compile-time `literal` path agree for a real in-tree well-known ID.
4. `build unittest-tui && build unittest-sdl` — confirm the uint32→uint64 widening did not break downstream libraries.
5. Serialization roundtrip tests in `tests/variantdatabase.cpp` pass byte-for-byte (hashes are internal only; wire format uses names).
6. Sanity: `unittest-promeki` startup does not abort (no latent collisions among existing well-known IDs across all currently-used tags).
