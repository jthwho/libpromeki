/**
 * @file      dir.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <filesystem>
#include <doctest/doctest.h>
#include <promeki/dir.h>
#include <promeki/file.h>
#include <promeki/libraryoptions.h>
#include <promeki/numnameseq.h>
#include <promeki/platform.h>

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

TEST_CASE("Dir: temp respects LibraryOptions::TempDir override") {
        // Save and restore so the singleton state doesn't leak into
        // other tests.
        const String saved = LibraryOptions::instance().getAs<String>(LibraryOptions::TempDir);

        LibraryOptions::instance().set(LibraryOptions::TempDir, String("/var/lib/promeki-test-override"));
        CHECK(Dir::temp().path().toString() == "/var/lib/promeki-test-override");

        // Empty value falls back to the OS default — the system temp
        // path is platform-dependent, but it must be non-empty and
        // exist for the build environment to be sane.
        LibraryOptions::instance().set(LibraryOptions::TempDir, String());
        Dir def = Dir::temp();
        CHECK_FALSE(def.path().isEmpty());
        CHECK(def.exists());

        LibraryOptions::instance().set(LibraryOptions::TempDir, saved);
}

TEST_CASE("Dir: ipc default is non-empty") {
        // The override is cleared by other tests; make sure the default
        // path exists and is non-empty on any platform we run on.
        const String saved = LibraryOptions::instance().getAs<String>(LibraryOptions::IpcDir);
        LibraryOptions::instance().set(LibraryOptions::IpcDir, String());

        Dir d = Dir::ipc();
        CHECK_FALSE(d.path().isEmpty());
#if defined(PROMEKI_PLATFORM_LINUX)
        CHECK(d.path().toString() == String("/dev/shm/promeki"));
#endif

        LibraryOptions::instance().set(LibraryOptions::IpcDir, saved);
}

TEST_CASE("Dir: ipc respects LibraryOptions::IpcDir override") {
        const String saved = LibraryOptions::instance().getAs<String>(LibraryOptions::IpcDir);

        LibraryOptions::instance().set(LibraryOptions::IpcDir, String("/var/run/promeki-test-override"));
        CHECK(Dir::ipc().path().toString() == String("/var/run/promeki-test-override"));

        LibraryOptions::instance().set(LibraryOptions::IpcDir, String());
        Dir def = Dir::ipc();
        CHECK_FALSE(def.path().isEmpty());

        LibraryOptions::instance().set(LibraryOptions::IpcDir, saved);
}

TEST_CASE("Dir: mkdir and remove") {
        Dir      t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_mkdir";
        Dir      d(testDir);

        // Clean up if left over from a previous run
        if (d.exists()) d.removeRecursively();

        Error err = d.mkdir();
        CHECK(err.isOk());
        CHECK(d.exists());
        CHECK(d.isEmpty());

        err = d.remove();
        CHECK(err.isOk());
        CHECK_FALSE(d.exists());
}

TEST_CASE("Dir: mkpath and removeRecursively") {
        Dir      t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_mkpath" / "sub1" / "sub2";
        Dir      d(testDir);

        // Clean up root if left over
        Dir root(t.path() / "promeki_dir_test_mkpath");
        if (root.exists()) root.removeRecursively();

        Error err = d.mkpath();
        CHECK(err.isOk());
        CHECK(d.exists());

        err = root.removeRecursively();
        CHECK(err.isOk());
        CHECK_FALSE(root.exists());
}

TEST_CASE("Dir: entryList") {
        Dir      t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_entrylist";
        Dir      d(testDir);

        if (d.exists()) d.removeRecursively();
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
        Dir      t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_filter";
        Dir      d(testDir);

        if (d.exists()) d.removeRecursively();
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
        Dir      t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_empty";
        Dir      d(testDir);

        if (d.exists()) d.removeRecursively();
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

        Dir   t = Dir::temp();
        Error err = Dir::setCurrent(t.path());
        CHECK(err.isOk());

        Dir now = Dir::current();
        // Resolve symlinks for comparison
        std::error_code ec;
        auto            expected = std::filesystem::canonical(t.path().toStdPath(), ec);
        auto            actual = std::filesystem::canonical(now.path().toStdPath(), ec);
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
        Dir      d(fp);
        CHECK(d.path().toString() == "/tmp");
}

// ============================================================================
// Dir::numberedSequences
// ============================================================================

TEST_CASE("Dir: numberedSequences single sequence") {
        Dir      t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_numseq_single";
        Dir      d(testDir);
        if (d.exists()) d.removeRecursively();
        d.mkdir();

        createTestFile(testDir.toStdPath() / "render.0001.exr", 'a');
        createTestFile(testDir.toStdPath() / "render.0002.exr", 'b');
        createTestFile(testDir.toStdPath() / "render.0003.exr", 'c');
        createTestFile(testDir.toStdPath() / "render.0005.exr", 'e');

        NumNameSeq::List seqs = d.numberedSequences();
        REQUIRE(seqs.size() == 1);
        CHECK(seqs[0].name().prefix() == "render.");
        CHECK(seqs[0].name().suffix() == ".exr");
        CHECK(seqs[0].name().digits() == 4);
        CHECK(seqs[0].name().isPadded());
        CHECK(seqs[0].head() == 1);
        CHECK(seqs[0].tail() == 5);
        CHECK(seqs[0].length() == 5);

        d.removeRecursively();
}

TEST_CASE("Dir: numberedSequences multiple sequences") {
        Dir      t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_numseq_multi";
        Dir      d(testDir);
        if (d.exists()) d.removeRecursively();
        d.mkdir();

        createTestFile(testDir.toStdPath() / "shotA.001.exr", 'a');
        createTestFile(testDir.toStdPath() / "shotA.002.exr", 'b');
        createTestFile(testDir.toStdPath() / "shotA.003.exr", 'c');
        createTestFile(testDir.toStdPath() / "shotB.0100.dpx", 'd');
        createTestFile(testDir.toStdPath() / "shotB.0200.dpx", 'e');
        createTestFile(testDir.toStdPath() / "readme.txt", 'f');

        NumNameSeq::List seqs = d.numberedSequences();
        // Expect 2 image sequences.  "readme.txt" is not a numname.
        CHECK(seqs.size() == 2);

        // Find each sequence by prefix
        int idxA = -1, idxB = -1;
        for (size_t i = 0; i < seqs.size(); i++) {
                if (seqs[i].name().prefix() == "shotA.") idxA = (int)i;
                if (seqs[i].name().prefix() == "shotB.") idxB = (int)i;
        }
        REQUIRE(idxA >= 0);
        REQUIRE(idxB >= 0);

        CHECK(seqs[idxA].head() == 1);
        CHECK(seqs[idxA].tail() == 3);
        CHECK(seqs[idxA].length() == 3);
        CHECK(seqs[idxA].name().digits() == 3);

        CHECK(seqs[idxB].head() == 100);
        CHECK(seqs[idxB].tail() == 200);
        CHECK(seqs[idxB].length() == 101);
        CHECK(seqs[idxB].name().digits() == 4);

        d.removeRecursively();
}

TEST_CASE("Dir: numberedSequences empty directory") {
        Dir      t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_numseq_empty";
        Dir      d(testDir);
        if (d.exists()) d.removeRecursively();
        d.mkdir();

        NumNameSeq::List seqs = d.numberedSequences();
        CHECK(seqs.isEmpty());

        d.removeRecursively();
}

TEST_CASE("Dir: numberedSequences only non-numname files") {
        Dir      t = Dir::temp();
        FilePath testDir = t.path() / "promeki_dir_test_numseq_nonames";
        Dir      d(testDir);
        if (d.exists()) d.removeRecursively();
        d.mkdir();

        createTestFile(testDir.toStdPath() / "readme.txt", 'a');
        createTestFile(testDir.toStdPath() / "notes.md", 'b');

        NumNameSeq::List seqs = d.numberedSequences();
        CHECK(seqs.isEmpty());

        d.removeRecursively();
}

TEST_CASE("Dir: numberedSequences nonexistent directory") {
        Dir              d("/nonexistent_promeki_numseq_path_98765");
        NumNameSeq::List seqs = d.numberedSequences();
        CHECK(seqs.isEmpty());
}
