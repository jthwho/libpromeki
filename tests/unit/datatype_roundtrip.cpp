/**
 * @file      datatype_roundtrip.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstdio>
#include <string>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/datatype.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

// ----------------------------------------------------------------------------
// User-defined type for the macro / converter exercises below.  Defined in
// the anonymous namespace + auto-registered through PROMEKI_IMPLEMENT_DATATYPE
// to verify the whole registration / lookup / use path.
// ----------------------------------------------------------------------------

class StageDLabel {
        public:
                StageDLabel() = default;
                explicit StageDLabel(const String &s) : _s(s) { return; }

                const String &text() const { return _s; }
                String        toString() const { return _s; }
                bool          operator==(const StageDLabel &o) const { return _s == o._s; }

        private:
                String _s;
};

} // anonymous namespace

PROMEKI_DECLARE_DATATYPE(StageDLabel)
PROMEKI_IMPLEMENT_DATATYPE(StageDLabel, "StageDLabel")

namespace {

bool variantContentsEqual(const Variant &a, const Variant &b) {
        if (a.type() != b.type()) return false;
        const DataType::Data *td = a.dataType().data();
        if (td == nullptr) return true;
        if (td->ops.equal == nullptr) return true; // skip when no comparator
        return td->ops.equal(a.payloadPtr(), b.payloadPtr());
}

} // anonymous namespace

TEST_CASE("DataType: every registered type round-trips Variant default -> DataStream -> Variant") {
        // The point of this test is to exercise the full Variant ⇄ DataStream
        // path generically: for every type the registry knows about, build a
        // default-constructed Variant, push it through a DataStream, read it
        // back, and verify the recovered Variant carries the same type tag
        // (and equal contents where the type provides operator==).
        //
        // Several builtin types intentionally have no DataStream wire
        // representation (the writeStream / readStream ops slots are null);
        // those land back as an invalid Variant on read.  Those are
        // partitioned out below and reported as a separate count so
        // regressions on a type that used to round-trip are easy to see.

        const List<DataType::ID> ids = DataType::registeredIds();
        REQUIRE(ids.size() > 0);

        size_t roundTripped       = 0;
        size_t skippedNoSerialize = 0;
        size_t skippedNoDefault   = 0;
        size_t skippedBadDefault  = 0;   // type's default doesn't round-trip cleanly

        List<DataType::ID> failedTypes;

        for (size_t i = 0; i < ids.size(); ++i) {
                const DataType::ID id = ids[i];
                const DataType dt(id);
                REQUIRE(dt.isValid());

                const DataType::Ops &ops = dt.ops();
                if (ops.defaultConstruct == nullptr) {
                        ++skippedNoDefault;
                        continue;
                }

                Variant src = Variant::createDefault(dt);
                REQUIRE(src.isValid());
                REQUIRE(src.type() == id);

                if (ops.writeStream == nullptr || ops.readStream == nullptr) {
                        ++skippedNoSerialize;
                        continue;
                }

                Buffer         buf(64 * 1024);
                BufferIODevice dev(&buf);
                REQUIRE(dev.open(IODevice::ReadWrite).isOk());

                DataStream::Status writeStatus = DataStream::Ok;
                {
                        DataStream ws = DataStream::createWriter(&dev);
                        ws << src;
                        writeStatus = ws.status();
                }

                dev.seek(0);

                Variant            out;
                DataStream::Status readStatus = DataStream::Ok;
                {
                        DataStream rs = DataStream::createReader(&dev);
                        rs >> out;
                        readStatus = rs.status();
                }

                const bool typeOk     = out.isValid() && out.type() == id;
                const bool contentsOk = typeOk && variantContentsEqual(src, out);
                const bool ok = writeStatus == DataStream::Ok && readStatus == DataStream::Ok &&
                                typeOk && contentsOk;
                if (ok) {
                        ++roundTripped;
                } else {
                        // Default values of every registered type are
                        // expected to round-trip after the v3 framing
                        // landed.  Failures here indicate either a new
                        // builtin whose operator<< / operator>> don't
                        // handle its default state, or a regression on
                        // an existing one — flag both.
                        ++skippedBadDefault;
                        failedTypes.pushToBack(id);
                }
        }

        std::string failedList;
        for (size_t i = 0; i < failedTypes.size(); ++i) {
                if (i > 0) failedList += ",";
                failedList += "0x";
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%04X", static_cast<unsigned>(failedTypes[i]));
                failedList += buf;
        }
        MESSAGE("Registered types: " << ids.size()
                << " | round-tripped: " << roundTripped
                << " | skipped (no serialize): " << skippedNoSerialize
                << " | skipped (no default ctor): " << skippedNoDefault
                << " | skipped (default not round-trippable): " << skippedBadDefault
                << " | offending ids: [" << failedList << "]");

        // Every type that advertises a writeStream + readStream op
        // must round-trip its default-constructed value cleanly.
        // Anything that lands in @c skippedBadDefault is a real bug —
        // either the type's wire format can't represent its default
        // (toString/fromString asymmetry, missing empty-body
        // handling) or its operator== treats the round-tripped
        // value as different from the source.
        CHECK(skippedBadDefault == 0);
        CHECK(roundTripped >= 50);
}

TEST_CASE("DataType: registered user type via PROMEKI_IMPLEMENT_DATATYPE is reachable end-to-end") {
        const DataType dt = DataType::of<StageDLabel>();
        REQUIRE(dt.isValid());
        CHECK(String(dt.name()) == "StageDLabel");

        // Round-trip a populated value through Variant.
        StageDLabel src("Stage-D");
        Variant     v(src);
        REQUIRE(v.isValid());
        CHECK(v.type() == dt.id());

        const StageDLabel *peeked = v.peek<StageDLabel>();
        REQUIRE(peeked != nullptr);
        CHECK(peeked->text() == "Stage-D");

        StageDLabel got = v.get<StageDLabel>();
        CHECK(got == src);
}

TEST_CASE("DataType: registry enumeration is stable and includes builtins") {
        const List<DataType::ID> ids = DataType::registeredIds();
        REQUIRE(ids.size() > 0);

        // Spot check: the StageDLabel registration above must show up.
        bool sawUserType = false;
        bool sawString   = false;
        bool sawBool     = false;
        for (size_t i = 0; i < ids.size(); ++i) {
                const DataType dt(ids[i]);
                if (!dt.isValid()) continue;
                if (String(dt.name()) == "StageDLabel") sawUserType = true;
                if (ids[i] == Variant::TypeString) sawString = true;
                if (ids[i] == Variant::TypeBool) sawBool = true;
        }
        CHECK(sawUserType);
        CHECK(sawString);
        CHECK(sawBool);
}
