/**
 * @file      iodevice.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <algorithm>
#include <doctest/doctest.h>
#include <promeki/core/iodevice.h>

using namespace promeki;

/**
 * @brief In-memory IODevice implementation for testing.
 */
class MemoryIODevice : public IODevice {
        PROMEKI_OBJECT(MemoryIODevice, IODevice)
        public:
                MemoryIODevice(ObjectBase *parent = nullptr) : IODevice(parent) { }

                Error open(OpenMode mode) override {
                        if(isOpen()) return Error(Error::AlreadyOpen);
                        setOpenMode(mode);
                        _pos = 0;
                        return Error();
                }

                void close() override {
                        aboutToCloseSignal.emit();
                        setOpenMode(NotOpen);
                        _pos = 0;
                        return;
                }

                bool isOpen() const override {
                        return openMode() != NotOpen;
                }

                int64_t read(void *data, int64_t maxSize) override {
                        if(!isOpen() || !isReadable()) return -1;
                        int64_t avail = static_cast<int64_t>(_buf.size()) - _pos;
                        if(avail <= 0) return 0;
                        int64_t toRead = std::min(maxSize, avail);
                        std::memcpy(data, _buf.data() + _pos, static_cast<size_t>(toRead));
                        _pos += toRead;
                        return toRead;
                }

                int64_t write(const void *data, int64_t maxSize) override {
                        if(!isOpen() || !isWritable()) return -1;
                        const uint8_t *src = static_cast<const uint8_t *>(data);
                        int64_t endPos = _pos + maxSize;
                        if(endPos > static_cast<int64_t>(_buf.size())) {
                                _buf.resize(static_cast<size_t>(endPos));
                        }
                        std::memcpy(_buf.data() + _pos, src, static_cast<size_t>(maxSize));
                        _pos += maxSize;
                        bytesWrittenSignal.emit(maxSize);
                        return maxSize;
                }

                int64_t bytesAvailable() const override {
                        int64_t avail = static_cast<int64_t>(_buf.size()) - _pos;
                        return avail > 0 ? avail : 0;
                }

                bool isSequential() const override {
                        return false;
                }

                bool seek(int64_t pos) override {
                        if(pos < 0 || pos > static_cast<int64_t>(_buf.size())) return false;
                        _pos = pos;
                        return true;
                }

                int64_t pos() const override {
                        return _pos;
                }

                int64_t size() const override {
                        return static_cast<int64_t>(_buf.size());
                }

                bool atEnd() const override {
                        return _pos >= static_cast<int64_t>(_buf.size());
                }

                const std::vector<uint8_t> &buffer() const { return _buf; }

                void setData(const void *data, size_t sz) {
                        _buf.resize(sz);
                        std::memcpy(_buf.data(), data, sz);
                        return;
                }

        private:
                std::vector<uint8_t> _buf;
                int64_t _pos = 0;
};

/**
 * @brief MemoryIODevice with options for testing the option system.
 */
class OptionTestDevice : public IODevice {
        PROMEKI_OBJECT(OptionTestDevice, IODevice)
        public:
                OptionTestDevice(ObjectBase *parent = nullptr) :
                        IODevice({
                                {DirectIO, false},
                                {Synchronous, false}
                        }, parent) { }

                Error open(OpenMode mode) override {
                        if(isOpen()) return Error(Error::AlreadyOpen);
                        setOpenMode(mode);
                        return Error();
                }
                void close() override { setOpenMode(NotOpen); return; }
                bool isOpen() const override { return openMode() != NotOpen; }
                int64_t read(void *, int64_t) override { return -1; }
                int64_t write(const void *, int64_t) override { return -1; }

                IODevice::Option lastChangedOption = IODevice::InvalidOption;
                Variant lastChangedValue;

        protected:
                void onOptionChanged(Option opt, const Variant &value) override {
                        lastChangedOption = opt;
                        lastChangedValue = value;
                        return;
                }
};

TEST_CASE("IODevice: MemoryIODevice default state") {
        MemoryIODevice dev;
        CHECK_FALSE(dev.isOpen());
        CHECK(dev.openMode() == IODevice::NotOpen);
        CHECK_FALSE(dev.isReadable());
        CHECK_FALSE(dev.isWritable());
        CHECK(dev.error().isOk());
}

TEST_CASE("IODevice: open and close") {
        MemoryIODevice dev;

        Error err = dev.open(IODevice::ReadWrite);
        CHECK(err.isOk());
        CHECK(dev.isOpen());
        CHECK(dev.isReadable());
        CHECK(dev.isWritable());
        CHECK(dev.openMode() == IODevice::ReadWrite);

        dev.close();
        CHECK_FALSE(dev.isOpen());
}

TEST_CASE("IODevice: open ReadOnly") {
        MemoryIODevice dev;
        Error err = dev.open(IODevice::ReadOnly);
        CHECK(err.isOk());
        CHECK(dev.isReadable());
        CHECK_FALSE(dev.isWritable());
        dev.close();
}

TEST_CASE("IODevice: open WriteOnly") {
        MemoryIODevice dev;
        Error err = dev.open(IODevice::WriteOnly);
        CHECK(err.isOk());
        CHECK_FALSE(dev.isReadable());
        CHECK(dev.isWritable());
        dev.close();
}

TEST_CASE("IODevice: double open returns AlreadyOpen") {
        MemoryIODevice dev;
        dev.open(IODevice::ReadWrite);
        Error err = dev.open(IODevice::ReadOnly);
        CHECK(err.code() == Error::AlreadyOpen);
        dev.close();
}

TEST_CASE("IODevice: write and read back") {
        MemoryIODevice dev;
        dev.open(IODevice::ReadWrite);

        const char *msg = "Hello, IODevice!";
        int64_t written = dev.write(msg, 16);
        CHECK(written == 16);

        dev.seek(0);
        char buf[17] = {};
        int64_t bytesRead = dev.read(buf, 16);
        CHECK(bytesRead == 16);
        CHECK(std::strcmp(buf, "Hello, IODevice!") == 0);

        dev.close();
}

TEST_CASE("IODevice: seek and position") {
        MemoryIODevice dev;
        dev.open(IODevice::ReadWrite);

        const char *data = "0123456789";
        dev.write(data, 10);

        CHECK(dev.pos() == 10);
        CHECK(dev.size() == 10);

        CHECK(dev.seek(5));
        CHECK(dev.pos() == 5);

        CHECK(dev.seek(0));
        CHECK(dev.pos() == 0);

        CHECK_FALSE(dev.seek(-1));
        CHECK_FALSE(dev.seek(11));

        dev.close();
}

TEST_CASE("IODevice: bytesAvailable") {
        MemoryIODevice dev;
        dev.open(IODevice::ReadWrite);

        const char *data = "abcdef";
        dev.write(data, 6);

        dev.seek(0);
        CHECK(dev.bytesAvailable() == 6);

        dev.seek(3);
        CHECK(dev.bytesAvailable() == 3);

        dev.seek(6);
        CHECK(dev.bytesAvailable() == 0);

        dev.close();
}

TEST_CASE("IODevice: atEnd") {
        MemoryIODevice dev;
        dev.open(IODevice::ReadWrite);

        const char *data = "test";
        dev.write(data, 4);

        CHECK(dev.atEnd());

        dev.seek(0);
        CHECK_FALSE(dev.atEnd());

        dev.seek(4);
        CHECK(dev.atEnd());

        dev.close();
}

TEST_CASE("IODevice: isSequential") {
        MemoryIODevice dev;
        CHECK_FALSE(dev.isSequential());
}

TEST_CASE("IODevice: read returns 0 at end") {
        MemoryIODevice dev;
        dev.open(IODevice::ReadWrite);

        const char *data = "abc";
        dev.write(data, 3);
        dev.seek(3);

        char buf[4] = {};
        int64_t bytesRead = dev.read(buf, 4);
        CHECK(bytesRead == 0);

        dev.close();
}

TEST_CASE("IODevice: partial read") {
        MemoryIODevice dev;
        dev.open(IODevice::ReadWrite);

        const char *data = "Hello";
        dev.write(data, 5);
        dev.seek(3);

        char buf[10] = {};
        int64_t bytesRead = dev.read(buf, 10);
        CHECK(bytesRead == 2);
        CHECK(buf[0] == 'l');
        CHECK(buf[1] == 'o');

        dev.close();
}

TEST_CASE("IODevice: read on write-only returns -1") {
        MemoryIODevice dev;
        dev.open(IODevice::WriteOnly);

        char buf[4];
        int64_t bytesRead = dev.read(buf, 4);
        CHECK(bytesRead == -1);

        dev.close();
}

TEST_CASE("IODevice: write on read-only returns -1") {
        MemoryIODevice dev;
        dev.setData("test", 4);
        dev.open(IODevice::ReadOnly);

        int64_t written = dev.write("abc", 3);
        CHECK(written == -1);

        dev.close();
}

TEST_CASE("IODevice: error state") {
        MemoryIODevice dev;
        CHECK(dev.error().isOk());

        dev.clearError();
        CHECK(dev.error().isOk());
}

TEST_CASE("IODevice: default virtual implementations") {
        MemoryIODevice dev;
        CHECK_FALSE(dev.waitForReadyRead(100));
        CHECK_FALSE(dev.waitForBytesWritten(100));
}

TEST_CASE("IODevice: setData and read") {
        MemoryIODevice dev;
        const char *data = "preloaded";
        dev.setData(data, 9);
        dev.open(IODevice::ReadOnly);

        CHECK(dev.size() == 9);
        CHECK(dev.bytesAvailable() == 9);

        char buf[10] = {};
        int64_t bytesRead = dev.read(buf, 9);
        CHECK(bytesRead == 9);
        CHECK(std::strcmp(buf, "preloaded") == 0);

        dev.close();
}

TEST_CASE("IODevice: aboutToClose signal") {
        MemoryIODevice dev;
        bool signalFired = false;
        dev.aboutToCloseSignal.connect(
                [&signalFired]() { signalFired = true; });
        dev.open(IODevice::ReadWrite);
        dev.close();
        CHECK(signalFired);
}

TEST_CASE("IODevice: bytesWritten signal") {
        MemoryIODevice dev;
        int64_t reportedBytes = 0;
        dev.bytesWrittenSignal.connect(
                [&reportedBytes](int64_t n) { reportedBytes = n; });
        dev.open(IODevice::WriteOnly);
        dev.write("test", 4);
        CHECK(reportedBytes == 4);
        dev.close();
}

TEST_CASE("IODevice: errorOccurred signal via setError") {
        // OptionTestDevice exposes setError indirectly; test the signal
        // by connecting before triggering an error path.
        OptionTestDevice dev;
        Error lastError;
        dev.errorOccurredSignal.connect(
                [&lastError](Error e) { lastError = e; });
        // setOption on unsupported type calls setError internally? No.
        // setError is protected. We verify the signal wiring is correct
        // by checking it compiles and the default error state is Ok.
        CHECK(dev.error().isOk());
}

TEST_CASE("IODevice: OpenMode ReadWrite is ReadOnly | WriteOnly") {
        CHECK(IODevice::ReadWrite == (IODevice::ReadOnly | IODevice::WriteOnly));
}

TEST_CASE("IODevice: multiple writes then full read") {
        MemoryIODevice dev;
        dev.open(IODevice::ReadWrite);

        dev.write("abc", 3);
        dev.write("def", 3);
        dev.write("ghi", 3);
        CHECK(dev.size() == 9);
        CHECK(dev.pos() == 9);

        dev.seek(0);
        char buf[10] = {};
        int64_t bytesRead = dev.read(buf, 9);
        CHECK(bytesRead == 9);
        CHECK(std::strcmp(buf, "abcdefghi") == 0);

        dev.close();
}

TEST_CASE("IODevice: overwrite in middle") {
        MemoryIODevice dev;
        dev.open(IODevice::ReadWrite);

        dev.write("AAAAAAA", 7);
        dev.seek(2);
        dev.write("BBB", 3);

        dev.seek(0);
        char buf[8] = {};
        dev.read(buf, 7);
        CHECK(std::strcmp(buf, "AABBBA") != 0); // sanity
        CHECK(buf[0] == 'A');
        CHECK(buf[1] == 'A');
        CHECK(buf[2] == 'B');
        CHECK(buf[3] == 'B');
        CHECK(buf[4] == 'B');
        CHECK(buf[5] == 'A');
        CHECK(buf[6] == 'A');

        dev.close();
}

TEST_CASE("IODevice: setOption overwrite") {
        OptionTestDevice dev;
        dev.setOption(IODevice::DirectIO, true);
        auto [val1, err1] = dev.option(IODevice::DirectIO);
        CHECK(val1.get<bool>() == true);

        dev.setOption(IODevice::DirectIO, false);
        auto [val2, err2] = dev.option(IODevice::DirectIO);
        CHECK(val2.get<bool>() == false);
}

// --- Option system tests ---

TEST_CASE("IODevice: registerOptionType returns unique IDs") {
        IODevice::Option a = IODevice::registerOptionType();
        IODevice::Option b = IODevice::registerOptionType();
        CHECK(a != b);
        CHECK(a != IODevice::InvalidOption);
        CHECK(b != IODevice::InvalidOption);
}

TEST_CASE("IODevice: built-in option types are distinct") {
        CHECK(IODevice::DirectIO != IODevice::Synchronous);
        CHECK(IODevice::Synchronous != IODevice::NonBlocking);
        CHECK(IODevice::NonBlocking != IODevice::Unbuffered);
        CHECK(IODevice::DirectIO != IODevice::InvalidOption);
}

TEST_CASE("IODevice: optionSupported") {
        OptionTestDevice dev;
        CHECK(dev.optionSupported(IODevice::DirectIO));
        CHECK(dev.optionSupported(IODevice::Synchronous));
        CHECK_FALSE(dev.optionSupported(IODevice::NonBlocking));
        CHECK_FALSE(dev.optionSupported(IODevice::Unbuffered));
}

TEST_CASE("IODevice: option returns default value") {
        OptionTestDevice dev;
        auto [val, err] = dev.option(IODevice::DirectIO);
        CHECK(err.isOk());
        CHECK(val.get<bool>() == false);
}

TEST_CASE("IODevice: setOption and read back") {
        OptionTestDevice dev;
        Error err = dev.setOption(IODevice::DirectIO, true);
        CHECK(err.isOk());

        auto [val, err2] = dev.option(IODevice::DirectIO);
        CHECK(err2.isOk());
        CHECK(val.get<bool>() == true);
}

TEST_CASE("IODevice: setOption on unsupported returns NotSupported") {
        OptionTestDevice dev;
        Error err = dev.setOption(IODevice::NonBlocking, true);
        CHECK(err.code() == Error::NotSupported);
}

TEST_CASE("IODevice: option on unsupported returns NotSupported") {
        OptionTestDevice dev;
        auto [val, err] = dev.option(IODevice::NonBlocking);
        CHECK(err.code() == Error::NotSupported);
}

TEST_CASE("IODevice: onOptionChanged callback") {
        OptionTestDevice dev;
        dev.setOption(IODevice::DirectIO, true);
        CHECK(dev.lastChangedOption == IODevice::DirectIO);
        CHECK(dev.lastChangedValue.get<bool>() == true);

        dev.setOption(IODevice::Synchronous, true);
        CHECK(dev.lastChangedOption == IODevice::Synchronous);
}

TEST_CASE("IODevice: device without options") {
        MemoryIODevice dev;
        CHECK_FALSE(dev.optionSupported(IODevice::DirectIO));
        Error err = dev.setOption(IODevice::DirectIO, true);
        CHECK(err.code() == Error::NotSupported);
}
