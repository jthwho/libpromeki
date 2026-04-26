/**
 * @file      asyncbufferqueue.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <cstring>

#include <promeki/asyncbufferqueue.h>
#include <promeki/buffer.h>

using namespace promeki;

namespace {

// Helper: build a Buffer::Ptr containing the given byte sequence.
Buffer::Ptr makeSegment(const char *bytes, size_t n) {
        Buffer::Ptr ptr = Buffer::Ptr::create(n);
        if(n > 0) std::memcpy(ptr.modify()->data(), bytes, n);
        ptr.modify()->setSize(n);
        return ptr;
}

}  // namespace

TEST_CASE("AsyncBufferQueue_OpensReadOnlyAndRejectsWriteOnly") {
        AsyncBufferQueue q;
        CHECK(q.open(IODevice::WriteOnly).isError());
        CHECK(q.open(IODevice::ReadWrite).isError());
        CHECK(q.open(IODevice::ReadOnly).isOk());
        CHECK(q.isOpen());
        CHECK(q.isSequential());
        CHECK_FALSE(q.atEnd());        // writer side still open
        CHECK(q.bytesAvailable() == 0);
}

TEST_CASE("AsyncBufferQueue_EnqueueWakesReaderAndReadDrainsBytes") {
        AsyncBufferQueue q;
        REQUIRE(q.open(IODevice::ReadOnly).isOk());

        std::atomic<int> readyCount{0};
        q.readyReadSignal.connect([&]() { readyCount.fetch_add(1); });

        Error err = q.enqueue(makeSegment("hello", 5));
        CHECK(err.isOk());
        CHECK(readyCount.load() == 1);
        CHECK(q.bytesAvailable() == 5);

        char tmp[8] = {0};
        const int64_t got = q.read(tmp, sizeof(tmp));
        CHECK(got == 5);
        CHECK(std::memcmp(tmp, "hello", 5) == 0);
        CHECK(q.bytesAvailable() == 0);
        CHECK_FALSE(q.atEnd());        // writer side still open
}

TEST_CASE("AsyncBufferQueue_MultiSegmentDrainsInOrder") {
        AsyncBufferQueue q;
        REQUIRE(q.open(IODevice::ReadOnly).isOk());

        REQUIRE(q.enqueue(makeSegment("foo", 3)).isOk());
        REQUIRE(q.enqueue(makeSegment("bar", 3)).isOk());
        REQUIRE(q.enqueue(makeSegment("baz", 3)).isOk());
        CHECK(q.bytesAvailable() == 9);
        CHECK(q.segmentCount() == 3);

        // Read with a too-small buffer first to verify partial-segment
        // accounting (offset advance) actually works.
        char chunk[4] = {0};
        int64_t got = q.read(chunk, 4);
        CHECK(got == 4);
        CHECK(std::memcmp(chunk, "foob", 4) == 0);
        CHECK(q.bytesAvailable() == 5);

        char rest[16] = {0};
        got = q.read(rest, sizeof(rest));
        CHECK(got == 5);
        CHECK(std::memcmp(rest, "arbaz", 5) == 0);
        CHECK(q.bytesAvailable() == 0);
}

TEST_CASE("AsyncBufferQueue_ZeroLengthAndInvalidSegmentsAreIgnored") {
        AsyncBufferQueue q;
        REQUIRE(q.open(IODevice::ReadOnly).isOk());

        std::atomic<int> readyCount{0};
        q.readyReadSignal.connect([&]() { readyCount.fetch_add(1); });

        // Empty pointer.
        CHECK(q.enqueue(Buffer::Ptr()).isOk());
        // Zero-byte buffer.
        CHECK(q.enqueue(makeSegment("", 0)).isOk());
        CHECK(readyCount.load() == 0);
        CHECK(q.bytesAvailable() == 0);
}

TEST_CASE("AsyncBufferQueue_CloseWritingWakesReaderAndFlipsAtEnd") {
        AsyncBufferQueue q;
        REQUIRE(q.open(IODevice::ReadOnly).isOk());

        std::atomic<int> readyCount{0};
        q.readyReadSignal.connect([&]() { readyCount.fetch_add(1); });

        // Empty queue + writer-open: read returns 0 but atEnd is false.
        char tmp[4] = {0};
        CHECK(q.read(tmp, sizeof(tmp)) == 0);
        CHECK_FALSE(q.atEnd());

        q.closeWriting();
        CHECK(readyCount.load() == 1);
        CHECK(q.read(tmp, sizeof(tmp)) == 0);
        CHECK(q.atEnd());

        // Subsequent close is idempotent (no extra wakeups).
        q.closeWriting();
        CHECK(readyCount.load() == 1);
        CHECK(q.isWritingClosed());

        // Enqueue after closeWriting must fail rather than appearing
        // to succeed silently.
        const Error err = q.enqueue(makeSegment("late", 4));
        CHECK(err.isError());
        CHECK(err == Error::NotOpen);
}

TEST_CASE("AsyncBufferQueue_DrainAfterCloseWritingFlipsAtEndOnceQueueEmpty") {
        AsyncBufferQueue q;
        REQUIRE(q.open(IODevice::ReadOnly).isOk());

        REQUIRE(q.enqueue(makeSegment("abc", 3)).isOk());
        REQUIRE(q.enqueue(makeSegment("def", 3)).isOk());
        q.closeWriting();
        CHECK_FALSE(q.atEnd());        // bytes still queued

        char tmp[2] = {0};
        CHECK(q.read(tmp, sizeof(tmp)) == 2);
        CHECK_FALSE(q.atEnd());

        char rest[8] = {0};
        const int64_t got = q.read(rest, sizeof(rest));
        CHECK(got == 4);
        CHECK(q.atEnd());        // queue empty + writer closed → done
        CHECK(q.read(rest, sizeof(rest)) == 0);
}

TEST_CASE("AsyncBufferQueue_WriteIsRefused") {
        AsyncBufferQueue q;
        REQUIRE(q.open(IODevice::ReadOnly).isOk());
        const char buf[] = "nope";
        CHECK(q.write(buf, 4) == -1);
}

TEST_CASE("AsyncBufferQueue_CloseClearsQueue") {
        AsyncBufferQueue q;
        REQUIRE(q.open(IODevice::ReadOnly).isOk());
        REQUIRE(q.enqueue(makeSegment("payload", 7)).isOk());
        CHECK(q.bytesAvailable() == 7);
        CHECK(q.close().isOk());
        CHECK_FALSE(q.isOpen());
        CHECK(q.bytesAvailable() == 0);
}
