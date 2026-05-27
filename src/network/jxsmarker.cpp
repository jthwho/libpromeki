/**
 * @file      jxsmarker.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/jxsmarker.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Reads a big-endian 16-bit marker code at @p p, or 0 when the
// caller has insufficient bytes remaining.
uint16_t readBe16(const uint8_t *p) {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                                     static_cast<uint16_t>(p[1]));
}

// Returns @c true when @p code is a JPEG XS main-header marker —
// any marker between SOC and the first SLH that carries a 2-byte
// length field after the marker code.  Used to distinguish length-
// prefixed segments from the bare SOC / EOC / SLH-introducing-
// slice-data sentinels.
bool isLengthPrefixedMarker(uint16_t code) {
        switch (code) {
                case JxsMarker::Cap:
                case JxsMarker::Pih:
                case JxsMarker::Cdt:
                case JxsMarker::Wgt:
                case JxsMarker::Com:
                case JxsMarker::Nlt:
                case JxsMarker::Cwd:
                case JxsMarker::Cts:
                case JxsMarker::Crg: return true;
                default: return false;
        }
}

} // namespace

JxsMarker::ParseResult JxsMarker::parse(const void *data, size_t size) {
        ParseResult out;
        if (data == nullptr || size < 4) return out;

        const uint8_t *bytes = static_cast<const uint8_t *>(data);

        // Codestream must start with SOC.
        if (readBe16(bytes) != Soc) return out;

        // Walk past main-header markers.  Each length-prefixed
        // marker carries a 2-byte BE length value immediately after
        // the marker code; the length value includes the length-
        // field bytes themselves but not the marker code, so the
        // bytes-to-advance from the start of the marker code is
        // @c 2 + length.
        size_t pos = 2; // past SOC
        while (pos + 2 <= size) {
                const uint16_t code = readBe16(bytes + pos);
                if (code == Slh) break;
                if (code == Eoc) {
                        // No slices at all — codestream ended at the
                        // EOC immediately after the main header.
                        out.mainHeaderSize = pos;
                        out.eocOffset = pos;
                        out.valid = true;
                        return out;
                }
                if (!isLengthPrefixedMarker(code)) {
                        // Unknown / unexpected marker in the main
                        // header position.  Treat as malformed —
                        // the caller's diagnostic surface keeps the
                        // partial state for inspection.
                        out.mainHeaderSize = pos;
                        return out;
                }
                if (pos + 4 > size) return out; // length field truncated
                const uint16_t segLen = readBe16(bytes + pos + 2);
                if (segLen < 2 || pos + 2 + segLen > size) {
                        // Bogus segment length — refuse to advance.
                        return out;
                }
                pos += 2 + segLen;
        }

        // @c pos now sits at the first SLH (or at EOF without SLH —
        // handled above).
        out.mainHeaderSize = pos;
        if (pos + 2 > size) return out;
        if (readBe16(bytes + pos) != Slh) return out;

        // Walk slices.  Each slice starts at an SLH marker and ends
        // immediately before the next SLH or EOC marker.  ISO
        // 21122-1 §A.4 says 0xFF in slice coefficient data is
        // reserved for marker prefix; scanning forward for the next
        // 0xFF and inspecting the following byte is a reliable
        // boundary detector.
        size_t sliceStart = pos;
        size_t scanPos = pos;
        while (scanPos + 2 <= size) {
                // The SLH marker that opens the current slice
                // carries a 2-byte length field; skip past it so we
                // don't immediately re-detect the same marker as a
                // boundary.
                if (scanPos == sliceStart && readBe16(bytes + scanPos) == Slh) {
                        if (scanPos + 4 > size) return out;
                        const uint16_t slhLen = readBe16(bytes + scanPos + 2);
                        if (slhLen < 2 || scanPos + 2 + slhLen > size) return out;
                        scanPos += 2 + slhLen;
                        continue;
                }
                if (bytes[scanPos] != 0xFF) {
                        ++scanPos;
                        continue;
                }
                // Possible marker boundary — check the next byte.
                if (scanPos + 2 > size) {
                        // Trailing 0xFF without a marker code — close
                        // the open slice at this position and bail.
                        SliceRange r;
                        r.offset = sliceStart;
                        r.size = scanPos - sliceStart;
                        out.slices.pushToBack(r);
                        return out;
                }
                const uint16_t code = readBe16(bytes + scanPos);
                if (code == Slh) {
                        SliceRange r;
                        r.offset = sliceStart;
                        r.size = scanPos - sliceStart;
                        out.slices.pushToBack(r);
                        sliceStart = scanPos;
                        continue;
                }
                if (code == Eoc) {
                        SliceRange r;
                        r.offset = sliceStart;
                        r.size = scanPos - sliceStart;
                        out.slices.pushToBack(r);
                        out.eocOffset = scanPos;
                        out.valid = true;
                        return out;
                }
                // 0xFF followed by a non-marker byte: per ISO 21122-1
                // §A.4 the encoder should never emit this in slice
                // data.  We treat it as a one-byte stride forward
                // and keep scanning — recoverable from a malformed
                // bitstream without giving up the slice boundaries
                // we already found.
                ++scanPos;
        }

        // Reached EOF without EOC — close any open slice with the
        // remaining bytes and report malformed.
        if (scanPos > sliceStart) {
                SliceRange r;
                r.offset = sliceStart;
                r.size = scanPos - sliceStart;
                out.slices.pushToBack(r);
        }
        return out;
}

PROMEKI_NAMESPACE_END
