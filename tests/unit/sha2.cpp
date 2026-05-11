/**
 * @file      sha2.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <string>
#include <promeki/sha2.h>

using namespace promeki;

// FIPS 180-4 / RFC 6234 test vectors.

TEST_CASE("SHA256: empty string") {
        SHA256Digest d = sha256("", 0);
        CHECK(d.toHexString() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("SHA256: 'abc'") {
        SHA256Digest d = sha256("abc", 3);
        CHECK(d.toHexString() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA256: 56-byte FIPS vector") {
        const char  *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        SHA256Digest d = sha256(msg, std::strlen(msg));
        CHECK(d.toHexString() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("SHA256: 'a'") {
        SHA256Digest d = sha256("a", 1);
        CHECK(d.toHexString() == "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb");
}

TEST_CASE("SHA256: 'hello world'") {
        const char  *msg = "hello world";
        SHA256Digest d = sha256(msg, std::strlen(msg));
        CHECK(d.toHexString() == "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
}

TEST_CASE("SHA256: deterministic across calls") {
        const char  *msg = "the quick brown fox jumps over the lazy dog";
        SHA256Digest d1 = sha256(msg, std::strlen(msg));
        SHA256Digest d2 = sha256(msg, std::strlen(msg));
        CHECK(d1 == d2);
}

TEST_CASE("SHA256: 1,000,000 'a' characters") {
        // Standard FIPS test vector: 1M repetitions of 'a'.
        std::string msg(1'000'000, 'a');
        SHA256Digest d = sha256(msg.data(), msg.size());
        CHECK(d.toHexString() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Streaming class.

TEST_CASE("Sha256: streaming empty") {
        Sha256 h;
        SHA256Digest d = h.finalize();
        CHECK(d.toHexString() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("Sha256: streaming single update equals one-shot") {
        const char  *msg = "abc";
        Sha256       h;
        h.update(msg, 3);
        CHECK(h.finalize() == sha256(msg, 3));
}

TEST_CASE("Sha256: streaming multi-update equals one-shot") {
        const char  *part1 = "the quick brown fox ";
        const char  *part2 = "jumps over ";
        const char  *part3 = "the lazy dog";
        std::string  whole = std::string(part1) + part2 + part3;

        Sha256 h;
        h.update(part1, std::strlen(part1));
        h.update(part2, std::strlen(part2));
        h.update(part3, std::strlen(part3));
        CHECK(h.finalize() == sha256(whole.data(), whole.size()));
}

TEST_CASE("Sha256: streaming spans 64-byte block boundaries") {
        // 200 bytes = 3 full blocks + 8-byte tail; feed in awkward
        // 7-byte chunks to exercise the in-context buffer flushing.
        std::string msg(200, '\0');
        for (size_t i = 0; i < msg.size(); ++i) msg[i] = static_cast<char>(i & 0xff);

        Sha256 h;
        for (size_t off = 0; off < msg.size(); off += 7) {
                size_t take = std::min<size_t>(7, msg.size() - off);
                h.update(msg.data() + off, take);
        }
        CHECK(h.finalize() == sha256(msg.data(), msg.size()));
}

TEST_CASE("Sha256: streaming 1M 'a' equals one-shot") {
        std::string msg(1'000'000, 'a');
        Sha256      h;
        // Feed in 65,535-byte chunks to land on awkward block offsets.
        for (size_t off = 0; off < msg.size(); off += 65535) {
                size_t take = std::min<size_t>(65535, msg.size() - off);
                h.update(msg.data() + off, take);
        }
        SHA256Digest d = h.finalize();
        CHECK(d.toHexString() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
