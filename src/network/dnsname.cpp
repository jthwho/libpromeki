/**
 * @file      dnsname.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/dnsname.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        constexpr uint8_t LabelLengthMask  = 0x3F;  // bottom 6 bits of label-length byte
        constexpr uint8_t PointerFlagMask  = 0xC0;  // top 2 bits set => compression pointer
        constexpr uint8_t PointerFlagValue = 0xC0;
        constexpr size_t  MaxLabelBytes    = 63;

        // Lower-case one ASCII byte; leave the high bits alone (the
        // DNS spec is ASCII-only; IDN names ride through punycode).
        inline char asciiLower(char c) {
                return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
        }

        // Appends @p raw with each label boundary escape applied
        // (used by both @ref dnsEscapeLabel and @ref dnsJoinName).
        void appendEscaped(String &out, const char *bytes, size_t n) {
                for (size_t i = 0; i < n; ++i) {
                        char c = bytes[i];
                        if (c == '.' || c == '\\') out += '\\';
                        out += c;
                }
        }

} // anonymous namespace

String dnsEscapeLabel(const String &raw) {
        String out;
        const char  *src = raw.cstr();
        const size_t n   = (src != nullptr) ? raw.size() : 0;
        appendEscaped(out, src, n);
        return out;
}

String dnsUnescapeLabel(const String &escaped) {
        String out;
        const char  *src = escaped.cstr();
        const size_t n   = (src != nullptr) ? escaped.size() : 0;
        for (size_t i = 0; i < n; ++i) {
                char c = src[i];
                if (c != '\\') {
                        out += c;
                        continue;
                }
                if (i + 1 >= n) {
                        // Dangling backslash; preserve verbatim.
                        out += c;
                        continue;
                }
                char next = src[i + 1];
                if (next == '.' || next == '\\') {
                        out += next;
                        ++i;
                        continue;
                }
                if (i + 3 < n &&
                    next >= '0' && next <= '9' &&
                    src[i + 2] >= '0' && src[i + 2] <= '9' &&
                    src[i + 3] >= '0' && src[i + 3] <= '9') {
                        // Three-digit decimal byte escape per RFC 1035.
                        int v = (next - '0') * 100 + (src[i + 2] - '0') * 10 + (src[i + 3] - '0');
                        if (v >= 0 && v <= 255) {
                                out += static_cast<char>(v);
                                i += 3;
                                continue;
                        }
                }
                // Unknown escape — preserve the backslash.
                out += c;
        }
        return out;
}

List<String> dnsSplitName(const String &name) {
        List<String> out;
        const char  *src = name.cstr();
        const size_t n   = (src != nullptr) ? name.size() : 0;

        String current;
        for (size_t i = 0; i < n; ++i) {
                char c = src[i];
                if (c == '\\' && i + 1 < n) {
                        char next = src[i + 1];
                        if (next == '.' || next == '\\') {
                                current += next;
                                ++i;
                                continue;
                        }
                        if (i + 3 < n &&
                            next >= '0' && next <= '9' &&
                            src[i + 2] >= '0' && src[i + 2] <= '9' &&
                            src[i + 3] >= '0' && src[i + 3] <= '9') {
                                int v = (next - '0') * 100 + (src[i + 2] - '0') * 10 + (src[i + 3] - '0');
                                if (v >= 0 && v <= 255) {
                                        current += static_cast<char>(v);
                                        i += 3;
                                        continue;
                                }
                        }
                        current += c;
                        continue;
                }
                if (c == '.') {
                        if (i == n - 1) break;   // trailing root marker
                        out += current;
                        current = String();
                        continue;
                }
                current += c;
        }
        if (!current.isEmpty() || (n > 0 && src[n - 1] != '.')) {
                out += current;
        }
        return out;
}

String dnsJoinName(const List<String> &rawLabels) {
        String out;
        for (const String &lab : rawLabels) {
                appendEscaped(out, lab.cstr(), lab.size());
                out += '.';
        }
        if (out.isEmpty()) out += '.';
        return out;
}

String dnsCanonicalName(const String &name) {
        // Strip any text-form escapes first so case-folding sees the
        // raw bytes, then re-join with a single trailing dot.  We
        // never produce escapes in the canonical form because the
        // cache lookup never needs them.
        List<String> labels = dnsSplitName(name);
        String out;
        for (const String &lab : labels) {
                const char  *src = lab.cstr();
                const size_t n   = (src != nullptr) ? lab.size() : 0;
                for (size_t i = 0; i < n; ++i) out += asciiLower(src[i]);
                out += '.';
        }
        if (out.isEmpty()) out += '.';
        return out;
}

Result<DnsNameDecodeResult> decodeName(const uint8_t *data, size_t len, size_t offset) {
        DnsNameDecodeResult result;
        if (data == nullptr) return makeError<DnsNameDecodeResult>(Error::Invalid);

        // The "next offset" we return to the caller is the byte right
        // after the *first* terminating label or compression pointer
        // we hit at the original position — even if the encoding
        // chains through pointers to assemble the full name.
        bool   nextOffsetCaptured = false;
        size_t cursor             = offset;
        int    hops               = 0;
        size_t materialized       = 0;
        String name;

        while (true) {
                if (cursor >= len) {
                        return makeError<DnsNameDecodeResult>(Error::ParseFailed);
                }
                const uint8_t b = data[cursor];

                // Compression pointer (RFC 1035 §4.1.4).  Top two
                // bits set: the low 6 bits of this byte plus the
                // next byte form a 14-bit offset.
                if ((b & PointerFlagMask) == PointerFlagValue) {
                        if (cursor + 1 >= len) {
                                return makeError<DnsNameDecodeResult>(Error::ParseFailed);
                        }
                        const size_t target =
                                ((static_cast<size_t>(b) & 0x3F) << 8) |
                                static_cast<size_t>(data[cursor + 1]);
                        if (!nextOffsetCaptured) {
                                result.nextOffset  = cursor + 2;
                                nextOffsetCaptured = true;
                        }
                        // Pointers must go backward — RFC 1035 §4.1.4
                        // doesn't strictly forbid forward references
                        // but every mainstream implementation rejects
                        // them as a loop-prevention measure.  Reject
                        // self-references too.
                        if (target >= cursor) {
                                return makeError<DnsNameDecodeResult>(Error::ParseFailed);
                        }
                        if (++hops > MaxNamePointerHops) {
                                return makeError<DnsNameDecodeResult>(Error::ParseFailed);
                        }
                        cursor = target;
                        continue;
                }

                // Reserved label-length values (RFC 6891 §6.1.2).
                // Length bytes 0x40..0xBF are reserved and have
                // historically signalled extended/binary labels —
                // none in current use.  Reject them.
                if ((b & PointerFlagMask) != 0) {
                        return makeError<DnsNameDecodeResult>(Error::ParseFailed);
                }

                // Standard label-length byte.  Zero terminates the name.
                const size_t labelLen = static_cast<size_t>(b & LabelLengthMask);
                if (labelLen == 0) {
                        if (!nextOffsetCaptured) {
                                result.nextOffset  = cursor + 1;
                                nextOffsetCaptured = true;
                        }
                        break;
                }
                if (labelLen > MaxLabelBytes) {
                        return makeError<DnsNameDecodeResult>(Error::ParseFailed);
                }
                if (cursor + 1 + labelLen > len) {
                        return makeError<DnsNameDecodeResult>(Error::ParseFailed);
                }
                if (materialized + labelLen + 1 > MaxNameWireBytes) {
                        return makeError<DnsNameDecodeResult>(Error::ParseFailed);
                }

                // Append the escaped form of the label bytes.
                const char *bytes = reinterpret_cast<const char *>(data + cursor + 1);
                appendEscaped(name, bytes, labelLen);
                name += '.';
                materialized += labelLen + 1;
                cursor       += 1 + labelLen;
        }

        if (name.isEmpty()) name = String(".");
        result.name = name;
        return makeResult(std::move(result));
}

Error skipName(const uint8_t *data, size_t len, size_t &offset) {
        if (data == nullptr) return Error(Error::Invalid);
        size_t cursor = offset;
        while (true) {
                if (cursor >= len) return Error(Error::ParseFailed);
                const uint8_t b = data[cursor];
                if ((b & PointerFlagMask) == PointerFlagValue) {
                        if (cursor + 1 >= len) return Error(Error::ParseFailed);
                        offset = cursor + 2;
                        return Error();
                }
                if ((b & PointerFlagMask) != 0) return Error(Error::ParseFailed);
                const size_t labelLen = static_cast<size_t>(b & LabelLengthMask);
                if (labelLen == 0) {
                        offset = cursor + 1;
                        return Error();
                }
                if (labelLen > MaxLabelBytes) return Error(Error::ParseFailed);
                if (cursor + 1 + labelLen > len) return Error(Error::ParseFailed);
                cursor += 1 + labelLen;
        }
}

Error encodeName(const String &name, List<uint8_t> &out, DnsNameCompressionMap *dict) {
        // Split into raw label bytes (escapes resolved).  An empty
        // input or a bare "." encodes as a single zero terminator.
        List<String> labels = dnsSplitName(name);
        if (labels.isEmpty()) {
                out += static_cast<uint8_t>(0);
                return Error();
        }

        const size_t labelCount = labels.size();
        size_t       startSize  = out.size();
        for (size_t i = 0; i < labelCount; ++i) {
                const String &lab = labels[i];
                if (lab.size() > MaxLabelBytes) return Error(Error::Invalid);

                // Build the canonical suffix from this label onward
                // and look it up in the compression dictionary.  We
                // walk in order rather than longest-first because
                // shorter suffixes are strict suffixes of longer
                // ones; if a longer suffix matched we'd have hit it
                // on a previous iteration of the outer loop in a
                // prior name encode.  For *this* name we can't
                // back-reference labels we haven't yet emitted.
                if (dict != nullptr) {
                        String suffix;
                        for (size_t j = i; j < labelCount; ++j) {
                                const String &s = labels[j];
                                const char   *p = s.cstr();
                                const size_t  n = (p != nullptr) ? s.size() : 0;
                                for (size_t k = 0; k < n; ++k) suffix += asciiLower(p[k]);
                                suffix += '.';
                        }
                        auto it = dict->find(suffix);
                        if (it != dict->end()) {
                                const uint16_t target = it->second;
                                if (target <= 0x3FFF) {
                                        // Emit the compression
                                        // pointer (2 bytes).  All
                                        // labels prior to this point
                                        // are already in @p out.
                                        out += static_cast<uint8_t>(0xC0 | ((target >> 8) & 0x3F));
                                        out += static_cast<uint8_t>(target & 0xFF);
                                        // Sanity-check total size.
                                        if (out.size() - startSize > MaxNameWireBytes) {
                                                return Error(Error::Invalid);
                                        }
                                        return Error();
                                }
                        }
                }

                // No compression hit — register this suffix's offset
                // (if it's reachable from a future encoded name) and
                // emit the length-prefixed label.
                if (dict != nullptr) {
                        const size_t off = out.size();
                        if (off <= 0x3FFF) {
                                String suffix;
                                for (size_t j = i; j < labelCount; ++j) {
                                        const String &s = labels[j];
                                        const char   *p = s.cstr();
                                        const size_t  n = (p != nullptr) ? s.size() : 0;
                                        for (size_t k = 0; k < n; ++k) suffix += asciiLower(p[k]);
                                        suffix += '.';
                                }
                                if (dict->find(suffix) == dict->end()) {
                                        (*dict)[suffix] = static_cast<uint16_t>(off);
                                }
                        }
                }
                out += static_cast<uint8_t>(lab.size());
                const char  *p = lab.cstr();
                const size_t n = (p != nullptr) ? lab.size() : 0;
                for (size_t k = 0; k < n; ++k) out += static_cast<uint8_t>(p[k]);
        }
        // Terminating root label.
        out += static_cast<uint8_t>(0);

        // Total encoded size guard (label bytes + length prefixes + root).
        if (out.size() - startSize > MaxNameWireBytes) return Error(Error::Invalid);
        return Error();
}

PROMEKI_NAMESPACE_END
