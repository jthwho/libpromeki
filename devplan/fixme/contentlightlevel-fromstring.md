# ContentLightLevel / MasteringDisplay: missing `fromString()` parsers

**Files:** `include/promeki/contentlightlevel.h`,
`include/promeki/masteringdisplay.h`,
`src/core/variantserialize.cpp`,
`demos/promeki-pipeline/frontend/src/components/SpecField.vue`

**FIXME:** `ContentLightLevel` and `MasteringDisplay` don't expose
`fromString()` (or `Variant`-friendly JSON) round-trip parsers. The
pipeline-demo frontend currently constructs the canonical formatted
string (e.g. `MaxCLL=1000 MaxFALL=400 cd/m²`) and regex-parses it
back in `SpecField.vue`'s composite editors, which is good enough
within a session but loses information when the formatted layout
changes. Fix: add `ContentLightLevel::fromString(const String &,
Error*)` and `MasteringDisplay::fromString(...)`, or wire both
types into `Variant`'s JSON serializers (`TypeContentLightLevel`,
`TypeMasteringDisplay`) so they round-trip through the schema
cleanly. Removes the regex parser from `SpecField.vue`'s composite
editors.

## Tasks

- [ ] Add `ContentLightLevel::fromString(const String &, Error*)`
  symmetric with `toString()`.
- [ ] Add `MasteringDisplay::fromString(const String &, Error*)`
  symmetric with `toString()`.
- [ ] Alternatively, teach `Variant`'s JSON serializer to emit /
  consume both types as structured objects rather than relying on
  the toString form as the wire shape.
- [ ] Once landed, drop the `parseCll` / `parseMd` regexes from
  `SpecField.vue` in the pipeline demo and let the editor read
  fields from a structured value.
