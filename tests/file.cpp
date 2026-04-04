/**
 * @file      file.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/file.h>
#include <promeki/result.h>

using namespace promeki;

static const char *testPath(const char *name) {
        static char buf[256];
        std::snprintf(buf, sizeof(buf), "/tmp/promeki_test_%s.tmp", name);
        return buf;
}

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

TEST_CASE("File: construction with FilePath") {
        FilePath fp("/tmp/test_file");
        File f(fp);
        CHECK(f.filename() == "/tmp/test_file");
        CHECK_FALSE(f.isOpen());
}

TEST_CASE("File: open nonexistent file for read fails") {
        File f("/tmp/promeki_test_nonexistent_file_xyz");
        Error err = f.open(IODevice::ReadOnly);
        CHECK(err.isError());
        CHECK_FALSE(f.isOpen());
}

TEST_CASE("File: create, write, read, close") {
        const char *path = testPath("rw");
        std::remove(path);

        {
                File f(path);
                Error err = f.open(IODevice::ReadWrite, File::Create | File::Truncate);
                CHECK(err.isOk());
                CHECK(f.isOpen());
                CHECK(f.isReadable());
                CHECK(f.isWritable());

                const char *data = "Hello, promeki!";
                int64_t written = f.write(data, 15);
                CHECK(written == 15);

                // Seek back to beginning
                Error seekErr = f.seek(0);
                CHECK(seekErr.isOk());

                char buf[16] = {};
                int64_t bytesRead = f.read(buf, 15);
                CHECK(bytesRead == 15);
                CHECK(String(buf) == "Hello, promeki!");

                f.close();
                CHECK_FALSE(f.isOpen());
        }

        std::remove(path);
}

TEST_CASE("File: position tracking") {
        const char *path = testPath("pos");
        std::remove(path);

        File f(path);
        Error err = f.open(IODevice::ReadWrite, File::Create | File::Truncate);
        REQUIRE(err.isOk());

        const char *data = "0123456789";
        f.write(data, 10);

        int64_t p = f.pos();
        CHECK(p == 10);

        CHECK(f.seek(5).isOk());
        CHECK(f.pos() == 5);

        auto [pos1, err1] = f.seekFromCurrent(2);
        CHECK(err1.isOk());
        CHECK(pos1 == 7);

        auto [pos2, err2] = f.seekFromEnd(0);
        CHECK(err2.isOk());
        CHECK(pos2 == 10);

        f.close();
        std::remove(path);
}

TEST_CASE("File: truncate") {
        const char *path = testPath("trunc");
        std::remove(path);

        File f(path);
        Error err = f.open(IODevice::ReadWrite, File::Create | File::Truncate);
        REQUIRE(err.isOk());

        const char *data = "0123456789";
        f.write(data, 10);

        err = f.truncate(5);
        CHECK(err.isOk());

        CHECK(value(f.size()) == 5);

        f.close();
        std::remove(path);
}

TEST_CASE("File: size and atEnd") {
        const char *path = testPath("size");
        std::remove(path);

        File f(path);
        Error err = f.open(IODevice::ReadWrite, File::Create | File::Truncate);
        REQUIRE(err.isOk());

        CHECK(value(f.size()) == 0);
        CHECK(f.atEnd());

        f.write("hello", 5);
        CHECK(value(f.size()) == 5);
        CHECK(f.atEnd());

        f.seek(0);
        CHECK_FALSE(f.atEnd());

        f.close();
        std::remove(path);
}

TEST_CASE("File: isSequential returns false") {
        File f;
        CHECK_FALSE(f.isSequential());
}

TEST_CASE("File: polymorphic use via IODevice pointer") {
        const char *path = testPath("poly");
        std::remove(path);

        File f(path);
        IODevice *dev = &f;

        Error err = f.open(IODevice::ReadWrite, File::Create | File::Truncate);
        REQUIRE(err.isOk());

        // Write via IODevice interface
        const char *data = "polymorphic!";
        int64_t written = dev->write(data, 12);
        CHECK(written == 12);

        // Seek and read via IODevice interface
        CHECK(dev->seek(0).isOk());
        char buf[13] = {};
        int64_t n = dev->read(buf, 12);
        CHECK(n == 12);
        CHECK(std::strcmp(buf, "polymorphic!") == 0);

        dev->close();
        std::remove(path);
}

TEST_CASE("File: aboutToClose signal emitted on close") {
        const char *path = testPath("sig_close");
        std::remove(path);

        File f(path);
        f.open(IODevice::WriteOnly, File::Create | File::Truncate);

        bool signalFired = false;
        f.aboutToCloseSignal.connect([&signalFired]() {
                signalFired = true;
        });

        f.close();
        CHECK(signalFired);

        std::remove(path);
}

TEST_CASE("File: bytesWritten signal emitted on write") {
        const char *path = testPath("sig_write");
        std::remove(path);

        File f(path);
        f.open(IODevice::WriteOnly, File::Create | File::Truncate);

        int64_t reportedBytes = 0;
        f.bytesWrittenSignal.connect([&reportedBytes](int64_t n) {
                reportedBytes = n;
        });

        f.write("test", 4);
        CHECK(reportedBytes == 4);

        f.close();
        std::remove(path);
}

TEST_CASE("File: DirectIO, Synchronous, NonBlocking default to false") {
        File f;
        CHECK_FALSE(f.isDirectIO());
        CHECK_FALSE(f.isSynchronous());
        CHECK_FALSE(f.isNonBlocking());
        CHECK_FALSE(f.isUnbuffered());
}

TEST_CASE("File: NonBlocking toggle on open file") {
        const char *path = testPath("opt_nb");
        std::remove(path);

        File f(path);
        f.open(IODevice::WriteOnly, File::Create | File::Truncate);

        f.setNonBlocking(true);
        CHECK(f.isNonBlocking());

        f.setNonBlocking(false);
        CHECK_FALSE(f.isNonBlocking());

        f.close();
        std::remove(path);
}

TEST_CASE("File: DirectIO forces unbuffered on, restores on disable") {
        File f;

        CHECK_FALSE(f.isUnbuffered());

        // Enable DirectIO — unbuffered should become true
        f.setDirectIO(true);
        CHECK(f.isUnbuffered());

        // Disable DirectIO — unbuffered should restore to false
        f.setDirectIO(false);
        CHECK_FALSE(f.isUnbuffered());
}

TEST_CASE("File: DirectIO save/restore preserves user-set unbuffered") {
        File f;

        // User explicitly sets unbuffered to true
        f.setUnbuffered(true);

        // Enable DirectIO — unbuffered is already true, saved state = true
        f.setDirectIO(true);
        CHECK(f.isUnbuffered());

        // Disable DirectIO — unbuffered should restore to true (not false)
        f.setDirectIO(false);
        CHECK(f.isUnbuffered());
}

TEST_CASE("File: buffered read works correctly") {
        const char *path = testPath("bufread");
        std::remove(path);

        // Write data with a raw File
        {
                File writer(path);
                writer.open(IODevice::WriteOnly, File::Create | File::Truncate);
                const char *data = "line1\nline2\nline3\n";
                writer.write(data, static_cast<int64_t>(std::strlen(data)));
                writer.close();
        }

        // Read it back using buffered read methods
        {
                File reader(path);
                reader.open(IODevice::ReadOnly);

                Buffer line1 = reader.readLine();
                CHECK(line1.isValid());
                CHECK(line1.size() == 6);
                CHECK(std::memcmp(line1.data(), "line1\n", 6) == 0);

                Buffer line2 = reader.readLine();
                CHECK(line2.isValid());
                CHECK(line2.size() == 6);
                CHECK(std::memcmp(line2.data(), "line2\n", 6) == 0);

                Buffer line3 = reader.readLine();
                CHECK(line3.isValid());
                CHECK(line3.size() == 6);
                CHECK(std::memcmp(line3.data(), "line3\n", 6) == 0);

                reader.close();
        }

        std::remove(path);
}

TEST_CASE("File: readAll") {
        const char *path = testPath("readall");
        std::remove(path);

        {
                File writer(path);
                writer.open(IODevice::WriteOnly, File::Create | File::Truncate);
                writer.write("all the data", 12);
                writer.close();
        }

        {
                File reader(path);
                reader.open(IODevice::ReadOnly);

                Buffer all = reader.readAll();
                CHECK(all.isValid());
                CHECK(all.size() == 12);
                CHECK(std::memcmp(all.data(), "all the data", 12) == 0);

                reader.close();
        }

        std::remove(path);
}

TEST_CASE("File: open already-open file returns error") {
        const char *path = testPath("dblopen");
        std::remove(path);

        File f(path);
        Error err = f.open(IODevice::WriteOnly, File::Create | File::Truncate);
        REQUIRE(err.isOk());

        err = f.open(IODevice::ReadOnly);
        CHECK(err.isError());
        CHECK(err.code() == Error::AlreadyOpen);

        f.close();
        std::remove(path);
}

TEST_CASE("File: close on non-open file is safe") {
        File f;
        f.close(); // Should not crash or error
        CHECK_FALSE(f.isOpen());
}

TEST_CASE("File: read on write-only returns -1") {
        const char *path = testPath("wo_read");
        std::remove(path);

        File f(path);
        f.open(IODevice::WriteOnly, File::Create | File::Truncate);

        char buf[4];
        int64_t n = f.read(buf, 4);
        CHECK(n == -1);

        f.close();
        std::remove(path);
}

TEST_CASE("File: write on read-only returns -1") {
        const char *path = testPath("ro_write");
        std::remove(path);

        // Create the file first
        {
                File f(path);
                f.open(IODevice::WriteOnly, File::Create | File::Truncate);
                f.write("data", 4);
                f.close();
        }

        File f(path);
        f.open(IODevice::ReadOnly);

        int64_t n = f.write("x", 1);
        CHECK(n == -1);

        f.close();
        std::remove(path);
}

TEST_CASE("File: setFilename") {
        File f;
        f.setFilename("/tmp/new_name");
        CHECK(f.filename() == "/tmp/new_name");
}

TEST_CASE("File: pos/seek/size on closed file") {
        File f;
        CHECK(f.pos() == 0);
        CHECK(value(f.size()) == 0);
        CHECK(f.atEnd());
        CHECK(f.seek(0).isError());
        CHECK(isError(f.seekFromCurrent(0)));
        CHECK(isError(f.seekFromEnd(0)));
}

TEST_CASE("File: truncate on closed file returns error") {
        File f;
        Error err = f.truncate(0);
        CHECK(err.isError());
}

// ---------------------------------------------------------------
// Helper: write a file filled with a known byte pattern.
// Byte at offset i = (i & 0xFF).
// ---------------------------------------------------------------
static void writePatternFile(const char *path, size_t totalSize) {
        File f(path);
        Error err = f.open(IODevice::WriteOnly, File::Create | File::Truncate);
        REQUIRE(err.isOk());
        // Write in 4K chunks
        uint8_t chunk[4096];
        size_t written = 0;
        while(written < totalSize) {
                size_t chunkSz = std::min(sizeof(chunk), totalSize - written);
                for(size_t i = 0; i < chunkSz; i++) {
                        chunk[i] = static_cast<uint8_t>((written + i) & 0xFF);
                }
                int64_t n = f.write(chunk, static_cast<int64_t>(chunkSz));
                REQUIRE(n == static_cast<int64_t>(chunkSz));
                written += chunkSz;
        }
        f.close();
}

// Verify that `data` of length `len` starting at logical file offset `startOffset`
// matches the pattern: byte at file offset i = (i & 0xFF).
static bool verifyPattern(const void *data, size_t len, size_t startOffset) {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        for(size_t i = 0; i < len; i++) {
                if(p[i] != static_cast<uint8_t>((startOffset + i) & 0xFF)) return false;
        }
        return true;
}

TEST_CASE("File: seek invalidates read buffer") {
        const char *path = testPath("seek_inv");
        std::remove(path);

        // Write "AAAABBBB" (8 bytes)
        {
                File f(path);
                f.open(IODevice::WriteOnly, File::Create | File::Truncate);
                f.write("AAAABBBB", 8);
                f.close();
        }

        File f(path);
        f.open(IODevice::ReadOnly);

        // First read fills the buffer and returns "AAAA"
        char buf[4] = {};
        int64_t n = f.read(buf, 4);
        CHECK(n == 4);
        CHECK(std::memcmp(buf, "AAAA", 4) == 0);

        // Seek back to 0 — buffer must be invalidated
        f.seek(0);

        // Read again — should get "AAAA" not "BBBB"
        char buf2[4] = {};
        n = f.read(buf2, 4);
        CHECK(n == 4);
        CHECK(std::memcmp(buf2, "AAAA", 4) == 0);

        f.close();
        std::remove(path);
}

TEST_CASE("File: directIOAlignment returns non-zero for open file") {
        const char *path = testPath("dio_align");
        std::remove(path);

        File f(path);
        f.open(IODevice::ReadWrite, File::Create | File::Truncate);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        CHECK(alignErr.isOk());
        CHECK(align > 0);
        // Should be a power of two
        CHECK((align & (align - 1)) == 0);

        f.close();
        std::remove(path);
}

TEST_CASE("File: directIOAlignment returns error for closed file") {
        File f;
        CHECK(isError(f.directIOAlignment()));
}

TEST_CASE("File: readBulk aligned position and size") {
        const char *path = testPath("bulk_aligned");
        std::remove(path);

        const size_t fileSize = 128 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Seek to an aligned position, read aligned size
        int64_t offset = static_cast<int64_t>(align);
        int64_t readSize = static_cast<int64_t>(align * 3);
        f.seek(offset);

        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        CHECK(buf.size() == 0);
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(buf.size() == static_cast<size_t>(readSize));
        CHECK(verifyPattern(buf.data(), static_cast<size_t>(readSize),
                            static_cast<size_t>(offset)));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk unaligned position") {
        const char *path = testPath("bulk_uoff");
        std::remove(path);

        const size_t fileSize = 128 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Position is 7 bytes into the first block, size spans multiple blocks
        int64_t offset = 7;
        int64_t readSize = static_cast<int64_t>(align * 4);
        f.seek(offset);

        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(verifyPattern(buf.data(), static_cast<size_t>(readSize),
                            static_cast<size_t>(offset)));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk unaligned size") {
        const char *path = testPath("bulk_usz");
        std::remove(path);

        const size_t fileSize = 128 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Aligned position, but size is not a multiple of alignment
        int64_t offset = static_cast<int64_t>(align * 2);
        int64_t readSize = static_cast<int64_t>(align * 3 + 13);
        f.seek(offset);

        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(verifyPattern(buf.data(), static_cast<size_t>(readSize),
                            static_cast<size_t>(offset)));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk unaligned position and size") {
        const char *path = testPath("bulk_uboth");
        std::remove(path);

        const size_t fileSize = 128 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Both position and end are misaligned
        int64_t offset = 37;
        int64_t readSize = static_cast<int64_t>(align * 5 + 99);
        f.seek(offset);

        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(verifyPattern(buf.data(), static_cast<size_t>(readSize),
                            static_cast<size_t>(offset)));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk region smaller than alignment (no DIO portion)") {
        const char *path = testPath("bulk_small");
        std::remove(path);

        const size_t fileSize = 128 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Read a region that fits entirely within one alignment block
        int64_t offset = static_cast<int64_t>(align / 2);
        int64_t readSize = static_cast<int64_t>(align / 4);
        f.seek(offset);

        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(verifyPattern(buf.data(), static_cast<size_t>(readSize),
                            static_cast<size_t>(offset)));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk on closed file returns NotOpen") {
        File f;
        Buffer buf(1024);
        Error err = f.readBulk(buf, 1024);
        CHECK(err.isError());
        CHECK(err.code() == Error::NotOpen);
}

TEST_CASE("File: readBulk with buffer too small returns BufferTooSmall") {
        const char *path = testPath("bulk_toosmall");
        std::remove(path);

        const size_t fileSize = 64 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Seek to an unaligned position so shift is needed,
        // then allocate a buffer that doesn't account for the shift
        f.seek(7);
        Buffer buf(100); // too small: needs 100 + shift
        Error err = f.readBulk(buf, 100);
        CHECK(err.isError());
        CHECK(err.code() == Error::BufferTooSmall);

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk with non-host-accessible buffer returns error") {
        // An invalid (default-constructed) buffer is not host-accessible
        const char *path = testPath("bulk_nha");
        std::remove(path);

        const size_t fileSize = 64 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        Buffer buf; // invalid buffer
        Error err = f.readBulk(buf, 100);
        CHECK(err.isError());

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk large media-like read") {
        const char *path = testPath("bulk_media");
        std::remove(path);

        // Simulate a container file: 4K header, then a 1MB "image" payload
        // at an unaligned offset, then a small footer.
        const size_t headerSize = 4096;
        const size_t imageSize = 1024 * 1024;
        const size_t footerSize = 128;
        const size_t fileSize = headerSize + imageSize + footerSize;

        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Simulate: we parsed the header normally and now want the image payload
        // at an unaligned offset (e.g. header ends mid-block).
        int64_t payloadOffset = 4000; // Not aligned on typical 4K block
        int64_t payloadSize = static_cast<int64_t>(imageSize);
        f.seek(payloadOffset);

        Buffer payload(static_cast<size_t>(payloadSize) + align, align);
        REQUIRE(payload.isValid());
        Error err = f.readBulk(payload, payloadSize);
        CHECK(err.isOk());
        CHECK(verifyPattern(payload.data(), static_cast<size_t>(payloadSize),
                            static_cast<size_t>(payloadOffset)));

        f.close();
        std::remove(path);
}

TEST_CASE("File: manual DIO workflow (step-by-step)") {
        const char *path = testPath("manual_dio");
        std::remove(path);

        // Write a file with known pattern
        const size_t fileSize = 128 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Scenario: "image data" starts at an unaligned offset
        int64_t dataOffset = 100;
        int64_t dataSize = static_cast<int64_t>(align * 10 + 200);

        // Step 1: Calculate alignment boundaries
        int64_t A = static_cast<int64_t>(align);
        int64_t alignedStart = (dataOffset + A - 1) & ~(A - 1);
        int64_t alignedEnd = (dataOffset + dataSize) & ~(A - 1);
        size_t headBytes = static_cast<size_t>(alignedStart - dataOffset);
        size_t tailBytes = static_cast<size_t>((dataOffset + dataSize) - alignedEnd);
        int64_t dioBytes = alignedEnd - alignedStart;
        REQUIRE(dioBytes > 0);

        // Step 2: Allocate buffer with DIO alignment, shift so DIO
        // destination is aligned.  shift = dataOffset % align.
        size_t shift = static_cast<size_t>(dataOffset % A);
        Buffer buf(shift + static_cast<size_t>(dataSize), align);
        REQUIRE(buf.isValid());
        buf.shiftData(shift);
        uint8_t *dest = static_cast<uint8_t *>(buf.data());

        // Step 3: Read unaligned head with normal I/O
        if(headBytes > 0) {
                f.seek(dataOffset);
                int64_t n = f.read(dest, static_cast<int64_t>(headBytes));
                CHECK(n == static_cast<int64_t>(headBytes));
        }

        // Step 4: Enable DIO, read aligned middle
        f.setDirectIO(true);
        CHECK(f.isDirectIO());
        CHECK(f.isUnbuffered());
        f.seek(alignedStart);

        // Verify the DIO destination pointer is aligned
        uintptr_t dioPtr = reinterpret_cast<uintptr_t>(dest + headBytes);
        CHECK((dioPtr % align) == 0);

        // Read aligned data unbuffered (DIO mode forces unbuffered)
        int64_t n = f.read(dest + headBytes, dioBytes);
        CHECK(n == dioBytes);

        // Step 5: Disable DIO, read tail
        f.setDirectIO(false);
        CHECK_FALSE(f.isDirectIO());

        if(tailBytes > 0) {
                f.seek(alignedEnd);
                n = f.read(dest + headBytes + static_cast<size_t>(dioBytes),
                           static_cast<int64_t>(tailBytes));
                CHECK(n == static_cast<int64_t>(tailBytes));
        }

        // Step 6: Verify the entire payload
        CHECK(verifyPattern(dest, static_cast<size_t>(dataSize),
                            static_cast<size_t>(dataOffset)));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk at file start (position 0, aligned)") {
        const char *path = testPath("bulk_zero");
        std::remove(path);

        const size_t fileSize = 64 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Position is already 0 (aligned), read aligned size
        int64_t readSize = static_cast<int64_t>(align * 8);
        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(verifyPattern(buf.data(), static_cast<size_t>(readSize), 0));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk exact single block") {
        const char *path = testPath("bulk_1blk");
        std::remove(path);

        const size_t fileSize = 64 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Read exactly one aligned block from an aligned position
        f.seek(static_cast<int64_t>(align));
        int64_t readSize = static_cast<int64_t>(align);
        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(verifyPattern(buf.data(), static_cast<size_t>(readSize), align));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk past EOF returns short buffer (aligned start)") {
        const char *path = testPath("bulk_eof1");
        std::remove(path);

        // Write a file smaller than what we'll request
        const size_t fileSize = 5000;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Start at position 0 (aligned), request more than what's left
        int64_t readSize = 8192;
        REQUIRE(readSize > static_cast<int64_t>(fileSize));

        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(buf.size() == fileSize);
        CHECK(verifyPattern(buf.data(), buf.size(), 0));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk past EOF returns short buffer (unaligned start)") {
        const char *path = testPath("bulk_eof2");
        std::remove(path);

        const size_t fileSize = 5000;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Start at unaligned position, request more than remains
        int64_t offset = 37;
        f.seek(offset);
        int64_t readSize = static_cast<int64_t>(fileSize); // more than fileSize - offset
        REQUIRE(offset + readSize > static_cast<int64_t>(fileSize));

        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(buf.size() == fileSize - static_cast<size_t>(offset));
        CHECK(verifyPattern(buf.data(), buf.size(), static_cast<size_t>(offset)));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk past EOF with sub-block region") {
        const char *path = testPath("bulk_eof3");
        std::remove(path);

        // Write a small file that fits within one alignment block
        const size_t fileSize = 100;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);
        REQUIRE(fileSize < align); // must fit in one block

        // Request more than the entire file
        int64_t readSize = static_cast<int64_t>(align * 2);
        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(buf.size() == fileSize);
        CHECK(verifyPattern(buf.data(), buf.size(), 0));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk exactly to EOF") {
        const char *path = testPath("bulk_exact_eof");
        std::remove(path);

        const size_t fileSize = 10000;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Read exactly the remaining bytes from an offset
        int64_t offset = 1234;
        f.seek(offset);
        int64_t readSize = static_cast<int64_t>(fileSize) - offset;

        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(buf.size() == static_cast<size_t>(readSize));
        CHECK(verifyPattern(buf.data(), buf.size(), static_cast<size_t>(offset)));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk past EOF — EOF in DIO middle portion") {
        const char *path = testPath("bulk_eof_mid");
        std::remove(path);

        // Create a file where EOF falls in the middle of the DIO-aligned region.
        // File is align*2 + 100 bytes.  Read from position 0 requesting align*4,
        // so EOF hits during the DIO portion.
        File tmp(path);
        tmp.open(IODevice::WriteOnly, File::Create | File::Truncate);
        tmp.close();

        // Re-open to get actual alignment
        tmp.setFilename(path);
        tmp.open(IODevice::ReadWrite, File::Create | File::Truncate);
        auto [align, alignErr] = tmp.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);
        tmp.close();

        const size_t fileSize = align * 2 + 100;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        int64_t readSize = static_cast<int64_t>(align * 4);
        REQUIRE(readSize > static_cast<int64_t>(fileSize));

        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());
        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(buf.size() == fileSize);
        CHECK(verifyPattern(buf.data(), buf.size(), 0));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk DIO fallback on tmpfs") {
        // /tmp is typically tmpfs on Linux, which does not support O_DIRECT.
        // readBulk should succeed by falling back to normal I/O when the
        // DIO portion fails.
        const char *path = testPath("bulk_dio_fb");
        std::remove(path);

        const size_t fileSize = 128 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Read a region that spans multiple alignment blocks so there's a DIO portion
        int64_t readSize = static_cast<int64_t>(align * 4);
        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());

        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(buf.size() == static_cast<size_t>(readSize));
        CHECK(verifyPattern(buf.data(), static_cast<size_t>(readSize), 0));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk DIO fallback with unaligned position on tmpfs") {
        const char *path = testPath("bulk_dio_fb2");
        std::remove(path);

        const size_t fileSize = 128 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Unaligned position, large read that will have head + DIO + tail
        int64_t offset = 777;
        f.seek(offset);
        int64_t readSize = static_cast<int64_t>(align * 6 + 333);

        Buffer buf(static_cast<size_t>(readSize) + align, align);
        REQUIRE(buf.isValid());

        Error err = f.readBulk(buf, readSize);
        CHECK(err.isOk());
        CHECK(buf.size() == static_cast<size_t>(readSize));
        CHECK(verifyPattern(buf.data(), static_cast<size_t>(readSize),
                            static_cast<size_t>(offset)));

        f.close();
        std::remove(path);
}

TEST_CASE("File: readBulk with invalid size returns error") {
        const char *path = testPath("bulk_badsize");
        std::remove(path);

        const size_t fileSize = 64 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        Buffer buf(1024);
        Error err = f.readBulk(buf, 0);
        CHECK(err.isError());
        CHECK(err.code() == Error::InvalidArgument);

        err = f.readBulk(buf, -1);
        CHECK(err.isError());
        CHECK(err.code() == Error::InvalidArgument);

        f.close();
        std::remove(path);
}

TEST_CASE("File: pos() accounts for buffered read-ahead") {
        const char *path = testPath("pos_buf");
        std::remove(path);

        // Write 16K of pattern data (larger than the 8K default read buffer)
        const size_t fileSize = 16384;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());
        CHECK(f.pos() == 0);

        // Read 10 bytes — the buffer will read-ahead up to 8192 bytes from
        // the device, but pos() must report the logical position (10).
        char buf[10];
        int64_t n = f.read(buf, 10);
        REQUIRE(n == 10);
        CHECK(f.pos() == 10);
        CHECK(verifyPattern(buf, 10, 0));

        // Read 100 more bytes — pos() should advance to 110
        char buf2[100];
        n = f.read(buf2, 100);
        REQUIRE(n == 100);
        CHECK(f.pos() == 110);
        CHECK(verifyPattern(buf2, 100, 10));

        // Seek to a new position and read — pos() must be correct after seek too
        f.seek(5000);
        CHECK(f.pos() == 5000);

        char buf3[50];
        n = f.read(buf3, 50);
        REQUIRE(n == 50);
        CHECK(f.pos() == 5050);
        CHECK(verifyPattern(buf3, 50, 5000));

        // Read across the buffer boundary (read more than remaining buffered data)
        // to ensure pos() stays correct when the buffer is refilled
        f.seek(0);
        char bigbuf[12000];
        n = f.read(bigbuf, 12000);
        REQUIRE(n == 12000);
        CHECK(f.pos() == 12000);
        CHECK(verifyPattern(bigbuf, 12000, 0));

        f.close();
        std::remove(path);
}

TEST_CASE("File: construction with const char*") {
        const char *path = "/tmp/test_cstr_file";
        File f(path);
        CHECK(f.filename() == "/tmp/test_cstr_file");
        CHECK_FALSE(f.isOpen());
}

TEST_CASE("File: handle returns valid fd when open") {
        const char *path = testPath("handle");
        std::remove(path);

        File f(path);
        CHECK(f.handle() == File::FileHandleClosedValue);

        f.open(IODevice::WriteOnly, File::Create | File::Truncate);
        REQUIRE(f.isOpen());
        CHECK(f.handle() != File::FileHandleClosedValue);

        f.close();
        CHECK(f.handle() == File::FileHandleClosedValue);
        std::remove(path);
}

TEST_CASE("File: Synchronous toggle on open file") {
        const char *path = testPath("opt_sync");
        std::remove(path);

        File f(path);
        f.open(IODevice::WriteOnly, File::Create | File::Truncate);

        Error err = f.setSynchronous(true);
        CHECK(err.isOk());
        CHECK(f.isSynchronous());

        err = f.setSynchronous(false);
        CHECK(err.isOk());
        CHECK_FALSE(f.isSynchronous());

        f.close();
        std::remove(path);
}

TEST_CASE("File: Append flag writes to end") {
        const char *path = testPath("append");
        std::remove(path);

        // Create file with initial content
        {
                File f(path);
                f.open(IODevice::WriteOnly, File::Create | File::Truncate);
                f.write("hello", 5);
                f.close();
        }

        // Reopen in append mode and write more
        {
                File f(path);
                Error err = f.open(IODevice::WriteOnly, File::Append);
                CHECK(err.isOk());
                f.write("world", 5);
                f.close();
        }

        // Verify combined content
        {
                File f(path);
                f.open(IODevice::ReadOnly);
                Buffer all = f.readAll();
                CHECK(all.size() == 10);
                CHECK(std::memcmp(all.data(), "helloworld", 10) == 0);
                f.close();
        }

        std::remove(path);
}

TEST_CASE("File: Exclusive flag fails if file exists") {
        const char *path = testPath("excl");
        std::remove(path);

        // Create the file first
        {
                File f(path);
                f.open(IODevice::WriteOnly, File::Create | File::Truncate);
                f.write("x", 1);
                f.close();
        }

        // Try to create with Exclusive — should fail since file exists
        {
                File f(path);
                Error err = f.open(IODevice::WriteOnly, File::Create | File::Exclusive);
                CHECK(err.isError());
                CHECK(err.code() == Error::Exists);
                CHECK_FALSE(f.isOpen());
        }

        std::remove(path);
}

TEST_CASE("File: Exclusive flag succeeds if file does not exist") {
        const char *path = testPath("excl_new");
        std::remove(path);

        File f(path);
        Error err = f.open(IODevice::WriteOnly, File::Create | File::Exclusive);
        CHECK(err.isOk());
        CHECK(f.isOpen());

        f.close();
        std::remove(path);
}

TEST_CASE("File: DIO toggle during reads preserves data") {
        const char *path = testPath("dio_toggle");
        std::remove(path);

        const size_t fileSize = 64 * 1024;
        writePatternFile(path, fileSize);

        File f(path);
        f.open(IODevice::ReadOnly);
        REQUIRE(f.isOpen());

        auto [align, alignErr] = f.directIOAlignment();
        REQUIRE(alignErr.isOk());
        REQUIRE(align > 0);

        // Read first 100 bytes normally (buffered)
        f.seek(0);
        char header[100] = {};
        int64_t n = f.read(header, 100);
        CHECK(n == 100);
        CHECK(verifyPattern(header, 100, 0));

        // Toggle DIO on, read an aligned block
        f.setDirectIO(true);
        f.seek(static_cast<int64_t>(align));
        Buffer dioBuf(align, align);
        REQUIRE(dioBuf.isValid());
        n = f.read(dioBuf.data(), static_cast<int64_t>(align));
        CHECK(n == static_cast<int64_t>(align));
        CHECK(verifyPattern(dioBuf.data(), align, align));

        // Toggle DIO off, read more normally
        f.setDirectIO(false);
        f.seek(static_cast<int64_t>(align * 2));
        char trailer[50] = {};
        n = f.read(trailer, 50);
        CHECK(n == 50);
        CHECK(verifyPattern(trailer, 50, align * 2));

        f.close();
        std::remove(path);
}
