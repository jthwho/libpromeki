/**
 * @file      resource.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests the Resource compiled-in resource filesystem and the
 * resource-path branches in File and Dir.
 */

#include <doctest/doctest.h>
#include <promeki/config.h>
#include <promeki/resource.h>
#include <promeki/file.h>
#include <promeki/dir.h>
#include <promeki/buffer.h>

using namespace promeki;

#if PROMEKI_ENABLE_CIRF

// All built-in promeki resources live under :/.PROMEKI/. The font
// path is the canonical "is the resource set wired up at all" probe
// — if these tests don't see the bytes, the cirf integration is
// broken end-to-end.
static const String kFontPath = ":/.PROMEKI/fonts/FiraCodeNerdFontMono-Regular.ttf";
static const String kFontDir = ":/.PROMEKI/fonts";

// ============================================================================
// Resource: prefix detection
// ============================================================================

TEST_CASE("Resource: isResourcePath true cases") {
        CHECK(Resource::isResourcePath(":/"));
        CHECK(Resource::isResourcePath(":/foo"));
        CHECK(Resource::isResourcePath(":/foo/bar.txt"));
        CHECK(Resource::isResourcePath(":/.PROMEKI/fonts/x.ttf"));
}

TEST_CASE("Resource: isResourcePath false cases") {
        CHECK_FALSE(Resource::isResourcePath(""));
        CHECK_FALSE(Resource::isResourcePath("/"));
        CHECK_FALSE(Resource::isResourcePath("/foo/bar"));
        CHECK_FALSE(Resource::isResourcePath("foo/bar"));
        CHECK_FALSE(Resource::isResourcePath(":foo/bar"));
        CHECK_FALSE(Resource::isResourcePath("./foo"));
}

TEST_CASE("Resource: stripPrefix") {
        CHECK(Resource::stripPrefix(":/foo/bar.txt") == "foo/bar.txt");
        CHECK(Resource::stripPrefix(":/").isEmpty());
        CHECK(Resource::stripPrefix("/foo/bar").isEmpty());
        CHECK(Resource::stripPrefix("foo").isEmpty());
}

// ============================================================================
// Resource: lookup of the built-in :/.PROMEKI set
// ============================================================================

TEST_CASE("Resource: built-in font file is reachable") {
        CHECK(Resource::exists(kFontPath));
        CHECK(Resource::findFile(kFontPath) != nullptr);
        // The TrueType font should be at least one disk page; the
        // exact size is irrelevant — we just need to know we got
        // real bytes back, not an empty stub.
        CHECK(Resource::size(kFontPath) > 4096);
}

TEST_CASE("Resource: built-in font folder is reachable") {
        CHECK(Resource::exists(kFontDir));
        CHECK(Resource::findFolder(kFontDir) != nullptr);
}

TEST_CASE("Resource: nonexistent paths return nothing") {
        CHECK_FALSE(Resource::exists(":/.PROMEKI/does/not/exist"));
        CHECK(Resource::findFile(":/.PROMEKI/does/not/exist") == nullptr);
        CHECK(Resource::findFolder(":/.PROMEKI/does/not/exist") == nullptr);
        CHECK(Resource::size(":/.PROMEKI/does/not/exist") == 0);
}

TEST_CASE("Resource: data() returns a non-owning Buffer view") {
        Error  err;
        Buffer buf = Resource::data(kFontPath, &err);
        REQUIRE(err == Error(Error::Ok));
        REQUIRE(buf.isValid());
        CHECK(buf.size() == Resource::size(kFontPath));
        // The buffer is a view into .rodata, so it must be host
        // accessible and the underlying allocation should match the
        // logical size (no shift, no over-allocation).
        CHECK(buf.isHostAccessible());
        CHECK(buf.allocSize() == buf.size());
}

TEST_CASE("Resource: data() reports NotExist for missing paths") {
        Error  err;
        Buffer buf = Resource::data(":/.PROMEKI/missing.bin", &err);
        CHECK(err == Error(Error::NotExist));
        CHECK_FALSE(buf.isValid());
}

TEST_CASE("Resource: mime() returns a non-empty type for known files") {
        // cirf guesses the MIME type by extension. The exact value
        // is implementation-defined, but it must be non-empty for
        // a known extension.
        String mime = Resource::mime(kFontPath);
        CHECK_FALSE(mime.isEmpty());
}

// ============================================================================
// Resource: directory listings
// ============================================================================

TEST_CASE("Resource: listFiles returns expected entries") {
        Error          err;
        List<FilePath> files = Resource::listFiles(kFontDir, &err);
        CHECK(err == Error(Error::Ok));

        bool sawFont = false;
        for (const FilePath &fp : files) {
                if (fp.toString() == kFontPath) sawFont = true;
        }
        CHECK(sawFont);
}

TEST_CASE("Resource: listFolders is empty inside fonts dir") {
        // .PROMEKI/fonts only contains files, no subdirectories.
        Error          err;
        List<FilePath> folders = Resource::listFolders(kFontDir, &err);
        CHECK(err == Error(Error::Ok));
        CHECK(folders.size() == 0);
}

TEST_CASE("Resource: listEntries lists the files in the fonts dir") {
        Error          err;
        List<FilePath> all = Resource::listEntries(kFontDir, &err);
        CHECK(err == Error(Error::Ok));
        CHECK(all.size() >= 1);
}

TEST_CASE("Resource: list functions on missing path set NotExist") {
        Error          err;
        List<FilePath> files = Resource::listFiles(":/.PROMEKI/nope", &err);
        CHECK(err == Error(Error::NotExist));
        CHECK(files.size() == 0);
}

// ============================================================================
// Resource: registerRoot / unregisterRoot lifecycle
// ============================================================================

TEST_CASE("Resource: registerRoot rejects null root") {
        Error err = Resource::registerRoot(nullptr, "test");
        CHECK(err == Error(Error::InvalidArgument));
}

TEST_CASE("Resource: unregister + re-register the built-in set") {
        // Capture the current root so we can put it back. findFolder
        // with the bare ":/" path returns the root of the registered
        // mount whose prefix is empty — that's our built-in set.
        const cirf_folder_t *root = Resource::findFolder(":/");
        REQUIRE(root != nullptr);

        // Tear down — every :/.PROMEKI lookup should now miss.
        Resource::unregisterRoot("");
        CHECK_FALSE(Resource::exists(kFontPath));
        CHECK(Resource::findFile(kFontPath) == nullptr);

        // Restore so the rest of the suite still has fonts.
        Error err = Resource::registerRoot(root, "");
        CHECK(err == Error(Error::Ok));
        CHECK(Resource::exists(kFontPath));
}

// ============================================================================
// File: resource paths
// ============================================================================

TEST_CASE("File: open a resource path read-only") {
        File f(kFontPath);
        CHECK(f.filename() == kFontPath);
        CHECK_FALSE(f.isOpen());

        Error err = f.open(File::ReadOnly);
        REQUIRE(err == Error(Error::Ok));
        CHECK(f.isOpen());
        CHECK(f.isResource());

        auto [sz, sErr] = f.size();
        REQUIRE(sErr == Error(Error::Ok));
        CHECK(sz == static_cast<int64_t>(Resource::size(kFontPath)));
        CHECK(f.pos() == 0);
        CHECK_FALSE(f.atEnd());

        Buffer all = f.readAll();
        CHECK(all.size() == static_cast<size_t>(sz));
        CHECK(f.atEnd());
        f.close();
        CHECK_FALSE(f.isOpen());
}

TEST_CASE("File: write modes on resource paths fail with ReadOnly") {
        File f(kFontPath);
        CHECK(f.open(File::WriteOnly) == Error(Error::ReadOnly));
        CHECK_FALSE(f.isOpen());

        File f2(kFontPath);
        CHECK(f2.open(File::ReadWrite) == Error(Error::ReadOnly));
        CHECK_FALSE(f2.isOpen());
}

TEST_CASE("File: write() to a read-open resource fails") {
        File f(kFontPath);
        REQUIRE(f.open(File::ReadOnly) == Error(Error::Ok));
        const char buf[] = "abcd";
        int64_t    n = f.write(buf, 4);
        CHECK(n == -1);
}

TEST_CASE("File: seek + pos on a resource path") {
        File f(kFontPath);
        REQUIRE(f.open(File::ReadOnly) == Error(Error::Ok));
        auto [sz, _] = f.size();
        REQUIRE(sz > 16);

        REQUIRE(f.seek(8) == Error(Error::Ok));
        CHECK(f.pos() == 8);

        // Out-of-range seek is rejected.
        CHECK(f.seek(sz + 1) == Error(Error::IllegalSeek));
        CHECK(f.seek(-1) == Error(Error::IllegalSeek));

        // Reading after a partial seek returns the tail.
        REQUIRE(f.seek(sz - 4) == Error(Error::Ok));
        char    buf[16] = {};
        int64_t n = f.read(buf, sizeof(buf));
        CHECK(n == 4);
        CHECK(f.atEnd());
}

TEST_CASE("File: open nonexistent resource path returns NotExist") {
        File f(":/.PROMEKI/no-such-file.bin");
        CHECK(f.open(File::ReadOnly) == Error(Error::NotExist));
        CHECK_FALSE(f.isOpen());
}

// ============================================================================
// Dir: resource paths
// ============================================================================

TEST_CASE("Dir: existing resource directory") {
        Dir d(kFontDir);
        CHECK(d.exists());
        CHECK_FALSE(d.isEmpty());

        List<FilePath> entries = d.entryList();
        CHECK(entries.size() >= 1);

        // entryList(filter) on the resource branch runs fnmatch
        // against each entry's basename. The bundled .ttf file
        // must be present.
        List<FilePath> ttfs = d.entryList("*.ttf");
        REQUIRE(ttfs.size() >= 1);
        bool sawFont = false;
        for (const FilePath &fp : ttfs) {
                if (fp.toString() == kFontPath) sawFont = true;
        }
        CHECK(sawFont);
        // Sanity: a filter that cannot match anything returns empty.
        List<FilePath> nope = d.entryList("*.xyz");
        CHECK(nope.size() == 0);
}

TEST_CASE("Dir: nonexistent resource directory") {
        Dir d(":/.PROMEKI/no-such-folder");
        CHECK_FALSE(d.exists());
        CHECK(d.isEmpty());
        CHECK(d.entryList().size() == 0);
}

TEST_CASE("Dir: mutating ops on a resource path return ReadOnly") {
        Dir d(":/.PROMEKI/scratch");
        CHECK(d.mkdir() == Error(Error::ReadOnly));
        CHECK(d.mkpath() == Error(Error::ReadOnly));
        CHECK(d.remove() == Error(Error::ReadOnly));
        CHECK(d.removeRecursively() == Error(Error::ReadOnly));
}

#endif // PROMEKI_ENABLE_CIRF
