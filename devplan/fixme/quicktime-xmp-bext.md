# QuickTime XMP parser only matches the `bext:` prefix

**Files:** `src/proav/quicktime_reader.cpp` (`extractBextElement`,
`parseUdta`), `src/proav/quicktime_writer.cpp`
(`buildBextXmpPacket`, `appendUdta`)

**FIXME:** The XMP reader in `QuickTimeReader::parseUdta` is a
minimal substring-based extractor. It locates `<bext:localName>`
and its matching closing tag via `String::find` — no
namespace-aware XML parsing, no prefix resolution. If a third-party
tool emits the same bext namespace URI
(`http://ns.adobe.com/bwf/bext/1.0/`) under a different prefix, the
reader will miss the fields even though the XMP is spec-compliant.
Example that would slip through:

```xml
<rdf:Description xmlns:ns1="http://ns.adobe.com/bwf/bext/1.0/">
  <ns1:umid>0123456789abcdef...</ns1:umid>
</rdf:Description>
```

The writer side (`buildBextXmpPacket`) also hand-rolls XML by string
concatenation with manual escaping of `&`, `<`, `>`. Round-tripping
our own output is unaffected because we always emit the `bext:`
prefix, and most media tools that embed BWF XMP use the same
convention by default. The gap matters only when ingesting packets
from tools that bind the namespace URI to a non-standard prefix.

## Tasks

- [ ] Blocked on adding a real XML parser to the core library
  (tracked separately as a library-level TODO).
- [ ] Once proper XML support lands, replace `extractBextElement()`
  with namespace-aware lookup against
  `http://ns.adobe.com/bwf/bext/1.0/` so any prefix bound to that
  URI is accepted.
- [ ] Replace the string-concatenation emitter in
  `buildBextXmpPacket()` with structured XML output.
- [ ] Add a round-trip test with a hand-crafted XMP packet that uses
  a non-`bext:` prefix, to verify the new parser handles it.
