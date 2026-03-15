/**
 * @file      fileinfo.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/fileinfo.h>
#include <promeki/core/string.h>

using namespace promeki;

TEST_CASE("FileInfo: construction from path") {
        FileInfo fi("/tmp/test.txt");
        CHECK(fi.fileName() == "test.txt");
}

TEST_CASE("FileInfo: baseName") {
        FileInfo fi("/home/user/document.pdf");
        CHECK(fi.baseName() == "document");
}

TEST_CASE("FileInfo: suffix") {
        FileInfo fi("/home/user/image.png");
        CHECK(fi.suffix() == "png");
}

TEST_CASE("FileInfo: absolutePath") {
        FileInfo fi("/home/user/file.txt");
        CHECK(fi.absolutePath() == "/home/user");
}

TEST_CASE("FileInfo: non-existent file") {
        FileInfo fi("/nonexistent/path/file.xyz");
        CHECK_FALSE(fi.exists());
        CHECK_FALSE(fi.isFile());
        CHECK_FALSE(fi.isDirectory());
}

TEST_CASE("FileInfo: root directory exists") {
        FileInfo fi("/tmp");
        CHECK(fi.exists());
        CHECK(fi.isDirectory());
        CHECK_FALSE(fi.isFile());
}

TEST_CASE("FileInfo: fileName with no extension") {
        FileInfo fi("/home/user/Makefile");
        CHECK(fi.fileName() == "Makefile");
        CHECK(fi.baseName() == "Makefile");
}

TEST_CASE("FileInfo: size of non-existent file") {
        FileInfo fi("/nonexistent/file.txt");
        CHECK(fi.size() == 0);
}
