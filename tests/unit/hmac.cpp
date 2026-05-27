/**
 * @file      hmac.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <string>
#include <vector>
#include <promeki/hmac.h>

using namespace promeki;

// All vectors below are from RFC 4231 §4.

namespace {

std::vector<uint8_t> repeat(uint8_t b, size_t n) {
        return std::vector<uint8_t>(n, b);
}

} // namespace

// -----------------------------------------------------------------------
// Test Case 1: 20-byte key of 0x0b, "Hi There".
// -----------------------------------------------------------------------
TEST_CASE("HmacSha256: RFC4231 case 1") {
        auto         key  = repeat(0x0b, 20);
        const char  *data = "Hi There";
        SHA256Digest d    = hmacSha256(key.data(), key.size(), data, std::strlen(data));
        CHECK(d.toHexString() == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

// -----------------------------------------------------------------------
// Test Case 2: "Jefe", "what do ya want for nothing?".
// -----------------------------------------------------------------------
TEST_CASE("HmacSha256: RFC4231 case 2") {
        const char  *key  = "Jefe";
        const char  *data = "what do ya want for nothing?";
        SHA256Digest d    = hmacSha256(key, std::strlen(key), data, std::strlen(data));
        CHECK(d.toHexString() == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

// -----------------------------------------------------------------------
// Test Case 3: 20-byte 0xaa key, 50 0xdd-byte data.
// -----------------------------------------------------------------------
TEST_CASE("HmacSha256: RFC4231 case 3") {
        auto         key  = repeat(0xaa, 20);
        auto         data = repeat(0xdd, 50);
        SHA256Digest d    = hmacSha256(key.data(), key.size(), data.data(), data.size());
        CHECK(d.toHexString() == "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
}

// -----------------------------------------------------------------------
// Test Case 4: 25-byte 0x01..0x19 key, 50 0xcd-byte data.
// -----------------------------------------------------------------------
TEST_CASE("HmacSha256: RFC4231 case 4") {
        std::vector<uint8_t> key(25);
        for (size_t i = 0; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i + 1);
        auto         data = repeat(0xcd, 50);
        SHA256Digest d    = hmacSha256(key.data(), key.size(), data.data(), data.size());
        CHECK(d.toHexString() == "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
}

// -----------------------------------------------------------------------
// Test Case 6: 131-byte 0xaa key (longer than block size — exercises the
// HMAC pre-hash path), short data.
// -----------------------------------------------------------------------
TEST_CASE("HmacSha256: RFC4231 case 6 (oversize key triggers pre-hash)") {
        auto        key  = repeat(0xaa, 131);
        const char *data = "Test Using Larger Than Block-Size Key - Hash Key First";
        SHA256Digest d   = hmacSha256(key.data(), key.size(), data, std::strlen(data));
        CHECK(d.toHexString() == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

// -----------------------------------------------------------------------
// Test Case 7: 131-byte oversize key, long data.
// -----------------------------------------------------------------------
TEST_CASE("HmacSha256: RFC4231 case 7 (oversize key, long data)") {
        auto         key  = repeat(0xaa, 131);
        const char  *data =
            "This is a test using a larger than block-size key and a larger "
            "than block-size data. The key needs to be hashed before being used "
            "by the HMAC algorithm.";
        SHA256Digest d = hmacSha256(key.data(), key.size(), data, std::strlen(data));
        CHECK(d.toHexString() == "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
}

// -----------------------------------------------------------------------
// Streaming class.
// -----------------------------------------------------------------------
TEST_CASE("HmacSha256: streaming single-update equals one-shot") {
        auto         key  = repeat(0x0b, 20);
        const char  *data = "Hi There";
        HmacSha256   h(key.data(), key.size());
        h.update(data, std::strlen(data));
        SHA256Digest streamed = h.finalize();
        SHA256Digest oneShot  = hmacSha256(key.data(), key.size(), data, std::strlen(data));
        CHECK(streamed == oneShot);
}

TEST_CASE("HmacSha256: streaming multi-region matches one-shot") {
        auto                key   = repeat(0xaa, 20);
        std::vector<uint8_t> region1 = repeat(0xab, 700);
        std::vector<uint8_t> region2 = repeat(0xcd, 304);

        HmacSha256 h(key.data(), key.size());
        h.update(region1.data(), region1.size());
        h.update(region2.data(), region2.size());
        SHA256Digest streamed = h.finalize();

        std::vector<uint8_t> joined;
        joined.insert(joined.end(), region1.begin(), region1.end());
        joined.insert(joined.end(), region2.begin(), region2.end());
        SHA256Digest oneShot = hmacSha256(key.data(), key.size(), joined.data(), joined.size());
        CHECK(streamed == oneShot);
}

TEST_CASE("HmacSha256: streaming oversize-key path") {
        // Verifies that the constructor's pre-hash for keys > block size
        // produces the same effective key the one-shot path does.
        auto         key  = repeat(0xaa, 131);
        const char  *data = "Test Using Larger Than Block-Size Key - Hash Key First";
        HmacSha256   h(key.data(), key.size());
        h.update(data, std::strlen(data));
        CHECK(h.finalize() == hmacSha256(key.data(), key.size(), data, std::strlen(data)));
}

// -----------------------------------------------------------------------
// HmacSha1 — RFC 2202 vectors (the SHA-1 counterpart spec).
// -----------------------------------------------------------------------
TEST_CASE("HmacSha1: RFC2202 case 1") {
        auto        key  = repeat(0x0b, 20);
        const char *data = "Hi There";
        SHA1Digest  d    = hmacSha1(key.data(), key.size(), data, std::strlen(data));
        CHECK(d.toHexString() == "b617318655057264e28bc0b6fb378c8ef146be00");
}

TEST_CASE("HmacSha1: RFC2202 case 2") {
        const char *key  = "Jefe";
        const char *data = "what do ya want for nothing?";
        SHA1Digest  d    = hmacSha1(key, std::strlen(key), data, std::strlen(data));
        CHECK(d.toHexString() == "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
}

TEST_CASE("HmacSha1: RFC2202 case 3 (oversize key)") {
        auto        key  = repeat(0xaa, 80);
        const char *data = "Test Using Larger Than Block-Size Key - Hash Key First";
        SHA1Digest  d    = hmacSha1(key.data(), key.size(), data, std::strlen(data));
        CHECK(d.toHexString() == "aa4ae5e15272d00e95705637ce8a3b55ed402112");
}
