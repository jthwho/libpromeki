/**
 * @file      bufferediodevice.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <algorithm>
#include <vector>
#include <doctest/doctest.h>
#include <promeki/core/bufferediodevice.h>

using namespace promeki;

/**
 * @brief In-memory BufferedIODevice implementation for testing.
 */
class BufferedMemoryDevice : public BufferedIODevice {
        PROMEKI_OBJECT(BufferedMemoryDevice, BufferedIODevice)
        public:
                BufferedMemoryDevice(ObjectBase *parent = nullptr) :
                        BufferedIODevice(parent) { }

                Error open(OpenMode mode) override {
                        if(isOpen()) return Error(Error::AlreadyOpen);
                        setOpenMode(mode);
                        _pos = 0;
                        ensureReadBuffer();
                        return Error();
                }

                void close() override {
                        aboutToCloseSignal.emit();
                        setOpenMode(NotOpen);
                        _pos = 0;
                        resetReadBuffer();
                        return;
                }

                bool isOpen() const override {
                        return openMode() != NotOpen;
                }

                int64_t write(const void *data, int64_t maxSize) override {
                        if(!isOpen() || !isWritable()) return -1;
                        const uint8_t *src = static_cast<const uint8_t *>(data);
                        int64_t endPos = _pos + maxSize;
                        if(endPos > static_cast<int64_t>(_storage.size())) {
                                _storage.resize(static_cast<size_t>(endPos));
                        }
                        std::memcpy(_storage.data() + _pos, src, static_cast<size_t>(maxSize));
                        _pos += maxSize;
                        bytesWrittenSignal.emit(maxSize);
                        return maxSize;
                }

                void setData(const void *data, size_t sz) {
                        _storage.resize(sz);
                        std::memcpy(_storage.data(), data, sz);
                        return;
                }

                const std::vector<uint8_t> &storage() const { return _storage; }

        protected:
                int64_t readFromDevice(void *data, int64_t maxSize) override {
                        int64_t avail = static_cast<int64_t>(_storage.size()) - _pos;
                        if(avail <= 0) return 0;
                        int64_t toRead = std::min(maxSize, avail);
                        std::memcpy(data, _storage.data() + _pos, static_cast<size_t>(toRead));
                        _pos += toRead;
                        return toRead;
                }

                int64_t deviceBytesAvailable() const override {
                        int64_t avail = static_cast<int64_t>(_storage.size()) - _pos;
                        return avail > 0 ? avail : 0;
                }

        private:
                std::vector<uint8_t> _storage;
                int64_t _pos = 0;
};

TEST_CASE("BufferedIODevice: default state") {
        BufferedMemoryDevice dev;
        CHECK_FALSE(dev.isOpen());
        CHECK(dev.readBufferSize() == 0);
}

TEST_CASE("BufferedIODevice: buffered read") {
        BufferedMemoryDevice dev;
        const char *data = "Hello, BufferedIODevice!";
        dev.setData(data, 24);
        dev.open(IODevice::ReadOnly);

        char buf[25] = {};
        int64_t n = dev.read(buf, 24);
        CHECK(n == 24);
        CHECK(std::strcmp(buf, "Hello, BufferedIODevice!") == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: readLine with newline") {
        BufferedMemoryDevice dev;
        const char *data = "line1\nline2\nline3\n";
        dev.setData(data, std::strlen(data));
        dev.open(IODevice::ReadOnly);

        Buffer line1 = dev.readLine();
        CHECK(line1.isValid());
        CHECK(line1.size() == 6);
        CHECK(std::memcmp(line1.data(), "line1\n", 6) == 0);

        Buffer line2 = dev.readLine();
        CHECK(line2.isValid());
        CHECK(line2.size() == 6);
        CHECK(std::memcmp(line2.data(), "line2\n", 6) == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: readLine without trailing newline") {
        BufferedMemoryDevice dev;
        const char *data = "no newline";
        dev.setData(data, std::strlen(data));
        dev.open(IODevice::ReadOnly);

        Buffer line = dev.readLine();
        CHECK(line.isValid());
        CHECK(line.size() == 10);
        CHECK(std::memcmp(line.data(), "no newline", 10) == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: readLine maxLength") {
        BufferedMemoryDevice dev;
        const char *data = "a very long line\n";
        dev.setData(data, std::strlen(data));
        dev.open(IODevice::ReadOnly);

        Buffer line = dev.readLine(5);
        CHECK(line.isValid());
        CHECK(line.size() == 5);
        CHECK(std::memcmp(line.data(), "a ver", 5) == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: readAll") {
        BufferedMemoryDevice dev;
        const char *data = "all the data here";
        dev.setData(data, std::strlen(data));
        dev.open(IODevice::ReadOnly);

        Buffer all = dev.readAll();
        CHECK(all.isValid());
        CHECK(all.size() == 17);
        CHECK(std::memcmp(all.data(), "all the data here", 17) == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: readBytes") {
        BufferedMemoryDevice dev;
        const char *data = "0123456789";
        dev.setData(data, 10);
        dev.open(IODevice::ReadOnly);

        Buffer result = dev.readBytes(5);
        CHECK(result.isValid());
        CHECK(result.size() == 5);
        CHECK(std::memcmp(result.data(), "01234", 5) == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: canReadLine") {
        BufferedMemoryDevice dev;
        const char *data = "hello\nworld";
        dev.setData(data, std::strlen(data));
        dev.open(IODevice::ReadOnly);

        // Force data into buffer by reading a small amount
        char tmp[1];
        dev.read(tmp, 0);

        // Need to trigger a buffer fill first
        CHECK_FALSE(dev.canReadLine()); // buffer empty initially

        // Read one byte to trigger fill
        dev.read(tmp, 1);
        CHECK(dev.canReadLine());

        dev.close();
}

TEST_CASE("BufferedIODevice: peek void* overload") {
        BufferedMemoryDevice dev;
        const char *data = "peek test";
        dev.setData(data, 9);
        dev.open(IODevice::ReadOnly);

        // Trigger buffer fill
        char tmp[1];
        dev.read(tmp, 1);
        CHECK(tmp[0] == 'p');

        // Peek should not consume data
        char peekBuf[4] = {};
        int64_t n = dev.peek(peekBuf, 4);
        CHECK(n == 4);
        CHECK(std::memcmp(peekBuf, "eek ", 4) == 0);

        // Read same data again - should still be there
        char readBuf[4] = {};
        dev.read(readBuf, 4);
        CHECK(std::memcmp(readBuf, "eek ", 4) == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: peek Buffer overload") {
        BufferedMemoryDevice dev;
        const char *data = "buffer peek";
        dev.setData(data, 11);
        dev.open(IODevice::ReadOnly);

        // Trigger buffer fill
        char tmp[1];
        dev.read(tmp, 1);

        Buffer peeked = dev.peek(5);
        CHECK(peeked.isValid());
        CHECK(peeked.size() == 5);
        CHECK(std::memcmp(peeked.data(), "uffer", 5) == 0);

        // Data not consumed
        char readBuf[5] = {};
        dev.read(readBuf, 5);
        CHECK(std::memcmp(readBuf, "uffer", 5) == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: bytesAvailable includes buffered data") {
        BufferedMemoryDevice dev;
        const char *data = "bytesavail";
        dev.setData(data, 10);
        dev.open(IODevice::ReadOnly);

        // Before any read, bytesAvailable = deviceBytesAvailable
        CHECK(dev.bytesAvailable() == 10);

        // Read 3 bytes (triggers buffer fill)
        char tmp[3];
        dev.read(tmp, 3);

        // bytesAvailable should reflect remaining buffered + device
        CHECK(dev.bytesAvailable() == 7);

        dev.close();
}

TEST_CASE("BufferedIODevice: large read bypasses buffer") {
        BufferedMemoryDevice dev;
        // Create data larger than the default buffer (8192)
        std::vector<uint8_t> bigData(16384);
        for(size_t i = 0; i < bigData.size(); i++) {
                bigData[i] = static_cast<uint8_t>(i & 0xFF);
        }
        dev.setData(bigData.data(), bigData.size());
        dev.open(IODevice::ReadOnly);

        std::vector<uint8_t> readBuf(16384);
        int64_t n = dev.read(readBuf.data(), 16384);
        CHECK(n == 16384);
        CHECK(std::memcmp(readBuf.data(), bigData.data(), 16384) == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: setReadBuffer before open succeeds") {
        BufferedMemoryDevice dev;
        Buffer customBuf(4096);
        CHECK(customBuf.isHostAccessible());
        Error err = dev.setReadBuffer(std::move(customBuf));
        CHECK(err.isOk());
        CHECK(dev.readBufferSize() == 4096);
}

TEST_CASE("BufferedIODevice: setReadBuffer with secure memory succeeds") {
        BufferedMemoryDevice dev;
        Buffer secureBuf(4096, Buffer::DefaultAlign, MemSpace::SystemSecure);
        CHECK(secureBuf.isHostAccessible());
        Error err = dev.setReadBuffer(std::move(secureBuf));
        CHECK(err.isOk());
        CHECK(dev.readBufferSize() == 4096);
}

TEST_CASE("BufferedIODevice: setReadBuffer while open returns error") {
        BufferedMemoryDevice dev;
        dev.setData("test", 4);
        dev.open(IODevice::ReadOnly);

        Buffer customBuf(4096);
        Error err = dev.setReadBuffer(std::move(customBuf));
        CHECK(err.code() == Error::AlreadyOpen);

        dev.close();
}

TEST_CASE("BufferedIODevice: buffer reuse across close/reopen") {
        BufferedMemoryDevice dev;
        const char *data1 = "first";
        dev.setData(data1, 5);
        dev.open(IODevice::ReadOnly);

        char buf[6] = {};
        dev.read(buf, 5);
        CHECK(std::strcmp(buf, "first") == 0);

        dev.close();

        // Reopen with new data
        const char *data2 = "second";
        dev.setData(data2, 6);
        dev.open(IODevice::ReadOnly);

        char buf2[7] = {};
        dev.read(buf2, 6);
        CHECK(std::strcmp(buf2, "second") == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: stale buffer data does not leak across sessions") {
        BufferedMemoryDevice dev;

        // First session: read a long string to fill the buffer
        const char *data1 = "AAAABBBBCCCCDDDD";
        dev.setData(data1, 16);
        dev.open(IODevice::ReadOnly);

        char buf1[17] = {};
        dev.read(buf1, 16);
        CHECK(std::strcmp(buf1, "AAAABBBBCCCCDDDD") == 0);

        dev.close();

        // Second session: read a shorter string
        const char *data2 = "XY";
        dev.setData(data2, 2);
        dev.open(IODevice::ReadOnly);

        char buf2[3] = {};
        int64_t n = dev.read(buf2, 2);
        CHECK(n == 2);
        CHECK(buf2[0] == 'X');
        CHECK(buf2[1] == 'Y');

        // Verify nothing more is available
        char extra;
        CHECK(dev.read(&extra, 1) == 0);
        CHECK(dev.bytesAvailable() == 0);
        CHECK_FALSE(dev.canReadLine());

        dev.close();
}

TEST_CASE("BufferedIODevice: write then read across reopen") {
        BufferedMemoryDevice dev;

        // Session 1: write data
        dev.open(IODevice::WriteOnly);
        dev.write("hello", 5);
        dev.close();

        // Session 2: read it back
        dev.open(IODevice::ReadOnly);
        char buf[6] = {};
        int64_t n = dev.read(buf, 5);
        CHECK(n == 5);
        CHECK(std::strcmp(buf, "hello") == 0);
        dev.close();

        // Session 3: write different data, then read back
        dev.open(IODevice::WriteOnly);
        dev.write("world!", 6);
        dev.close();

        dev.open(IODevice::ReadOnly);
        char buf2[7] = {};
        n = dev.read(buf2, 6);
        CHECK(n == 6);
        CHECK(std::strcmp(buf2, "world!") == 0);

        // Nothing left
        CHECK(dev.read(buf2, 1) == 0);
        dev.close();
}

TEST_CASE("BufferedIODevice: readLine resets correctly across sessions") {
        BufferedMemoryDevice dev;

        // Session 1: read lines
        const char *data1 = "aaa\nbbb\n";
        dev.setData(data1, 8);
        dev.open(IODevice::ReadOnly);

        Buffer line1 = dev.readLine();
        CHECK(line1.size() == 4);
        CHECK(std::memcmp(line1.data(), "aaa\n", 4) == 0);

        dev.close();

        // Session 2: completely different data
        const char *data2 = "xxx\n";
        dev.setData(data2, 4);
        dev.open(IODevice::ReadOnly);

        // Should get "xxx\n", not leftover "bbb\n" from session 1
        Buffer line2 = dev.readLine();
        CHECK(line2.size() == 4);
        CHECK(std::memcmp(line2.data(), "xxx\n", 4) == 0);

        // Nothing left
        Buffer line3 = dev.readLine();
        CHECK_FALSE(line3.isValid());

        dev.close();
}

TEST_CASE("BufferedIODevice: peek resets correctly across sessions") {
        BufferedMemoryDevice dev;

        // Session 1: fill buffer via read, then peek
        const char *data1 = "ABCDEF";
        dev.setData(data1, 6);
        dev.open(IODevice::ReadOnly);

        char tmp[3];
        dev.read(tmp, 3);

        char peekBuf[3] = {};
        int64_t pn = dev.peek(peekBuf, 3);
        CHECK(pn == 3);
        CHECK(std::memcmp(peekBuf, "DEF", 3) == 0);

        dev.close();

        // Session 2: peek should see new data, not leftover
        const char *data2 = "12";
        dev.setData(data2, 2);
        dev.open(IODevice::ReadOnly);

        // Trigger buffer fill
        dev.read(tmp, 1);
        CHECK(tmp[0] == '1');

        char peekBuf2[1] = {};
        pn = dev.peek(peekBuf2, 1);
        CHECK(pn == 1);
        CHECK(peekBuf2[0] == '2');

        dev.close();
}

TEST_CASE("BufferedIODevice: readAll on empty device") {
        BufferedMemoryDevice dev;
        dev.open(IODevice::ReadOnly);

        Buffer all = dev.readAll();
        CHECK_FALSE(all.isValid());

        dev.close();
}

TEST_CASE("BufferedIODevice: readLine on empty device") {
        BufferedMemoryDevice dev;
        dev.open(IODevice::ReadOnly);

        Buffer line = dev.readLine();
        CHECK_FALSE(line.isValid());

        dev.close();
}

TEST_CASE("BufferedIODevice: peek on empty buffer") {
        BufferedMemoryDevice dev;
        dev.open(IODevice::ReadOnly);

        char buf[4];
        int64_t n = dev.peek(buf, 4);
        CHECK(n == 0);

        Buffer peeked = dev.peek(4);
        CHECK_FALSE(peeked.isValid());

        dev.close();
}

TEST_CASE("BufferedIODevice: multiple small reads") {
        BufferedMemoryDevice dev;
        const char *data = "ABCDEFGHIJ";
        dev.setData(data, 10);
        dev.open(IODevice::ReadOnly);

        for(int i = 0; i < 10; i++) {
                char c;
                int64_t n = dev.read(&c, 1);
                CHECK(n == 1);
                CHECK(c == ('A' + i));
        }

        // Should be at end now
        char c;
        int64_t n = dev.read(&c, 1);
        CHECK(n == 0);

        dev.close();
}

TEST_CASE("BufferedIODevice: read on write-only returns -1") {
        BufferedMemoryDevice dev;
        dev.open(IODevice::WriteOnly);

        char buf[4];
        int64_t n = dev.read(buf, 4);
        CHECK(n == -1);

        dev.close();
}

TEST_CASE("BufferedIODevice: readBytes zero returns empty") {
        BufferedMemoryDevice dev;
        dev.setData("data", 4);
        dev.open(IODevice::ReadOnly);

        Buffer result = dev.readBytes(0);
        CHECK_FALSE(result.isValid());

        dev.close();
}

TEST_CASE("BufferedIODevice: readLine on non-open device") {
        BufferedMemoryDevice dev;
        Buffer line = dev.readLine();
        CHECK_FALSE(line.isValid());
}

TEST_CASE("BufferedIODevice: readAll on non-open device") {
        BufferedMemoryDevice dev;
        Buffer all = dev.readAll();
        CHECK_FALSE(all.isValid());
}

TEST_CASE("BufferedIODevice: peek on non-open device") {
        BufferedMemoryDevice dev;
        char buf[4];
        int64_t n = dev.peek(buf, 4);
        CHECK(n == -1);

        Buffer peeked = dev.peek(4);
        CHECK_FALSE(peeked.isValid());
}

TEST_CASE("BufferedIODevice: canReadLine on non-open device") {
        BufferedMemoryDevice dev;
        CHECK_FALSE(dev.canReadLine());
}

TEST_CASE("BufferedIODevice: readLine on write-only device") {
        BufferedMemoryDevice dev;
        dev.open(IODevice::WriteOnly);
        Buffer line = dev.readLine();
        CHECK_FALSE(line.isValid());
        dev.close();
}

TEST_CASE("BufferedIODevice: readAll on write-only device") {
        BufferedMemoryDevice dev;
        dev.open(IODevice::WriteOnly);
        Buffer all = dev.readAll();
        CHECK_FALSE(all.isValid());
        dev.close();
}

TEST_CASE("BufferedIODevice: custom buffer with specific size") {
        BufferedMemoryDevice dev;
        // Set a very small buffer to test buffer cycling
        Buffer smallBuf(16);
        dev.setReadBuffer(std::move(smallBuf));

        const char *data = "This is a longer string that exceeds buffer size";
        dev.setData(data, std::strlen(data));
        dev.open(IODevice::ReadOnly);

        Buffer all = dev.readAll();
        CHECK(all.isValid());
        CHECK(all.size() == std::strlen(data));
        CHECK(std::memcmp(all.data(), data, std::strlen(data)) == 0);

        dev.close();
}
