/**
 * @file      filepath.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/dir.h>
#include <promeki/filepath.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

using namespace promeki;

namespace {

        // Returns a uniquely-named subfolder under Dir::temp().  Each
        // link-API test creates its own folder so concurrent test
        // workers don't tread on each other's symlink fixtures.
        // Dir::temp() picks up the LibraryOptions::TempDir override on
        // this machine ( /mnt/data/tmp/promeki/ ), so the scratch tree
        // does not land on the host's tmpfs.
        std::filesystem::path makeScratchDir(const char *tag) {
                std::filesystem::path base = Dir::temp().path().toStdPath();
                std::filesystem::path dir =
                        base / (std::string("filepath-test-") + tag + "-" +
                                std::to_string(::getpid()) + "-" +
                                std::to_string(std::rand()));
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                return dir;
        }

        // Writes @p body to @p path, truncating any existing file.
        // Returns @c true on success, @c false on any I/O error.
        bool writeFile(const std::filesystem::path &path, const std::string &body) {
                std::FILE *fp = std::fopen(path.string().c_str(), "wb");
                if (fp == nullptr) return false;
                if (!body.empty()) {
                        std::fwrite(body.data(), 1, body.size(), fp);
                }
                std::fclose(fp);
                return true;
        }

} // namespace

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

// =============================================================================
// Link / pseudo-symlink APIs
// =============================================================================

TEST_CASE("FilePath: isSymlink — false for regular file") {
        auto dir = makeScratchDir("issym-reg");
        auto reg = dir / "regular.txt";
        REQUIRE(writeFile(reg, "hello"));
        CHECK_FALSE(FilePath(reg).isSymlink());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: isSymlink — true for OS symlink, even when broken") {
        auto dir = makeScratchDir("issym-os");
        auto target = dir / "target.txt";
        auto link = dir / "link.txt";
        REQUIRE(writeFile(target, "hello"));
        std::error_code ec;
        std::filesystem::create_symlink(target, link, ec);
        REQUIRE_FALSE(ec);
        CHECK(FilePath(link).isSymlink());
        // Break the target — the link itself is still a symlink.
        std::filesystem::remove(target);
        CHECK(FilePath(link).isSymlink());
        CHECK_FALSE(FilePath(link).exists()); // broken
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: writePseudoSymlink + isPseudoSymlink round-trip") {
        auto dir = makeScratchDir("pseudo-pos");
        auto pseudo = dir / "default";
        Error e = FilePath(pseudo).writePseudoSymlink(FilePath("ggml-small.bin"));
        REQUIRE(e.isOk());
        CHECK(FilePath(pseudo).isPseudoSymlink());
        CHECK(FilePath(pseudo).isLink());
        Result<FilePath> r = FilePath(pseudo).readPseudoSymlink();
        REQUIRE(r.second().isOk());
        CHECK(r.first().toString() == "ggml-small.bin");
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: isPseudoSymlink — trailing whitespace tolerated") {
        auto dir = makeScratchDir("pseudo-ws");
        auto pseudo = dir / "default";
        // Manually staged with surrounding whitespace + the magic
        // header, to confirm readPseudoSymlink trims correctly.
        std::string body(FilePath::kPseudoSymlinkMagic);
        body += "\n   ggml-small.bin   \n";
        REQUIRE(writeFile(pseudo, body));
        CHECK(FilePath(pseudo).isPseudoSymlink());
        Result<FilePath> r = FilePath(pseudo).readPseudoSymlink();
        REQUIRE(r.second().isOk());
        CHECK(r.first().toString() == "ggml-small.bin");
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: isPseudoSymlink — magic header required") {
        auto dir = makeScratchDir("pseudo-no-magic");
        auto file = dir / "default";
        // Plain path content, no magic header — must NOT be detected
        // as a pseudo-symlink so a stray VERSION / tag file in the
        // tree isn't misclassified.
        REQUIRE(writeFile(file, "ggml-small.bin"));
        CHECK_FALSE(FilePath(file).isPseudoSymlink());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: isPseudoSymlink — empty file rejected") {
        auto dir = makeScratchDir("pseudo-empty");
        auto pseudo = dir / "default";
        REQUIRE(writeFile(pseudo, ""));
        CHECK_FALSE(FilePath(pseudo).isPseudoSymlink());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: isPseudoSymlink — magic without payload rejected") {
        auto dir = makeScratchDir("pseudo-magic-only");
        auto pseudo = dir / "default";
        std::string body(FilePath::kPseudoSymlinkMagic);
        body += "\n";
        REQUIRE(writeFile(pseudo, body));
        CHECK_FALSE(FilePath(pseudo).isPseudoSymlink());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: isPseudoSymlink — oversized file rejected") {
        auto dir = makeScratchDir("pseudo-big");
        auto pseudo = dir / "default";
        // Magic + body padded one byte past the cap.  Confirms the
        // size guard runs even when the header would otherwise look
        // valid.
        std::string body(FilePath::kPseudoSymlinkMagic);
        body += "\n";
        body.append(FilePath::kPseudoSymlinkMaxBytes, 'a');
        REQUIRE(writeFile(pseudo, body));
        CHECK_FALSE(FilePath(pseudo).isPseudoSymlink());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: isPseudoSymlink — NUL byte in payload rejected") {
        auto dir = makeScratchDir("pseudo-nul");
        auto pseudo = dir / "default";
        std::string body(FilePath::kPseudoSymlinkMagic);
        body += "\nggml-small";
        body.push_back('\0');
        body += ".bin\n";
        REQUIRE(writeFile(pseudo, body));
        CHECK_FALSE(FilePath(pseudo).isPseudoSymlink());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: isPseudoSymlink — OS symlink is not a pseudo-symlink") {
        auto dir = makeScratchDir("pseudo-vs-os");
        auto target = dir / "target.bin";
        auto link = dir / "default";
        REQUIRE(writeFile(target, "x"));
        std::error_code ec;
        std::filesystem::create_symlink(target, link, ec);
        REQUIRE_FALSE(ec);
        CHECK(FilePath(link).isSymlink());
        CHECK_FALSE(FilePath(link).isPseudoSymlink());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: readLink — OS symlink") {
        auto dir = makeScratchDir("readlink-os");
        auto target = dir / "real.bin";
        auto link = dir / "default";
        REQUIRE(writeFile(target, "x"));
        std::error_code ec;
        std::filesystem::create_symlink(target, link, ec);
        REQUIRE_FALSE(ec);
        Result<FilePath> r = FilePath(link).readLink();
        REQUIRE(r.second().isOk());
        // Stored target may be relative or absolute depending on
        // how create_symlink ran; check the filename either way.
        CHECK(r.first().fileName() == "real.bin");
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: readLink — pseudo-symlink") {
        auto dir = makeScratchDir("readlink-pseudo");
        auto pseudo = dir / "default";
        REQUIRE(FilePath(pseudo).writePseudoSymlink(FilePath("ggml-small.bin")).isOk());
        Result<FilePath> r = FilePath(pseudo).readLink();
        REQUIRE(r.second().isOk());
        CHECK(r.first().toString() == "ggml-small.bin");
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: readLink — returns Invalid on regular file") {
        auto dir = makeScratchDir("readlink-not-link");
        auto reg = dir / "regular.txt";
        // Plain text content without the magic header — must be
        // rejected by readLink.
        REQUIRE(writeFile(reg, "this is just a text file\nwith multiple lines\n"));
        Result<FilePath> r = FilePath(reg).readLink();
        CHECK(r.second().isError());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: resolveLink — OS symlink chain") {
        auto dir = makeScratchDir("resolve-chain");
        auto real = dir / "real.bin";
        auto hop1 = dir / "hop1";
        auto hop2 = dir / "hop2";
        REQUIRE(writeFile(real, "x"));
        std::error_code ec;
        std::filesystem::create_symlink(real, hop1, ec);
        REQUIRE_FALSE(ec);
        std::filesystem::create_symlink(hop1, hop2, ec);
        REQUIRE_FALSE(ec);
        Result<FilePath> r = FilePath(hop2).resolveLink();
        REQUIRE(r.second().isOk());
        // Final hop should point at real.bin regardless of whether
        // each hop stored its target absolute or relative.
        CHECK(r.first().fileName() == "real.bin");
        CHECK(FilePath(r.first()).exists());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: resolveLink — pseudo-symlink with relative target") {
        auto dir = makeScratchDir("resolve-pseudo-rel");
        auto real = dir / "real.bin";
        auto pseudo = dir / "default";
        REQUIRE(writeFile(real, "binary-payload"));
        REQUIRE(FilePath(pseudo).writePseudoSymlink(FilePath("real.bin")).isOk());
        Result<FilePath> r = FilePath(pseudo).resolveLink();
        REQUIRE(r.second().isOk());
        // Relative pseudo-symlinks resolve against the link's parent.
        CHECK(FilePath(r.first()).exists());
        CHECK(r.first().fileName() == "real.bin");
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: resolveLink — mixed OS + pseudo chain") {
        auto dir = makeScratchDir("resolve-mixed");
        auto real = dir / "real.bin";
        auto pseudo = dir / "step";        // pseudo → real.bin
        auto osLink = dir / "default";     // OS symlink → step
        REQUIRE(writeFile(real, "binary-payload"));
        REQUIRE(FilePath(pseudo).writePseudoSymlink(FilePath("real.bin")).isOk());
        std::error_code ec;
        std::filesystem::create_symlink(pseudo, osLink, ec);
        REQUIRE_FALSE(ec);
        Result<FilePath> r = FilePath(osLink).resolveLink();
        REQUIRE(r.second().isOk());
        CHECK(r.first().fileName() == "real.bin");
        CHECK(FilePath(r.first()).exists());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: resolveLink — non-link returns self with Ok") {
        auto dir = makeScratchDir("resolve-nonlink");
        auto reg = dir / "regular.txt";
        // Plain content (no magic header) — not a link.
        REQUIRE(writeFile(reg, "this is just a text file\n"));
        Result<FilePath> r = FilePath(reg).resolveLink();
        REQUIRE(r.second().isOk());
        CHECK(r.first().toString() == FilePath(reg).toString());
        std::filesystem::remove_all(dir);
}

TEST_CASE("FilePath: resolveLink — loop detection") {
        auto dir = makeScratchDir("resolve-loop");
        auto a = dir / "a";
        auto b = dir / "b";
        // Two pseudo-symlinks pointing at each other — resolveLink
        // must bail out cleanly.
        REQUIRE(FilePath(a).writePseudoSymlink(FilePath("b")).isOk());
        REQUIRE(FilePath(b).writePseudoSymlink(FilePath("a")).isOk());
        Result<FilePath> r = FilePath(a).resolveLink();
        CHECK(r.second().isError());
        std::filesystem::remove_all(dir);
}
