/**
 * @file      file.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <doctest/doctest.h>
#include <promeki/file.h>

using namespace promeki;

TEST_CASE("File: default construction") {
        File f;
        CHECK(f.filename().isEmpty());
        CHECK_FALSE(f.isOpen());
        CHECK(f.flags() == File::NoFlags);
}

TEST_CASE("File: construction with filename") {
        File f("/tmp/test_file");
        CHECK(f.filename() == "/tmp/test_file");
        CHECK_FALSE(f.isOpen());
}

TEST_CASE("File: flag queries on closed file") {
        File f;
        CHECK_FALSE(f.isReadable());
        CHECK_FALSE(f.isWritable());
        CHECK_FALSE(f.isDirectIO());
}

TEST_CASE("File: open nonexistent file for read fails") {
        File f("/tmp/promeki_test_nonexistent_file_xyz");
        Error err = f.open(File::ReadOnly);
        CHECK(err.isError());
        CHECK_FALSE(f.isOpen());
}

TEST_CASE("File: create, write, read, close") {
        const char *path = "/tmp/promeki_test_file_rw.tmp";
        // Clean up any previous run
        std::remove(path);

        {
                File f(path);
                Error err = f.open(File::Create | File::ReadWrite | File::Truncate);
                CHECK(err.isOk());
                CHECK(f.isOpen());
                CHECK(f.isReadable());
                CHECK(f.isWritable());

                const char *data = "Hello, promeki!";
                File::FileBytes written = f.write(data, 15);
                CHECK(written == 15);

                // Seek back to beginning
                File::FileBytes pos = f.seek(0);
                CHECK(pos == 0);

                char buf[16] = {};
                File::FileBytes bytesRead = f.read(buf, 15);
                CHECK(bytesRead == 15);
                CHECK(String(buf) == "Hello, promeki!");

                f.close();
                CHECK_FALSE(f.isOpen());
        }

        std::remove(path);
}

TEST_CASE("File: position tracking") {
        const char *path = "/tmp/promeki_test_file_pos.tmp";
        std::remove(path);

        File f(path);
        Error err = f.open(File::Create | File::ReadWrite | File::Truncate);
        REQUIRE(err.isOk());

        const char *data = "0123456789";
        f.write(data, 10);

        File::FileBytes pos = f.position();
        CHECK(pos == 10);

        f.seek(5);
        CHECK(f.position() == 5);

        f.seekFromCurrent(2);
        CHECK(f.position() == 7);

        f.seekFromEnd(0);
        CHECK(f.position() == 10);

        f.close();
        std::remove(path);
}

TEST_CASE("File: truncate") {
        const char *path = "/tmp/promeki_test_file_trunc.tmp";
        std::remove(path);

        File f(path);
        Error err = f.open(File::Create | File::ReadWrite | File::Truncate);
        REQUIRE(err.isOk());

        const char *data = "0123456789";
        f.write(data, 10);

        err = f.truncate(5);
        CHECK(err.isOk());

        f.seekFromEnd(0);
        CHECK(f.position() == 5);

        f.close();
        std::remove(path);
}

TEST_CASE("File: ReadWrite flag is combo of ReadOnly|WriteOnly") {
        CHECK(File::ReadWrite == (File::ReadOnly | File::WriteOnly));
}
