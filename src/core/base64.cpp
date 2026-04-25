/**
 * @file      base64.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/base64.h>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Decode lookup: maps a Base64 character to its 6-bit value, or
// 0xFF for invalid bytes.  Whitespace (space, tab, CR, LF) is also
// 0xFF — the caller filters whitespace before consulting the table.
static uint8_t kReverse[256];
static bool kReverseInit = false;

static void initReverse() {
        if(kReverseInit) return;
        std::memset(kReverse, 0xFF, sizeof(kReverse));
        for(uint8_t i = 0; i < 64; ++i) {
                kReverse[static_cast<uint8_t>(kAlphabet[i])] = i;
        }
        kReverseInit = true;
}

} // anonymous namespace

String Base64::encode(const void *data, size_t len) {
        if(data == nullptr || len == 0) return String();

        const uint8_t *src = static_cast<const uint8_t *>(data);
        String out;

        size_t i = 0;
        while(i + 3 <= len) {
                const uint32_t v = (uint32_t(src[i]) << 16) |
                                   (uint32_t(src[i + 1]) << 8) |
                                   uint32_t(src[i + 2]);
                out += kAlphabet[(v >> 18) & 0x3F];
                out += kAlphabet[(v >> 12) & 0x3F];
                out += kAlphabet[(v >> 6)  & 0x3F];
                out += kAlphabet[v         & 0x3F];
                i += 3;
        }

        const size_t rem = len - i;
        if(rem == 1) {
                const uint32_t v = uint32_t(src[i]) << 16;
                out += kAlphabet[(v >> 18) & 0x3F];
                out += kAlphabet[(v >> 12) & 0x3F];
                out += '=';
                out += '=';
        } else if(rem == 2) {
                const uint32_t v = (uint32_t(src[i]) << 16) |
                                   (uint32_t(src[i + 1]) << 8);
                out += kAlphabet[(v >> 18) & 0x3F];
                out += kAlphabet[(v >> 12) & 0x3F];
                out += kAlphabet[(v >> 6)  & 0x3F];
                out += '=';
        }
        return out;
}

String Base64::encode(const Buffer &buf) {
        if(!buf.isValid() || buf.size() == 0) return String();
        return encode(buf.data(), buf.size());
}

Buffer Base64::decode(const String &text, Error *err) {
        initReverse();
        if(err != nullptr) *err = Error::Ok;
        if(text.isEmpty()) return Buffer();

        // Two-pass: count valid input characters first so we can
        // allocate the output exactly once.  Whitespace and padding
        // are filtered.  Any other non-alphabet byte fails the decode.
        const char *src = text.cstr();
        const size_t inLen = text.byteCount();
        size_t valid = 0;
        size_t pad = 0;
        for(size_t i = 0; i < inLen; ++i) {
                const uint8_t c = static_cast<uint8_t>(src[i]);
                if(c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
                if(c == '=') { ++pad; continue; }
                if(kReverse[c] == 0xFF) {
                        if(err != nullptr) *err = Error::Invalid;
                        return Buffer();
                }
                ++valid;
        }
        if(valid == 0) return Buffer();

        // Each four-character group yields three bytes; the trailing
        // group may be 2 (one pad) or 3 (two pads) characters.
        const size_t fullGroups = valid / 4;
        const size_t tail = valid % 4;
        size_t outBytes = fullGroups * 3;
        if(tail == 2) outBytes += 1;
        else if(tail == 3) outBytes += 2;
        else if(tail != 0) {
                if(err != nullptr) *err = Error::Invalid;
                return Buffer();
        }

        Buffer out(outBytes == 0 ? 1 : outBytes);
        out.setSize(outBytes);
        uint8_t *dst = static_cast<uint8_t *>(out.data());

        uint32_t accum = 0;
        int bits = 0;
        size_t outIdx = 0;
        for(size_t i = 0; i < inLen; ++i) {
                const uint8_t c = static_cast<uint8_t>(src[i]);
                if(c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
                if(c == '=') break;
                accum = (accum << 6) | kReverse[c];
                bits += 6;
                if(bits >= 8) {
                        bits -= 8;
                        dst[outIdx++] = static_cast<uint8_t>((accum >> bits) & 0xFF);
                }
        }
        return out;
}

PROMEKI_NAMESPACE_END
