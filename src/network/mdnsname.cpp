/**
 * @file      mdnsname.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mdnsname.h>

PROMEKI_NAMESPACE_BEGIN

String mdnsEscapeLabel(const String &raw) {
        String out;
        const char  *src = raw.cstr();
        const size_t n   = (src != nullptr) ? raw.size() : 0;
        for (size_t i = 0; i < n; ++i) {
                char c = src[i];
                if (c == '.' || c == '\\') {
                        out += '\\';
                }
                out += c;
        }
        return out;
}

String mdnsUnescapeLabel(const String &escaped) {
        String out;
        const char  *src = escaped.cstr();
        const size_t n   = (src != nullptr) ? escaped.size() : 0;
        for (size_t i = 0; i < n; ++i) {
                char c = src[i];
                if (c != '\\') { out += c; continue; }
                // Backslash escape — look at the next byte.
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

List<String> mdnsSplitName(const String &name) {
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
                        // Unknown escape — keep the backslash as a
                        // literal byte and continue (the next char
                        // will be processed normally on the next
                        // iteration).
                        current += c;
                        continue;
                }
                if (c == '.') {
                        // Trailing root marker — terminates without
                        // emitting an empty label.
                        if (i == n - 1) break;
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

String mdnsJoinName(const List<String> &rawLabels) {
        String out;
        for (const String &lab : rawLabels) {
                out += mdnsEscapeLabel(lab);
                out += '.';
        }
        return out;
}

PROMEKI_NAMESPACE_END
