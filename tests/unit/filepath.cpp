/**
 * @file      filepath.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/filepath.h>

using namespace promeki;

TEST_CASE("FilePath: default construction") {
        FilePath p;
        CHECK(p.isEmpty());
        CHECK(p.toString().isEmpty());
}

TEST_CASE("FilePath: construction from String") {
        FilePath p(String("/home/user/file.txt"));
        CHECK_FALSE(p.isEmpty());
        CHECK(p.toString() == "/home/user/file.txt");
}

TEST_CASE("FilePath: construction from const char*") {
        FilePath p("/tmp/test.wav");
        CHECK(p.toString() == "/tmp/test.wav");
}

TEST_CASE("FilePath: construction from std::filesystem::path") {
        std::filesystem::path sp("/var/log/syslog");
        FilePath              p(sp);
        CHECK(p.toString() == "/var/log/syslog");
}

TEST_CASE("FilePath: fileName") {
        FilePath p("/home/user/document.pdf");
        CHECK(p.fileName() == "document.pdf");
}

TEST_CASE("FilePath: baseName") {
        FilePath p("/home/user/document.pdf");
        CHECK(p.baseName() == "document");
}

TEST_CASE("FilePath: suffix") {
        SUBCASE("normal extension") {
                FilePath p("/home/user/file.txt");
                CHECK(p.suffix() == "txt");
        }

        SUBCASE("no extension") {
                FilePath p("/home/user/Makefile");
                CHECK(p.suffix().isEmpty());
        }

        SUBCASE("multiple dots") {
                FilePath p("/home/user/archive.tar.gz");
                CHECK(p.suffix() == "gz");
        }
}

TEST_CASE("FilePath: completeSuffix") {
        SUBCASE("single extension") {
                FilePath p("/home/user/file.txt");
                CHECK(p.completeSuffix() == "txt");
        }

        SUBCASE("multiple extensions") {
                FilePath p("/home/user/archive.tar.gz");
                CHECK(p.completeSuffix() == "tar.gz");
        }

        SUBCASE("no extension") {
                FilePath p("/home/user/Makefile");
                CHECK(p.completeSuffix().isEmpty());
        }
}

TEST_CASE("FilePath: parent") {
        FilePath p("/home/user/file.txt");
        FilePath par = p.parent();
        CHECK(par.toString() == "/home/user");
}

TEST_CASE("FilePath: join and operator/") {
        FilePath base("/home/user");
        FilePath sub("documents");

        FilePath joined = base.join(sub);
        CHECK(joined.toString() == "/home/user/documents");

        FilePath joined2 = base / sub;
        CHECK(joined2.toString() == "/home/user/documents");

        FilePath joined3 = base / "downloads";
        CHECK(joined3.toString() == "/home/user/downloads");

        FilePath joined4 = base / String("music");
        CHECK(joined4.toString() == "/home/user/music");
}

TEST_CASE("FilePath: isAbsolute and isRelative") {
        FilePath abs("/home/user");
        CHECK(abs.isAbsolute());
        CHECK_FALSE(abs.isRelative());

        FilePath rel("src/main.cpp");
        CHECK(rel.isRelative());
        CHECK_FALSE(rel.isAbsolute());
}

TEST_CASE("FilePath: absolutePath") {
        FilePath abs("/home/user");
        FilePath result = abs.absolutePath();
        CHECK(result.isAbsolute());
        CHECK(result.toString() == "/home/user");
}

TEST_CASE("FilePath: exists") {
        FilePath exists("/tmp");
        CHECK(exists.exists());

        FilePath missing("/tmp/promeki_nonexistent_path_xyz_123");
        CHECK_FALSE(missing.exists());
}

TEST_CASE("FilePath: comparison operators") {
        FilePath a("/home/a");
        FilePath b("/home/b");
        FilePath a2("/home/a");

        CHECK(a == a2);
        CHECK(a != b);
        CHECK(a < b);
}

TEST_CASE("FilePath: empty string construction") {
        FilePath p(String(""));
        CHECK(p.isEmpty());
}

TEST_CASE("FilePath: dotfile suffix") {
        FilePath p("/home/user/.gitignore");
        // .gitignore has no stem-extension split; filesystem treats
        // the entire name as stem with an empty extension on some
        // implementations, or as extension with empty stem.
        // Just verify it doesn't crash and returns consistent results.
        String fn = p.fileName();
        CHECK(fn == ".gitignore");
}

TEST_CASE("FilePath: root path parent") {
        FilePath p("/");
        FilePath par = p.parent();
        CHECK(par.toString() == "/");
}

TEST_CASE("FilePath: toStdPath") {
        FilePath                     p("/tmp/test");
        const std::filesystem::path &sp = p.toStdPath();
        CHECK(sp == std::filesystem::path("/tmp/test"));
}

TEST_CASE("FilePath: relativeTo — sibling directory") {
        // /a/b relative to /a/c == ../b
        FilePath         target("/a/b");
        FilePath         base("/a/c");
        Result<FilePath> rel = target.relativeTo(base);
        REQUIRE(rel.second().isOk());
        CHECK(rel.first().isRelative());
        CHECK(rel.first().toString() == "../b");
}

TEST_CASE("FilePath: relativeTo — same directory returns dot") {
        FilePath         target("/a/b");
        FilePath         base("/a/b");
        Result<FilePath> rel = target.relativeTo(base);
        // std::filesystem::relative of /a/b from /a/b is "."
        REQUIRE(rel.second().isOk());
        CHECK(rel.first().toString() == ".");
}

TEST_CASE("FilePath: relativeTo — nested path") {
        // /a/b/c/d relative to /a/b == c/d
        FilePath         target("/a/b/c/d");
        FilePath         base("/a/b");
        Result<FilePath> rel = target.relativeTo(base);
        REQUIRE(rel.second().isOk());
        CHECK(rel.first().isRelative());
        CHECK(rel.first().toString() == "c/d");
}

TEST_CASE("FilePath: relativeTo — parent path") {
        // /a relative to /a/b/c == ../..
        FilePath         target("/a");
        FilePath         base("/a/b/c");
        Result<FilePath> rel = target.relativeTo(base);
        REQUIRE(rel.second().isOk());
        CHECK(rel.first().isRelative());
        CHECK(rel.first().toString() == "../..");
}
