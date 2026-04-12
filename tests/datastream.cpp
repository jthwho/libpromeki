/**
 * @file      datastream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <doctest/doctest.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>
#include <promeki/fileiodevice.h>
#include <promeki/buffer.h>
#include <promeki/string.h>
#include <promeki/variant.h>
#include <promeki/uuid.h>
#include <promeki/umid.h>
#include <promeki/timecode.h>
#include <promeki/datetime.h>
#include <promeki/timestamp.h>
#include <promeki/size2d.h>
#include <promeki/rational.h>
#include <promeki/framerate.h>
#include <promeki/stringlist.h>
#include <promeki/color.h>
#include <promeki/colormodel.h>
#include <promeki/memspace.h>
#include <promeki/pixelformat.h>
#include <promeki/pixeldesc.h>
#include <promeki/enum.h>
#include <promeki/enums.h>
#include <promeki/json.h>
#include <promeki/metadata.h>
#include <promeki/xyzcolor.h>
#include <promeki/audiodesc.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/rect.h>
#include <promeki/point.h>

using namespace promeki;

static const size_t TestBufSize = 4096;

// ============================================================================
// Helper: open a BufferIODevice for write, create a DataStream writer
// ============================================================================

struct WriterFixture {
        Buffer buf;
        BufferIODevice dev;
        WriterFixture() : buf(TestBufSize), dev(&buf) {
                dev.open(IODevice::ReadWrite);
        }
};

// ============================================================================
// Header / versioning tests
// ============================================================================

TEST_CASE("DataStream: writer writes header") {
        WriterFixture f;
        DataStream ws = DataStream::createWriter(&f.dev);
        CHECK(ws.status() == DataStream::Ok);
        CHECK(ws.version() == DataStream::CurrentVersion);
        // Header is 16 bytes: 4 magic + 2 version + 1 byte-order + 9 reserved
        CHECK(DataStream::HeaderSize == 16);
        CHECK(f.dev.pos() == 16);

        // Verify raw bytes
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        CHECK(raw[0] == 0x50); // 'P'
        CHECK(raw[1] == 0x4D); // 'M'
        CHECK(raw[2] == 0x44); // 'D'
        CHECK(raw[3] == 0x53); // 'S'
        // Version 1 in big-endian
        CHECK(raw[4] == 0x00);
        CHECK(raw[5] == 0x01);
        // Default byte order is BigEndian → 'B'
        CHECK(raw[6] == 'B');
        // Reserved bytes 7-15 must be zero
        for(int i = 7; i < 16; ++i) CHECK(raw[i] == 0x00);
}

TEST_CASE("DataStream: writer records little-endian byte order in header") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev, DataStream::LittleEndian);
                CHECK(ws.byteOrder() == DataStream::LittleEndian);
        }
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        CHECK(raw[6] == 'L');
        for(int i = 7; i < 16; ++i) CHECK(raw[i] == 0x00);
}

TEST_CASE("DataStream: reader auto-configures byte order from header") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev, DataStream::LittleEndian);
                ws << static_cast<uint32_t>(0x01020304);
        }
        f.dev.seek(0);
        DataStream rs = DataStream::createReader(&f.dev);
        CHECK(rs.status() == DataStream::Ok);
        CHECK(rs.byteOrder() == DataStream::LittleEndian);
        uint32_t val;
        rs >> val;
        CHECK(val == 0x01020304);
}

TEST_CASE("DataStream: reader rejects invalid byte-order marker") {
        WriterFixture f;
        DataStream ws = DataStream::createWriter(&f.dev);
        static_cast<uint8_t *>(f.buf.data())[6] = 0xFF;
        f.dev.seek(0);
        DataStream rs = DataStream::createReader(&f.dev);
        CHECK(rs.status() == DataStream::ReadCorruptData);
        CHECK(rs.errorContext().contains("byte-order marker"));
}

TEST_CASE("DataStream: reader rejects non-zero reserved bytes") {
        WriterFixture f;
        DataStream ws = DataStream::createWriter(&f.dev);
        // Poke a non-zero byte into the reserved area
        static_cast<uint8_t *>(f.buf.data())[10] = 0x42;
        f.dev.seek(0);
        DataStream rs = DataStream::createReader(&f.dev);
        CHECK(rs.status() == DataStream::ReadCorruptData);
        CHECK(rs.errorContext().contains("reserved byte"));
}

TEST_CASE("DataStream: reader reads and validates header") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<int32_t>(42);
        }
        f.dev.seek(0);
        DataStream rs = DataStream::createReader(&f.dev);
        CHECK(rs.status() == DataStream::Ok);
        CHECK(rs.version() == DataStream::CurrentVersion);

        int32_t val;
        rs >> val;
        CHECK(val == 42);
}

TEST_CASE("DataStream: reader rejects bad magic") {
        Buffer buf(TestBufSize);
        // Write a full 16-byte garbage header
        std::memset(buf.data(), 0xFF, 16);
        buf.setSize(16);

        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadOnly);
        DataStream rs = DataStream::createReader(&dev);
        CHECK(rs.status() == DataStream::ReadCorruptData);
        CHECK(rs.errorContext().contains("magic"));
}

TEST_CASE("DataStream: reader handles truncated header") {
        Buffer buf(TestBufSize);
        // Write only 2 bytes — not enough for the 4-byte magic
        static_cast<uint8_t *>(buf.data())[0] = 0x50;
        static_cast<uint8_t *>(buf.data())[1] = 0x4D;
        buf.setSize(2);

        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadOnly);
        DataStream rs = DataStream::createReader(&dev);
        CHECK(rs.status() == DataStream::ReadPastEnd);
}

TEST_CASE("DataStream: raw constructor skips header") {
        WriterFixture f;
        DataStream ds(&f.dev);
        CHECK(ds.status() == DataStream::Ok);
        CHECK(ds.version() == 0); // no header read
        CHECK(f.dev.pos() == 0); // no bytes consumed
}

// ============================================================================
// Primitive round-trip tests via BufferIODevice
// ============================================================================

TEST_CASE("DataStream: round-trip int8_t") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<int8_t>(-42);
                ws << static_cast<int8_t>(0);
                ws << static_cast<int8_t>(127);
                ws << static_cast<int8_t>(-128);
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                int8_t a, b, c, d;
                rs >> a >> b >> c >> d;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(a == -42);
                CHECK(b == 0);
                CHECK(c == 127);
                CHECK(d == -128);
        }
}

TEST_CASE("DataStream: round-trip uint8_t") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint8_t>(0) << static_cast<uint8_t>(255);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                uint8_t a, b;
                rs >> a >> b;
                CHECK(a == 0);
                CHECK(b == 255);
        }
}

TEST_CASE("DataStream: round-trip int16_t") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<int16_t>(-1234) << static_cast<int16_t>(32767);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                int16_t a, b;
                rs >> a >> b;
                CHECK(a == -1234);
                CHECK(b == 32767);
        }
}

TEST_CASE("DataStream: round-trip uint16_t") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint16_t>(0) << static_cast<uint16_t>(65535);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                uint16_t a, b;
                rs >> a >> b;
                CHECK(a == 0);
                CHECK(b == 65535);
        }
}

TEST_CASE("DataStream: round-trip int32_t") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<int32_t>(-123456789) << static_cast<int32_t>(2147483647);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                int32_t a, b;
                rs >> a >> b;
                CHECK(a == -123456789);
                CHECK(b == 2147483647);
        }
}

TEST_CASE("DataStream: round-trip uint32_t") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint32_t>(0) << static_cast<uint32_t>(4294967295u);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                uint32_t a, b;
                rs >> a >> b;
                CHECK(a == 0);
                CHECK(b == 4294967295u);
        }
}

TEST_CASE("DataStream: round-trip int64_t") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<int64_t>(-9876543210LL) << static_cast<int64_t>(9223372036854775807LL);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                int64_t a, b;
                rs >> a >> b;
                CHECK(a == -9876543210LL);
                CHECK(b == 9223372036854775807LL);
        }
}

TEST_CASE("DataStream: round-trip uint64_t") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint64_t>(0) << static_cast<uint64_t>(18446744073709551615ULL);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                uint64_t a, b;
                rs >> a >> b;
                CHECK(a == 0);
                CHECK(b == 18446744073709551615ULL);
        }
}

TEST_CASE("DataStream: round-trip float") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << 3.14f << -0.0f << 1.0e10f;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                float a, b, c;
                rs >> a >> b >> c;
                CHECK(a == doctest::Approx(3.14f));
                CHECK(b == -0.0f);
                CHECK(c == doctest::Approx(1.0e10f));
        }
}

TEST_CASE("DataStream: round-trip double") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << 3.141592653589793 << -1.0e-300 << 1.0e300;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                double a, b, c;
                rs >> a >> b >> c;
                CHECK(a == doctest::Approx(3.141592653589793));
                CHECK(b == doctest::Approx(-1.0e-300));
                CHECK(c == doctest::Approx(1.0e300));
        }
}

TEST_CASE("DataStream: round-trip bool") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << true << false;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                bool a, b;
                rs >> a >> b;
                CHECK(a == true);
                CHECK(b == false);
        }
}

// ============================================================================
// String round-trip
// ============================================================================

TEST_CASE("DataStream: round-trip String") {
        WriterFixture f;
        String hello("Hello, DataStream!");
        String empty;
        String special("line1\nline2\ttab");
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << hello << empty << special;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                String a, b, c;
                rs >> a >> b >> c;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(a == hello);
                CHECK(b.isEmpty());
                CHECK(c == special);
        }
}

TEST_CASE("DataStream: round-trip Unicode String") {
        WriterFixture f;
        const char *utf8 = "\xc3\xa9\xc3\xa0\xc3\xbc"; // éàü in UTF-8
        String unicode = String::fromUtf8(utf8, 6);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << unicode;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                String val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.length() == 3);
                CHECK(val.byteCount() == 6);
        }
}

// ============================================================================
// Buffer round-trip
// ============================================================================

TEST_CASE("DataStream: round-trip Buffer") {
        WriterFixture f;
        Buffer payload(64);
        std::memset(payload.data(), 0xAB, 64);
        payload.setSize(64);

        Buffer emptyPayload;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << payload << emptyPayload;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Buffer a, b;
                rs >> a >> b;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(a.size() == 64);
                CHECK(static_cast<uint8_t *>(a.data())[0] == 0xAB);
                CHECK(static_cast<uint8_t *>(a.data())[63] == 0xAB);
                CHECK_FALSE(b.isValid());
        }
}

// ============================================================================
// Variant round-trip
// ============================================================================

TEST_CASE("DataStream: round-trip Variant primitives") {
        WriterFixture f;
        Variant vInvalid;
        Variant vBool(true);
        Variant vU32(static_cast<uint32_t>(42));
        Variant vS64(static_cast<int64_t>(-99));
        Variant vDouble(2.718);
        Variant vString(String("test"));

        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vInvalid << vBool << vU32 << vS64 << vDouble << vString;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant a, b, c, d, e, g;
                rs >> a >> b >> c >> d >> e >> g;
                CHECK(rs.status() == DataStream::Ok);
                CHECK_FALSE(a.isValid());
                CHECK(b.get<bool>() == true);
                CHECK(c.get<uint32_t>() == 42);
                CHECK(d.get<int64_t>() == -99);
                CHECK(e.get<double>() == doctest::Approx(2.718));
                CHECK(g.get<String>() == "test");
        }
}

// ============================================================================
// Byte order switching
// ============================================================================

TEST_CASE("DataStream: big-endian vs little-endian wire format") {
        uint32_t testVal = 0x01020304;

        // Big-endian: 16-byte header + 1-byte type tag + 4-byte value
        WriterFixture f1;
        {
                DataStream ws = DataStream::createWriter(&f1.dev, DataStream::BigEndian);
                ws << testVal;
        }
        // After header(16) + tag(1), the 4 value bytes start at offset 17
        uint8_t *raw = static_cast<uint8_t *>(f1.buf.data());
        CHECK(raw[6] == 'B'); // byte-order marker
        CHECK(raw[16] == DataStream::TypeUInt32);
        CHECK(raw[17] == 0x01);
        CHECK(raw[18] == 0x02);
        CHECK(raw[19] == 0x03);
        CHECK(raw[20] == 0x04);

        f1.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f1.dev);
                CHECK(rs.byteOrder() == DataStream::BigEndian);
                uint32_t val;
                rs >> val;
                CHECK(val == 0x01020304);
        }

        // Little-endian
        WriterFixture f2;
        {
                DataStream ws = DataStream::createWriter(&f2.dev, DataStream::LittleEndian);
                ws << testVal;
        }
        raw = static_cast<uint8_t *>(f2.buf.data());
        CHECK(raw[6] == 'L'); // byte-order marker
        CHECK(raw[16] == DataStream::TypeUInt32);
        CHECK(raw[17] == 0x04);
        CHECK(raw[18] == 0x03);
        CHECK(raw[19] == 0x02);
        CHECK(raw[20] == 0x01);

        f2.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f2.dev);
                CHECK(rs.byteOrder() == DataStream::LittleEndian);
                uint32_t val;
                rs >> val;
                CHECK(val == 0x01020304);
        }
}

TEST_CASE("DataStream: byte order for 16-bit values") {
        WriterFixture f;
        uint16_t testVal = 0xABCD;
        {
                DataStream ws = DataStream::createWriter(&f.dev, DataStream::BigEndian);
                ws << testVal;
        }
        // header(16) + tag(1) = offset 17
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        CHECK(raw[16] == DataStream::TypeUInt16);
        CHECK(raw[17] == 0xAB);
        CHECK(raw[18] == 0xCD);
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                uint16_t val;
                rs >> val;
                CHECK(val == 0xABCD);
        }
}

TEST_CASE("DataStream: byte order for 64-bit values") {
        WriterFixture f;
        uint64_t testVal = 0x0102030405060708ULL;
        {
                DataStream ws = DataStream::createWriter(&f.dev, DataStream::BigEndian);
                ws << testVal;
        }
        // header(16) + tag(1) = offset 17
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data()) + 17;
        for(int i = 0; i < 8; ++i) CHECK(raw[i] == static_cast<uint8_t>(i + 1));
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                uint64_t val;
                rs >> val;
                CHECK(val == 0x0102030405060708ULL);
        }
}

// ============================================================================
// Error status
// ============================================================================

TEST_CASE("DataStream: type mismatch sets ReadCorruptData") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint8_t>(0x42);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                // Try to read as int32_t — tag will be TypeUInt8 not TypeInt32
                int32_t val = 0;
                rs >> val;
                CHECK(rs.status() == DataStream::ReadCorruptData);
                CHECK(val == 0);
        }
}

TEST_CASE("DataStream: ReadPastEnd on truncated data") {
        // Write a header + TypeUInt32 tag + only 2 of the 4 value bytes
        Buffer buf(TestBufSize);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream ws = DataStream::createWriter(&dev);
                // Write a uint32 tag + partial value using raw access
                uint8_t tag = DataStream::TypeUInt32;
                ws.writeRawData(&tag, 1);
                uint8_t partial[2] = { 0x01, 0x02 };
                ws.writeRawData(partial, 2);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                CHECK(rs.status() == DataStream::Ok);
                uint32_t val = 0;
                rs >> val;
                CHECK(rs.status() == DataStream::ReadPastEnd);
                CHECK(val == 0);
        }
}

TEST_CASE("DataStream: status propagates — no further reads after error") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint8_t>(0xFF);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                // Try to read as int32_t — will fail with type mismatch
                int32_t a = 0;
                uint8_t b = 0;
                rs >> a;
                CHECK(rs.status() != DataStream::Ok);
                rs >> b;
                CHECK(b == 0); // not read because status was already bad
        }
}

// ============================================================================
// Raw byte access
// ============================================================================

TEST_CASE("DataStream: readRawData and writeRawData") {
        WriterFixture f;
        const char *data = "raw bytes test";
        size_t len = std::strlen(data);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ssize_t n = ws.writeRawData(data, len);
                CHECK(n == static_cast<ssize_t>(len));
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                char out[32] = {};
                ssize_t n = rs.readRawData(out, len);
                CHECK(n == static_cast<ssize_t>(len));
                CHECK(std::memcmp(out, data, len) == 0);
        }
}

TEST_CASE("DataStream: skipRawData") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint8_t>(0xAA);
                ws << static_cast<uint8_t>(0xBB);
                ws << static_cast<uint8_t>(0xCC);
                ws << static_cast<uint8_t>(0xDD);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                // Each uint8_t is tag(1) + value(1) = 2 bytes.
                // Skip first entry (2 bytes)
                ssize_t skipped = rs.skipRawData(2);
                CHECK(skipped == 2);
                uint8_t val;
                rs >> val;
                CHECK(val == 0xBB);
        }
}

// ============================================================================
// atEnd
// ============================================================================

TEST_CASE("DataStream: atEnd") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint8_t>(42);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                CHECK_FALSE(rs.atEnd());
                uint8_t val;
                rs >> val;
                CHECK(rs.atEnd());
        }
}

// ============================================================================
// resetStatus
// ============================================================================

TEST_CASE("DataStream: resetStatus") {
        Buffer buf(4); // too small for the 16-byte header
        BufferIODevice dev(&buf);
        dev.open(IODevice::WriteOnly);
        DataStream ds = DataStream::createWriter(&dev);
        CHECK(ds.status() == DataStream::WriteFailed);
        ds.resetStatus();
        CHECK(ds.status() == DataStream::Ok);
}

// ============================================================================
// Mixed types in sequence
// ============================================================================

TEST_CASE("DataStream: mixed types round-trip") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint8_t>(1);
                ws << static_cast<int32_t>(-42);
                ws << 3.14;
                ws << String("mixed");
                ws << true;
                ws << static_cast<uint64_t>(0xDEADBEEFULL);
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                uint8_t a; int32_t b; double c; String d; bool e; uint64_t g;
                rs >> a >> b >> c >> d >> e >> g;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(a == 1);
                CHECK(b == -42);
                CHECK(c == doctest::Approx(3.14));
                CHECK(d == "mixed");
                CHECK(e == true);
                CHECK(g == 0xDEADBEEFULL);
        }
}

// ============================================================================
// Float byte order switching
// ============================================================================

TEST_CASE("DataStream: float round-trip with byte order switching") {
        float testVal = 1.5f;

        WriterFixture f1;
        {
                DataStream ws = DataStream::createWriter(&f1.dev);
                ws.setByteOrder(DataStream::BigEndian);
                ws << testVal;
        }
        f1.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f1.dev);
                rs.setByteOrder(DataStream::BigEndian);
                float val;
                rs >> val;
                CHECK(val == doctest::Approx(1.5f));
        }

        WriterFixture f2;
        {
                DataStream ws = DataStream::createWriter(&f2.dev);
                ws.setByteOrder(DataStream::LittleEndian);
                ws << testVal;
        }
        f2.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f2.dev);
                rs.setByteOrder(DataStream::LittleEndian);
                float val;
                rs >> val;
                CHECK(val == doctest::Approx(1.5f));
        }
}

// ============================================================================
// Variant: remaining numeric subtypes
// ============================================================================

TEST_CASE("DataStream: round-trip Variant all numeric subtypes") {
        WriterFixture f;
        Variant vU8(static_cast<uint8_t>(200));
        Variant vS8(static_cast<int8_t>(-42));
        Variant vU16(static_cast<uint16_t>(60000));
        Variant vS16(static_cast<int16_t>(-1234));
        Variant vS32(static_cast<int32_t>(-999999));
        Variant vU64(static_cast<uint64_t>(18446744073709551615ULL));
        Variant vFloat(1.5f);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vU8 << vS8 << vU16 << vS16 << vS32 << vU64 << vFloat;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant a, b, c, d, e, g, h;
                rs >> a >> b >> c >> d >> e >> g >> h;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(a.get<uint8_t>() == 200);
                CHECK(b.get<int8_t>() == -42);
                CHECK(c.get<uint16_t>() == 60000);
                CHECK(d.get<int16_t>() == -1234);
                CHECK(e.get<int32_t>() == -999999);
                CHECK(g.get<uint64_t>() == 18446744073709551615ULL);
                CHECK(h.get<float>() == doctest::Approx(1.5f));
        }
}

// ============================================================================
// Variant: complex types (DateTime, UUID)
// ============================================================================

TEST_CASE("DataStream: round-trip Variant DateTime") {
        WriterFixture f;
        DateTime dt = DateTime::fromString("2025-06-15 12:30:45");
        Variant vDt(dt);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vDt;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeDateTime);
                CHECK(val.get<DateTime>() == dt);
        }
}

TEST_CASE("DataStream: round-trip Variant UUID") {
        WriterFixture f;
        UUID id = UUID::generateV4();
        Variant vId(id);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vId;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeUUID);
                CHECK(val.get<UUID>() == id);
        }
}

TEST_CASE("DataStream: round-trip Variant UMID (Extended)") {
        WriterFixture f;
        UMID id = UMID::generate(UMID::Extended);
        Variant vId(id);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vId;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeUMID);
                UMID out = val.get<UMID>();
                CHECK(out.isValid());
                CHECK(out.length() == UMID::Extended);
                CHECK(out == id);
        }
}

TEST_CASE("DataStream: round-trip Variant UMID (Basic)") {
        WriterFixture f;
        UMID id = UMID::generate(UMID::Basic);
        Variant vId(id);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vId;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeUMID);
                UMID out = val.get<UMID>();
                CHECK(out.isValid());
                CHECK(out.length() == UMID::Basic);
                CHECK(out == id);
        }
}

// ============================================================================
// Double byte order round-trip
// ============================================================================

TEST_CASE("DataStream: double byte order round-trip both endiannesses") {
        double testVal = 1.23456789012345e100;

        WriterFixture f1;
        {
                DataStream ws = DataStream::createWriter(&f1.dev);
                ws.setByteOrder(DataStream::BigEndian);
                ws << testVal;
        }
        f1.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f1.dev);
                rs.setByteOrder(DataStream::BigEndian);
                double val;
                rs >> val;
                CHECK(val == doctest::Approx(testVal));
        }

        WriterFixture f2;
        {
                DataStream ws = DataStream::createWriter(&f2.dev);
                ws.setByteOrder(DataStream::LittleEndian);
                ws << testVal;
        }
        f2.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f2.dev);
                rs.setByteOrder(DataStream::LittleEndian);
                double val;
                rs >> val;
                CHECK(val == doctest::Approx(testVal));
        }
}

// ============================================================================
// WriteFailed error path
// ============================================================================

TEST_CASE("DataStream: WriteFailed on full buffer") {
        // Header alone is 16 bytes; 18 bytes leaves 2 for payload.
        Buffer buf(18);
        BufferIODevice dev(&buf);
        dev.open(IODevice::WriteOnly);
        DataStream ws = DataStream::createWriter(&dev);
        // Writing a uint32_t (tag + 4 bytes = 5 bytes) should fail with
        // only 2 bytes remaining.
        if(ws.status() == DataStream::Ok) {
                ws << static_cast<uint32_t>(42);
                CHECK(ws.status() == DataStream::WriteFailed);
        }
}

// ============================================================================
// Default byte order
// ============================================================================

TEST_CASE("DataStream: default byte order is BigEndian") {
        WriterFixture f;
        DataStream ds(&f.dev);
        CHECK(ds.byteOrder() == DataStream::BigEndian);
}

// ============================================================================
// device() accessor
// ============================================================================

TEST_CASE("DataStream: device accessor") {
        WriterFixture f;
        DataStream ds(&f.dev);
        CHECK(ds.device() == &f.dev);
}

// ============================================================================
// round-trip Variant Color
// ============================================================================

TEST_CASE("DataStream: round-trip Variant Color") {
        WriterFixture f;
        Color c(128, 64, 32, 200);
        Variant vc(c);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vc;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeColor);
                Color out = val.get<Color>();
                CHECK(out.r8() == 128);
                CHECK(out.g8() == 64);
                CHECK(out.b8() == 32);
                CHECK(out.a8() == 200);
        }
}

TEST_CASE("DataStream: round-trip Variant Color named constants") {
        WriterFixture f;
        Variant vc(Color::White);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vc;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeColor);
                Color out = val.get<Color>();
                CHECK(out.r8() == 255);
                CHECK(out.g8() == 255);
                CHECK(out.b8() == 255);
        }
}

// ============================================================================
// Variant: previously missing or broken subtypes
// ============================================================================

TEST_CASE("DataStream: round-trip Variant TimeStamp") {
        WriterFixture f;
        TimeStamp ts = TimeStamp::now();
        Variant vts(ts);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vts;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeTimeStamp);
                TimeStamp out = val.get<TimeStamp>();
                CHECK(out.nanoseconds() == ts.nanoseconds());
        }
}

TEST_CASE("DataStream: round-trip Variant Size2D") {
        WriterFixture f;
        Size2Du32 sz(1920, 1080);
        Variant vsz(sz);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vsz;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeSize2D);
                Size2Du32 out = val.get<Size2Du32>();
                CHECK(out.width() == 1920);
                CHECK(out.height() == 1080);
        }
}

TEST_CASE("DataStream: round-trip Variant Rational") {
        WriterFixture f;
        Rational<int> r(24000, 1001);
        Variant vr(r);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vr;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeRational);
                Rational<int> out = val.get<Rational<int>>();
                CHECK(out.numerator() == 24000);
                CHECK(out.denominator() == 1001);
        }
}

TEST_CASE("DataStream: round-trip Variant FrameRate") {
        WriterFixture f;
        FrameRate fr(FrameRate::RationalType(30000u, 1001u));
        Variant vfr(fr);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vfr;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeFrameRate);
                FrameRate out = val.get<FrameRate>();
                CHECK(out.numerator() == 30000u);
                CHECK(out.denominator() == 1001u);
        }
}

TEST_CASE("DataStream: round-trip Variant StringList") {
        WriterFixture f;
        StringList list;
        list.pushToBack(String("alpha"));
        list.pushToBack(String("beta, with comma"));  // would break a CSV encoding
        list.pushToBack(String(""));                  // empty element
        list.pushToBack(String("gamma"));
        Variant vlist(list);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vlist;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeStringList);
                StringList out = val.get<StringList>();
                REQUIRE(out.size() == 4);
                CHECK(out[0] == String("alpha"));
                CHECK(out[1] == String("beta, with comma"));
                CHECK(out[2].isEmpty());
                CHECK(out[3] == String("gamma"));
        }
}

TEST_CASE("DataStream: round-trip Variant empty StringList") {
        WriterFixture f;
        StringList list;
        Variant vlist(list);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vlist;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeStringList);
                CHECK(val.get<StringList>().size() == 0);
        }
}

TEST_CASE("DataStream: round-trip Variant ColorModel") {
        WriterFixture f;
        ColorModel cm(ColorModel::Rec709);
        Variant vcm(cm);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vcm;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeColorModel);
                ColorModel out = val.get<ColorModel>();
                CHECK(out.id() == ColorModel::Rec709);
        }
}

TEST_CASE("DataStream: round-trip Variant MemSpace") {
        WriterFixture f;
        MemSpace ms(MemSpace::SystemSecure);
        Variant vms(ms);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vms;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeMemSpace);
                MemSpace out = val.get<MemSpace>();
                CHECK(out.id() == MemSpace::SystemSecure);
        }
}

TEST_CASE("DataStream: round-trip Variant PixelFormat") {
        WriterFixture f;
        PixelFormat pf(PixelFormat::I_4x8);
        Variant vpf(pf);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vpf;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypePixelFormat);
                PixelFormat out = val.get<PixelFormat>();
                CHECK(out.id() == PixelFormat::I_4x8);
        }
}

TEST_CASE("DataStream: round-trip Variant PixelDesc") {
        WriterFixture f;
        PixelDesc pd(PixelDesc::RGB8_sRGB);
        Variant vpd(pd);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vpd;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypePixelDesc);
                PixelDesc out = val.get<PixelDesc>();
                CHECK(out.id() == PixelDesc::RGB8_sRGB);
        }
}

TEST_CASE("DataStream: round-trip Variant Timecode") {
        WriterFixture f;
        Timecode tc(Timecode::Mode(Timecode::NDF25), 10, 20, 30, 5);
        Variant vtc(tc);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vtc;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeTimecode);
                Timecode out = val.get<Timecode>();
                CHECK(out.hour() == 10);
                CHECK(out.min() == 20);
                CHECK(out.sec() == 30);
                CHECK(out.frame() == 5);
        }
}

TEST_CASE("DataStream: round-trip Variant Enum") {
        WriterFixture f;
        Variant vEnum(VideoPattern::Crosshatch);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vEnum;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeEnum);
                Enum out = val.get<Enum>();
                CHECK(out == VideoPattern::Crosshatch);
        }
}

TEST_CASE("DataStream: round-trip Variant DateTime preserves value") {
        // Previously the DateTime path lost sub-second precision because it
        // serialized via the default strftime format.  The new binary encoding
        // stores nanoseconds since epoch so the underlying time_point is
        // preserved exactly.
        WriterFixture f;
        DateTime dt = DateTime::now();
        Variant vdt(dt);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vdt;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant val;
                rs >> val;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(val.type() == Variant::TypeDateTime);
                CHECK(val.get<DateTime>() == dt);
        }
}

// ============================================================================
// Direct data-object operators (no Variant wrapper)
// ============================================================================

TEST_CASE("DataStream: direct round-trip UUID") {
        WriterFixture f;
        UUID id = UUID::generateV4();
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << id;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                UUID out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out == id);
        }
}

TEST_CASE("DataStream: direct round-trip UMID") {
        WriterFixture f;
        UMID id = UMID::generate(UMID::Extended);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << id;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                UMID out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.isValid());
                CHECK(out == id);
        }
}

TEST_CASE("DataStream: default-constructed UMID round-trip") {
        WriterFixture f;
        UMID id;  // invalid
        CHECK_FALSE(id.isValid());
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << id;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                UMID out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK_FALSE(out.isValid());
                CHECK(out == id);
        }
}

TEST_CASE("DataStream: direct round-trip DateTime") {
        WriterFixture f;
        DateTime dt = DateTime::now();
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << dt;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                DateTime out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out == dt);
        }
}

TEST_CASE("DataStream: direct round-trip TimeStamp") {
        WriterFixture f;
        TimeStamp ts = TimeStamp::now();
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << ts;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                TimeStamp out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.nanoseconds() == ts.nanoseconds());
        }
}

TEST_CASE("DataStream: direct round-trip Size2Du32") {
        WriterFixture f;
        Size2Du32 sz(3840, 2160);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << sz;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Size2Du32 out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.width() == 3840);
                CHECK(out.height() == 2160);
        }
}

TEST_CASE("DataStream: direct round-trip Rational") {
        WriterFixture f;
        Rational<int> r(24000, 1001);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << r;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Rational<int> out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.numerator() == 24000);
                CHECK(out.denominator() == 1001);
        }
}

TEST_CASE("DataStream: direct round-trip FrameRate") {
        WriterFixture f;
        FrameRate fr(FrameRate::RationalType(60000u, 1001u));
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << fr;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                FrameRate out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.numerator() == 60000u);
                CHECK(out.denominator() == 1001u);
        }
}

TEST_CASE("DataStream: direct round-trip Timecode") {
        WriterFixture f;
        Timecode tc(Timecode::Mode(Timecode::NDF30), 1, 2, 3, 4);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << tc;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Timecode out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.hour() == 1);
                CHECK(out.min() == 2);
                CHECK(out.sec() == 3);
                CHECK(out.frame() == 4);
        }
}

TEST_CASE("DataStream: direct round-trip Color") {
        WriterFixture f;
        Color c(10, 20, 30, 40);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << c;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Color out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.r8() == 10);
                CHECK(out.g8() == 20);
                CHECK(out.b8() == 30);
                CHECK(out.a8() == 40);
        }
}

TEST_CASE("DataStream: direct round-trip ColorModel") {
        WriterFixture f;
        ColorModel cm(ColorModel::LinearRec709);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << cm;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                ColorModel out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.id() == ColorModel::LinearRec709);
        }
}

TEST_CASE("DataStream: direct round-trip MemSpace") {
        WriterFixture f;
        MemSpace ms(MemSpace::System);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << ms;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                MemSpace out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.id() == MemSpace::System);
        }
}

TEST_CASE("DataStream: direct round-trip PixelFormat") {
        WriterFixture f;
        PixelFormat pf(PixelFormat::I_422_3x8);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << pf;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                PixelFormat out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.id() == PixelFormat::I_422_3x8);
        }
}

TEST_CASE("DataStream: direct round-trip PixelDesc") {
        WriterFixture f;
        PixelDesc pd(PixelDesc::BGR8_sRGB);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << pd;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                PixelDesc out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.id() == PixelDesc::BGR8_sRGB);
        }
}

TEST_CASE("DataStream: direct round-trip Enum") {
        WriterFixture f;
        Enum e = VideoPattern::ZonePlate;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << e;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Enum out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out == VideoPattern::ZonePlate);
        }
}

TEST_CASE("DataStream: direct round-trip StringList") {
        WriterFixture f;
        StringList list;
        list.pushToBack(String("one"));
        list.pushToBack(String("two, with comma"));
        list.pushToBack(String(""));
        list.pushToBack(String("four"));
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << list;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                StringList out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                REQUIRE(out.size() == 4);
                CHECK(out[0] == String("one"));
                CHECK(out[1] == String("two, with comma"));
                CHECK(out[2].isEmpty());
                CHECK(out[3] == String("four"));
        }
}

// ============================================================================
// Direct vs Variant interoperability
// ============================================================================

TEST_CASE("DataStream: direct write can be read as Variant") {
        WriterFixture f;
        UUID id = UUID::generateV4();
        Size2Du32 sz(1920, 1080);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << id << sz;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Variant a, b;
                rs >> a >> b;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(a.type() == Variant::TypeUUID);
                CHECK(a.get<UUID>() == id);
                CHECK(b.type() == Variant::TypeSize2D);
                CHECK(b.get<Size2Du32>() == sz);
        }
}

TEST_CASE("DataStream: Variant write can be read as direct type") {
        WriterFixture f;
        Color c(200, 150, 100, 255);
        Variant vc(c);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << vc;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Color out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.r8() == 200);
                CHECK(out.g8() == 150);
                CHECK(out.b8() == 100);
        }
}

// ============================================================================
// Buffer::Ptr round-trip (shared buffer)
// ============================================================================

TEST_CASE("DataStream: round-trip Buffer::Ptr") {
        WriterFixture f;
        Buffer::Ptr src = Buffer::Ptr::create(32);
        std::memset(src->data(), 0xCD, 32);
        src->setSize(32);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << src;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Buffer::Ptr out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                REQUIRE(out);
                CHECK(out->size() == 32);
                CHECK(static_cast<uint8_t *>(out->data())[0] == 0xCD);
                CHECK(static_cast<uint8_t *>(out->data())[31] == 0xCD);
        }
}

TEST_CASE("DataStream: null Buffer::Ptr round-trips as null") {
        WriterFixture f;
        Buffer::Ptr nullPtr;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << nullPtr;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Buffer::Ptr out = Buffer::Ptr::create(4); // pre-set to non-null
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK_FALSE(out); // remains null after read
        }
}

TEST_CASE("DataStream: null Buffer::Ptr is distinct from empty Buffer::Ptr") {
        WriterFixture f;
        Buffer::Ptr empty = Buffer::Ptr::create(0);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << empty;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Buffer::Ptr out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                // Empty-but-allocated Ptr reads back as a non-null Ptr
                REQUIRE(out);
        }
}

TEST_CASE("DataStream: direct Buffer read rejects null Buffer::Ptr wire form") {
        // A null Buffer::Ptr writes a TypeInvalid tag, which the direct
        // Buffer reader must not accept silently.
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << Buffer::Ptr();
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Buffer out;
                rs >> out;
                CHECK(rs.status() == DataStream::ReadCorruptData);
        }
}

// ============================================================================
// Container template operators
// ============================================================================

TEST_CASE("DataStream: round-trip List<int32_t>") {
        WriterFixture f;
        List<int32_t> list;
        list.pushToBack(1);
        list.pushToBack(-2);
        list.pushToBack(3);
        list.pushToBack(0x7FFFFFFF);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << list;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                List<int32_t> out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                REQUIRE(out.size() == 4);
                CHECK(out[0] == 1);
                CHECK(out[1] == -2);
                CHECK(out[2] == 3);
                CHECK(out[3] == 0x7FFFFFFF);
        }
}

TEST_CASE("DataStream: round-trip empty List") {
        WriterFixture f;
        List<String> empty;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << empty;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                List<String> out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.size() == 0);
        }
}

TEST_CASE("DataStream: round-trip List<UUID>") {
        WriterFixture f;
        List<UUID> ids;
        for(int i = 0; i < 5; ++i) ids.pushToBack(UUID::generateV4());
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << ids;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                List<UUID> out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                REQUIRE(out.size() == 5);
                for(int i = 0; i < 5; ++i) CHECK(out[i] == ids[i]);
        }
}

TEST_CASE("DataStream: round-trip Map<String,int32_t>") {
        WriterFixture f;
        Map<String, int32_t> map;
        map.insert(String("alpha"), 1);
        map.insert(String("beta"), 2);
        map.insert(String("gamma"), 3);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << map;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Map<String, int32_t> out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.size() == 3);
                CHECK(out.value(String("alpha"), -1) == 1);
                CHECK(out.value(String("beta"), -1) == 2);
                CHECK(out.value(String("gamma"), -1) == 3);
        }
}

TEST_CASE("DataStream: round-trip Set<String>") {
        WriterFixture f;
        Set<String> set;
        set.insert(String("red"));
        set.insert(String("green"));
        set.insert(String("blue"));
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << set;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Set<String> out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.size() == 3);
                CHECK(out.contains(String("red")));
                CHECK(out.contains(String("green")));
                CHECK(out.contains(String("blue")));
        }
}

TEST_CASE("DataStream: round-trip nested List<List<int32_t>>") {
        WriterFixture f;
        List<List<int32_t>> outer;
        {
                List<int32_t> a; a.pushToBack(1); a.pushToBack(2);
                outer.pushToBack(std::move(a));
        }
        {
                List<int32_t> b; b.pushToBack(3); b.pushToBack(4); b.pushToBack(5);
                outer.pushToBack(std::move(b));
        }
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << outer;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                List<List<int32_t>> out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                REQUIRE(out.size() == 2);
                REQUIRE(out[0].size() == 2);
                CHECK(out[0][0] == 1);
                CHECK(out[0][1] == 2);
                REQUIRE(out[1].size() == 3);
                CHECK(out[1][0] == 3);
                CHECK(out[1][2] == 5);
        }
}

TEST_CASE("DataStream: round-trip HashMap<String,int32_t>") {
        WriterFixture f;
        HashMap<String, int32_t> map;
        map.insert(String("width"), 1920);
        map.insert(String("height"), 1080);
        map.insert(String("fps"), 60);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << map;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                HashMap<String, int32_t> out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.size() == 3);
                CHECK(out.value(String("width"), -1) == 1920);
                CHECK(out.value(String("height"), -1) == 1080);
                CHECK(out.value(String("fps"), -1) == 60);
        }
}

TEST_CASE("DataStream: round-trip HashSet<int32_t>") {
        WriterFixture f;
        HashSet<int32_t> set;
        set.insert(10);
        set.insert(20);
        set.insert(30);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << set;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                HashSet<int32_t> out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.size() == 3);
                CHECK(out.contains(10));
                CHECK(out.contains(20));
                CHECK(out.contains(30));
        }
}

// ============================================================================
// Geometry templates
// ============================================================================

TEST_CASE("DataStream: round-trip Size2D<int32_t>") {
        WriterFixture f;
        Size2Di32 sz(-100, 200);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << sz;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Size2Di32 out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.width() == -100);
                CHECK(out.height() == 200);
        }
}

TEST_CASE("DataStream: Size2D<uint32_t> cannot be read as Size2D<int32_t>") {
        // The tagged inner values catch signed/unsigned mismatch.
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << Size2Du32(100, 200);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Size2Di32 out;
                rs >> out;
                CHECK(rs.status() == DataStream::ReadCorruptData);
        }
}

TEST_CASE("DataStream: round-trip Rect2Di32") {
        WriterFixture f;
        Rect2Di32 rect(10, 20, 640, 480);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << rect;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Rect2Di32 out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.x() == 10);
                CHECK(out.y() == 20);
                CHECK(out.width() == 640);
                CHECK(out.height() == 480);
        }
}

TEST_CASE("DataStream: round-trip Point2Df") {
        WriterFixture f;
        Point2Df p(3.5f, -2.25f);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << p;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Point2Df out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.x() == 3.5f);
                CHECK(out.y() == -2.25f);
        }
}

TEST_CASE("DataStream: round-trip Point3Di32") {
        WriterFixture f;
        Point3Di32 p(1, -2, 3);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << p;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Point3Di32 out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.x() == 1);
                CHECK(out.y() == -2);
                CHECK(out.z() == 3);
        }
}

TEST_CASE("DataStream: Point dimension mismatch is caught") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << Point3Di32(1, 2, 3);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Point2Di32 out;
                rs >> out;
                CHECK(rs.status() == DataStream::ReadCorruptData);
                CHECK(rs.errorContext().contains("Point dimension"));
        }
}

// ============================================================================
// JsonObject / JsonArray / Metadata
// ============================================================================

TEST_CASE("DataStream: round-trip JsonObject") {
        WriterFixture f;
        JsonObject obj;
        obj.set("name", String("clip001"));
        obj.set("width", 1920);
        obj.set("hdr", true);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << obj;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                JsonObject out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.getString("name") == String("clip001"));
                CHECK(out.getInt("width") == 1920);
                CHECK(out.getBool("hdr") == true);
        }
}

TEST_CASE("DataStream: round-trip JsonArray") {
        WriterFixture f;
        JsonArray arr;
        arr.add(String("a"));
        arr.add(String("b"));
        arr.add(42);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << arr;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                JsonArray out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.size() == 3);
                CHECK(out.getString(0) == String("a"));
                CHECK(out.getString(1) == String("b"));
                CHECK(out.getInt(2) == 42);
        }
}

TEST_CASE("DataStream: round-trip Metadata via VariantDatabase template") {
        WriterFixture f;
        Metadata meta;
        meta.set(Metadata::Title, String("My Clip"));
        meta.set(Metadata::FrameRate, Rational<int>(24000, 1001));
        meta.set(Metadata::FrameNumber, static_cast<int64_t>(42));
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << meta;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                Metadata out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.get(Metadata::Title).get<String>() == String("My Clip"));
                CHECK(out.get(Metadata::FrameNumber).get<int64_t>() == 42);
        }
}

// ============================================================================
// Result<T>-returning read API
// ============================================================================

TEST_CASE("DataStream: read<T>() returns Result on success") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<int32_t>(123);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                auto r = rs.read<int32_t>();
                CHECK(r.second().isOk());
                CHECK(r.first() == 123);
        }
}

TEST_CASE("DataStream: read<T>() returns Error on type mismatch") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint8_t>(0x42);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                auto r = rs.read<int32_t>();
                CHECK(r.second().isError());
                CHECK(r.second() == Error::CorruptData);
        }
}

TEST_CASE("DataStream: read<T>() works for data objects") {
        WriterFixture f;
        UUID id = UUID::generateV4();
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << id;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                auto [val, err] = rs.read<UUID>();
                CHECK(err.isOk());
                CHECK(val == id);
        }
}

// ============================================================================
// Error context
// ============================================================================

TEST_CASE("DataStream: type mismatch populates errorContext") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << static_cast<uint8_t>(0x01);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                int64_t val;
                rs >> val;
                CHECK(rs.status() == DataStream::ReadCorruptData);
                CHECK(!rs.errorContext().isEmpty());
                CHECK(rs.errorContext().contains("expected tag"));
        }
}

TEST_CASE("DataStream: resetStatus clears errorContext") {
        WriterFixture f;
        DataStream rs(&f.dev);
        rs.setError(DataStream::ReadPastEnd, String("boom"));
        CHECK(rs.status() == DataStream::ReadPastEnd);
        CHECK(rs.errorContext() == String("boom"));
        rs.resetStatus();
        CHECK(rs.status() == DataStream::Ok);
        CHECK(rs.errorContext().isEmpty());
}

TEST_CASE("DataStream: toError maps Status to Error codes") {
        WriterFixture f;
        DataStream rs(&f.dev);
        CHECK(rs.toError() == Error::Ok);
        rs.setError(DataStream::ReadPastEnd, String());
        CHECK(rs.toError() == Error::EndOfFile);
        rs.resetStatus();
        rs.setError(DataStream::ReadCorruptData, String());
        CHECK(rs.toError() == Error::CorruptData);
        rs.resetStatus();
        rs.setError(DataStream::WriteFailed, String());
        CHECK(rs.toError() == Error::IOError);
}

TEST_CASE("DataStream: setError preserves first error") {
        WriterFixture f;
        DataStream rs(&f.dev);
        rs.setError(DataStream::ReadPastEnd, String("first"));
        rs.setError(DataStream::ReadCorruptData, String("second"));
        CHECK(rs.status() == DataStream::ReadPastEnd);
        CHECK(rs.errorContext() == String("first"));
}

// ============================================================================
// Golden wire-format tests (pin down the exact byte layout)
// ============================================================================

// All golden tests below assume the 16-byte header so tag/payload offsets
// start at byte 16. The header layout itself is checked in the dedicated
// "writer writes header" test above.

TEST_CASE("DataStream golden: uint32_t big-endian exact bytes") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev, DataStream::BigEndian);
                ws << static_cast<uint32_t>(0xDEADBEEF);
        }
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        // Bytes 0-15: PMDS header (checked elsewhere)
        // Byte 16: TypeUInt32 tag (0x06)
        CHECK(raw[16] == 0x06);
        // Bytes 17-20: 0xDEADBEEF big-endian
        CHECK(raw[17] == 0xDE); CHECK(raw[18] == 0xAD);
        CHECK(raw[19] == 0xBE); CHECK(raw[20] == 0xEF);
        CHECK(f.dev.pos() == 21);
}

TEST_CASE("DataStream golden: String exact bytes") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev, DataStream::BigEndian);
                ws << String("hi");
        }
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        // Byte 16: TypeString tag (0x0C)
        CHECK(raw[16] == 0x0C);
        // Bytes 17-20: length = 2 (big-endian uint32)
        CHECK(raw[17] == 0x00); CHECK(raw[18] == 0x00);
        CHECK(raw[19] == 0x00); CHECK(raw[20] == 0x02);
        // Bytes 21-22: UTF-8 "hi"
        CHECK(raw[21] == 'h');
        CHECK(raw[22] == 'i');
        CHECK(f.dev.pos() == 23);
}

TEST_CASE("DataStream golden: UUID direct write exact bytes") {
        WriterFixture f;
        // Construct a UUID with a known byte pattern.
        UUID::DataFormat bytes;
        for(uint8_t i = 0; i < 16; ++i) bytes[i] = i;
        UUID id(bytes);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << id;
        }
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        // Byte 16: TypeUUID tag (0x10)
        CHECK(raw[16] == 0x10);
        // Bytes 17-32: raw UUID bytes
        for(int i = 0; i < 16; ++i) CHECK(raw[17 + i] == i);
        CHECK(f.dev.pos() == 33);
}

TEST_CASE("DataStream golden: Size2D<uint32_t> exact bytes") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev, DataStream::BigEndian);
                ws << Size2Du32(1920, 1080);
        }
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        // Byte 16: TypeSize2D tag (0x13)
        CHECK(raw[16] == 0x13);
        // Byte 17: TypeUInt32 tag (0x06) for width
        CHECK(raw[17] == 0x06);
        // Bytes 18-21: width = 1920 = 0x00000780 (big-endian)
        CHECK(raw[18] == 0x00); CHECK(raw[19] == 0x00);
        CHECK(raw[20] == 0x07); CHECK(raw[21] == 0x80);
        // Byte 22: TypeUInt32 tag (0x06) for height
        CHECK(raw[22] == 0x06);
        // Bytes 23-26: height = 1080 = 0x00000438 (big-endian)
        CHECK(raw[23] == 0x00); CHECK(raw[24] == 0x00);
        CHECK(raw[25] == 0x04); CHECK(raw[26] == 0x38);
        CHECK(f.dev.pos() == 27);
}

TEST_CASE("DataStream golden: Rational<int> exact bytes") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev, DataStream::BigEndian);
                ws << Rational<int>(24000, 1001);
        }
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        // Byte 16: TypeRational tag (0x14)
        CHECK(raw[16] == 0x14);
        // Byte 17: TypeInt32 tag (0x05) for numerator
        CHECK(raw[17] == 0x05);
        // Bytes 18-21: numerator = 24000 = 0x00005DC0 (big-endian)
        CHECK(raw[18] == 0x00); CHECK(raw[19] == 0x00);
        CHECK(raw[20] == 0x5D); CHECK(raw[21] == 0xC0);
        // Byte 22: TypeInt32 tag (0x05) for denominator
        CHECK(raw[22] == 0x05);
        // Bytes 23-26: denominator = 1001 = 0x000003E9 (big-endian)
        CHECK(raw[23] == 0x00); CHECK(raw[24] == 0x00);
        CHECK(raw[25] == 0x03); CHECK(raw[26] == 0xE9);
        CHECK(f.dev.pos() == 27);
}

TEST_CASE("DataStream golden: Invalid Variant exact bytes") {
        WriterFixture f;
        Variant invalid;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << invalid;
        }
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        // Byte 16: TypeInvalid tag (0x0E), no payload
        CHECK(raw[16] == 0x0E);
        CHECK(f.dev.pos() == 17);
}

TEST_CASE("DataStream golden: List<int32_t> exact bytes") {
        WriterFixture f;
        List<int32_t> list;
        list.pushToBack(1);
        list.pushToBack(2);
        {
                DataStream ws = DataStream::createWriter(&f.dev, DataStream::BigEndian);
                ws << list;
        }
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        // Byte 16: TypeList tag (0x20)
        CHECK(raw[16] == 0x20);
        // Bytes 17-21: count = 2 written as TypeUInt32 + big-endian value
        CHECK(raw[17] == 0x06); // TypeUInt32
        CHECK(raw[18] == 0x00); CHECK(raw[19] == 0x00);
        CHECK(raw[20] == 0x00); CHECK(raw[21] == 0x02);
        // Bytes 22-26: element 1 = int32(1) written as TypeInt32 + big-endian
        CHECK(raw[22] == 0x05); // TypeInt32
        CHECK(raw[23] == 0x00); CHECK(raw[24] == 0x00);
        CHECK(raw[25] == 0x00); CHECK(raw[26] == 0x01);
        // Bytes 27-31: element 2 = int32(2)
        CHECK(raw[27] == 0x05);
        CHECK(raw[28] == 0x00); CHECK(raw[29] == 0x00);
        CHECK(raw[30] == 0x00); CHECK(raw[31] == 0x02);
        CHECK(f.dev.pos() == 32);
}

TEST_CASE("DataStream golden: MemSpace exact bytes") {
        WriterFixture f;
        {
                DataStream ws = DataStream::createWriter(&f.dev, DataStream::BigEndian);
                ws << MemSpace(MemSpace::SystemSecure);
        }
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        // Byte 16: TypeMemSpace tag (0x19)
        CHECK(raw[16] == 0x19);
        // Byte 17: TypeUInt32 tag (0x06) for the ID
        CHECK(raw[17] == 0x06);
        // Bytes 18-21: ID = SystemSecure = 1 (big-endian)
        CHECK(raw[18] == 0x00); CHECK(raw[19] == 0x00);
        CHECK(raw[20] == 0x00); CHECK(raw[21] == 0x01);
        CHECK(f.dev.pos() == 22);
}

// ============================================================================
// XYZColor / AudioDesc / ImageDesc / MediaDesc
// ============================================================================

TEST_CASE("DataStream: round-trip XYZColor") {
        WriterFixture f;
        XYZColor col(0.3127, 0.3290, 0.3583);
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << col;
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                XYZColor out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.data()[0] == doctest::Approx(0.3127));
                CHECK(out.data()[1] == doctest::Approx(0.3290));
                CHECK(out.data()[2] == doctest::Approx(0.3583));
        }
}

TEST_CASE("DataStream: round-trip AudioDesc") {
        WriterFixture f;
        AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        desc.metadata().set(Metadata::Title, String("test track"));
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << desc;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                AudioDesc out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.dataType() == AudioDesc::PCMI_S16LE);
                CHECK(out.sampleRate() == 48000.0f);
                CHECK(out.channels() == 2);
                CHECK(out.metadata().get(Metadata::Title).get<String>() == String("test track"));
        }
}

TEST_CASE("DataStream: round-trip ImageDesc") {
        WriterFixture f;
        ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
        desc.setLinePad(16);
        desc.setLineAlign(64);
        desc.setInterlaceMode(InterlaceMode::InterlacedEvenFirst);
        desc.metadata().set(Metadata::Title, String("test frame"));
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << desc;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                ImageDesc out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.width() == 1920);
                CHECK(out.height() == 1080);
                CHECK(out.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
                CHECK(out.linePad() == 16);
                CHECK(out.lineAlign() == 64);
                CHECK(out.interlaceMode() == InterlaceMode::InterlacedEvenFirst);
                CHECK(out.metadata().get(Metadata::Title).get<String>() == String("test frame"));
        }
}

TEST_CASE("DataStream: round-trip MediaDesc") {
        WriterFixture f;
        MediaDesc desc;
        desc.setFrameRate(FrameRate(FrameRate::RationalType(24000u, 1001u)));
        desc.imageList().pushToBack(ImageDesc(1920, 1080, PixelDesc::RGBA8_sRGB));
        desc.audioList().pushToBack(AudioDesc(AudioDesc::PCMI_S24LE, 48000.0f, 6));
        desc.metadata().set(Metadata::Title, String("big clip"));
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << desc;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                MediaDesc out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(out.frameRate().numerator() == 24000u);
                CHECK(out.frameRate().denominator() == 1001u);
                REQUIRE(out.imageList().size() == 1);
                CHECK(out.imageList()[0].width() == 1920);
                CHECK(out.imageList()[0].height() == 1080);
                REQUIRE(out.audioList().size() == 1);
                CHECK(out.audioList()[0].dataType() == AudioDesc::PCMI_S24LE);
                CHECK(out.audioList()[0].channels() == 6);
                CHECK(out.metadata().get(Metadata::Title).get<String>() == String("big clip"));
        }
}

// ============================================================================
// List<Variant> / VariantList (generic template + Variant operators)
// ============================================================================

TEST_CASE("DataStream: round-trip VariantList") {
        WriterFixture f;
        VariantList list;
        list.pushToBack(Variant(static_cast<int32_t>(42)));
        list.pushToBack(Variant(String("hello")));
        list.pushToBack(Variant(true));
        list.pushToBack(Variant(3.14));
        list.pushToBack(Variant());  // invalid
        list.pushToBack(Variant(UUID::generateV4()));
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << list;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                VariantList out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                REQUIRE(out.size() == 6);
                CHECK(out[0].get<int32_t>() == 42);
                CHECK(out[1].get<String>() == String("hello"));
                CHECK(out[2].get<bool>() == true);
                CHECK(out[3].get<double>() == doctest::Approx(3.14));
                CHECK_FALSE(out[4].isValid());
                CHECK(out[5].type() == Variant::TypeUUID);
                CHECK(out[5].get<UUID>() == list[5].get<UUID>());
        }
}

// ============================================================================
// List<Buffer::Ptr>
// ============================================================================

TEST_CASE("DataStream: round-trip List<Buffer::Ptr> with shared buffers") {
        WriterFixture f;
        List<Buffer::Ptr> list;
        // Three distinct buffers with identifiable payloads.
        {
                Buffer::Ptr a = Buffer::Ptr::create(4);
                std::memset(a->data(), 0xAA, 4);
                a->setSize(4);
                list.pushToBack(std::move(a));
        }
        {
                Buffer::Ptr b = Buffer::Ptr::create(8);
                std::memset(b->data(), 0xBB, 8);
                b->setSize(8);
                list.pushToBack(std::move(b));
        }
        // A null entry in the middle to confirm null preservation inside
        // a container.
        list.pushToBack(Buffer::Ptr());
        {
                Buffer::Ptr c = Buffer::Ptr::create(2);
                std::memset(c->data(), 0xCC, 2);
                c->setSize(2);
                list.pushToBack(std::move(c));
        }
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws << list;
                CHECK(ws.status() == DataStream::Ok);
        }
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                List<Buffer::Ptr> out;
                rs >> out;
                CHECK(rs.status() == DataStream::Ok);
                REQUIRE(out.size() == 4);
                REQUIRE(out[0]);
                CHECK(out[0]->size() == 4);
                CHECK(static_cast<uint8_t *>(out[0]->data())[0] == 0xAA);
                REQUIRE(out[1]);
                CHECK(out[1]->size() == 8);
                CHECK(static_cast<uint8_t *>(out[1]->data())[0] == 0xBB);
                CHECK_FALSE(out[2]); // null preserved
                REQUIRE(out[3]);
                CHECK(out[3]->size() == 2);
                CHECK(static_cast<uint8_t *>(out[3]->data())[0] == 0xCC);
        }
}

// ============================================================================
// File round-trip via FileIODevice
// ============================================================================

TEST_CASE("DataStream: round-trip through FileIODevice") {
        // Use std::tmpfile() so the test owns a file handle that vanishes
        // when the process exits. We open it for read/write, write a few
        // values via a writer, rewind, and read them back with a reader.
        FILE *tmp = std::tmpfile();
        REQUIRE(tmp != nullptr);

        {
                FileIODevice dev(tmp, IODevice::ReadWrite);
                REQUIRE(dev.isOpen());
                DataStream ws = DataStream::createWriter(&dev);
                ws << static_cast<int32_t>(-12345);
                ws << String("round-trip through a real file");
                ws << Size2Du32(3840, 2160);
                ws << UUID::generateV4();
                CHECK(ws.status() == DataStream::Ok);
        }

        // Rewind via the underlying FILE*.
        std::rewind(tmp);

        UUID writtenId;
        {
                FileIODevice dev(tmp, IODevice::ReadWrite);
                REQUIRE(dev.isOpen());
                DataStream rs = DataStream::createReader(&dev);
                CHECK(rs.status() == DataStream::Ok);

                int32_t n = 0;
                String s;
                Size2Du32 sz;
                UUID id;
                rs >> n >> s >> sz >> id;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(n == -12345);
                CHECK(s == String("round-trip through a real file"));
                CHECK(sz == Size2Du32(3840, 2160));
                writtenId = id;
                CHECK(writtenId != UUID()); // got something back
        }

        std::fclose(tmp);
}
