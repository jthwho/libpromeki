/**
 * @file      dir.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <filesystem>
#include <doctest/doctest.h>
#include <promeki/core/dir.h>
#include <promeki/core/file.h>

using namespace promeki;

static void createTestFile(const std::filesystem::path &path, char ch) {
        File f(path.string());
        f.open(IODevice::WriteOnly, File::Create | File::Truncate);
        f.write(&ch, 1);
        f.close();
}

TEST_CASE("Dir: current directory") {
        Dir cur = Dir::current();
        CHECK_FALSE(cur.path().isEmpty());
        CHECK(cur.exists());
}

TEST_CASE("Dir: home directory") {
        Dir h = Dir::home();
        CHECK_FALSE(h.path().isEmpty());
        CHECK(h.exists());
}

TEST_CASE("Dir: temp directory") {
        Dir t = Dir::temp();
        CHECK_FALSE(t.path().isEmpty());
        CHECK(t.exists());
}

TEST_CASE("Dir: mkdir and remove") {
        Dir t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_mkdir";
        Dir d(testDir);

        // Clean up if left over from a previous run
        if(d.exists()) d.removeRecursively();

        Error err = d.mkdir();
        CHECK(err.isOk());
        CHECK(d.exists());
        CHECK(d.isEmpty());

        err = d.remove();
        CHECK(err.isOk());
        CHECK_FALSE(d.exists());
}

TEST_CASE("Dir: mkpath and removeRecursively") {
        Dir t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_mkpath" / "sub1" / "sub2";
        Dir d(testDir);

        // Clean up root if left over
        Dir root(t.path() / "promeki_dir_test_mkpath");
        if(root.exists()) root.removeRecursively();

        Error err = d.mkpath();
        CHECK(err.isOk());
        CHECK(d.exists());

        err = root.removeRecursively();
        CHECK(err.isOk());
        CHECK_FALSE(root.exists());
}

TEST_CASE("Dir: entryList") {
        Dir t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_entrylist";
        Dir d(testDir);

        if(d.exists()) d.removeRecursively();
        d.mkdir();

        // Create some files
        createTestFile(testDir.toStdPath() / "file1.txt", 'a');
        createTestFile(testDir.toStdPath() / "file2.txt", 'b');
        createTestFile(testDir.toStdPath() / "file3.dat", 'c');

        List<FilePath> entries = d.entryList();
        CHECK(entries.size() == 3);

        d.removeRecursively();
}

TEST_CASE("Dir: entryList with filter") {
        Dir t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_filter";
        Dir d(testDir);

        if(d.exists()) d.removeRecursively();
        d.mkdir();

        createTestFile(testDir.toStdPath() / "alpha.txt", 'a');
        createTestFile(testDir.toStdPath() / "beta.txt", 'b');
        createTestFile(testDir.toStdPath() / "gamma.dat", 'c');

        List<FilePath> txtFiles = d.entryList("*.txt");
        CHECK(txtFiles.size() == 2);

        List<FilePath> datFiles = d.entryList("*.dat");
        CHECK(datFiles.size() == 1);

        d.removeRecursively();
}

TEST_CASE("Dir: isEmpty") {
        Dir t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_empty";
        Dir d(testDir);

        if(d.exists()) d.removeRecursively();
        d.mkdir();
        CHECK(d.isEmpty());

        createTestFile(testDir.toStdPath() / "file.txt", 'x');
        CHECK_FALSE(d.isEmpty());

        d.removeRecursively();
}

TEST_CASE("Dir: non-existent directory") {
        Dir d("/nonexistent_promeki_test_path_12345");
        CHECK_FALSE(d.exists());
        CHECK(d.isEmpty());
}

TEST_CASE("Dir: setCurrent") {
        Dir original = Dir::current();

        Dir t = Dir::temp();
        Error err = Dir::setCurrent(t.path());
        CHECK(err.isOk());

        Dir now = Dir::current();
        // Resolve symlinks for comparison
        std::error_code ec;
        auto expected = std::filesystem::canonical(t.path().toStdPath(), ec);
        auto actual = std::filesystem::canonical(now.path().toStdPath(), ec);
        CHECK(expected == actual);

        // Restore
        Dir::setCurrent(original.path());
}

TEST_CASE("Dir: construction from String") {
        Dir d(String("/tmp"));
        CHECK(d.path().toString() == "/tmp");
}

TEST_CASE("Dir: construction from FilePath") {
        FilePath fp("/tmp");
        Dir d(fp);
        CHECK(d.path().toString() == "/tmp");
}
