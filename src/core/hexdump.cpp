/**
 * @file      hexdump.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/hexdump.h>
#if PROMEKI_ENABLE_CORE

PROMEKI_NAMESPACE_BEGIN

namespace {

// Append the low @p digits hex nibbles of @p value to @p out, most
// significant first, using the requested digit case.
void appendHex(String &out, uint64_t value, int digits, bool upper) {
        static const char lut[2][16] = {{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'},
                                        {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'}};
        const char *t = lut[upper ? 1 : 0];
        for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
                out.pushBack(t[(value >> shift) & 0xF]);
        }
}

} // namespace

String HexDump::build(const void *data, size_t len) const {
        String out;
        if (data == nullptr || len == 0) return out;
        const uint8_t *bytes = static_cast<const uint8_t *>(data);

        // 2 hex digits + 1 separating space per byte, plus address and
        // ASCII columns — reserve up front so the per-byte appends never
        // re-allocate.
        const size_t lineCount = (len + _bytesPerLine - 1) / _bytesPerLine;
        const size_t perLine = _indent.size() + (_showAddress ? size_t(_addressDigits) + _addressSuffix.size() : 0) +
                               _bytesPerLine * (_showAscii ? 4 : 3) + 2;
        out.reserve(lineCount * perLine);

        for (size_t off = 0; off < len; off += _bytesPerLine) {
                out += _indent;
                if (_showAddress) {
                        appendHex(out, _baseAddress + off, _addressDigits, _uppercase);
                        out += _addressSuffix;
                }

                const size_t lineBytes = (len - off < _bytesPerLine) ? (len - off) : _bytesPerLine;
                for (size_t i = 0; i < _bytesPerLine; ++i) {
                        if (i < lineBytes) {
                                appendHex(out, bytes[off + i], 2, _uppercase);
                                out.pushBack(' ');
                        } else if (_showAscii) {
                                // Pad the short final line so the ASCII gutter
                                // lines up with the full rows above it.
                                out += "   ";
                        }
                }

                if (_showAscii) {
                        for (size_t i = 0; i < lineBytes; ++i) {
                                const uint8_t b = bytes[off + i];
                                out.pushBack((b >= 0x20 && b <= 0x7E) ? char(b) : _nonPrintable);
                        }
                }

                out.pushBack('\n');
        }
        return out;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
