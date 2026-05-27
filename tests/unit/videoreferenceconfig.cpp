/**
 * @file      videoreferenceconfig.cpp
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
#include <promeki/result.h>
#include <promeki/variant.h>
#include <promeki/videoportref.h>
#include <promeki/videoreferenceconfig.h>

using namespace promeki;

// ============================================================================
// Construction / validity
// ============================================================================

TEST_CASE("VideoReferenceConfig: default is FreeRun + Auto and valid") {
        VideoReferenceConfig cfg;
        CHECK(cfg.source() == VideoReferenceSource::FreeRun);
        CHECK(cfg.family() == VideoReferenceRateFamily::Auto);
        CHECK_FALSE(cfg.signalPort().isValid());
        CHECK(cfg.isValid());
}

TEST_CASE("VideoReferenceConfig: source+family constructor populates both fields") {
        VideoReferenceConfig cfg(VideoReferenceSource::Genlock, VideoReferenceRateFamily::Integer);
        CHECK(cfg.source() == VideoReferenceSource::Genlock);
        CHECK(cfg.family() == VideoReferenceRateFamily::Integer);
        CHECK(cfg.isValid());
}

TEST_CASE("VideoReferenceConfig: FromSignal without a valid port is invalid") {
        VideoReferenceConfig cfg(VideoReferenceSource::FromSignal, VideoReferenceRateFamily::Integer);
        CHECK_FALSE(cfg.isValid());

        cfg.setSignalPort(VideoPortRef(VideoConnectorKind::Sdi, 1));
        CHECK(cfg.isValid());
}

TEST_CASE("VideoReferenceConfig: non-FromSignal sources ignore signalPort for validity") {
        // Even with an invalid port, FreeRun / Genlock / External / Ptp /
        // Word stay valid — signalPort is only consulted in FromSignal mode.
        VideoReferenceConfig cfg(VideoReferenceSource::FreeRun);
        cfg.setSignalPort(VideoPortRef()); // invalid port
        CHECK(cfg.isValid());

        cfg.setSource(VideoReferenceSource::External);
        CHECK(cfg.isValid());
}

// ============================================================================
// CoW mutators
// ============================================================================

TEST_CASE("VideoReferenceConfig: setters detach via CoW") {
        VideoReferenceConfig original(VideoReferenceSource::Genlock, VideoReferenceRateFamily::Integer);
        VideoReferenceConfig copy = original;
        CHECK(copy == original);

        copy.setSource(VideoReferenceSource::External);
        CHECK(copy.source() == VideoReferenceSource::External);
        CHECK(original.source() == VideoReferenceSource::Genlock);

        copy.setFamily(VideoReferenceRateFamily::Fractional);
        CHECK(copy.family() == VideoReferenceRateFamily::Fractional);
        CHECK(original.family() == VideoReferenceRateFamily::Integer);

        copy.setSignalPort(VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(copy.signalPort().isValid());
        CHECK_FALSE(original.signalPort().isValid());
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("VideoReferenceConfig: operator== compares every field") {
        VideoReferenceConfig a(VideoReferenceSource::Genlock, VideoReferenceRateFamily::Integer);
        VideoReferenceConfig b(VideoReferenceSource::Genlock, VideoReferenceRateFamily::Integer);
        CHECK(a == b);
        CHECK_FALSE(a != b);

        b.setFamily(VideoReferenceRateFamily::Fractional);
        CHECK(a != b);

        VideoReferenceConfig c(VideoReferenceSource::FromSignal, VideoReferenceRateFamily::Integer);
        c.setSignalPort(VideoPortRef(VideoConnectorKind::Sdi, 1));
        VideoReferenceConfig d(VideoReferenceSource::FromSignal, VideoReferenceRateFamily::Integer);
        d.setSignalPort(VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(c != d);
}

// ============================================================================
// String form
// ============================================================================

TEST_CASE("VideoReferenceConfig::toString emits '<source>:<family>' for non-FromSignal") {
        VideoReferenceConfig def;
        CHECK(def.toString() == String("freerun:auto"));

        VideoReferenceConfig genlock(VideoReferenceSource::Genlock, VideoReferenceRateFamily::Integer);
        CHECK(genlock.toString() == String("genlock:integer"));

        VideoReferenceConfig external(VideoReferenceSource::External, VideoReferenceRateFamily::Fractional);
        CHECK(external.toString() == String("external:fractional"));
}

TEST_CASE("VideoReferenceConfig::toString emits '<source>:<port>:<family>' for FromSignal") {
        VideoReferenceConfig cfg(VideoReferenceSource::FromSignal, VideoReferenceRateFamily::Fractional);
        cfg.setSignalPort(VideoPortRef(VideoConnectorKind::Sdi, 1));
        CHECK(cfg.toString() == String("fromsignal:sdi1:fractional"));
}

TEST_CASE("VideoReferenceConfig::fromString round-trips every well-known case") {
        const VideoReferenceConfig basicCases[] = {
                VideoReferenceConfig(),
                VideoReferenceConfig(VideoReferenceSource::FreeRun,  VideoReferenceRateFamily::Auto),
                VideoReferenceConfig(VideoReferenceSource::Genlock,  VideoReferenceRateFamily::Integer),
                VideoReferenceConfig(VideoReferenceSource::External, VideoReferenceRateFamily::Fractional),
                VideoReferenceConfig(VideoReferenceSource::Ptp,      VideoReferenceRateFamily::Integer),
                VideoReferenceConfig(VideoReferenceSource::Word,     VideoReferenceRateFamily::Fractional),
        };
        for (const auto &original : basicCases) {
                CAPTURE(original.toString());
                Result<VideoReferenceConfig> r = VideoReferenceConfig::fromString(original.toString());
                REQUIRE(r.second().isOk());
                CHECK(r.first() == original);
        }

        VideoReferenceConfig fromSignal(VideoReferenceSource::FromSignal, VideoReferenceRateFamily::Integer);
        fromSignal.setSignalPort(VideoPortRef(VideoConnectorKind::Sdi, 3));
        Result<VideoReferenceConfig> r = VideoReferenceConfig::fromString(fromSignal.toString());
        REQUIRE(r.second().isOk());
        CHECK(r.first() == fromSignal);
}

TEST_CASE("VideoReferenceConfig::fromString is case-insensitive") {
        Result<VideoReferenceConfig> r = VideoReferenceConfig::fromString(String("GENLOCK:Integer"));
        REQUIRE(r.second().isOk());
        CHECK(r.first().source() == VideoReferenceSource::Genlock);
        CHECK(r.first().family() == VideoReferenceRateFamily::Integer);

        Result<VideoReferenceConfig> fs = VideoReferenceConfig::fromString(String("FromSignal:SDI2:Fractional"));
        REQUIRE(fs.second().isOk());
        CHECK(fs.first().source() == VideoReferenceSource::FromSignal);
        CHECK(fs.first().signalPort() == VideoPortRef(VideoConnectorKind::Sdi, 2));
        CHECK(fs.first().family() == VideoReferenceRateFamily::Fractional);
}

TEST_CASE("VideoReferenceConfig::fromString rejects empty / missing-segment / unknown input") {
        CHECK(VideoReferenceConfig::fromString(String()).second() == Error::InvalidArgument);
        CHECK(VideoReferenceConfig::fromString(String("freerun")).second() == Error::InvalidArgument);
        CHECK(VideoReferenceConfig::fromString(String("freerun:")).second() == Error::InvalidArgument);
        CHECK(VideoReferenceConfig::fromString(String(":auto")).second() == Error::InvalidArgument);
        CHECK(VideoReferenceConfig::fromString(String("nope:auto")).second() == Error::InvalidArgument);
        CHECK(VideoReferenceConfig::fromString(String("freerun:nope")).second() == Error::InvalidArgument);
        // Non-FromSignal source rejects the three-segment form.
        CHECK(VideoReferenceConfig::fromString(String("genlock:sdi1:integer")).second() == Error::InvalidArgument);
        // FromSignal rejects the two-segment form (no port).
        CHECK(VideoReferenceConfig::fromString(String("fromsignal:integer")).second() == Error::InvalidArgument);
        // Bad port spec inside FromSignal.
        CHECK(VideoReferenceConfig::fromString(String("fromsignal:notaport:integer")).second()
              == Error::InvalidArgument);
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("VideoReferenceConfig: DataStream operators round-trip a FromSignal config") {
        VideoReferenceConfig original(VideoReferenceSource::FromSignal, VideoReferenceRateFamily::Fractional);
        original.setSignalPort(VideoPortRef(VideoConnectorKind::Sdi, 2));

        Buffer         storage(1024);
        BufferIODevice dev(&storage);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
                REQUIRE(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        VideoReferenceConfig round;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                REQUIRE(r.status() == DataStream::Ok);
        }
        CHECK(round == original);
}

TEST_CASE("VideoReferenceConfig: DataStream round-trip preserves the default") {
        VideoReferenceConfig original;
        Buffer               storage(1024);
        BufferIODevice       dev(&storage);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
        }
        dev.seek(0);
        VideoReferenceConfig round(VideoReferenceSource::Genlock, VideoReferenceRateFamily::Integer);
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                REQUIRE(r.status() == DataStream::Ok);
        }
        CHECK(round == original);
        CHECK(round.source() == VideoReferenceSource::FreeRun);
        CHECK(round.family() == VideoReferenceRateFamily::Auto);
}

// ============================================================================
// Variant / DataType registry integration
// ============================================================================

TEST_CASE("VideoReferenceConfig: DataType registry assigns DataTypeVideoReferenceConfig = 0x67") {
        DataType dt = DataType::of<VideoReferenceConfig>();
        REQUIRE(dt.isValid());
        CHECK(dt.id() == DataTypeVideoReferenceConfig);
        CHECK(dt.version() == 1u);
        CHECK(String(dt.name()) == "VideoReferenceConfig");
}

TEST_CASE("VideoReferenceConfig: round-trips through Variant") {
        VideoReferenceConfig original(VideoReferenceSource::Genlock, VideoReferenceRateFamily::Integer);
        Variant              v;
        v.set(original);
        CHECK(v.type() == DataTypeVideoReferenceConfig);
        VideoReferenceConfig out = v.get<VideoReferenceConfig>();
        CHECK(out == original);
}

TEST_CASE("VideoReferenceConfig: round-trips through Variant <-> String converter") {
        VideoReferenceConfig original(VideoReferenceSource::FromSignal, VideoReferenceRateFamily::Fractional);
        original.setSignalPort(VideoPortRef(VideoConnectorKind::Sdi, 1));
        Variant v(original);
        Error   err;
        String  s = v.toString(&err);
        REQUIRE(err.isOk());
        CHECK(s == String("fromsignal:sdi1:fractional"));

        Variant sv(s);
        Variant parsed = sv.convertTo(DataTypeVideoReferenceConfig, &err);
        REQUIRE(err.isOk());
        CHECK(parsed.type() == DataTypeVideoReferenceConfig);
        CHECK(parsed.get<VideoReferenceConfig>() == original);
}
