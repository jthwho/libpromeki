/**
 * @file      base64.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/base64.h>
#include <promeki/buffer.h>
#include <cstring>

using namespace promeki;

TEST_CASE("Base64 - round-trips RFC 4648 vectors") {
        // Vectors from RFC 4648 §10 — the canonical reference set
        // every implementation tests against.
        const struct { const char *plain; const char *encoded; } vectors[] = {
                { "",        ""         },
                { "f",       "Zg=="     },
                { "fo",      "Zm8="     },
                { "foo",     "Zm9v"     },
                { "foob",    "Zm9vYg==" },
                { "fooba",   "Zm9vYmE=" },
                { "foobar",  "Zm9vYmFy" }
        };
        for(const auto &v : vectors) {
                String enc = Base64::encode(v.plain, std::strlen(v.plain));
                CHECK(enc == String(v.encoded));
                Error err;
                Buffer dec = Base64::decode(String(v.encoded), &err);
                CHECK(err.isOk());
                if(std::strlen(v.plain) == 0) {
                        CHECK(dec.size() == 0);
                } else {
                        REQUIRE(dec.size() == std::strlen(v.plain));
                        CHECK(std::memcmp(dec.data(), v.plain, dec.size()) == 0);
                }
        }
}

TEST_CASE("Base64 - encode handles binary bytes") {
        // 16 bytes covering 0..255 selectively — a likely WebSocket
        // Sec-WebSocket-Key payload.
        const uint8_t bytes[16] = {
                0x00, 0xFF, 0x10, 0x80,
                0x7F, 0xC3, 0x55, 0xAA,
                0x01, 0x02, 0x03, 0x04,
                0xFE, 0xFD, 0xFC, 0xFB
        };
        String enc = Base64::encode(bytes, sizeof(bytes));
        CHECK(enc.byteCount() == 24);   // 16 bytes -> 24 chars exactly
        Error err;
        Buffer dec = Base64::decode(enc, &err);
        REQUIRE(err.isOk());
        REQUIRE(dec.size() == sizeof(bytes));
        CHECK(std::memcmp(dec.data(), bytes, sizeof(bytes)) == 0);
}

TEST_CASE("Base64 - decode tolerates whitespace") {
        // PEM-style line wrapping must round-trip transparently.
        const String pem = "Zm9v\nYmFy\n";
        Error err;
        Buffer dec = Base64::decode(pem, &err);
        REQUIRE(err.isOk());
        REQUIRE(dec.size() == 6);
        CHECK(std::memcmp(dec.data(), "foobar", 6) == 0);
}

TEST_CASE("Base64 - decode rejects garbage") {
        Error err;
        Base64::decode(String("@@@@"), &err);
        CHECK(err.isError());
}
