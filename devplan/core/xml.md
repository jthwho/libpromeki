# XML Object Family

**Phase:** 7
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests.

A CoW value-type handle family (`XmlDocument`, `XmlElement`, `XmlNode`,
`XmlAttribute`, `XmlName`) modelled on the existing JSON handles in
`include/promeki/json.h`. Backed by pugixml as the parsing / DOM
engine. Namespace-aware from day one. Carried natively by `Variant`
and serialised by `DataStream` — see [project_xml.md](../../README.md)
for the design summary.

Variant-tree interop (XML ↔ `VariantMap` / `VariantList` via the
`@attr` / `#text` convention) is **deferred**.

---

## Phase 1 — Vendor pugixml + CMake wiring

- [x] Submodule `thirdparty/pugixml` pinned at `v1.15` (tag `v1.15`,
      MIT-licensed).
- [x] `PROMEKI_USE_SYSTEM_PUGIXML` option defaulting OFF, mirroring
      `PROMEKI_USE_SYSTEM_NLOHMANN_JSON`.
- [x] Vendored build: `BUILD_SHARED_LIBS=OFF`, `PUGIXML_BUILD_TESTS=OFF`,
      `PUGIXML_INSTALL=OFF`, `CMAKE_POSITION_INDEPENDENT_CODE=ON`,
      `add_subdirectory EXCLUDE_FROM_ALL`, link `pugixml-static` as
      PRIVATE.
- [x] Public include path covers `thirdparty/pugixml/src/` so consumers
      can `#include <pugixml.hpp>` (matching nlohmann/json's PUBLIC
      include scheme — `XmlData` embeds `pugi::xml_document` by value).
- [x] Install rule for `pugixml.hpp` + `pugiconfig.hpp` into
      `${INCLUDE}/promeki/thirdparty`.
- [x] Tree builds end-to-end with the submodule wired in (no XML code
      yet — just verifying the link).

---

## Phase 1.5 — Rich parse errors

`Error *err` was too narrow for XML parsing — pugi reports a status,
byte offset, and a description that the consumer wants to see.

- [x] New `XmlParseError` value type next to `XmlDocument` in `xml.h`.
      Holds `pugi::xml_parse_status`, the byte `offset()`, derived
      `line()` / `column()` (1-based, computed by scanning the source
      for newlines up to offset), and pugi's `description()` as
      `message()`.
- [x] `XmlDocument::parse(String, XmlParseError *err)` and
      `XmlElement::parse(String, XmlParseError *err)` — replacing the
      old `Error *err` parameter outright (no compat shim).
- [x] `XmlParseError::toError()` maps pugi statuses to `Error` codes:
      `status_out_of_memory → NoMem`, `status_io_error → IOError`,
      `status_file_not_found → NotExist`, everything else
      → `ParseFailed`.
- [x] `XmlParseError::toString()` renders `"line N, col M: message"`.
- [x] `XmlElement::parse` synthesises `status_no_document_element`
      when the parse succeeded but no element survived (e.g. parsing
      a comment-only fragment).
- [x] `DataStream` read paths (`operator>>` for `XmlDocument` /
      `XmlElement`, `readVariantPayload(TypeXmlDocument)`) propagate
      the rich message into `setError(ReadCorruptData, ...)` text.
- [x] Tests cover line/column derivation, the default-constructed-OK
      state, `pugiStatus()`/`toError()` mapping, and the
      `no_document_element` synthesis path.

## Phase 2 — Core types

- [x] `xml.h` — `XmlData` with `pugi::xml_document` by value,
      `PROMEKI_SHARED_FINAL`, explicit deep-clone copy ctor via
      `pugi::xml_document::reset`. All public types live in this one
      header (mirrors `json.h`).
- [x] `XmlAttribute` — plain `(name, value)` value type. (Phase 3 will
      promote `name` to `XmlName` for namespaces.)
- [x] `XmlDocument` value handle: `parse(String, Error*)`, `toString`,
      `root()`, `setRoot()`, `clear()`, declaration get/set, doctype
      get/set, top-level comment / PI append, equality.
- [x] `XmlElement` value handle: `name()`, `setName()`, attribute
      get/set/remove/list, `child(name)`, `elements()`,
      `elementsNamed(name)`, `children()` (mixed), `text()`,
      `setText()`, `appendElement(name [, text])`, `appendText`,
      `appendCData`, `appendComment`,
      `appendProcessingInstruction`, `appendChild`, `clear()`,
      `parse()`, `toString()`, equality.
- [x] `XmlNode` discriminated handle (Element / Text / CData / Comment
      / ProcessingInstruction / Null / Undefined).
- [x] CoW: every mutator funnels through `_d.modify()`. Subtree
      accessors (`child`, `elements`, `root`) deep-copy via
      `pugi::xml_document::reset` / `append_copy`. Verified by the
      `XmlElement_SubtreeIndependence` and `XmlElement_CoW_CopyShares`
      tests.
- [x] `PROMEKI_FORMAT_VIA_TOSTRING` for `XmlDocument` and `XmlElement`.
- [x] **API note:** `appendElement(name)` returns `void`, not a child
      handle. CoW makes a "live handle into the parent" model
      ill-defined (mutations through it would either CoW-detach to a
      private copy or silently corrupt sibling handles). Users build a
      child as a separate `XmlElement` and pass it to `appendChild`,
      matching `JsonObject::set(key, JsonObject)`. Convenience
      `appendElement(name, text)` covers the common single-text-child
      case.

---

## Phase 3 — Namespaces (first-class)

- [x] `XmlName{uri, prefix, local}` value type with `qualified` /
      `clark` text forms, `parseClark`, equality on `(uri, local)`
      ignoring prefix hints.
- [x] On-demand prefix resolution by walking up the pugi node's
      ancestors (`lookupNamespaceUri`), avoiding the per-element scope
      cache. Cheap because most lookups are local; correctness comes
      from inheriting scope onto extracted subtrees.
- [x] Namespace-aware getters: `child(XmlName)`, `hasChild(XmlName)`,
      `elementsNamed(XmlName)`, `attribute(XmlName)`,
      `hasAttribute(XmlName)`. Match on `(uri, local)`.
- [x] Namespace-aware setters: `setAttribute(XmlName, value)`,
      `removeAttribute(XmlName)`, `appendElement(XmlName [, text])`.
      Uses existing in-scope binding for the URI when present; falls
      back to the user-supplied prefix hint, then to
      `xmlns:auto<N>`. For elements with empty prefix hint and no
      default namespace in scope, declares the URI as the default
      namespace on the new child.
- [x] Default namespace (`xmlns="..."`) handled as the binding for the
      empty prefix; unprefixed elements inherit it; unprefixed
      attributes do NOT (per the XML namespaces spec).
- [x] `XmlElement::namespaces()` returns the in-scope
      `Map<prefix, uri>` for that element.
- [x] Subtree extraction (`child`, `elements`, `elementsNamed`,
      `Document::root`) inherits ancestor `xmlns` declarations onto
      the extracted root via `inheritScopeOnto` so the deep-copied
      subtree remains namespace-resolvable in isolation.
- [x] `XmlAttribute::qname()` accessor — populated by
      `XmlElement::attributes()` against the owning element's scope.

---

## Phase 4 — DataStream

(Pulled forward into Phase 2 — the inline `operator<<` / `operator>>`
overloads live in `xml.h` and need the tag IDs to compile.)

- [x] `TypeXmlDocument = 0x55` and `TypeXmlElement = 0x56` added to
      `DataStream::TypeId`. `XmlDocument` / `XmlElement` forward-declared
      in `datastream.h` alongside `JsonObject` / `JsonArray`.
- [x] `operator<<` / `operator>>` for `XmlDocument` and `XmlElement`,
      length-prefixed text-form (matching `JsonObject` / `JsonArray`).
      Malformed payload sets `ReadCorruptData`. Round-trip tests in
      `tests/unit/xml.cpp`.

---

## Phase 5 — Variant carry

- [x] `X(TypeXmlDocument, XmlDocument)` added to `PROMEKI_VARIANT_TYPES`
      and the documentation table at the top of `variant.h`.
- [x] `variant.h` now `#include`s `xml.h` so the type is visible to the
      X-macro instantiations.
- [x] `has_free_write<XmlDocument>` / `has_free_read<XmlDocument>` true
      in `datastream.cpp` so the build-time coverage assertion stays
      satisfied.
- [x] Read-side switch case in `DataStream::readVariantPayload` —
      consumes the inner `TypeString` tag (matching the way
      `operator<<(DataStream&, const XmlDocument&)` writes the body),
      then parses via `XmlDocument::parse`. Malformed payload sets
      `ReadCorruptData`.
- [x] Tests: `Variant_HoldsXmlDocument` and
      `Variant_XmlDocument_DataStreamRoundtrip` cover the
      construct / get / DataStream round-trip path.
- [x] No Variant-tree interop (`@attr` / `#text` mapping) yet — still
      deferred per the original plan.

---

## Phase 6 — Doxygen

- [x] Class docs in `xml.h` for every public type
      (`XmlName`, `XmlAttribute`, `XmlElement`, `XmlDocument`,
      `XmlNode`, `XmlData`) with the json.h-style sections: Storage
      and copy semantics, Thread Safety, Example.
- [x] Cross-link `XmlDocument` ↔ `XmlElement` ↔ `XmlNode` ↔
      `XmlAttribute` ↔ `XmlName` via @c \@ref tags.
- [x] Added rows to `docs/dataobjects.md` under
      "Internally-CoW value-type handles" and updated the threading
      section to list `XmlDocument` / `XmlElement`.
- [x] One-line entry under "Active Projects" in the auto-memory
      `MEMORY.md` index pointing at `project_xml.md`.

---

## Phase 8 — General-purpose surface expansion

Round of follow-up additions making the XML interface a "general
purpose, easy to use" interface. All five subphases shipped together;
total 65 XML test cases / 264 assertions, full suite at 5120/5120.

### Phase 8A — XPath 1.0
- [x] `XmlElement::selectFirst(query, Error*)` → `XmlElement` (filters
      out non-element results).
- [x] `XmlElement::selectAll(query, Error*)` → `List<XmlElement>` in
      document order.
- [x] `XmlElement::selectFirstAttribute` /
      `XmlElement::selectAllAttributes` for attribute-yielding queries
      — returns `XmlAttribute` with the qname resolved against the
      owning element's in-scope `xmlns`.
- [x] `XmlDocument::selectFirst` / `selectAll` are thin wrappers over
      the document root.
- [x] Pugi `xpath_exception` caught and surfaced as
      `Error::ParseFailed`.

### Phase 8B — File I/O
- [x] `XmlDocument::loadFromPath(path, XmlParseError*)` reads via
      `File`, supports the `:/` resource namespace transparently.
- [x] `XmlDocument::saveToPath(path, indent)` writes via `File` with
      `Create | Truncate`. Returns `Error` (not `XmlParseError` —
      it's an output, not a parse).
- [x] IO failures synthesise `pugi::status_file_not_found` /
      `pugi::status_io_error` so callers see a uniform error path
      with `parse`.

### Phase 8C — Path navigation
- [x] `XmlElement::elementByPath("a/b/c")` wrapping pugi
      `first_element_by_path` (default `/` separator). Cheap shortcut
      for direct-child chains; XPath remains the answer for anything
      richer.
- [x] `XmlDocument::elementByPath` — root shortcut.

### Phase 8D — Child mutation primitives
- [x] `prependElement(name)` / `prependElement(name, text)`.
- [x] `prependChild(XmlElement)` / `prependChild(XmlNode)` /
      `prependChild(XmlDocument)`.
- [x] `insertChildAt(index, XmlElement)` / `insertChildAt(index, XmlNode)`
      — index space covers all child types; out-of-range clamps to
      append. The `XmlNode` overload reuses `appendChild` then moves
      the appended last child into position via `insert_copy_before` +
      `remove_child`, since pugi has no public node-move API.
- [x] `removeChild(name)` / `removeChild(qname)` — first-match
      element removal.
- [x] `removeChildAt(index)` — removes any node type by position.
- [x] `removeAllNamed(name)` / `removeAllNamed(qname)` — returns
      removed count.
- [x] `childCount()`.

### Phase 8E — Attribute order primitives
- [x] `prependAttribute(raw)` / `prependAttribute(qname)` — replaces
      any existing same-named attribute (so order is deterministic).
- [x] `insertAttributeAt(index, raw)` / `insertAttributeAt(index, qname)`.
- [x] `attributeCount()`.
- [x] Internal `resolveAttributeRawName` helper extracted from the
      original `setAttribute(qname)` so all three setters share the
      prefix-injection logic.

## Phase 7 — Tests (`tests/unit/xml.cpp`)

65 doctest cases covering Phases 1–8; full `unittest-promeki` suite
at 5120/5120.

- [x] Parse / serialize round-trip for plain XML (elements, attrs,
      text, CDATA, comments, PIs, declaration, doctype).
- [x] CoW: copy is O(1); first mutator detaches
      (`XmlElement_CoW_CopyShares`, `XmlDocument_CoW_CopyShares`).
- [x] Subtree accessor independence: mutating an extracted subtree
      doesn't touch the parent (`XmlElement_SubtreeIndependence`).
- [x] Namespace lookup: `xmlns:foo="..."`, default namespace,
      ancestor inheritance, attribute-vs-element rules
      (`XmlElement_Namespaces_*`,
      `XmlElement_Attributes_NoDefaultNsForUnprefixed`).
- [x] `xmlns:auto<N>` injection and prefix-hint reuse on
      namespace-aware setters (`XmlElement_SetAttribute_QName_*`,
      `XmlElement_AppendElement_QName_*`).
- [x] DataStream round-trip (Document, Element).
- [x] Variant payload round-trip
      (`Variant_HoldsXmlDocument`,
      `Variant_XmlDocument_DataStreamRoundtrip`).
- [x] Malformed input → `XmlParseError` with line/column/status/message.
- [x] Subtree extraction inherits ancestor scope so a deep-copied
      subtree remains namespace-resolvable in isolation
      (`XmlElement_SubtreeExtractInheritsScope`).
- [x] XPath (Phase 8A): element / attribute select, bad-query error.
- [x] File I/O (Phase 8B): save+load round-trip, missing-file error.
- [x] Path navigation (Phase 8C): `elementByPath` hit and miss.
- [x] Child mutation (Phase 8D): prepend, insertAt, removeAt, removeAllNamed.
- [x] Attribute order (Phase 8E): prepend, insertAt, replace-existing.
