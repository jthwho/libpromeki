/**
 * @file      fileiodevice.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/core/fileiodevice.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("FileIODevice: default state") {
        FileIODevice dev;
        CHECK_FALSE(dev.isOpen());
        CHECK(dev.file() == nullptr);
        CHECK(dev.filename().isEmpty());
        CHECK_FALSE(dev.ownsFile());
}

TEST_CASE("FileIODevice: open without file or filename fails") {
        FileIODevice dev;
        Error err = dev.open(IODevice::ReadWrite);
        CHECK(err.isError());
        CHECK(err.code() == Error::Invalid);
}

// ============================================================================
// FILE* constructor (external FILE)
// ============================================================================

TEST_CASE("FileIODevice: FILE* constructor is immediately open") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::ReadWrite);
        CHECK(dev.isOpen());
        CHECK(dev.file() == f);
        CHECK_FALSE(dev.ownsFile());
        CHECK(dev.isReadable());
        CHECK(dev.isWritable());
        dev.close();
        std::fclose(f);
}

TEST_CASE("FileIODevice: FILE* constructor ReadOnly") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::ReadOnly);
        CHECK(dev.isOpen());
        CHECK(dev.isReadable());
        CHECK_FALSE(dev.isWritable());
        dev.close();
        std::fclose(f);
}

TEST_CASE("FileIODevice: FILE* constructor WriteOnly") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::WriteOnly);
        CHECK(dev.isOpen());
        CHECK_FALSE(dev.isReadable());
        CHECK(dev.isWritable());
        dev.close();
        std::fclose(f);
}

TEST_CASE("FileIODevice: FILE* constructor double open returns AlreadyOpen") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::ReadWrite);
        Error err = dev.open(IODevice::ReadOnly);
        CHECK(err.code() == Error::AlreadyOpen);
        dev.close();
        std::fclose(f);
}

TEST_CASE("FileIODevice: FILE* constructor non-owned does not fclose") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        {
                FileIODevice dev(f, IODevice::ReadWrite);
                dev.write("hi", 2);
        } // destructor runs — should NOT fclose f
        // Verify the FILE is still usable
        std::rewind(f);
        char buf[2] = {};
        size_t n = std::fread(buf, 1, 2, f);
        CHECK(n == 2);
        CHECK(std::memcmp(buf, "hi", 2) == 0);
        std::fclose(f);
}

TEST_CASE("FileIODevice: FILE* constructor with OwnsFile flag") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::ReadWrite, FileIODevice::OwnsFile);
        CHECK(dev.ownsFile());
        CHECK(dev.isOpen());
        // close() should fclose the FILE
        Error err = dev.close();
        CHECK(err.isOk());
        CHECK(dev.file() == nullptr);
}

// ============================================================================
// Write and read via FILE* constructor
// ============================================================================

TEST_CASE("FileIODevice: write and read back via FILE*") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::ReadWrite);
        const char *data = "Hello FILE";
        int64_t n = dev.write(data, 10);
        CHECK(n == 10);
        std::rewind(f);
        char buf[10] = {};
        n = dev.read(buf, 10);
        CHECK(n == 10);
        CHECK(std::memcmp(buf, "Hello FILE", 10) == 0);
        dev.close();
        std::fclose(f);
}

TEST_CASE("FileIODevice: read on WriteOnly fails") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::WriteOnly);
        char buf[4];
        int64_t n = dev.read(buf, 4);
        CHECK(n == -1);
        dev.close();
        std::fclose(f);
}

TEST_CASE("FileIODevice: write on ReadOnly fails") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::ReadOnly);
        int64_t n = dev.write("hi", 2);
        CHECK(n == -1);
        dev.close();
        std::fclose(f);
}

// ============================================================================
// Filename-based (fopen) construction
// ============================================================================

TEST_CASE("FileIODevice: filename constructor") {
        FileIODevice dev("/tmp/promeki_test_fileiodevice.txt");
        CHECK_FALSE(dev.isOpen());
        CHECK(dev.filename() == "/tmp/promeki_test_fileiodevice.txt");
        CHECK(dev.file() == nullptr);
}

TEST_CASE("FileIODevice: setFilename") {
        FileIODevice dev;
        dev.setFilename("/tmp/promeki_test_fileiodevice2.txt");
        CHECK(dev.filename() == "/tmp/promeki_test_fileiodevice2.txt");
}

TEST_CASE("FileIODevice: open with filename creates FILE") {
        const char *path = "/tmp/promeki_test_fileiodevice_open.txt";
        {
                String fn(path);
                FileIODevice dev(fn);
                Error err = dev.open(IODevice::WriteOnly);
                CHECK(err.isOk());
                CHECK(dev.isOpen());
                CHECK(dev.file() != nullptr);
                CHECK(dev.ownsFile());
                dev.write("test data", 9);
                dev.close();
        }
        // Verify content was written
        {
                String fn(path);
                FileIODevice dev(fn);
                Error err = dev.open(IODevice::ReadOnly);
                CHECK(err.isOk());
                char buf[9] = {};
                int64_t n = dev.read(buf, 9);
                CHECK(n == 9);
                CHECK(std::memcmp(buf, "test data", 9) == 0);
                dev.close();
        }
        std::remove(path);
}

TEST_CASE("FileIODevice: open ReadWrite with filename") {
        const char *path = "/tmp/promeki_test_fileiodevice_rw.txt";
        String fn(path);
        FileIODevice dev(fn);
        Error err = dev.open(IODevice::ReadWrite);
        CHECK(err.isOk());
        CHECK(dev.isReadable());
        CHECK(dev.isWritable());
        dev.write("hello", 5);
        dev.seek(0);
        char buf[5] = {};
        dev.read(buf, 5);
        CHECK(std::memcmp(buf, "hello", 5) == 0);
        dev.close();
        std::remove(path);
}

TEST_CASE("FileIODevice: open with bad filename fails") {
        String badPath("/nonexistent/path/to/file.txt");
        FileIODevice dev(badPath);
        Error err = dev.open(IODevice::ReadOnly);
        CHECK(err.isError());
        CHECK(err.code() == Error::IOError);
        CHECK_FALSE(dev.isOpen());
}

TEST_CASE("FileIODevice: destructor closes owned FILE") {
        const char *path = "/tmp/promeki_test_fileiodevice_dtor.txt";
        {
                String fn(path);
                FileIODevice dev(fn);
                dev.open(IODevice::WriteOnly);
                dev.write("data", 4);
                // destructor closes and fclose's
        }
        // Verify file exists and has content
        FILE *f = std::fopen(path, "rb");
        REQUIRE(f != nullptr);
        char buf[4] = {};
        size_t n = std::fread(buf, 1, 4, f);
        CHECK(n == 4);
        CHECK(std::memcmp(buf, "data", 4) == 0);
        std::fclose(f);
        std::remove(path);
}

// ============================================================================
// takeFile
// ============================================================================

TEST_CASE("FileIODevice: takeFile transfers ownership") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::ReadWrite, FileIODevice::OwnsFile);
        CHECK(dev.ownsFile());

        FILE *taken = dev.takeFile();
        CHECK(taken == f);
        CHECK(dev.file() == nullptr);
        CHECK_FALSE(dev.isOpen());
        CHECK_FALSE(dev.ownsFile());

        // The FILE is still valid — caller owns it now
        std::fwrite("ok", 1, 2, taken);
        std::rewind(taken);
        char buf[2] = {};
        std::fread(buf, 1, 2, taken);
        CHECK(std::memcmp(buf, "ok", 2) == 0);
        std::fclose(taken);
}

TEST_CASE("FileIODevice: takeFile on filename-opened device") {
        const char *path = "/tmp/promeki_test_fileiodevice_take.txt";
        String fn(path);
        FileIODevice dev(fn);
        dev.open(IODevice::WriteOnly);
        dev.write("taken", 5);

        FILE *taken = dev.takeFile();
        CHECK(taken != nullptr);
        CHECK_FALSE(dev.isOpen());
        CHECK(dev.file() == nullptr);

        // The taken FILE was opened "wb" so close it and verify via fopen
        std::fclose(taken);
        FILE *verify = std::fopen(path, "rb");
        REQUIRE(verify != nullptr);
        char buf[5] = {};
        std::fread(buf, 1, 5, verify);
        CHECK(std::memcmp(buf, "taken", 5) == 0);
        std::fclose(verify);
        std::remove(path);
}

TEST_CASE("FileIODevice: takeFile on default-constructed returns nullptr") {
        FileIODevice dev;
        FILE *taken = dev.takeFile();
        CHECK(taken == nullptr);
}

// ============================================================================
// Seek and pos
// ============================================================================

TEST_CASE("FileIODevice: seek and pos") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::ReadWrite);
        dev.write("abcdefghij", 10);
        dev.seek(5);
        CHECK(dev.pos() == 5);
        char buf[5] = {};
        dev.read(buf, 5);
        CHECK(std::memcmp(buf, "fghij", 5) == 0);
        dev.close();
        std::fclose(f);
}

// ============================================================================
// isSequential and atEnd
// ============================================================================

TEST_CASE("FileIODevice: isSequential returns true") {
        FileIODevice dev;
        CHECK(dev.isSequential());
}

TEST_CASE("FileIODevice: atEnd on empty file") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::ReadOnly);
        char buf[1];
        dev.read(buf, 1);
        CHECK(dev.atEnd());
        dev.close();
        std::fclose(f);
}

TEST_CASE("FileIODevice: close when not open returns NotOpen") {
        FileIODevice dev;
        Error err = dev.close();
        CHECK(err.code() == Error::NotOpen);
}

TEST_CASE("FileIODevice: pos when not open returns 0") {
        FileIODevice dev;
        CHECK(dev.pos() == 0);
}

TEST_CASE("FileIODevice: seek when not open returns NotOpen") {
        FileIODevice dev;
        Error err = dev.seek(0);
        CHECK(err.code() == Error::NotOpen);
}

TEST_CASE("FileIODevice: atEnd when not open returns true") {
        FileIODevice dev;
        CHECK(dev.atEnd());
}

// ============================================================================
// Flush
// ============================================================================

TEST_CASE("FileIODevice: flush makes data visible to other readers") {
        const char *path = "/tmp/promeki_test_fileiodevice_flush.txt";
        String fn(path);
        std::remove(path);

        FileIODevice writer(fn);
        Error err = writer.open(IODevice::WriteOnly);
        REQUIRE(err.isOk());

        const char *msg = "flush test data";
        writer.write(msg, 15);

        // Before flush: open a second descriptor and try to read.
        // stdio buffers haven't been flushed so the file may be empty.
        FILE *reader = std::fopen(path, "rb");
        REQUIRE(reader != nullptr);
        char buf[15] = {};
        size_t n = std::fread(buf, 1, 15, reader);
        std::fclose(reader);
        // The kernel page cache might or might not have the data yet,
        // but we haven't flushed, so n could be 0 or 15 depending on
        // the platform.  We don't assert on pre-flush state because
        // it's non-deterministic; we just care that post-flush is
        // always correct.

        // After flush: data must be visible.
        writer.flush();

        reader = std::fopen(path, "rb");
        REQUIRE(reader != nullptr);
        std::memset(buf, 0, 15);
        n = std::fread(buf, 1, 15, reader);
        std::fclose(reader);
        CHECK(n == 15);
        CHECK(std::memcmp(buf, msg, 15) == 0);

        writer.close();
        std::remove(path);
}

TEST_CASE("FileIODevice: flush on closed device is safe") {
        FileIODevice dev;
        dev.flush(); // Should not crash
        CHECK(true);
}

TEST_CASE("FileIODevice: flush on FILE* constructor") {
        FILE *f = std::tmpfile();
        REQUIRE(f != nullptr);
        FileIODevice dev(f, IODevice::ReadWrite);
        dev.write("data", 4);
        dev.flush();
        // Verify the data was flushed by reading via the raw FILE*
        std::rewind(f);
        char buf[4] = {};
        size_t n = std::fread(buf, 1, 4, f);
        CHECK(n == 4);
        CHECK(std::memcmp(buf, "data", 4) == 0);
        dev.close();
        std::fclose(f);
}

// ============================================================================
// Append mode
// ============================================================================

TEST_CASE("FileIODevice: open with Append mode") {
        const char *path = "/tmp/promeki_test_fileiodevice_append.txt";
        std::remove(path);

        // Write initial data
        {
                String fn(path);
                FileIODevice dev(fn);
                Error err = dev.open(IODevice::WriteOnly);
                REQUIRE(err.isOk());
                dev.write("hello", 5);
                dev.close();
        }

        // Append more data
        {
                String fn(path);
                FileIODevice dev(fn);
                Error err = dev.open(IODevice::Append);
                REQUIRE(err.isOk());
                CHECK(dev.isWritable());
                dev.write(" world", 6);
                dev.close();
        }

        // Read back and verify concatenation
        {
                String fn(path);
                FileIODevice dev(fn);
                Error err = dev.open(IODevice::ReadOnly);
                REQUIRE(err.isOk());
                char buf[11] = {};
                int64_t n = dev.read(buf, 11);
                CHECK(n == 11);
                CHECK(std::memcmp(buf, "hello world", 11) == 0);
                dev.close();
        }

        std::remove(path);
}

// ============================================================================
// Misc edge cases
// ============================================================================

TEST_CASE("FileIODevice: stdinDevice returns valid device") {
        FileIODevice *dev = FileIODevice::stdinDevice();
        REQUIRE(dev != nullptr);
        CHECK(dev->isOpen());
        CHECK(dev->isReadable());
        CHECK_FALSE(dev->isWritable());
}

TEST_CASE("FileIODevice: stdoutDevice returns valid device") {
        FileIODevice *dev = FileIODevice::stdoutDevice();
        REQUIRE(dev != nullptr);
        CHECK(dev->isOpen());
        CHECK(dev->isWritable());
        CHECK_FALSE(dev->isReadable());
}

TEST_CASE("FileIODevice: stderrDevice returns valid device") {
        FileIODevice *dev = FileIODevice::stderrDevice();
        REQUIRE(dev != nullptr);
        CHECK(dev->isOpen());
        CHECK(dev->isWritable());
        CHECK_FALSE(dev->isReadable());
}

TEST_CASE("FileIODevice: stdio singletons are stable") {
        CHECK(FileIODevice::stdinDevice() == FileIODevice::stdinDevice());
        CHECK(FileIODevice::stdoutDevice() == FileIODevice::stdoutDevice());
        CHECK(FileIODevice::stderrDevice() == FileIODevice::stderrDevice());
}

TEST_CASE("FileIODevice: open with NotOpen mode returns Invalid") {
        const char *path = "/tmp/promeki_test_fileiodevice_notopen.txt";
        String fn(path);
        FileIODevice dev(fn);
        Error err = dev.open(IODevice::NotOpen);
        CHECK(err.isError());
        CHECK(err.code() == Error::Invalid);
}

TEST_CASE("FileIODevice: read when not open returns -1") {
        FileIODevice dev;
        char buf[4];
        int64_t n = dev.read(buf, 4);
        CHECK(n == -1);
}

TEST_CASE("FileIODevice: write when not open returns -1") {
        FileIODevice dev;
        int64_t n = dev.write("hi", 2);
        CHECK(n == -1);
}
