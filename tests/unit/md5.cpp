/**
 * @file      md5.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <promeki/md5.h>

using namespace promeki;

// RFC 1321 test vectors
TEST_CASE("MD5: empty string") {
        MD5Digest d = md5("", 0);
        CHECK(d.toHexString() == "d41d8cd98f00b204e9800998ecf8427e");
}

TEST_CASE("MD5: 'a'") {
        MD5Digest d = md5("a", 1);
        CHECK(d.toHexString() == "0cc175b9c0f1b6a831c399e269772661");
}

TEST_CASE("MD5: 'abc'") {
        MD5Digest d = md5("abc", 3);
        CHECK(d.toHexString() == "900150983cd24fb0d6963f7d28e17f72");
}

TEST_CASE("MD5: 'message digest'") {
        const char *msg = "message digest";
        MD5Digest   d = md5(msg, std::strlen(msg));
        CHECK(d.toHexString() == "f96b697d7cb7938d525a2f31aaf161d0");
}

TEST_CASE("MD5: 'abcdefghijklmnopqrstuvwxyz'") {
        const char *msg = "abcdefghijklmnopqrstuvwxyz";
        MD5Digest   d = md5(msg, std::strlen(msg));
        CHECK(d.toHexString() == "c3fcd3d76192e4007dfb496cca67e13b");
}

TEST_CASE("MD5: mixed case alphabet + digits") {
        const char *msg = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        MD5Digest   d = md5(msg, std::strlen(msg));
        CHECK(d.toHexString() == "d174ab98d277d9f5a5611c2c9f419d9f");
}

TEST_CASE("MD5: numeric string") {
        const char *msg = "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
        MD5Digest   d = md5(msg, std::strlen(msg));
        CHECK(d.toHexString() == "57edf4a22be3c955ac49da2e2107b67a");
}

TEST_CASE("MD5: deterministic") {
        const char *msg = "hello world";
        MD5Digest   d1 = md5(msg, std::strlen(msg));
        MD5Digest   d2 = md5(msg, std::strlen(msg));
        CHECK(d1 == d2);
}
