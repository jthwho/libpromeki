/**
 * @file      sdioutputfanoutconfig.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_CORE

#include <doctest/doctest.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/enums_video.h>
#include <promeki/sdioutputfanoutconfig.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/videoportref.h>

using namespace promeki;

namespace {

        VideoPortRef sdi(int idx) { return VideoPortRef(VideoConnectorKind::Sdi, idx); }

} // namespace

TEST_CASE("SdiOutputFanoutConfig: round-trips a single-link config with one destination") {
        SdiOutputFanoutConfig::PortList group;
        group.pushToBack(sdi(1));
        SdiOutputFanoutConfig::GroupList groups;
        groups.pushToBack(group);
        SdiOutputFanoutConfig cfg(SdiLinkStandard::SL_3GA, groups);

        REQUIRE(cfg.isValid());
        CHECK(cfg.groupCount() == 1);
        const String s = cfg.toString();
        CHECK(s == String("sl_3ga:sdi1"));

        Result<SdiOutputFanoutConfig> rt = SdiOutputFanoutConfig::fromString(s);
        REQUIRE(rt.second().isOk());
        CHECK(rt.first() == cfg);
        CHECK(rt.first().primary().standard() == SdiLinkStandard::SL_3GA);
        REQUIRE(rt.first().primary().ports().size() == 1);
        CHECK(rt.first().primary().ports().at(0) == sdi(1));
}

TEST_CASE("SdiOutputFanoutConfig: parses single-link with multiple destinations") {
        // The user's "this is the format" example: one source out
        // three SDI ports.
        Result<SdiOutputFanoutConfig> rt =
                SdiOutputFanoutConfig::fromString(String("sl_hd:sdi1,sdi2,sdi3"));
        REQUIRE(rt.second().isOk());
        const SdiOutputFanoutConfig &cfg = rt.first();
        CHECK(cfg.standard() == SdiLinkStandard::SL_HD);
        CHECK(cfg.isValid());
        REQUIRE(cfg.groupCount() == 3);
        CHECK(cfg.groups().at(0).at(0) == sdi(1));
        CHECK(cfg.groups().at(1).at(0) == sdi(2));
        CHECK(cfg.groups().at(2).at(0) == sdi(3));

        // Round-trip back through toString.
        CHECK(cfg.toString() == String("sl_hd:sdi1,sdi2,sdi3"));

        // asSignalConfigs splits the fanout into per-destination
        // SdiSignalConfig instances (each gets the same standard).
        const List<SdiSignalConfig> sigs = cfg.asSignalConfigs();
        REQUIRE(sigs.size() == 3);
        for (size_t i = 0; i < sigs.size(); ++i) {
                CHECK(sigs.at(i).standard() == SdiLinkStandard::SL_HD);
                REQUIRE(sigs.at(i).ports().size() == 1);
        }
}

TEST_CASE("SdiOutputFanoutConfig: parses dual-link with two destination groups") {
        // The user's stated string form: standard:portA+portB,portC+portD.
        Result<SdiOutputFanoutConfig> rt =
                SdiOutputFanoutConfig::fromString(String("dl_3g:sdi1+sdi2,sdi3+sdi4"));
        REQUIRE(rt.second().isOk());
        const SdiOutputFanoutConfig &cfg = rt.first();
        CHECK(cfg.standard() == SdiLinkStandard::DL_3G);
        CHECK(cfg.isValid()); // two ports per group, matches DL cable count.
        REQUIRE(cfg.groupCount() == 2);
        REQUIRE(cfg.groups().at(0).size() == 2);
        REQUIRE(cfg.groups().at(1).size() == 2);
        CHECK(cfg.groups().at(0).at(0) == sdi(1));
        CHECK(cfg.groups().at(0).at(1) == sdi(2));
        CHECK(cfg.groups().at(1).at(0) == sdi(3));
        CHECK(cfg.groups().at(1).at(1) == sdi(4));

        CHECK(cfg.toString() == String("dl_3g:sdi1+sdi2,sdi3+sdi4"));
}

TEST_CASE("SdiOutputFanoutConfig: parses quad-link 2SI with two destination groups") {
        Result<SdiOutputFanoutConfig> rt = SdiOutputFanoutConfig::fromString(
                String("ql_3g_2si:sdi1+sdi2+sdi3+sdi4,sdi5+sdi6+sdi7+sdi8"));
        REQUIRE(rt.second().isOk());
        const SdiOutputFanoutConfig &cfg = rt.first();
        CHECK(cfg.standard() == SdiLinkStandard::QL_3G_2SI);
        CHECK(cfg.isValid()); // 4 ports per group, matches QL cable count.
        REQUIRE(cfg.groupCount() == 2);
        REQUIRE(cfg.groups().at(0).size() == 4);
        REQUIRE(cfg.groups().at(1).size() == 4);
}

TEST_CASE("SdiOutputFanoutConfig: rejects groups with the wrong cable count for the standard") {
        // DL_3G demands 2 cables per group; "sdi1+sdi2+sdi3" gives
        // three.  fromString parses successfully (purely syntactic),
        // but isValid catches the semantic mismatch.
        Result<SdiOutputFanoutConfig> rt =
                SdiOutputFanoutConfig::fromString(String("dl_3g:sdi1+sdi2+sdi3,sdi4+sdi5"));
        REQUIRE(rt.second().isOk());
        CHECK_FALSE(rt.first().isValid());
}

TEST_CASE("SdiOutputFanoutConfig: rejects malformed input strings") {
        CHECK(SdiOutputFanoutConfig::fromString(String("")).second().isError());
        // Trailing comma — empty group.
        CHECK(SdiOutputFanoutConfig::fromString(String("sl_hd:sdi1,")).second().isError());
        // Doubled comma — empty middle group.
        CHECK(SdiOutputFanoutConfig::fromString(String("sl_hd:sdi1,,sdi2")).second().isError());
        // Trailing '+' inside a group.
        CHECK(SdiOutputFanoutConfig::fromString(String("dl_3g:sdi1+,sdi2+sdi3")).second().isError());
        // Unknown standard.
        CHECK(SdiOutputFanoutConfig::fromString(String("bogus_std:sdi1")).second().isError());
        // Bad port spec.
        CHECK(SdiOutputFanoutConfig::fromString(String("sl_hd:not_a_port")).second().isError());
}

TEST_CASE("SdiOutputFanoutConfig: empty config is invalid and serializes to the bare standard") {
        SdiOutputFanoutConfig cfg;
        CHECK_FALSE(cfg.isValid());
        CHECK(cfg.groupCount() == 0);
        CHECK(cfg.toString() == String("auto"));
}

TEST_CASE("SdiOutputFanoutConfig: primary() returns a default SdiSignalConfig for an empty config") {
        SdiOutputFanoutConfig cfg;
        SdiSignalConfig       p = cfg.primary();
        CHECK(p.ports().isEmpty());
}

TEST_CASE("SdiOutputFanoutConfig: DataStream round-trip preserves the full group structure") {
        // Build a dual-link fanout with two groups, each two ports.
        Result<SdiOutputFanoutConfig> orig =
                SdiOutputFanoutConfig::fromString(String("dl_3g:sdi1+sdi2,sdi3+sdi4"));
        REQUIRE(orig.second().isOk());

        // Encode to a buffer.
        Buffer buf(512);
        REQUIRE(buf.isValid());
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        REQUIRE(dev.isOpen());
        DataStream writer(&dev);
        writer.setByteOrder(DataStream::LittleEndian);
        CHECK(orig.first().writeToStream(writer).isOk());
        CHECK(dev.pos() > 0);

        // Seek back to beginning and decode.
        dev.seek(0);
        DataStream reader(&dev);
        reader.setByteOrder(DataStream::LittleEndian);
        Result<SdiOutputFanoutConfig> decoded =
                SdiOutputFanoutConfig::readFromStream<1>(reader);
        REQUIRE(decoded.second().isOk());
        CHECK(decoded.first() == orig.first());
        CHECK(decoded.first().standard() == SdiLinkStandard::DL_3G);
        REQUIRE(decoded.first().groupCount() == 2);
        REQUIRE(decoded.first().groups().at(0).size() == 2);
        REQUIRE(decoded.first().groups().at(1).size() == 2);
}

TEST_CASE("SdiOutputFanoutConfig::fromSignal: wraps a populated signal as a 1-group fanout") {
        SdiSignalConfig sig = SdiSignalConfig::dualLink(SdiLinkStandard::DL_3G, sdi(1), sdi(2));
        SdiOutputFanoutConfig fan = SdiOutputFanoutConfig::fromSignal(sig);

        CHECK(fan.standard() == SdiLinkStandard::DL_3G);
        REQUIRE(fan.groupCount() == 1);
        REQUIRE(fan.groups().at(0).size() == 2);
        CHECK(fan.groups().at(0).at(0) == sdi(1));
        CHECK(fan.groups().at(0).at(1) == sdi(2));
        CHECK(fan.isValid());

        // primary() round-trips back to the original signal.
        SdiSignalConfig roundtrip = fan.primary();
        CHECK(roundtrip == sig);
}

TEST_CASE("SdiOutputFanoutConfig::fromSignal: empty/default signal yields default fanout") {
        SdiSignalConfig empty;
        SdiOutputFanoutConfig fan = SdiOutputFanoutConfig::fromSignal(empty);

        CHECK(fan.standard() == SdiLinkStandard::Auto);
        CHECK(fan.groupCount() == 0);
        CHECK_FALSE(fan.isValid()); // No groups → not a valid fanout.

        // Round-trip: primary() of the empty fanout reproduces the
        // original default signal.
        CHECK(fan.primary() == empty);
}

TEST_CASE("SdiOutputFanoutConfig::fromSignal: single-link signal lands in 1-cable group") {
        SdiSignalConfig sig = SdiSignalConfig::singleLink(SdiLinkStandard::SL_12G, sdi(3));
        SdiOutputFanoutConfig fan = SdiOutputFanoutConfig::fromSignal(sig);

        REQUIRE(fan.groupCount() == 1);
        REQUIRE(fan.groups().at(0).size() == 1);
        CHECK(fan.groups().at(0).at(0) == sdi(3));
        CHECK(fan.isValid());
}

TEST_CASE("SdiOutputFanoutConfig: mutators copy-on-write the underlying impl") {
        SdiOutputFanoutConfig a;
        a.setStandard(SdiLinkStandard::SL_HD);
        SdiOutputFanoutConfig::PortList g1;
        g1.pushToBack(sdi(1));
        a.appendGroup(g1);

        SdiOutputFanoutConfig b = a; // shares impl
        SdiOutputFanoutConfig::PortList g2;
        g2.pushToBack(sdi(2));
        b.appendGroup(g2); // CoW detach
        CHECK(a.groupCount() == 1);
        CHECK(b.groupCount() == 2);
}

#endif // PROMEKI_ENABLE_CORE
