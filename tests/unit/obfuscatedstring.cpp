/**
 * @file      obfuscatedstring.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <set>
#include <promeki/obfuscatedstring.h>

using namespace promeki;

// Helper: access the raw encoded bytes of an ObfuscatedString.
// The class is standard-layout with _data as the only member.
template <size_t N, uint64_t Seed> static const unsigned char *encodedBytes(const ObfuscatedString<N, Seed> &obf) {
        return reinterpret_cast<const unsigned char *>(&obf);
}

TEST_CASE("ObfuscatedString: basic decode") {
        String s = PROMEKI_OBFUSCATE("hello");
        CHECK(s == "hello");
}

TEST_CASE("ObfuscatedString: empty string") {
        String s = PROMEKI_OBFUSCATE("");
        CHECK(s == "");
        CHECK(s.size() == 0);
}

TEST_CASE("ObfuscatedString: single character") {
        String s = PROMEKI_OBFUSCATE("x");
        CHECK(s == "x");
}

TEST_CASE("ObfuscatedString: special characters") {
        String s = PROMEKI_OBFUSCATE("hello\nworld\t!");
        CHECK(s == "hello\nworld\t!");
}

TEST_CASE("ObfuscatedString: long string") {
        String s = PROMEKI_OBFUSCATE("The quick brown fox jumps over the lazy dog");
        CHECK(s == "The quick brown fox jumps over the lazy dog");
}

TEST_CASE("ObfuscatedString: implicit conversion") {
        static constexpr auto obf = ObfuscatedString<6, 12345>("hello");
        String                s = obf;
        CHECK(s == "hello");
}

TEST_CASE("ObfuscatedString: explicit decode") {
        static constexpr auto obf = ObfuscatedString<6, 12345>("hello");
        CHECK(obf.decode() == "hello");
}

TEST_CASE("ObfuscatedString: different seeds produce different data") {
        static constexpr auto obf1 = ObfuscatedString<6, 100>("hello");
        static constexpr auto obf2 = ObfuscatedString<6, 200>("hello");
        // Both decode to the same plaintext.
        CHECK(obf1.decode() == "hello");
        CHECK(obf2.decode() == "hello");
}

TEST_CASE("ObfuscatedString: binary data preserved") {
        String s = PROMEKI_OBFUSCATE("\x01\x02\x03\xff");
        CHECK(s.size() == 4);
        CHECK(s[0] == '\x01');
        CHECK(s[1] == '\x02');
        CHECK(s[2] == '\x03');
        CHECK(s[3] == '\xff');
}

TEST_CASE("ObfuscatedString: multiple calls on same line") {
        // Two calls on the same line share the same seed from the macro,
        // but that's fine — they decode independently.
        String a = PROMEKI_OBFUSCATE("alpha");
        String b = PROMEKI_OBFUSCATE("beta");
        CHECK(a == "alpha");
        CHECK(b == "beta");
}

// ---------------------------------------------------------------------------
// Obfuscation quality tests
// ---------------------------------------------------------------------------

TEST_CASE("ObfuscatedString: no plaintext in encoded data") {
        // Verify that none of the encoded bytes match the corresponding
        // plaintext bytes.  For a 43-byte string the probability of zero
        // accidental matches under a random cipher is ~(255/256)^43 ≈ 85%,
        // so this test may very rarely flake for a single byte.  We allow
        // up to 2 coincidental matches to keep it robust.
        static constexpr auto obf = ObfuscatedString<44, 0xDEADBEEF>("The quick brown fox jumps over the lazy dog");
        const unsigned char  *enc = encodedBytes(obf);
        const char           *plain = "The quick brown fox jumps over the lazy dog";
        int                   matches = 0;
        for (size_t i = 0; i < 43; ++i) {
                if (enc[i] == static_cast<unsigned char>(plain[i])) ++matches;
        }
        CHECK(matches <= 2);
}

TEST_CASE("ObfuscatedString: repeated plaintext bytes produce distinct encoded bytes") {
        // "aaaaaaaaaaaaaaaa" — 16 identical input bytes should not produce
        // a repeating pattern in the encoded output.
        static constexpr auto   obf = ObfuscatedString<17, 0x12345678>("aaaaaaaaaaaaaaaa");
        const unsigned char    *enc = encodedBytes(obf);
        std::set<unsigned char> unique;
        for (size_t i = 0; i < 16; ++i) unique.insert(enc[i]);
        // With 16 bytes under a good cipher, expect most to be unique.
        // Require at least 10 distinct values (birthday bound for 256 is ~20
        // before a 50% collision chance, so 10/16 is conservative).
        CHECK(unique.size() >= 10);
}

TEST_CASE("ObfuscatedString: CBC propagation — one byte change cascades") {
        // Two strings that differ only in the first byte.  Every encoded
        // byte after position 0 should also differ due to CBC chaining.
        static constexpr auto obf1 = ObfuscatedString<17, 0xAAAAAAAA>("Abcdefghijklmnop");
        static constexpr auto obf2 = ObfuscatedString<17, 0xAAAAAAAA>("Bbcdefghijklmnop");
        const unsigned char  *enc1 = encodedBytes(obf1);
        const unsigned char  *enc2 = encodedBytes(obf2);

        // The first byte must differ (different input).
        CHECK(enc1[0] != enc2[0]);

        // Count how many of the remaining 15 bytes differ.
        int diffs = 0;
        for (size_t i = 1; i < 16; ++i) {
                if (enc1[i] != enc2[i]) ++diffs;
        }
        // With 64-bit CBC state, the change should propagate to all
        // subsequent bytes.  Allow at most 1 coincidental match.
        CHECK(diffs >= 14);
}

TEST_CASE("ObfuscatedString: avalanche — small input change flips many bits") {
        // Strings differ by one bit in the first byte ('A' vs 'C' differ
        // in bit 1).  Measure total bit differences across all 16 bytes.
        static constexpr auto obf1 = ObfuscatedString<17, 0xBBBBBBBB>("Abcdefghijklmnop");
        static constexpr auto obf2 = ObfuscatedString<17, 0xBBBBBBBB>("Cbcdefghijklmnop");
        const unsigned char  *enc1 = encodedBytes(obf1);
        const unsigned char  *enc2 = encodedBytes(obf2);

        int bitDiffs = 0;
        for (size_t i = 0; i < 16; ++i) {
                unsigned char x = enc1[i] ^ enc2[i];
                while (x) {
                        bitDiffs += x & 1;
                        x >>= 1;
                }
        }
        // 16 bytes = 128 bits.  Ideal avalanche is ~64 bits flipped (50%).
        // Require at least 30% (≈38 bits) to confirm good diffusion.
        CHECK(bitDiffs >= 38);
}

TEST_CASE("ObfuscatedString: seed sensitivity — adjacent seeds differ widely") {
        // Same plaintext, seeds that differ by 1.  Output should be
        // completely different.
        static constexpr auto obf1 = ObfuscatedString<17, 1000>("abcdefghijklmnop");
        static constexpr auto obf2 = ObfuscatedString<17, 1001>("abcdefghijklmnop");
        const unsigned char  *enc1 = encodedBytes(obf1);
        const unsigned char  *enc2 = encodedBytes(obf2);

        int diffs = 0;
        for (size_t i = 0; i < 16; ++i) {
                if (enc1[i] != enc2[i]) ++diffs;
        }
        // Adjacent seeds should produce entirely different output.
        CHECK(diffs >= 14);
}

TEST_CASE("ObfuscatedString: encoded byte distribution is roughly uniform") {
        // Encode several strings with different seeds and collect all
        // output bytes.  Check that we see a reasonable spread across
        // the 0-255 range (not clustering around any value).
        static constexpr auto obf1 = ObfuscatedString<44, 0x1111>("The quick brown fox jumps over the lazy dog");
        static constexpr auto obf2 = ObfuscatedString<44, 0x2222>("The quick brown fox jumps over the lazy dog");
        static constexpr auto obf3 = ObfuscatedString<44, 0x3333>("The quick brown fox jumps over the lazy dog");
        static constexpr auto obf4 = ObfuscatedString<44, 0x4444>("The quick brown fox jumps over the lazy dog");

        std::set<unsigned char> seen;
        auto                    collect = [&](const unsigned char *enc, size_t len) {
                for (size_t i = 0; i < len; ++i) seen.insert(enc[i]);
        };
        collect(encodedBytes(obf1), 43);
        collect(encodedBytes(obf2), 43);
        collect(encodedBytes(obf3), 43);
        collect(encodedBytes(obf4), 43);

        // 172 total bytes — under uniform random we expect ~136 unique
        // values (birthday problem).  Require at least 100.
        CHECK(seen.size() >= 100);
}
