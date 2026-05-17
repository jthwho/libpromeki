/**
 * @file      datatype.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/datatype.h>
#include <promeki/datastream.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/string.h>

using namespace promeki;

namespace {

// Minimal user-defined type covering all the concept-detected paths:
// member toString(), operator==, free DataStream operator<< / >>.
class SamplePoint {
        public:
                SamplePoint() = default;
                SamplePoint(int x, int y) : _x(x), _y(y) { return; }

                int x() const { return _x; }
                int y() const { return _y; }

                String toString() const { return String::number(_x) + "," + String::number(_y); }

                bool operator==(const SamplePoint &o) const { return _x == o._x && _y == o._y; }

        private:
                int _x = 0;
                int _y = 0;
};

// Canonical framed wire format: outer frame carries SamplePoint's
// registered DataTypeID so a Variant holding a SamplePoint
// round-trips through the v3 framing.
DataStream &operator<<(DataStream &s, const SamplePoint &p) {
        s.beginFrame(::promeki::DataType::of<SamplePoint>().id(), 1);
        s << static_cast<int32_t>(p.x());
        s << static_cast<int32_t>(p.y());
        s.endFrame();
        return s;
}

DataStream &operator>>(DataStream &s, SamplePoint &p) {
        if (!s.readFrame(::promeki::DataType::of<SamplePoint>().id())) {
                p = SamplePoint();
                return s;
        }
        int32_t x = 0;
        int32_t y = 0;
        s >> x;
        s >> y;
        p = SamplePoint(x, y);
        return s;
}

// Lighter type — no operator<< / operator>> available, so writeStream
// and readStream slots stay nullptr.  Used to verify negative concept
// detection.
class SampleLabel {
        public:
                SampleLabel() = default;
                explicit SampleLabel(const String &s) : _s(s) { return; }

                String toString() const { return _s; }
                bool   operator==(const SampleLabel &o) const { return _s == o._s; }

        private:
                String _s;
};

// Bare type — nothing implemented.  All optional ops stay nullptr.
class SampleBare {
        public:
                SampleBare() = default;
                int payload = 0;
};

} // anonymous namespace

PROMEKI_DECLARE_DATATYPE(SamplePoint)
PROMEKI_DECLARE_DATATYPE(SampleLabel)
PROMEKI_DECLARE_DATATYPE(SampleBare)

// SamplePoint serializes via a free operator<<; opt it in to
// auto-generation of the DataType writeStream / readStream slots.
// (SampleLabel and SampleBare deliberately leave the slots unset.)
namespace promeki { namespace Detail {
template <> struct HasFreeDataStreamWrite<SamplePoint> : std::true_type {};
template <> struct HasFreeDataStreamRead<SamplePoint>  : std::true_type {};
} }

// Auto-allocated user ID (drawn from DataTypeUserBegin range).
PROMEKI_IMPLEMENT_DATATYPE(SamplePoint, "SamplePoint")
// Pinned user ID — exercises the PROMEKI_IMPLEMENT_DATATYPE_ID path
// and lets us assert the wire-format ID survives across instantiations.
PROMEKI_IMPLEMENT_DATATYPE_ID(SampleLabel, "SampleLabel", 0xFFF0)
PROMEKI_IMPLEMENT_DATATYPE(SampleBare, "SampleBare")

TEST_CASE("DataType: registration round-trip via PROMEKI_IMPLEMENT_DATATYPE") {
        DataType dt = DataType::of<SamplePoint>();
        CHECK(dt.isValid());
        CHECK(String(dt.name()) == "SamplePoint");
        CHECK(dt.size() == sizeof(SamplePoint));
        CHECK(dt.alignment() == alignof(SamplePoint));
        CHECK(dt.cppType() == std::type_index(typeid(SamplePoint)));
        // Auto-allocated IDs must land in the user range.
        CHECK(static_cast<uint16_t>(dt.id()) >= static_cast<uint16_t>(DataTypeUserBegin));
        CHECK(static_cast<uint16_t>(dt.id()) <= static_cast<uint16_t>(DataTypeUserEnd));
}

TEST_CASE("DataType: PROMEKI_IMPLEMENT_DATATYPE_ID pins the wire-format ID") {
        DataType dt = DataType::of<SampleLabel>();
        REQUIRE(dt.isValid());
        CHECK(static_cast<uint16_t>(dt.id()) == 0xFFF0);
}

TEST_CASE("DataType: byId / byName lookup matches of<T>") {
        DataType expected = DataType::of<SamplePoint>();
        REQUIRE(expected.isValid());

        DataType byIdHandle = DataType::byId(expected.id());
        CHECK(byIdHandle == expected);

        DataType byNameHandle = DataType::byName("SamplePoint");
        CHECK(byNameHandle == expected);

        // Missing lookups return invalid handles.
        CHECK_FALSE(DataType::byId(static_cast<DataTypeID>(0x3FFE)).isValid());
        CHECK_FALSE(DataType::byName("NoSuchType").isValid());
}

TEST_CASE("DataType: default-constructed handle is invalid") {
        DataType dt;
        CHECK_FALSE(dt.isValid());
        CHECK(dt.id() == DataTypeInvalid);
        CHECK(String(dt.name()) == "Invalid");
        CHECK(dt.size() == 0);
        CHECK(dt.alignment() == 1);
}

TEST_CASE("DataType: concept-detected ops fire for types that satisfy them") {
        DataType dt = DataType::of<SamplePoint>();
        const DataType::Ops &ops = dt.ops();

        // Always-populated lifetime slots (SamplePoint is default-constructible).
        REQUIRE(ops.defaultConstruct != nullptr);
        REQUIRE(ops.copyConstruct != nullptr);
        REQUIRE(ops.moveConstruct != nullptr);
        REQUIRE(ops.destroy != nullptr);

        // Concept-detected slots.
        REQUIRE(ops.equal != nullptr);
        REQUIRE(ops.toString != nullptr);
        REQUIRE(ops.writeStream != nullptr);
        REQUIRE(ops.readStream != nullptr);

        // Exercise the lifecycle through the ops table.
        alignas(alignof(SamplePoint)) std::byte aBuf[sizeof(SamplePoint)];
        alignas(alignof(SamplePoint)) std::byte bBuf[sizeof(SamplePoint)];
        SamplePoint src(3, 4);

        ops.copyConstruct(&aBuf, &src);
        ops.copyConstruct(&bBuf, &src);

        CHECK(ops.equal(&aBuf, &bBuf));

        Error err = Error::Invalid;
        String s  = ops.toString(&aBuf, &err);
        CHECK(err.isOk());
        CHECK(s == "3,4");

        ops.destroy(&aBuf);
        ops.destroy(&bBuf);
}

TEST_CASE("DataType: writeStream / readStream round-trip via DataStream") {
        DataType dt = DataType::of<SamplePoint>();
        const DataType::Ops &ops = dt.ops();
        REQUIRE(ops.writeStream != nullptr);
        REQUIRE(ops.readStream != nullptr);

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        SamplePoint src(7, -11);
        {
                DataStream w = DataStream::createWriter(&dev, DataStream::LittleEndian);
                ops.writeStream(w, &src);
                CHECK(w.status() == DataStream::Ok);
        }
        dev.seek(0);

        alignas(alignof(SamplePoint)) std::byte recvBuf[sizeof(SamplePoint)];
        SamplePoint init;
        ops.copyConstruct(&recvBuf, &init);
        {
                DataStream r = DataStream::createReader(&dev);
                ops.readStream(r, &recvBuf);
                CHECK(r.status() == DataStream::Ok);
        }

        CHECK(ops.equal(&recvBuf, &src));
        ops.destroy(&recvBuf);
}

TEST_CASE("DataType: ops slots absent when concepts are unsatisfied") {
        DataType label = DataType::of<SampleLabel>();
        const DataType::Ops &lops = label.ops();
        CHECK(lops.equal != nullptr);
        CHECK(lops.toString != nullptr);
        CHECK(lops.writeStream == nullptr);
        CHECK(lops.readStream == nullptr);

        DataType bare = DataType::of<SampleBare>();
        const DataType::Ops &bops = bare.ops();
        CHECK(bops.equal == nullptr);
        CHECK(bops.toString == nullptr);
        CHECK(bops.writeStream == nullptr);
        CHECK(bops.readStream == nullptr);
        // Lifetime slots are still mandatory.
        CHECK(bops.copyConstruct != nullptr);
        CHECK(bops.moveConstruct != nullptr);
        CHECK(bops.destroy != nullptr);
}

TEST_CASE("DataType: rejected registrations return invalid handles") {
        // ID collision against the already-registered SampleLabel.
        struct Collider {};
        DataType clash = DataType::registerType<Collider>("Collider",
                                                          static_cast<DataTypeID>(0xFFF0));
        CHECK_FALSE(clash.isValid());

        // Empty name.
        struct AnotherCollider {};
        DataType empty = DataType::registerType<AnotherCollider>("");
        CHECK_FALSE(empty.isValid());
}

TEST_CASE("DataType: implicit ID -> DataType conversion via constructor") {
        DataType source = DataType::of<SamplePoint>();
        REQUIRE(source.isValid());

        DataType viaId(source.id());
        CHECK(viaId == source);

        DataType viaInvalid(DataTypeInvalid);
        CHECK_FALSE(viaInvalid.isValid());
}

TEST_CASE("DataType: defaultConstruct populates ops + drives Variant::createDefault") {
        DataType dt = DataType::of<SamplePoint>();
        REQUIRE(dt.isValid());
        REQUIRE(dt.ops().defaultConstruct != nullptr);

        Variant v = Variant::createDefault(dt);
        REQUIRE(v.isValid());
        CHECK(v.type() == dt.id());

        const SamplePoint *p = v.peek<SamplePoint>();
        REQUIRE(p != nullptr);
        CHECK(p->x() == 0);
        CHECK(p->y() == 0);
}

TEST_CASE("Variant::createDefault returns invalid when type is not registered or default-constructible") {
        Variant fromInvalid = Variant::createDefault(DataType());
        CHECK_FALSE(fromInvalid.isValid());
}
