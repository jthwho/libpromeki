/**
 * @file      datastream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <algorithm>
#include <doctest/doctest.h>
#include <promeki/core/datastream.h>
#include <promeki/core/bufferiodevice.h>
#include <promeki/core/buffer.h>
#include <promeki/core/string.h>
#include <promeki/core/variant.h>
#include <promeki/core/uuid.h>
#include <promeki/core/timecode.h>
#include <promeki/core/datetime.h>

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
        // Header should be 6 bytes: 4 magic + 2 version
        CHECK(f.dev.pos() == 6);

        // Verify raw bytes
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        CHECK(raw[0] == 0x50); // 'P'
        CHECK(raw[1] == 0x4D); // 'M'
        CHECK(raw[2] == 0x44); // 'D'
        CHECK(raw[3] == 0x53); // 'S'
        // Version 1 in big-endian
        CHECK(raw[4] == 0x00);
        CHECK(raw[5] == 0x01);
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
        // Write garbage instead of a valid header
        std::memset(buf.data(), 0xFF, 6);
        buf.setSize(6);

        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadOnly);
        DataStream rs = DataStream::createReader(&dev);
        CHECK(rs.status() == DataStream::ReadCorruptData);
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

        // Big-endian: 6-byte header + 1-byte type tag + 4-byte value
        WriterFixture f1;
        {
                DataStream ws = DataStream::createWriter(&f1.dev);
                ws.setByteOrder(DataStream::BigEndian);
                ws << testVal;
        }
        // After header(6) + tag(1), the 4 value bytes start at offset 7
        uint8_t *raw = static_cast<uint8_t *>(f1.buf.data());
        CHECK(raw[6] == DataStream::TypeUInt32);
        CHECK(raw[7] == 0x01);
        CHECK(raw[8] == 0x02);
        CHECK(raw[9] == 0x03);
        CHECK(raw[10] == 0x04);

        f1.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f1.dev);
                rs.setByteOrder(DataStream::BigEndian);
                uint32_t val;
                rs >> val;
                CHECK(val == 0x01020304);
        }

        // Little-endian
        WriterFixture f2;
        {
                DataStream ws = DataStream::createWriter(&f2.dev);
                ws.setByteOrder(DataStream::LittleEndian);
                ws << testVal;
        }
        raw = static_cast<uint8_t *>(f2.buf.data());
        CHECK(raw[6] == DataStream::TypeUInt32);
        CHECK(raw[7] == 0x04);
        CHECK(raw[8] == 0x03);
        CHECK(raw[9] == 0x02);
        CHECK(raw[10] == 0x01);

        f2.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f2.dev);
                rs.setByteOrder(DataStream::LittleEndian);
                uint32_t val;
                rs >> val;
                CHECK(val == 0x01020304);
        }
}

TEST_CASE("DataStream: byte order for 16-bit values") {
        WriterFixture f;
        uint16_t testVal = 0xABCD;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws.setByteOrder(DataStream::BigEndian);
                ws << testVal;
        }
        // header(6) + tag(1) = offset 7
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data());
        CHECK(raw[6] == DataStream::TypeUInt16);
        CHECK(raw[7] == 0xAB);
        CHECK(raw[8] == 0xCD);
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                rs.setByteOrder(DataStream::BigEndian);
                uint16_t val;
                rs >> val;
                CHECK(val == 0xABCD);
        }
}

TEST_CASE("DataStream: byte order for 64-bit values") {
        WriterFixture f;
        uint64_t testVal = 0x0102030405060708ULL;
        {
                DataStream ws = DataStream::createWriter(&f.dev);
                ws.setByteOrder(DataStream::BigEndian);
                ws << testVal;
        }
        // header(6) + tag(1) = offset 7
        uint8_t *raw = static_cast<uint8_t *>(f.buf.data()) + 7;
        for(int i = 0; i < 8; ++i) CHECK(raw[i] == static_cast<uint8_t>(i + 1));
        f.dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&f.dev);
                rs.setByteOrder(DataStream::BigEndian);
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
        Buffer buf(2); // too small for header
        BufferIODevice dev(&buf);
        dev.open(IODevice::WriteOnly);
        DataStream ds = DataStream::createWriter(&dev);
        // Header write should fail (6 bytes into 2-byte buffer)
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
        Buffer buf(8); // Only 8 bytes — header alone is 6
        BufferIODevice dev(&buf);
        dev.open(IODevice::WriteOnly);
        DataStream ws = DataStream::createWriter(&dev);
        // Header takes 6 bytes, leaving 2 bytes. Writing a uint32_t
        // (tag + 4 bytes = 5 bytes) should fail.
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
