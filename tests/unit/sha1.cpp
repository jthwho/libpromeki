/**
 * @file      sha1.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <string>
#include <promeki/sha1.h>

using namespace promeki;

// RFC 3174 test vectors
TEST_CASE("SHA1: empty string") {
        SHA1Digest d = sha1("", 0);
        CHECK(d.toHexString() == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

TEST_CASE("SHA1: 'abc'") {
        SHA1Digest d = sha1("abc", 3);
        CHECK(d.toHexString() == "a9993e364706816aba3e25717850c26c9cd0d89d");
}

TEST_CASE("SHA1: 'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'") {
        const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        SHA1Digest  d = sha1(msg, std::strlen(msg));
        CHECK(d.toHexString() == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST_CASE("SHA1: 'a'") {
        SHA1Digest d = sha1("a", 1);
        CHECK(d.toHexString() == "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8");
}

TEST_CASE("SHA1: deterministic") {
        const char *msg = "hello world";
        SHA1Digest  d1 = sha1(msg, std::strlen(msg));
        SHA1Digest  d2 = sha1(msg, std::strlen(msg));
        CHECK(d1 == d2);
}

TEST_CASE("SHA1: known hash for 'hello world'") {
        const char *msg = "hello world";
        SHA1Digest  d = sha1(msg, std::strlen(msg));
        CHECK(d.toHexString() == "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");
}

// Streaming class.

TEST_CASE("Sha1: streaming empty") {
        Sha1       h;
        SHA1Digest d = h.finalize();
        CHECK(d.toHexString() == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

TEST_CASE("Sha1: streaming single update equals one-shot") {
        const char *msg = "abc";
        Sha1        h;
        h.update(msg, 3);
        CHECK(h.finalize() == sha1(msg, 3));
}

TEST_CASE("Sha1: streaming multi-update equals one-shot") {
        const char  *p1 = "the quick brown fox ";
        const char  *p2 = "jumps over ";
        const char  *p3 = "the lazy dog";
        std::string  whole = std::string(p1) + p2 + p3;

        Sha1 h;
        h.update(p1, std::strlen(p1));
        h.update(p2, std::strlen(p2));
        h.update(p3, std::strlen(p3));
        CHECK(h.finalize() == sha1(whole.data(), whole.size()));
}

TEST_CASE("Sha1: streaming spans 64-byte block boundaries") {
        std::string msg(200, '\0');
        for (size_t i = 0; i < msg.size(); ++i) msg[i] = static_cast<char>(i & 0xff);

        Sha1 h;
        for (size_t off = 0; off < msg.size(); off += 7) {
                size_t take = std::min<size_t>(7, msg.size() - off);
                h.update(msg.data() + off, take);
        }
        CHECK(h.finalize() == sha1(msg.data(), msg.size()));
}
