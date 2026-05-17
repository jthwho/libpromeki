/**
 * @file      sdisignalconfig.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/datatype.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/variant.h>
#include <promeki/videoportref.h>

using namespace promeki;

// ============================================================================
// SdiLinkStandard free helpers
// ============================================================================

TEST_CASE("cablesFor: maps every SdiLinkStandard to its physical cable count") {
        CHECK(cablesFor(SdiLinkStandard::Auto)      == 0);
        CHECK(cablesFor(SdiLinkStandard::SL_SD)     == 1);
        CHECK(cablesFor(SdiLinkStandard::SL_HD)     == 1);
        CHECK(cablesFor(SdiLinkStandard::DL_HD)     == 2);
        CHECK(cablesFor(SdiLinkStandard::SL_3GA)    == 1);
        CHECK(cablesFor(SdiLinkStandard::SL_3GB)    == 1);
        CHECK(cablesFor(SdiLinkStandard::DL_3GB)    == 2);
        CHECK(cablesFor(SdiLinkStandard::DL_3G)     == 2);
        CHECK(cablesFor(SdiLinkStandard::QL_3G_SQD) == 4);
        CHECK(cablesFor(SdiLinkStandard::QL_3G_2SI) == 4);
        CHECK(cablesFor(SdiLinkStandard::SL_6G)     == 1);
        CHECK(cablesFor(SdiLinkStandard::SL_12G)    == 1);
        CHECK(cablesFor(SdiLinkStandard::SL_24G)    == 1);
}

TEST_CASE("isDualLink: identifies all dual-link variants") {
        CHECK_FALSE(isDualLink(SdiLinkStandard::Auto));
        CHECK_FALSE(isDualLink(SdiLinkStandard::SL_SD));
        CHECK_FALSE(isDualLink(SdiLinkStandard::SL_HD));
        CHECK(isDualLink(SdiLinkStandard::DL_HD));
        CHECK_FALSE(isDualLink(SdiLinkStandard::SL_3GA));
        CHECK_FALSE(isDualLink(SdiLinkStandard::SL_3GB));
        CHECK(isDualLink(SdiLinkStandard::DL_3GB));
        CHECK(isDualLink(SdiLinkStandard::DL_3G));
        CHECK_FALSE(isDualLink(SdiLinkStandard::QL_3G_SQD));
        CHECK_FALSE(isDualLink(SdiLinkStandard::QL_3G_2SI));
        CHECK_FALSE(isDualLink(SdiLinkStandard::SL_12G));
}

TEST_CASE("isQuadLink: identifies both quad-link mappings") {
        CHECK_FALSE(isQuadLink(SdiLinkStandard::Auto));
        CHECK_FALSE(isQuadLink(SdiLinkStandard::SL_3GA));
        CHECK_FALSE(isQuadLink(SdiLinkStandard::DL_3G));
        CHECK(isQuadLink(SdiLinkStandard::QL_3G_SQD));
        CHECK(isQuadLink(SdiLinkStandard::QL_3G_2SI));
        CHECK_FALSE(isQuadLink(SdiLinkStandard::SL_12G));
}

TEST_CASE("nominalDataRateGbps: matches the published spec rates") {
        CHECK(nominalDataRateGbps(SdiLinkStandard::Auto)      == doctest::Approx(0.0));
        CHECK(nominalDataRateGbps(SdiLinkStandard::SL_SD)     == doctest::Approx(0.270));
        CHECK(nominalDataRateGbps(SdiLinkStandard::SL_HD)     == doctest::Approx(1.485));
        CHECK(nominalDataRateGbps(SdiLinkStandard::DL_HD)     == doctest::Approx(2.970));
        CHECK(nominalDataRateGbps(SdiLinkStandard::SL_3GA)    == doctest::Approx(2.970));
        CHECK(nominalDataRateGbps(SdiLinkStandard::SL_3GB)    == doctest::Approx(2.970));
        CHECK(nominalDataRateGbps(SdiLinkStandard::DL_3GB)    == doctest::Approx(2.970));
        CHECK(nominalDataRateGbps(SdiLinkStandard::DL_3G)     == doctest::Approx(5.940));
        CHECK(nominalDataRateGbps(SdiLinkStandard::QL_3G_SQD) == doctest::Approx(11.880));
        CHECK(nominalDataRateGbps(SdiLinkStandard::QL_3G_2SI) == doctest::Approx(11.880));
        CHECK(nominalDataRateGbps(SdiLinkStandard::SL_6G)     == doctest::Approx(5.940));
        CHECK(nominalDataRateGbps(SdiLinkStandard::SL_12G)    == doctest::Approx(11.880));
        CHECK(nominalDataRateGbps(SdiLinkStandard::SL_24G)    == doctest::Approx(23.760));
}

// ============================================================================
// Construction / accessors
// ============================================================================

TEST_CASE("SdiSignalConfig: default-constructed has Auto standard and no ports") {
        SdiSignalConfig cfg;
        CHECK(cfg.standard() == SdiLinkStandard::Auto);
        CHECK(cfg.ports().isEmpty());
        CHECK(cfg.cableCount() == 0);
        CHECK(cfg.isValid()); // Auto + 0 ports is valid (unspecified)
}

TEST_CASE("SdiSignalConfig: explicit constructor populates standard + ports") {
        SdiSignalConfig::PortList ports;
        ports.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 1));
        SdiSignalConfig cfg(SdiLinkStandard::SL_HD, ports);
        CHECK(cfg.standard() == SdiLinkStandard::SL_HD);
        CHECK(cfg.cableCount() == 1);
        CHECK(cfg.ports().at(0) == VideoPortRef(VideoConnectorKind::Sdi, 1));
}

// ============================================================================
// Factories
// ============================================================================

TEST_CASE("SdiSignalConfig::singleLink wires a 1-port config") {
        SdiSignalConfig cfg = SdiSignalConfig::singleLink(SdiLinkStandard::SL_12G,
                                                          VideoPortRef(VideoConnectorKind::Sdi, 1));
        CHECK(cfg.cableCount() == 1);
        CHECK(cfg.standard() == SdiLinkStandard::SL_12G);
        CHECK(cfg.isValid());
}

TEST_CASE("SdiSignalConfig::dualLink wires a 2-port config") {
        SdiSignalConfig cfg = SdiSignalConfig::dualLink(SdiLinkStandard::DL_3G,
                                                        VideoPortRef(VideoConnectorKind::Sdi, 1),
                                                        VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(cfg.cableCount() == 2);
        CHECK(cfg.isValid());
}

TEST_CASE("SdiSignalConfig::quadLink wires a 4-port config") {
        SdiSignalConfig cfg = SdiSignalConfig::quadLink(SdiLinkStandard::QL_3G_2SI,
                                                        VideoPortRef(VideoConnectorKind::Sdi, 1),
                                                        VideoPortRef(VideoConnectorKind::Sdi, 2),
                                                        VideoPortRef(VideoConnectorKind::Sdi, 3),
                                                        VideoPortRef(VideoConnectorKind::Sdi, 4));
        CHECK(cfg.cableCount() == 4);
        CHECK(cfg.isValid());
}

// ============================================================================
// CoW mutators
// ============================================================================

TEST_CASE("SdiSignalConfig: setters detach via CoW") {
        SdiSignalConfig original = SdiSignalConfig::singleLink(SdiLinkStandard::SL_HD,
                                                               VideoPortRef(VideoConnectorKind::Sdi, 1));
        SdiSignalConfig copy = original;
        CHECK(copy == original);

        copy.setStandard(SdiLinkStandard::SL_12G);
        CHECK(copy.standard() == SdiLinkStandard::SL_12G);
        CHECK(original.standard() == SdiLinkStandard::SL_HD);

        copy.appendPort(VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(copy.cableCount() == 2);
        CHECK(original.cableCount() == 1);

        SdiSignalConfig::PortList newPorts;
        newPorts.pushToBack(VideoPortRef(VideoConnectorKind::Sdi, 9));
        copy.setPorts(std::move(newPorts));
        CHECK(copy.cableCount() == 1);
        CHECK(copy.ports().at(0).index() == 9);
        CHECK(original.ports().at(0).index() == 1);
}

// ============================================================================
// Validation
// ============================================================================

TEST_CASE("SdiSignalConfig::validate: Auto accepts any cable count") {
        SdiSignalConfig empty;
        CHECK(empty.validate().isOk());

        SdiSignalConfig withPort(SdiLinkStandard::Auto, {});
        withPort.appendPort(VideoPortRef(VideoConnectorKind::Sdi, 1));
        CHECK(withPort.validate().isOk());
}

TEST_CASE("SdiSignalConfig::validate: single-link standards require exactly 1 port") {
        SdiSignalConfig good = SdiSignalConfig::singleLink(SdiLinkStandard::SL_12G,
                                                            VideoPortRef(VideoConnectorKind::Sdi, 1));
        CHECK(good.validate().isOk());

        SdiSignalConfig zeroPorts(SdiLinkStandard::SL_12G, {});
        CHECK(zeroPorts.validate() == Error::InvalidArgument);

        good.appendPort(VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(good.validate() == Error::InvalidArgument);
}

TEST_CASE("SdiSignalConfig::validate: dual-link requires exactly 2 ports") {
        SdiSignalConfig good = SdiSignalConfig::dualLink(SdiLinkStandard::DL_3G,
                                                          VideoPortRef(VideoConnectorKind::Sdi, 1),
                                                          VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(good.validate().isOk());

        SdiSignalConfig onePort(SdiLinkStandard::DL_3G, {});
        onePort.appendPort(VideoPortRef(VideoConnectorKind::Sdi, 1));
        CHECK(onePort.validate() == Error::InvalidArgument);
}

TEST_CASE("SdiSignalConfig::validate: quad-link requires exactly 4 ports") {
        SdiSignalConfig good = SdiSignalConfig::quadLink(SdiLinkStandard::QL_3G_2SI,
                                                          VideoPortRef(VideoConnectorKind::Sdi, 1),
                                                          VideoPortRef(VideoConnectorKind::Sdi, 2),
                                                          VideoPortRef(VideoConnectorKind::Sdi, 3),
                                                          VideoPortRef(VideoConnectorKind::Sdi, 4));
        CHECK(good.validate().isOk());

        SdiSignalConfig three(SdiLinkStandard::QL_3G_2SI, {});
        three.appendPort(VideoPortRef(VideoConnectorKind::Sdi, 1));
        three.appendPort(VideoPortRef(VideoConnectorKind::Sdi, 2));
        three.appendPort(VideoPortRef(VideoConnectorKind::Sdi, 3));
        CHECK(three.validate() == Error::InvalidArgument);
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("SdiSignalConfig: operator== compares standard and port list field-wise") {
        SdiSignalConfig a = SdiSignalConfig::singleLink(SdiLinkStandard::SL_HD,
                                                         VideoPortRef(VideoConnectorKind::Sdi, 1));
        SdiSignalConfig b = SdiSignalConfig::singleLink(SdiLinkStandard::SL_HD,
                                                         VideoPortRef(VideoConnectorKind::Sdi, 1));
        CHECK(a == b);
        CHECK_FALSE(a != b);

        b.setStandard(SdiLinkStandard::SL_3GA);
        CHECK(a != b);

        SdiSignalConfig c = SdiSignalConfig::singleLink(SdiLinkStandard::SL_HD,
                                                         VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(a != c);
}

// ============================================================================
// String form
// ============================================================================

TEST_CASE("SdiSignalConfig::toString emits standard:port1+port2+... shape") {
        SdiSignalConfig single = SdiSignalConfig::singleLink(SdiLinkStandard::SL_12G,
                                                              VideoPortRef(VideoConnectorKind::Sdi, 1));
        CHECK(single.toString() == String("sl_12g:sdi1"));

        SdiSignalConfig quad = SdiSignalConfig::quadLink(SdiLinkStandard::QL_3G_2SI,
                                                          VideoPortRef(VideoConnectorKind::Sdi, 1),
                                                          VideoPortRef(VideoConnectorKind::Sdi, 2),
                                                          VideoPortRef(VideoConnectorKind::Sdi, 3),
                                                          VideoPortRef(VideoConnectorKind::Sdi, 4));
        CHECK(quad.toString() == String("ql_3g_2si:sdi1+sdi2+sdi3+sdi4"));

        SdiSignalConfig dual = SdiSignalConfig::dualLink(SdiLinkStandard::DL_3G,
                                                          VideoPortRef(VideoConnectorKind::Sdi, 1),
                                                          VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(dual.toString() == String("dl_3g:sdi1+sdi2"));
}

TEST_CASE("SdiSignalConfig::toString omits the colon when no ports are present") {
        SdiSignalConfig autoCfg;
        CHECK(autoCfg.toString() == String("auto"));

        SdiSignalConfig hdNoPorts(SdiLinkStandard::SL_HD, {});
        CHECK(hdNoPorts.toString() == String("sl_hd"));
}

TEST_CASE("SdiSignalConfig::fromString round-trips every well-known case") {
        const SdiSignalConfig cases[] = {
                SdiSignalConfig(),
                SdiSignalConfig(SdiLinkStandard::SL_HD, {}),
                SdiSignalConfig::singleLink(SdiLinkStandard::SL_SD, VideoPortRef(VideoConnectorKind::Sdi, 1)),
                SdiSignalConfig::singleLink(SdiLinkStandard::SL_12G, VideoPortRef(VideoConnectorKind::Sdi, 1)),
                SdiSignalConfig::dualLink(SdiLinkStandard::DL_3G,
                                          VideoPortRef(VideoConnectorKind::Sdi, 1),
                                          VideoPortRef(VideoConnectorKind::Sdi, 2)),
                SdiSignalConfig::quadLink(SdiLinkStandard::QL_3G_2SI,
                                          VideoPortRef(VideoConnectorKind::Sdi, 1),
                                          VideoPortRef(VideoConnectorKind::Sdi, 2),
                                          VideoPortRef(VideoConnectorKind::Sdi, 3),
                                          VideoPortRef(VideoConnectorKind::Sdi, 4)),
        };
        for (const auto &original : cases) {
                CAPTURE(original.toString());
                Result<SdiSignalConfig> r = SdiSignalConfig::fromString(original.toString());
                REQUIRE(r.second().isOk());
                CHECK(r.first() == original);
        }
}

TEST_CASE("SdiSignalConfig::fromString is case-insensitive on the standard prefix") {
        Result<SdiSignalConfig> r = SdiSignalConfig::fromString(String("SL_HD:Sdi1"));
        REQUIRE(r.second().isOk());
        CHECK(r.first().standard() == SdiLinkStandard::SL_HD);
        CHECK(r.first().ports().at(0) == VideoPortRef(VideoConnectorKind::Sdi, 1));

        Result<SdiSignalConfig> mixed = SdiSignalConfig::fromString(String("Ql_3G_2Si:SDI1+SDI2+SDI3+SDI4"));
        REQUIRE(mixed.second().isOk());
        CHECK(mixed.first().standard() == SdiLinkStandard::QL_3G_2SI);
        CHECK(mixed.first().cableCount() == 4);
}

TEST_CASE("SdiSignalConfig::fromString rejects empty / unknown / malformed input") {
        CHECK(SdiSignalConfig::fromString(String()).second() == Error::InvalidArgument);
        CHECK(SdiSignalConfig::fromString(String(":")).second() == Error::InvalidArgument);
        CHECK(SdiSignalConfig::fromString(String("nope:sdi1")).second() == Error::InvalidArgument);
        CHECK(SdiSignalConfig::fromString(String("sl_hd:bogusport")).second() == Error::InvalidArgument);
        CHECK(SdiSignalConfig::fromString(String("sl_hd:sdi1+")).second() == Error::InvalidArgument);
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("SdiSignalConfig: DataStream operators round-trip a populated config") {
        SdiSignalConfig original = SdiSignalConfig::quadLink(SdiLinkStandard::QL_3G_2SI,
                                                              VideoPortRef(VideoConnectorKind::Sdi, 1),
                                                              VideoPortRef(VideoConnectorKind::Sdi, 2),
                                                              VideoPortRef(VideoConnectorKind::Sdi, 3),
                                                              VideoPortRef(VideoConnectorKind::Sdi, 4));
        Buffer         storage(4096);
        BufferIODevice dev(&storage);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
                REQUIRE(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        SdiSignalConfig round;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                REQUIRE(r.status() == DataStream::Ok);
        }
        CHECK(round == original);
}

TEST_CASE("SdiSignalConfig: DataStream round-trip preserves an empty default") {
        SdiSignalConfig original;
        Buffer          storage(1024);
        BufferIODevice  dev(&storage);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
        }
        dev.seek(0);
        SdiSignalConfig round = SdiSignalConfig::singleLink(SdiLinkStandard::SL_HD,
                                                             VideoPortRef(VideoConnectorKind::Sdi, 7));
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                REQUIRE(r.status() == DataStream::Ok);
        }
        CHECK(round == original);
        CHECK(round.standard() == SdiLinkStandard::Auto);
        CHECK(round.ports().isEmpty());
}

// ============================================================================
// Variant / DataType registry integration
// ============================================================================

TEST_CASE("SdiSignalConfig: DataType registry assigns DataTypeSdiSignalConfig = 0x65") {
        DataType dt = DataType::of<SdiSignalConfig>();
        REQUIRE(dt.isValid());
        CHECK(dt.id() == DataTypeSdiSignalConfig);
        CHECK(dt.version() == 1u);
        CHECK(String(dt.name()) == "SdiSignalConfig");
}

TEST_CASE("SdiSignalConfig: round-trips through Variant") {
        SdiSignalConfig original = SdiSignalConfig::singleLink(SdiLinkStandard::SL_12G,
                                                                VideoPortRef(VideoConnectorKind::Sdi, 1));
        Variant         v;
        v.set(original);
        CHECK(v.type() == DataTypeSdiSignalConfig);
        SdiSignalConfig out = v.get<SdiSignalConfig>();
        CHECK(out == original);
}

TEST_CASE("SdiSignalConfig: round-trips through Variant <-> String converter") {
        SdiSignalConfig original = SdiSignalConfig::dualLink(SdiLinkStandard::DL_3G,
                                                              VideoPortRef(VideoConnectorKind::Sdi, 1),
                                                              VideoPortRef(VideoConnectorKind::Sdi, 2));
        Variant v(original);
        Error   err;
        String  s = v.toString(&err);
        REQUIRE(err.isOk());
        CHECK(s == String("dl_3g:sdi1+sdi2"));

        Variant sv(s);
        Variant parsed = sv.convertTo(DataTypeSdiSignalConfig, &err);
        REQUIRE(err.isOk());
        CHECK(parsed.type() == DataTypeSdiSignalConfig);
        CHECK(parsed.get<SdiSignalConfig>() == original);
}
