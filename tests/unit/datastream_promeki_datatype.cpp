/**
 * @file      datastream_promeki_datatype.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/datastream.h>
#include <promeki/datatype.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/string.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

// ============================================================================
// Throwaway type exercising the new PROMEKI_DATATYPE pattern
// ============================================================================
//
// Pinned to a user-range ID (0xFFE0) that no other test claims.  Bumps
// currentVersion to 2 so the test also covers the multi-version reader
// table — both V1 (legacy encoding) and V2 (current encoding) must
// round-trip through the framework operator<< / operator>> pair.
class SampleWidget {
        public:
                PROMEKI_DATATYPE(SampleWidget, 0xFFE0, 2)

                SampleWidget() = default;
                SampleWidget(int32_t x, int32_t y, const String &label)
                    : _x(x), _y(y), _label(label) { return; }

                int32_t x() const { return _x; }
                int32_t y() const { return _y; }
                const String &label() const { return _label; }

                bool operator==(const SampleWidget &o) const {
                        return _x == o._x && _y == o._y && _label == o._label;
                }

                // The PROMEKI_DATATYPE framework detects these the same
                // way it detects toString / fromString — the macro only
                // emits the trait struct; the body of writeToStream and
                // the readFromStream<V> specializations are user code.
                Error writeToStream(DataStream &s) const;
                template <uint32_t V> static Result<SampleWidget> readFromStream(DataStream &s);

        private:
                int32_t _x = 0;
                int32_t _y = 0;
                String  _label;
};

// V2 (current) wire body: x, y, label.  Matches the canonical layout
// the macro's currentVersion = 2 promises to emit.
Error SampleWidget::writeToStream(DataStream &s) const {
        s << _x;
        s << _y;
        s << _label;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<SampleWidget> SampleWidget::readFromStream<2>(DataStream &s) {
        int32_t x = 0;
        int32_t y = 0;
        String  label;
        s >> x;
        s >> y;
        s >> label;
        if (s.status() != DataStream::Ok) return makeError<SampleWidget>(s.toError());
        return makeResult(SampleWidget(x, y, label));
}

// V1 (legacy) wire body: x, y only — no label field, default-initialized
// on read.  Exists solely to prove the multi-version dispatch table
// routes correctly; not used by writeToStream (which always emits V2).
template <>
Result<SampleWidget> SampleWidget::readFromStream<1>(DataStream &s) {
        int32_t x = 0;
        int32_t y = 0;
        s >> x;
        s >> y;
        if (s.status() != DataStream::Ok) return makeError<SampleWidget>(s.toError());
        return makeResult(SampleWidget(x, y, String()));
}

} // anonymous namespace

TEST_CASE("PROMEKI_DATATYPE: metadata struct is populated") {
        CHECK(static_cast<uint16_t>(SampleWidget::promekiDataType::id) == 0xFFE0);
        CHECK(SampleWidget::promekiDataType::version == 2);
        CHECK(String(SampleWidget::promekiDataType::name) == "SampleWidget");

        // The self-registering static drives DataType::of<T> via the
        // global registry; both lookups must agree on the same record.
        DataType dt = DataType::of<SampleWidget>();
        REQUIRE(dt.isValid());
        CHECK(static_cast<uint16_t>(dt.id()) == 0xFFE0);
        CHECK(String(dt.name()) == "SampleWidget");
        CHECK(dt.version() == 2);
}

TEST_CASE("PROMEKI_DATATYPE: framework operator<< / operator>> round-trip") {
        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());

        SampleWidget src(7, -11, String("alpha"));
        {
                DataStream w = DataStream::createWriter(&dev, DataStream::LittleEndian);
                w << src;
                CHECK(w.status() == DataStream::Ok);
        }

        REQUIRE(dev.seek(0).isOk());
        SampleWidget dst;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> dst;
                CHECK(r.status() == DataStream::Ok);
        }
        CHECK(dst == src);
}

TEST_CASE("PROMEKI_DATATYPE: Variant payload round-trip") {
        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());

        SampleWidget src(42, 99, String("variant-carried"));
        Variant      vsrc(src);
        REQUIRE(vsrc.dataType() == DataType::of<SampleWidget>());

        {
                DataStream w = DataStream::createWriter(&dev, DataStream::BigEndian);
                w << vsrc;
                CHECK(w.status() == DataStream::Ok);
        }

        REQUIRE(dev.seek(0).isOk());
        Variant vdst;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> vdst;
                CHECK(r.status() == DataStream::Ok);
        }
        REQUIRE(vdst.dataType() == DataType::of<SampleWidget>());
        const SampleWidget *recovered = vdst.peek<SampleWidget>();
        REQUIRE(recovered != nullptr);
        CHECK(*recovered == src);
}

TEST_CASE("PROMEKI_DATATYPE: reader table routes by wire version") {
        // Manually emit a V1 frame (legacy two-int32 body) and read it
        // through the framework operator>>.  The reader table must
        // route to readFromStream<1>, which drops the label field.
        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());

        {
                DataStream w = DataStream::createWriter(&dev, DataStream::LittleEndian);
                w.beginFrame(SampleWidget::promekiDataType::id, 1);
                w << static_cast<int32_t>(123);
                w << static_cast<int32_t>(456);
                w.endFrame();
                CHECK(w.status() == DataStream::Ok);
        }

        REQUIRE(dev.seek(0).isOk());
        SampleWidget dst(0, 0, String("clobber-me"));
        {
                DataStream r = DataStream::createReader(&dev);
                r >> dst;
                CHECK(r.status() == DataStream::Ok);
        }
        CHECK(dst.x() == 123);
        CHECK(dst.y() == 456);
        // V1 has no label, so the V1 reader leaves it empty.
        CHECK(dst.label() == String());
}

TEST_CASE("PROMEKI_DATATYPE: unknown wire version is rejected") {
        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());

        {
                DataStream w = DataStream::createWriter(&dev, DataStream::LittleEndian);
                // Emit a SampleWidget frame at version 99 — outside the
                // 1..currentVersion range the macro built a reader for.
                w.beginFrame(SampleWidget::promekiDataType::id, 99);
                w << static_cast<int32_t>(0);
                w.endFrame();
                CHECK(w.status() == DataStream::Ok);
        }

        REQUIRE(dev.seek(0).isOk());
        SampleWidget dst(1, 2, String("preset"));
        {
                DataStream r = DataStream::createReader(&dev);
                r >> dst;
                CHECK(r.status() == DataStream::ReadCorruptData);
        }
        // Reader resets dst on error so callers don't see partial state.
        CHECK(dst == SampleWidget());
}

TEST_CASE("PROMEKI_DATATYPE: DataType ops wired through framework operators") {
        // The new concept path in makeDefaultOps must populate
        // writeStream / readStream slots even though SampleWidget has
        // no hand-rolled free operator<< / operator>>.
        DataType dt = DataType::of<SampleWidget>();
        REQUIRE(dt.isValid());
        const DataType::Ops &ops = dt.ops();
        CHECK(ops.writeStream != nullptr);
        CHECK(ops.readStream  != nullptr);
}
