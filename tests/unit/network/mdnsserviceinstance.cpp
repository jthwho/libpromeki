/**
 * @file      mdnsserviceinstance.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mdnsserviceinstance.h>
#include <promeki/variant.h>
#include <promeki/datastream.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>

using namespace promeki;

namespace {

        MdnsServiceInstance makeSample() {
                MdnsServiceInstance inst;
                inst.setInstanceName("Studio Camera 2");
                inst.setType(MdnsServiceType("ravenna", MdnsServiceType::Protocol::Tcp));
                inst.setHostname("camera2.local.");
                inst.setPort(9000);

                List<Ipv4Address> v4;
                v4 += Ipv4Address(192, 168, 1, 42);
                inst.setIpv4Addresses(v4);

                MdnsTxtRecord txt;
                txt.set("path", "/stream");
                txt.setKey("tls");
                inst.setTxt(txt);

                inst.setInterfaceIndex(3);
                return inst;
        }

} // namespace

TEST_CASE("MdnsServiceInstance: default is invalid") {
        MdnsServiceInstance inst;
        CHECK_FALSE(inst.isValid());
        CHECK(inst.fqdn().isEmpty());
        CHECK(inst.port() == 0);
        CHECK(inst.interfaceIndex() == MdnsServiceInstance::InvalidInterfaceIndex);
}

TEST_CASE("MdnsServiceInstance: isValid requires instanceName and type") {
        MdnsServiceInstance inst;
        inst.setInstanceName("foo");
        CHECK_FALSE(inst.isValid());          // type still invalid
        inst.setType(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        CHECK(inst.isValid());
        inst.setInstanceName(String());
        CHECK_FALSE(inst.isValid());          // empty name back to invalid
}

TEST_CASE("MdnsServiceInstance: fqdn composition") {
        MdnsServiceInstance inst;
        inst.setInstanceName("Studio Camera 2");
        inst.setType(MdnsServiceType("ravenna", MdnsServiceType::Protocol::Tcp));
        CHECK(inst.fqdn() == String("Studio Camera 2._ravenna._tcp.local."));
}

TEST_CASE("MdnsServiceInstance: fqdn empty when invalid") {
        MdnsServiceInstance inst;
        inst.setInstanceName("only-name");        // type still invalid
        CHECK(inst.fqdn().isEmpty());
}

TEST_CASE("MdnsServiceInstance: setters round-trip cleanly") {
        MdnsServiceInstance inst = makeSample();
        CHECK(inst.instanceName() == String("Studio Camera 2"));
        CHECK(inst.type().app() == String("ravenna"));
        CHECK(inst.hostname() == String("camera2.local."));
        CHECK(inst.port() == 9000);
        REQUIRE(inst.ipv4Addresses().size() == 1);
        CHECK(inst.ipv4Addresses()[0] == Ipv4Address(192, 168, 1, 42));
        CHECK(inst.txt().value("path") == String("/stream"));
        CHECK(inst.txt().presence("tls") == MdnsTxtRecord::Presence::KeyOnly);
        CHECK(inst.interfaceIndex() == 3);
}

TEST_CASE("MdnsServiceInstance: operator== compares identity (type + name) only") {
        MdnsServiceInstance a = makeSample();
        MdnsServiceInstance b = makeSample();
        CHECK(a == b);

        // Same identity, different state — still equal.
        b.setPort(9001);
        b.setHostname("other.local.");
        CHECK(a == b);
        CHECK_FALSE(a != b);

        // Different identity (instance name) — not equal.
        b.setInstanceName("Camera 99");
        CHECK_FALSE(a == b);
        CHECK(a != b);
}

TEST_CASE("MdnsServiceInstance: operator== is case-insensitive on instanceName") {
        MdnsServiceInstance a = makeSample();
        MdnsServiceInstance b = makeSample();
        b.setInstanceName("studio camera 2");   // upper-case in makeSample
        CHECK(a == b);
}

TEST_CASE("MdnsServiceInstance: hasSameContent reflects publisher-controlled fields") {
        MdnsServiceInstance a = makeSample();
        MdnsServiceInstance b = makeSample();
        CHECK(a.hasSameContent(b));

        b.setPort(9001);
        CHECK_FALSE(a.hasSameContent(b));

        b.setPort(9000);
        // Timing diverges — content stays the same.
        b.setLastSeen(TimeStamp::now());
        b.setTtl(Duration::fromSeconds(7));
        CHECK(a.hasSameContent(b));
}

TEST_CASE("MdnsServiceInstance: hasSameSnapshot is full-field including timing") {
        MdnsServiceInstance a = makeSample();
        MdnsServiceInstance b = makeSample();
        CHECK(a.hasSameSnapshot(b));

        b.setLastSeen(TimeStamp::now());
        CHECK_FALSE(a.hasSameSnapshot(b));
}

TEST_CASE("MdnsServiceInstance: Variant round-trip") {
        MdnsServiceInstance original = makeSample();
        Variant             v = original;
        CHECK(v.type() == DataTypeMdnsServiceInstance);

        MdnsServiceInstance retrieved = v.get<MdnsServiceInstance>();
        // Use hasSameSnapshot for round-trip checks — operator==
        // only compares identity now.
        CHECK(retrieved.hasSameSnapshot(original));
        CHECK(retrieved.fqdn() == original.fqdn());
}

TEST_CASE("MdnsServiceInstance: DataStream round-trip") {
        MdnsServiceInstance original = makeSample();

        Buffer              buf(8192);
        BufferIODevice      dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << original;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream          rs = DataStream::createReader(&dev);
                MdnsServiceInstance loaded;
                rs >> loaded;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(loaded.hasSameSnapshot(original));
                CHECK(loaded.fqdn() == original.fqdn());
        }
}

TEST_CASE("MdnsServiceInstance: toString includes key fields") {
        MdnsServiceInstance inst = makeSample();
        String              s = inst.toString();
        CHECK(s.find("Studio Camera 2") != String::npos);
        CHECK(s.find("ravenna") != String::npos);
        CHECK(s.find("9000")    != String::npos);
}
