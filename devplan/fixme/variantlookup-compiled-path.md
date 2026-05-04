# VariantLookup: compiled path for hot lookups

**File:** `include/promeki/variantlookup.h:359`

**FIXME(compiled-path):** `VariantLookup<T>::resolve()` currently
re-parses and re-hashes every dotted segment on every call.  Hot
evaluators (`VariantQuery`, format templates, frame metadata
templates, etc.) walk the same key thousands of times per second,
which is wasted work given the keys are static.

The proposed shape is a `VariantLookup::Path` value that
pre-resolves a full dotted key — e.g.
`"Video[0].Meta.Timecode"` — into a sequence of integer IDs plus
kind-transition tags at parse time, so evaluators do integer-only
lookups at run time.  The path needs to remember its kind
transitions (`indexedChild → database`, etc.) and each hop's
target type, which is why this is a non-trivial follow-up.

## Tasks

- [ ] Design the `VariantLookup::Path` value type (segment IDs,
  kind transitions, target-type tags).
- [ ] Add a `Path::compile(const String &key, Error *err)` that
  walks the registry once.
- [ ] Add `resolve(const T &, const Path &, Variant &out, ...)`
  and `assign(...)` overloads.
- [ ] Wire `VariantQuery` and the format-template hot paths to the
  compiled-path API.
- [ ] Benchmark before/after on a representative metadata-heavy
  workload.
