/**
 * @file      mdnstxtrecord.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mdnstxtrecord.h>
#include <promeki/variant.h>
#include <promeki/datastream.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <cstring>

using namespace promeki;
using Presence = MdnsTxtRecord::Presence;

TEST_CASE("MdnsTxtRecord: default is empty") {
        MdnsTxtRecord t;
        CHECK(t.isEmpty());
        CHECK(t.count() == 0);
        CHECK_FALSE(t.contains("anything"));
        CHECK(t.presence("anything") == Presence::Absent);
        CHECK(t.value("anything") == String());
        CHECK(t.value("anything", "fallback") == String("fallback"));
}

TEST_CASE("MdnsTxtRecord: set / presence / value") {
        MdnsTxtRecord t;
        t.set("path", "/admin");
        t.set("version", "2.1");
        CHECK(t.count() == 2);
        CHECK(t.presence("path") == Presence::Present);
        CHECK(t.value("path") == String("/admin"));
        CHECK(t.value("version") == String("2.1"));
}

TEST_CASE("MdnsTxtRecord: setKey is distinct from setEmpty") {
        MdnsTxtRecord t;
        t.setKey("tls");
        t.setEmpty("flags");
        CHECK(t.presence("tls")   == Presence::KeyOnly);
        CHECK(t.presence("flags") == Presence::Empty);
        // Both surface as an empty value to the convenience accessor.
        CHECK(t.value("tls")   == String());
        CHECK(t.value("flags") == String());
}

TEST_CASE("MdnsTxtRecord: lookup is case-insensitive but preserves original casing") {
        MdnsTxtRecord t;
        t.set("Version", "1.0");
        CHECK(t.contains("version"));
        CHECK(t.contains("VERSION"));
        CHECK(t.value("version") == String("1.0"));
        // Iteration sees the original casing.
        StringList ks = t.keys();
        REQUIRE(ks.size() == 1);
        CHECK(ks[0] == String("Version"));

        // Re-setting only updates the value; the original casing wins.
        t.set("version", "1.1");
        ks = t.keys();
        REQUIRE(ks.size() == 1);
        CHECK(ks[0] == String("Version"));
        CHECK(t.value("Version") == String("1.1"));
}

TEST_CASE("MdnsTxtRecord: remove") {
        MdnsTxtRecord t;
        t.set("a", "1");
        t.set("b", "2");
        t.remove("A");                 // case-insensitive
        CHECK(t.count() == 1);
        CHECK_FALSE(t.contains("a"));
        CHECK(t.contains("b"));
        t.remove("missing");           // no-op
        CHECK(t.count() == 1);
}

TEST_CASE("MdnsTxtRecord: forEach iterates in registration order") {
        MdnsTxtRecord t;
        t.set("path", "/admin");
        t.setKey("tls");
        t.set("version", "2");

        StringList seenKeys;
        t.forEach([&](const String &key, Presence p, const String &v) {
                (void)p; (void)v;
                seenKeys += key;
        });
        REQUIRE(seenKeys.size() == 3);
        CHECK(seenKeys[0] == String("path"));
        CHECK(seenKeys[1] == String("tls"));
        CHECK(seenKeys[2] == String("version"));
}

TEST_CASE("MdnsTxtRecord: encode empty record emits single zero byte") {
        MdnsTxtRecord t;
        Buffer        wire = t.encode();
        REQUIRE(wire.size() == 1);
        CHECK(static_cast<const uint8_t *>(wire.data())[0] == 0);
}

TEST_CASE("MdnsTxtRecord: encode shapes match RFC 6763 §6") {
        MdnsTxtRecord t;
        t.set("a", "v");
        t.setEmpty("b");
        t.setKey("c");

        Buffer wire = t.encode();
        // "a=v" (3 bytes) "b=" (2 bytes) "c" (1 byte), each preceded
        // by a length byte → 1+3 + 1+2 + 1+1 = 9 bytes.
        REQUIRE(wire.size() == 9);
        const uint8_t *p = static_cast<const uint8_t *>(wire.data());
        CHECK(p[0] == 3);
        CHECK(std::memcmp(p + 1, "a=v", 3) == 0);
        CHECK(p[4] == 2);
        CHECK(std::memcmp(p + 5, "b=", 2) == 0);
        CHECK(p[7] == 1);
        CHECK(p[8] == 'c');
}

TEST_CASE("MdnsTxtRecord: decode the empty record (single zero byte)") {
        uint8_t bytes[1] = { 0 };
        auto r = MdnsTxtRecord::decode(bytes, sizeof(bytes));
        REQUIRE(r.second().isOk());
        CHECK(r.first().isEmpty());
}

TEST_CASE("MdnsTxtRecord: decode all three entry shapes") {
        // <3>"a=v" <2>"b=" <1>"c"
        uint8_t bytes[] = {
                3, 'a','=','v',
                2, 'b','=',
                1, 'c',
        };
        auto r = MdnsTxtRecord::decode(bytes, sizeof(bytes));
        REQUIRE(r.second().isOk());
        const MdnsTxtRecord &t = r.first();
        CHECK(t.count() == 3);
        CHECK(t.presence("a") == Presence::Present);
        CHECK(t.value("a")    == String("v"));
        CHECK(t.presence("b") == Presence::Empty);
        CHECK(t.presence("c") == Presence::KeyOnly);
}

TEST_CASE("MdnsTxtRecord: decode rejects truncated entry") {
        uint8_t bytes[] = { 4, 'a','b','c' };   // length 4 but only 3 bytes follow
        auto r = MdnsTxtRecord::decode(bytes, sizeof(bytes));
        CHECK_FALSE(r.second().isOk());
}

TEST_CASE("MdnsTxtRecord: decode skips zero-length padding entries") {
        uint8_t bytes[] = {
                0,                  // padding
                3, 'a','=','v',
                0,                  // padding
        };
        auto r = MdnsTxtRecord::decode(bytes, sizeof(bytes));
        REQUIRE(r.second().isOk());
        CHECK(r.first().count() == 1);
        CHECK(r.first().value("a") == String("v"));
}

TEST_CASE("MdnsTxtRecord: decode collapses duplicate keys (RFC 6763 §6.4)") {
        uint8_t bytes[] = {
                3, 'a','=','1',
                3, 'a','=','2',
        };
        auto r = MdnsTxtRecord::decode(bytes, sizeof(bytes));
        REQUIRE(r.second().isOk());
        CHECK(r.first().count() == 1);
        // First occurrence wins.
        CHECK(r.first().value("a") == String("1"));
}

TEST_CASE("MdnsTxtRecord: decode skips empty-key entry") {
        uint8_t bytes[] = {
                3, '=','v','x',           // key empty before '=', dropped
                3, 'a','=','1',
        };
        auto r = MdnsTxtRecord::decode(bytes, sizeof(bytes));
        REQUIRE(r.second().isOk());
        CHECK(r.first().count() == 1);
        CHECK(r.first().value("a") == String("1"));
}

TEST_CASE("MdnsTxtRecord: encode/decode round-trip") {
        MdnsTxtRecord t;
        t.set("path", "/admin");
        t.setKey("tls");
        t.setEmpty("flags");
        t.set("version", "2.1");

        Buffer        wire = t.encode();
        auto          r    = MdnsTxtRecord::decode(wire);
        REQUIRE(r.second().isOk());
        CHECK(r.first() == t);
        CHECK(r.first().keys() == t.keys());
}

TEST_CASE("MdnsTxtRecord: equality is order-sensitive but case-insensitive on keys") {
        MdnsTxtRecord a, b;
        a.set("x", "1");
        a.set("y", "2");
        b.set("X", "1");
        b.set("Y", "2");
        CHECK(a == b);

        MdnsTxtRecord c;
        c.set("y", "2");
        c.set("x", "1");
        CHECK_FALSE(a == c);  // order differs
}

TEST_CASE("MdnsTxtRecord: oversize value truncates at MaxEntryBytes") {
        MdnsTxtRecord t;
        String        bigValue(300, 'x');         // 300 bytes
        t.set("k", bigValue);
        Buffer wire = t.encode();
        REQUIRE(wire.size() > 0);
        // The first entry's length byte is what we read.  With the
        // "k=" prefix (2 bytes), the truncated entry is exactly 255
        // bytes on the wire — that means at most 253 value bytes.
        const uint8_t *p = static_cast<const uint8_t *>(wire.data());
        CHECK(p[0] == MdnsTxtRecord::MaxEntryBytes);
}

TEST_CASE("MdnsTxtRecord: Variant round-trip") {
        MdnsTxtRecord original;
        original.set("path", "/admin");
        original.setKey("tls");
        Variant       v = original;
        CHECK(v.type() == DataTypeMdnsTxtRecord);

        MdnsTxtRecord retrieved = v.get<MdnsTxtRecord>();
        CHECK(retrieved == original);
}

TEST_CASE("MdnsTxtRecord: DataStream round-trip") {
        MdnsTxtRecord original;
        original.set("path", "/admin");
        original.setKey("tls");
        original.setEmpty("flags");

        Buffer          buf(4096);
        BufferIODevice  dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << original;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream    rs = DataStream::createReader(&dev);
                MdnsTxtRecord loaded;
                rs >> loaded;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(loaded == original);
        }
}
