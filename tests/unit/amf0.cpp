/**
 * @file      amf0.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <string>
#include <vector>
#include <promeki/amf0.h>
#include <promeki/buffer.h>

using namespace promeki;

namespace {

// Round-trip helper: serialize a value, parse it back, expect equal.
void roundTrip(const Amf0Value &v) {
        Buffer buf;
        Error  err = v.serialize(buf);
        REQUIRE(err.isOk());
        Result<Amf0Value::List> r = Amf0Reader::read(static_cast<const uint8_t *>(buf.data()), buf.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first()[0] == v);
}

// Quick byte-level inspector.
const uint8_t *bufBytes(const Buffer &b) { return static_cast<const uint8_t *>(b.data()); }

} // namespace

// ---- Construction / type predicates ----

TEST_CASE("Amf0Value: default is Null") {
        Amf0Value v;
        CHECK(v.type() == Amf0Value::Null);
        CHECK(v.isNull());
}

TEST_CASE("Amf0Value: Boolean") {
        Amf0Value t(true);
        Amf0Value f(false);
        CHECK(t.isBoolean());
        CHECK(t.asBool() == true);
        CHECK(f.asBool() == false);
}

TEST_CASE("Amf0Value: Number from int promotes to double") {
        Amf0Value v(42);
        CHECK(v.isNumber());
        CHECK(v.asNumber() == 42.0);
}

TEST_CASE("Amf0Value: String") {
        Amf0Value v("hello");
        CHECK(v.isString());
        CHECK(v.asString() == "hello");
}

TEST_CASE("Amf0Value: Undefined / Unsupported / Reference") {
        CHECK(Amf0Value::undefined().isUndefined());
        CHECK(Amf0Value::unsupported().isUnsupportedMarker());
        CHECK(Amf0Value::reference(7).isReference());
        CHECK(Amf0Value::reference(7).referenceIndex() == 7);
}

TEST_CASE("Amf0Value: Object factory + setField") {
        Amf0Value o = Amf0Value::object({{"a", 1}, {"b", "two"}});
        CHECK(o.isObject());
        CHECK(o.fields().size() == 2);
        CHECK(o.find("a") != nullptr);
        CHECK(o.find("a")->asNumber() == 1.0);
        CHECK(o.find("b")->asString() == "two");
}

TEST_CASE("Amf0Value: setField preserves insertion order") {
        Amf0Value o = Amf0Value::object();
        o.setField("zulu", 1);
        o.setField("alpha", 2);
        o.setField("mike", 3);
        REQUIRE(o.fields().size() == 3);
        CHECK(o.fields()[0].first == "zulu");
        CHECK(o.fields()[1].first == "alpha");
        CHECK(o.fields()[2].first == "mike");
}

TEST_CASE("Amf0Value: setField on existing key replaces in place") {
        Amf0Value o = Amf0Value::object();
        o.setField("a", 1);
        o.setField("b", 2);
        o.setField("a", 99);  // replace
        REQUIRE(o.fields().size() == 2);
        CHECK(o.fields()[0].first == "a");
        CHECK(o.fields()[0].second.asNumber() == 99.0);
        CHECK(o.fields()[1].first == "b");
}

TEST_CASE("Amf0Value: StrictArray") {
        Amf0Value a = Amf0Value::strictArray({1, 2, 3, "four"});
        CHECK(a.isStrictArray());
        REQUIRE(a.items().size() == 4);
        CHECK(a.items()[0].asNumber() == 1.0);
        CHECK(a.items()[3].asString() == "four");
}

TEST_CASE("Amf0Value: Date") {
        Amf0Value d = Amf0Value::date(1234567890.0, 0);
        CHECK(d.isDate());
        CHECK(d.dateMs() == 1234567890.0);
        CHECK(d.dateTimezone() == 0);
}

TEST_CASE("Amf0Value: TypedObject") {
        Amf0Value t = Amf0Value::typedObject("MyClass", {{"x", 1}});
        CHECK(t.isTypedObject());
        CHECK(t.className() == "MyClass");
        REQUIRE(t.fields().size() == 1);
}

TEST_CASE("Amf0Value: copy-on-write doesn't share mutations") {
        Amf0Value a = Amf0Value::object({{"a", 1}});
        Amf0Value b = a;  // refcount bump
        b.setField("a", 99);
        CHECK(a.find("a")->asNumber() == 1.0);
        CHECK(b.find("a")->asNumber() == 99.0);
}

// ---- Equality ----

TEST_CASE("Amf0Value: equality") {
        CHECK(Amf0Value() == Amf0Value());
        CHECK(Amf0Value(true) == Amf0Value(true));
        CHECK(Amf0Value(true) != Amf0Value(false));
        CHECK(Amf0Value("x") == Amf0Value("x"));
        CHECK(Amf0Value(1.5) == Amf0Value(1.5));
        CHECK(Amf0Value(1.5) != Amf0Value(2.5));
        CHECK(Amf0Value::object({{"k", 1}}) == Amf0Value::object({{"k", 1}}));
        CHECK(Amf0Value::object({{"k", 1}}) != Amf0Value::object({{"k", 2}}));
        // Order-sensitive: same fields, different order -> not equal.
        CHECK(Amf0Value::object({{"a", 1}, {"b", 2}}) != Amf0Value::object({{"b", 2}, {"a", 1}}));
}

// ---- Wire-format round-trip ----

TEST_CASE("Amf0: Null round-trip") {
        Buffer b;
        Amf0Value().serialize(b);
        REQUIRE(b.size() == 1);
        CHECK(bufBytes(b)[0] == Amf0Value::MarkerNull);
        roundTrip(Amf0Value());
}

TEST_CASE("Amf0: Boolean round-trip") {
        Buffer b;
        Amf0Value(true).serialize(b);
        REQUIRE(b.size() == 2);
        CHECK(bufBytes(b)[0] == Amf0Value::MarkerBoolean);
        CHECK(bufBytes(b)[1] == 0x01);
        roundTrip(Amf0Value(true));
        roundTrip(Amf0Value(false));
}

TEST_CASE("Amf0: Number round-trip") {
        roundTrip(Amf0Value(0.0));
        roundTrip(Amf0Value(1.0));
        roundTrip(Amf0Value(-3.14159));
        roundTrip(Amf0Value(1e300));
        roundTrip(Amf0Value(static_cast<int64_t>(1234567890LL)));
}

TEST_CASE("Amf0: String round-trip") {
        roundTrip(Amf0Value(""));
        roundTrip(Amf0Value("hello"));
        // Non-ASCII: build via fromUtf8 so the source string is the UTF-8
        // codepoint sequence the wire actually carries (not Latin-1 bytes).
        roundTrip(Amf0Value(promeki::String::fromUtf8("non-ASCII: \xc3\xa9", 13)));
}

TEST_CASE("Amf0: short String wire layout") {
        Buffer    b;
        Amf0Value("ab").serialize(b);
        REQUIRE(b.size() == 5);
        const uint8_t *p = bufBytes(b);
        CHECK(p[0] == Amf0Value::MarkerString);
        CHECK(p[1] == 0x00);
        CHECK(p[2] == 0x02);  // length = 2
        CHECK(p[3] == 'a');
        CHECK(p[4] == 'b');
}

TEST_CASE("Amf0: LongString promotion at >= 64 KiB") {
        promeki::String big(static_cast<size_t>(0x10000), 'x');  // exactly 64 KiB
        Amf0Value       v(big);
        Buffer          b;
        REQUIRE(v.serialize(b).isOk());
        const uint8_t *p = bufBytes(b);
        CHECK(p[0] == Amf0Value::MarkerLongString);
        // 4-byte length field follows.
        uint32_t len = (static_cast<uint32_t>(p[1]) << 24) | (static_cast<uint32_t>(p[2]) << 16) |
                       (static_cast<uint32_t>(p[3]) << 8) | static_cast<uint32_t>(p[4]);
        CHECK(len == 0x10000);
        // Round-trip parses back as a String value.
        Result<Amf0Value::List> r = Amf0Reader::read(p, b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first()[0].isString());
        CHECK(r.first()[0].asString().byteCount() == 0x10000);
}

TEST_CASE("Amf0: Object round-trip preserves field order") {
        Amf0Value o = Amf0Value::object({{"app", "live"}, {"tcUrl", "rtmp://h/live"}, {"flashVer", "FMLE/3.0"}});
        Buffer b;
        REQUIRE(o.serialize(b).isOk());
        Result<Amf0Value::List> r = Amf0Reader::read(bufBytes(b), b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        const Amf0Value &parsed = r.first()[0];
        REQUIRE(parsed.isObject());
        REQUIRE(parsed.fields().size() == 3);
        // Critically: order must match.
        CHECK(parsed.fields()[0].first == "app");
        CHECK(parsed.fields()[1].first == "tcUrl");
        CHECK(parsed.fields()[2].first == "flashVer");
        // Whole-tree equality.
        CHECK(parsed == o);
}

TEST_CASE("Amf0: Object terminator is 0x00 0x00 0x09") {
        Amf0Value      o = Amf0Value::object({{"k", "v"}});
        Buffer         b;
        REQUIRE(o.serialize(b).isOk());
        const uint8_t *p = bufBytes(b);
        // 1 marker + (2-byte len + 1 byte 'k') + (1 marker + 2-byte len + 1 byte 'v') + 3-byte terminator
        CHECK(p[0] == Amf0Value::MarkerObject);
        CHECK(p[b.size() - 3] == 0x00);
        CHECK(p[b.size() - 2] == 0x00);
        CHECK(p[b.size() - 1] == Amf0Value::MarkerObjectEnd);
}

TEST_CASE("Amf0: EcmaArray with count hint") {
        Amf0Value a = Amf0Value::ecmaArray({{"k1", 1}, {"k2", 2}}, 42);
        Buffer    b;
        REQUIRE(a.serialize(b).isOk());
        const uint8_t *p = bufBytes(b);
        CHECK(p[0] == Amf0Value::MarkerEcmaArray);
        // Count hint: 4-byte BE = 42 = 0x0000002a
        CHECK(p[1] == 0x00);
        CHECK(p[2] == 0x00);
        CHECK(p[3] == 0x00);
        CHECK(p[4] == 0x2A);
        // Round-trip preserves the hint.
        Result<Amf0Value::List> r = Amf0Reader::read(p, b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first()[0].isEcmaArray());
        CHECK(r.first()[0].ecmaCountHint() == 42);
        CHECK(r.first()[0] == a);
}

TEST_CASE("Amf0: StrictArray round-trip preserves order") {
        Amf0Value a = Amf0Value::strictArray({1, 2, 3, "four", true});
        roundTrip(a);
}

TEST_CASE("Amf0: Date round-trip carries timezone") {
        roundTrip(Amf0Value::date(0.0, 0));
        roundTrip(Amf0Value::date(1234567890123.0, 0));
        // Non-zero timezone is malformed-per-FMS but we round-trip it for fidelity.
        roundTrip(Amf0Value::date(1.0, 60));
}

TEST_CASE("Amf0: XmlDocument round-trip uses LongString-style 32-bit length") {
        Amf0Value      v = Amf0Value::xmlDocument("<x>hi</x>");
        Buffer         b;
        REQUIRE(v.serialize(b).isOk());
        const uint8_t *p = bufBytes(b);
        CHECK(p[0] == Amf0Value::MarkerXmlDocument);
        CHECK(p[1] == 0x00);
        CHECK(p[2] == 0x00);
        CHECK(p[3] == 0x00);
        CHECK(p[4] == 0x09);
        roundTrip(v);
}

TEST_CASE("Amf0: TypedObject round-trip carries class name") {
        Amf0Value t = Amf0Value::typedObject("com.example.MyClass", {{"x", 1.5}, {"y", -2.5}});
        roundTrip(t);
}

TEST_CASE("Amf0: Reference value round-trip") {
        roundTrip(Amf0Value::reference(0));
        roundTrip(Amf0Value::reference(0xFFFF));
}

TEST_CASE("Amf0: Unsupported value round-trip") {
        roundTrip(Amf0Value::unsupported());
}

TEST_CASE("Amf0: Undefined value round-trip") {
        roundTrip(Amf0Value::undefined());
}

TEST_CASE("Amf0: nested Object inside Object round-trip") {
        Amf0Value inner = Amf0Value::object({{"x", 1}});
        Amf0Value outer = Amf0Value::object({{"inner", inner}, {"flag", true}});
        roundTrip(outer);
}

TEST_CASE("Amf0: nested StrictArray inside Object round-trip") {
        Amf0Value v = Amf0Value::object(
            {{"list", Amf0Value::strictArray({1, 2, 3})}, {"name", "foo"}});
        roundTrip(v);
}

// ---- Real-world RTMP command shapes ----

TEST_CASE("Amf0: RTMP connect body shape parses cleanly") {
        // Mirror the layout an RTMP client emits for `connect`:
        //   string  "connect"
        //   number  1.0  (transactionId)
        //   object  { app, type, flashVer, tcUrl, fpad, capabilities,
        //             audioCodecs, videoCodecs, videoFunction, objectEncoding }
        Amf0Value cmd("connect");
        Amf0Value txn(1.0);
        Amf0Value body = Amf0Value::object({{"app", "live"},
                                            {"type", "nonprivate"},
                                            {"flashVer", "FMLE/3.0 (compatible; libpromeki/1.0)"},
                                            {"tcUrl", "rtmp://example.com/live"},
                                            {"fpad", false},
                                            {"capabilities", 239.0},
                                            {"audioCodecs", 4071.0},
                                            {"videoCodecs", 252.0},
                                            {"videoFunction", 1.0},
                                            {"objectEncoding", 0.0}});
        Buffer b;
        REQUIRE(cmd.serialize(b).isOk());
        REQUIRE(txn.serialize(b).isOk());
        REQUIRE(body.serialize(b).isOk());

        Result<Amf0Value::List> r = Amf0Reader::read(bufBytes(b), b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 3);
        CHECK(r.first()[0] == cmd);
        CHECK(r.first()[1] == txn);
        CHECK(r.first()[2] == body);
        // Specifically verify field order survives.
        CHECK(r.first()[2].fields()[0].first == "app");
        CHECK(r.first()[2].fields()[9].first == "objectEncoding");
}

TEST_CASE("Amf0: onStatus body shape parses cleanly") {
        // Mirror the layout an RTMP server emits for `onStatus`:
        //   string  "onStatus"
        //   number  0.0
        //   null
        //   object  { level, code, description }
        Amf0Value cmd("onStatus");
        Amf0Value txn(0.0);
        Amf0Value nullArg;
        Amf0Value info = Amf0Value::object(
            {{"level", "status"}, {"code", "NetStream.Publish.Start"}, {"description", "Started publishing"}});

        Buffer b;
        cmd.serialize(b);
        txn.serialize(b);
        nullArg.serialize(b);
        info.serialize(b);

        Result<Amf0Value::List> r = Amf0Reader::read(bufBytes(b), b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 4);
        CHECK(r.first()[0].asString() == "onStatus");
        CHECK(r.first()[2].isNull());
        CHECK(r.first()[3].find("code")->asString() == "NetStream.Publish.Start");
}

// ---- Error paths ----

TEST_CASE("Amf0Reader: empty input yields empty list") {
        Result<Amf0Value::List> r = Amf0Reader::read(nullptr, 0);
        CHECK(r.second().isOk());
        CHECK(r.first().isEmpty());
}

TEST_CASE("Amf0Reader: truncated number returns OutOfRange") {
        const uint8_t bytes[] = {Amf0Value::MarkerNumber, 0x00};
        Result<Amf0Value::List> r = Amf0Reader::read(bytes, sizeof(bytes));
        CHECK(r.second() == Error::OutOfRange);
}

TEST_CASE("Amf0Reader: truncated string returns OutOfRange") {
        const uint8_t bytes[] = {Amf0Value::MarkerString, 0x00, 0x05, 'a', 'b'};  // claim 5 bytes, give 2
        Result<Amf0Value::List> r = Amf0Reader::read(bytes, sizeof(bytes));
        CHECK(r.second() == Error::OutOfRange);
}

TEST_CASE("Amf0Reader: object missing terminator returns OutOfRange") {
        // Object with one kv but no end-of-object sentinel.
        const uint8_t bytes[] = {Amf0Value::MarkerObject, 0x00, 0x01, 'k', Amf0Value::MarkerNull};
        Result<Amf0Value::List> r = Amf0Reader::read(bytes, sizeof(bytes));
        CHECK(r.second() == Error::OutOfRange);
}

TEST_CASE("Amf0Reader: empty key without ObjectEnd marker is CorruptData") {
        // Object body: empty key followed by a non-ObjectEnd byte.
        const uint8_t bytes[] = {Amf0Value::MarkerObject, 0x00, 0x00, 0x99};
        Result<Amf0Value::List> r = Amf0Reader::read(bytes, sizeof(bytes));
        CHECK(r.second() == Error::CorruptData);
}

TEST_CASE("Amf0Reader: AMF3 switch marker returns NotSupported") {
        const uint8_t bytes[] = {Amf0Value::MarkerAvmPlusObject};
        Result<Amf0Value::List> r = Amf0Reader::read(bytes, sizeof(bytes));
        CHECK(r.second() == Error::NotSupported);
}

TEST_CASE("Amf0Reader: unknown marker returns CorruptData") {
        const uint8_t bytes[] = {0x99};
        Result<Amf0Value::List> r = Amf0Reader::read(bytes, sizeof(bytes));
        CHECK(r.second() == Error::CorruptData);
}

TEST_CASE("Amf0Reader: bare ObjectEnd at value position is CorruptData") {
        const uint8_t bytes[] = {Amf0Value::MarkerObjectEnd};
        Result<Amf0Value::List> r = Amf0Reader::read(bytes, sizeof(bytes));
        CHECK(r.second() == Error::CorruptData);
}

// ---- Amf0Writer streaming ----

TEST_CASE("Amf0Writer: writeNumber/writeString match Amf0Value::serialize") {
        Buffer     a;
        Amf0Writer w(a);
        REQUIRE(w.writeString("connect").isOk());
        REQUIRE(w.writeNumber(1.0).isOk());

        Buffer ref;
        REQUIRE(Amf0Value("connect").serialize(ref).isOk());
        REQUIRE(Amf0Value(1.0).serialize(ref).isOk());

        REQUIRE(a.size() == ref.size());
        CHECK(std::memcmp(a.data(), ref.data(), a.size()) == 0);
}

TEST_CASE("Amf0Writer: writeObject round-trips through reader") {
        Buffer     b;
        Amf0Writer w(b);
        Amf0Value::FieldList fl;
        fl.pushToBack(Amf0Value::Field("a", 1));
        fl.pushToBack(Amf0Value::Field("b", "two"));
        REQUIRE(w.writeObject(fl).isOk());

        Result<Amf0Value::List> r = Amf0Reader::read(bufBytes(b), b.size());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first()[0] == Amf0Value::object({{"a", 1}, {"b", "two"}}));
}

TEST_CASE("Amf0Writer: bytesWritten tracks output size") {
        Buffer     b;
        Amf0Writer w(b);
        REQUIRE(w.writeBoolean(true).isOk());
        CHECK(w.bytesWritten() == 2);
        REQUIRE(w.writeNumber(0.0).isOk());
        CHECK(w.bytesWritten() == 2 + 9);
}

TEST_CASE("Amf0Writer: appending grows the underlying buffer") {
        Buffer     b;
        Amf0Writer w(b);
        // Force a buffer growth by writing a long string > 256 bytes.
        promeki::String s(static_cast<size_t>(1000), 'x');
        REQUIRE(w.writeString(s).isOk());
        // Marker + 2-byte len + 1000 bytes = 1003.
        CHECK(b.size() == 1003);
        Result<Amf0Value::List> r = Amf0Reader::read(bufBytes(b), b.size());
        REQUIRE(r.second().isOk());
        CHECK(r.first()[0].asString().byteCount() == 1000);
}
