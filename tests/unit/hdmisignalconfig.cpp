/**
 * @file      hdmisignalconfig.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/datatype.h>
#include <promeki/error.h>
#include <promeki/hdmisignalconfig.h>
#include <promeki/result.h>
#include <promeki/variant.h>
#include <promeki/videoportref.h>

using namespace promeki;

// ============================================================================
// Construction / validity
// ============================================================================

TEST_CASE("HdmiSignalConfig: default-constructed has invalid port and Auto hint") {
        HdmiSignalConfig cfg;
        CHECK_FALSE(cfg.isValid());
        CHECK_FALSE(cfg.port().isValid());
        CHECK(cfg.versionHint() == HdmiSpecVersion::Auto);
}

TEST_CASE("HdmiSignalConfig: explicit constructor populates port + version hint") {
        HdmiSignalConfig cfg(VideoPortRef(VideoConnectorKind::Hdmi, 1), HdmiSpecVersion::Hdmi21);
        CHECK(cfg.isValid());
        CHECK(cfg.port() == VideoPortRef(VideoConnectorKind::Hdmi, 1));
        CHECK(cfg.versionHint() == HdmiSpecVersion::Hdmi21);
}

TEST_CASE("HdmiSignalConfig: version hint defaults to Auto when omitted") {
        HdmiSignalConfig cfg(VideoPortRef(VideoConnectorKind::Hdmi, 2));
        CHECK(cfg.isValid());
        CHECK(cfg.versionHint() == HdmiSpecVersion::Auto);
}

TEST_CASE("HdmiSignalConfig: invalid port makes the config invalid regardless of hint") {
        HdmiSignalConfig cfg(VideoPortRef(), HdmiSpecVersion::Hdmi21);
        CHECK_FALSE(cfg.isValid());
}

// ============================================================================
// CoW mutators
// ============================================================================

TEST_CASE("HdmiSignalConfig: setters detach via CoW") {
        HdmiSignalConfig original(VideoPortRef(VideoConnectorKind::Hdmi, 1), HdmiSpecVersion::Hdmi20);
        HdmiSignalConfig copy = original;
        CHECK(copy == original);

        copy.setPort(VideoPortRef(VideoConnectorKind::Hdmi, 3));
        CHECK(copy.port().index() == 3);
        CHECK(original.port().index() == 1);

        copy.setVersionHint(HdmiSpecVersion::Hdmi21);
        CHECK(copy.versionHint() == HdmiSpecVersion::Hdmi21);
        CHECK(original.versionHint() == HdmiSpecVersion::Hdmi20);
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("HdmiSignalConfig: operator== compares port and version hint") {
        HdmiSignalConfig a(VideoPortRef(VideoConnectorKind::Hdmi, 1), HdmiSpecVersion::Hdmi20);
        HdmiSignalConfig b(VideoPortRef(VideoConnectorKind::Hdmi, 1), HdmiSpecVersion::Hdmi20);
        CHECK(a == b);
        CHECK_FALSE(a != b);

        b.setVersionHint(HdmiSpecVersion::Hdmi21);
        CHECK(a != b);

        HdmiSignalConfig c(VideoPortRef(VideoConnectorKind::Hdmi, 2), HdmiSpecVersion::Hdmi20);
        CHECK(a != c);
}

// ============================================================================
// String form
// ============================================================================

TEST_CASE("HdmiSignalConfig::toString emits 'version:port' lower-case") {
        HdmiSignalConfig a(VideoPortRef(VideoConnectorKind::Hdmi, 1), HdmiSpecVersion::Auto);
        CHECK(a.toString() == String("auto:hdmi1"));

        HdmiSignalConfig b(VideoPortRef(VideoConnectorKind::Hdmi, 2), HdmiSpecVersion::Hdmi21);
        CHECK(b.toString() == String("hdmi21:hdmi2"));

        HdmiSignalConfig c(VideoPortRef(VideoConnectorKind::Hdmi, 3), HdmiSpecVersion::Hdmi14);
        CHECK(c.toString() == String("hdmi14:hdmi3"));

        HdmiSignalConfig def;
        CHECK(def.toString() == String("auto:auto"));
}

TEST_CASE("HdmiSignalConfig::fromString round-trips every spec version") {
        const HdmiSpecVersion versions[] = {
                HdmiSpecVersion::Auto, HdmiSpecVersion::Hdmi14,
                HdmiSpecVersion::Hdmi20, HdmiSpecVersion::Hdmi21,
        };
        for (auto v : versions) {
                HdmiSignalConfig original(VideoPortRef(VideoConnectorKind::Hdmi, 1), v);
                CAPTURE(original.toString());
                Result<HdmiSignalConfig> r = HdmiSignalConfig::fromString(original.toString());
                REQUIRE(r.second().isOk());
                CHECK(r.first() == original);
        }
}

TEST_CASE("HdmiSignalConfig::fromString is case-insensitive on the version segment") {
        Result<HdmiSignalConfig> r = HdmiSignalConfig::fromString(String("HDMI21:HDMI2"));
        REQUIRE(r.second().isOk());
        CHECK(r.first().port() == VideoPortRef(VideoConnectorKind::Hdmi, 2));
        CHECK(r.first().versionHint() == HdmiSpecVersion::Hdmi21);
}

TEST_CASE("HdmiSignalConfig::fromString rejects empty / missing-separator / unknown input") {
        CHECK(HdmiSignalConfig::fromString(String()).second() == Error::InvalidArgument);
        CHECK(HdmiSignalConfig::fromString(String("hdmi1")).second() == Error::InvalidArgument);
        CHECK(HdmiSignalConfig::fromString(String(":hdmi1")).second() == Error::InvalidArgument);
        CHECK(HdmiSignalConfig::fromString(String("hdmi21:")).second() == Error::InvalidArgument);
        CHECK(HdmiSignalConfig::fromString(String("auto:notaport")).second() == Error::InvalidArgument);
        CHECK(HdmiSignalConfig::fromString(String("hdmi22:hdmi1")).second() == Error::InvalidArgument);
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("HdmiSignalConfig: DataStream operators round-trip a populated config") {
        HdmiSignalConfig original(VideoPortRef(VideoConnectorKind::Hdmi, 2), HdmiSpecVersion::Hdmi21);
        Buffer           storage(1024);
        BufferIODevice   dev(&storage);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
                REQUIRE(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        HdmiSignalConfig round;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                REQUIRE(r.status() == DataStream::Ok);
        }
        CHECK(round == original);
}

TEST_CASE("HdmiSignalConfig: DataStream round-trip preserves the default config") {
        HdmiSignalConfig original;
        Buffer           storage(1024);
        BufferIODevice   dev(&storage);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
        }
        dev.seek(0);
        // Pre-populate the destination so the reader has to overwrite.
        HdmiSignalConfig round(VideoPortRef(VideoConnectorKind::Hdmi, 7), HdmiSpecVersion::Hdmi21);
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                REQUIRE(r.status() == DataStream::Ok);
        }
        CHECK(round == original);
        CHECK_FALSE(round.isValid());
}

// ============================================================================
// Variant / DataType registry integration
// ============================================================================

TEST_CASE("HdmiSignalConfig: DataType registry assigns DataTypeHdmiSignalConfig = 0x66") {
        DataType dt = DataType::of<HdmiSignalConfig>();
        REQUIRE(dt.isValid());
        CHECK(dt.id() == DataTypeHdmiSignalConfig);
        CHECK(dt.version() == 1u);
        CHECK(String(dt.name()) == "HdmiSignalConfig");
}

TEST_CASE("HdmiSignalConfig: round-trips through Variant") {
        HdmiSignalConfig original(VideoPortRef(VideoConnectorKind::Hdmi, 1), HdmiSpecVersion::Hdmi21);
        Variant          v;
        v.set(original);
        CHECK(v.type() == DataTypeHdmiSignalConfig);
        HdmiSignalConfig out = v.get<HdmiSignalConfig>();
        CHECK(out == original);
}

TEST_CASE("HdmiSignalConfig: round-trips through Variant <-> String converter") {
        HdmiSignalConfig original(VideoPortRef(VideoConnectorKind::Hdmi, 4), HdmiSpecVersion::Hdmi20);
        Variant          v(original);
        Error            err;
        String           s = v.toString(&err);
        REQUIRE(err.isOk());
        CHECK(s == String("hdmi20:hdmi4"));

        Variant sv(s);
        Variant parsed = sv.convertTo(DataTypeHdmiSignalConfig, &err);
        REQUIRE(err.isOk());
        CHECK(parsed.type() == DataTypeHdmiSignalConfig);
        CHECK(parsed.get<HdmiSignalConfig>() == original);
}
