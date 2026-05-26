/**
 * @file      fileinfo.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/dir.h>
#include <promeki/fileinfo.h>
#include <promeki/filepath.h>
#include <promeki/string.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

using namespace promeki;

namespace {

        std::filesystem::path makeScratchDir(const char *tag) {
                std::filesystem::path base = Dir::temp().path().toStdPath();
                std::filesystem::path dir =
                        base / (std::string("fileinfo-test-") + tag + "-" +
                                std::to_string(::getpid()) + "-" +
                                std::to_string(std::rand()));
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                return dir;
        }

        bool writeFile(const std::filesystem::path &path, const std::string &body) {
                std::FILE *fp = std::fopen(path.string().c_str(), "wb");
                if (fp == nullptr) return false;
                if (!body.empty()) std::fwrite(body.data(), 1, body.size(), fp);
                std::fclose(fp);
                return true;
        }

} // namespace

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
        CHECK(fi.suffix() == "");
}

TEST_CASE("FileInfo: suffix on dotfile") {
        FileInfo fi("/home/user/.gitignore");
        CHECK(fi.suffix() == "");
}

TEST_CASE("FileInfo: suffix on compound extension") {
        FileInfo fi("/home/user/archive.tar.gz");
        CHECK(fi.suffix() == "gz");
}

TEST_CASE("FileInfo: size of non-existent file") {
        FileInfo fi("/nonexistent/file.txt");
        auto [sz, err] = fi.size();
        CHECK(err == Error::NotExist);
        CHECK(sz == 0);
}

TEST_CASE("FileInfo: FilePath constructor") {
        FilePath fp("/home/user/document.pdf");
        FileInfo fi(fp);
        CHECK(fi.fileName() == "document.pdf");
        CHECK(fi.baseName() == "document");
}

TEST_CASE("FileInfo: filePath accessor") {
        FileInfo fi("/tmp/test.txt");
        FilePath fp = fi.filePath();
        CHECK(fp.toString() == "/tmp/test.txt");
        CHECK(fp.fileName() == "test.txt");
}

TEST_CASE("FileInfo: FilePath round-trip") {
        FilePath original("/home/user/image.png");
        FileInfo fi(original);
        FilePath result = fi.filePath();
        CHECK(result.toString() == original.toString());
}

TEST_CASE("FileInfo: isSymlink + isLink — OS symlink") {
        auto dir = makeScratchDir("issym");
        auto target = dir / "real.bin";
        auto link = dir / "default";
        REQUIRE(writeFile(target, "payload"));
        std::error_code ec;
        std::filesystem::create_symlink(target, link, ec);
        REQUIRE_FALSE(ec);
        FileInfo fi{FilePath(link)};
        CHECK(fi.isSymlink());
        CHECK(fi.isLink());
        CHECK_FALSE(fi.isPseudoSymlink());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FileInfo: isPseudoSymlink + isLink — magic-header file") {
        auto dir = makeScratchDir("pseudo");
        auto pseudo = dir / "default";
        REQUIRE(FilePath(pseudo).writePseudoSymlink(FilePath("ggml-small.bin")).isOk());
        FileInfo fi{FilePath(pseudo)};
        CHECK(fi.isPseudoSymlink());
        CHECK(fi.isLink());
        CHECK_FALSE(fi.isSymlink());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FileInfo: isLink — false for ordinary regular file") {
        auto dir = makeScratchDir("notalink");
        auto reg = dir / "regular.txt";
        REQUIRE(writeFile(reg, "just text\nmultiple lines\n"));
        FileInfo fi{FilePath(reg)};
        CHECK_FALSE(fi.isSymlink());
        CHECK_FALSE(fi.isPseudoSymlink());
        CHECK_FALSE(fi.isLink());
        std::filesystem::remove_all(dir);
}
