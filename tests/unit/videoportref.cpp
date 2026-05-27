/**
 * @file      videoportref.cpp
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
#include <promeki/json.h>
#include <promeki/result.h>
#include <promeki/variant.h>
#include <promeki/videoportref.h>

using namespace promeki;

// ============================================================================
// Construction / validity
// ============================================================================

TEST_CASE("VideoPortRef: default-constructed is invalid (Auto, index 0)") {
        VideoPortRef ref;
        CHECK_FALSE(ref.isValid());
        CHECK(ref.kind() == VideoConnectorKind::Auto);
        CHECK(ref.index() == 0);
}

TEST_CASE("VideoPortRef: explicit constructor populates kind + index") {
        VideoPortRef ref(VideoConnectorKind::Sdi, 1);
        CHECK(ref.isValid());
        CHECK(ref.kind() == VideoConnectorKind::Sdi);
        CHECK(ref.index() == 1);
}

TEST_CASE("VideoPortRef: Auto kind is never valid even with a positive index") {
        VideoPortRef ref(VideoConnectorKind::Auto, 5);
        CHECK_FALSE(ref.isValid());
}

TEST_CASE("VideoPortRef: zero / negative index is invalid") {
        CHECK_FALSE(VideoPortRef(VideoConnectorKind::Sdi, 0).isValid());
        CHECK_FALSE(VideoPortRef(VideoConnectorKind::Hdmi, -1).isValid());
}

// ============================================================================
// CoW mutators
// ============================================================================

TEST_CASE("VideoPortRef: setters detach from a shared handle") {
        VideoPortRef original(VideoConnectorKind::Sdi, 1);
        VideoPortRef copy = original;
        CHECK(copy == original);

        copy.setIndex(3);
        CHECK(copy.index() == 3);
        CHECK(original.index() == 1); // original untouched

        copy.setKind(VideoConnectorKind::Hdmi);
        CHECK(copy.kind() == VideoConnectorKind::Hdmi);
        CHECK(original.kind() == VideoConnectorKind::Sdi); // original untouched
}

// ============================================================================
// Equality / ordering
// ============================================================================

TEST_CASE("VideoPortRef: operator== compares kind and index") {
        VideoPortRef a(VideoConnectorKind::Sdi, 1);
        VideoPortRef b(VideoConnectorKind::Sdi, 1);
        VideoPortRef c(VideoConnectorKind::Sdi, 2);
        VideoPortRef d(VideoConnectorKind::Hdmi, 1);
        CHECK(a == b);
        CHECK(a != c);
        CHECK(a != d);
}

TEST_CASE("VideoPortRef: operator< orders by kind then by index") {
        VideoPortRef sdi1(VideoConnectorKind::Sdi, 1);
        VideoPortRef sdi2(VideoConnectorKind::Sdi, 2);
        VideoPortRef hdmi1(VideoConnectorKind::Hdmi, 1);

        // Sdi == 1, Hdmi == 2 — Sdi sorts first numerically.
        CHECK(sdi1 < hdmi1);
        CHECK_FALSE(hdmi1 < sdi1);

        // Within the same kind, index breaks ties.
        CHECK(sdi1 < sdi2);
        CHECK_FALSE(sdi2 < sdi1);

        // Equality: neither side is less than the other.
        VideoPortRef sdi1Dup(VideoConnectorKind::Sdi, 1);
        CHECK_FALSE(sdi1 < sdi1Dup);
        CHECK_FALSE(sdi1Dup < sdi1);
}

// ============================================================================
// String form
// ============================================================================

TEST_CASE("VideoPortRef: toString emits lower-case kind + index") {
        CHECK(VideoPortRef(VideoConnectorKind::Sdi, 1).toString() == String("sdi1"));
        CHECK(VideoPortRef(VideoConnectorKind::Hdmi, 2).toString() == String("hdmi2"));
        CHECK(VideoPortRef(VideoConnectorKind::DisplayPort, 4).toString() == String("displayport4"));
        CHECK(VideoPortRef(VideoConnectorKind::Sfp, 3).toString() == String("sfp3"));
}

TEST_CASE("VideoPortRef: toString returns 'auto' for the Auto kind regardless of index") {
        CHECK(VideoPortRef().toString() == String("auto"));
        CHECK(VideoPortRef(VideoConnectorKind::Auto, 5).toString() == String("auto"));
}

TEST_CASE("VideoPortRef: fromString round-trips every connector kind") {
        const VideoPortRef cases[] = {
                VideoPortRef(VideoConnectorKind::Sdi, 1),
                VideoPortRef(VideoConnectorKind::Hdmi, 2),
                VideoPortRef(VideoConnectorKind::DisplayPort, 3),
                VideoPortRef(VideoConnectorKind::Composite, 1),
                VideoPortRef(VideoConnectorKind::Component, 1),
                VideoPortRef(VideoConnectorKind::SVideo, 1),
                VideoPortRef(VideoConnectorKind::Sfp, 4),
        };
        for (const auto &original : cases) {
                CAPTURE(original.toString());
                Result<VideoPortRef> r = VideoPortRef::fromString(original.toString());
                REQUIRE(r.second().isOk());
                CHECK(r.first() == original);
        }
}

TEST_CASE("VideoPortRef: fromString accepts 'auto' as the invalid sentinel") {
        Result<VideoPortRef> r = VideoPortRef::fromString(String("auto"));
        REQUIRE(r.second().isOk());
        CHECK_FALSE(r.first().isValid());
        CHECK(r.first().kind() == VideoConnectorKind::Auto);
}

TEST_CASE("VideoPortRef: fromString is case-insensitive on the kind prefix") {
        Result<VideoPortRef> upper = VideoPortRef::fromString(String("SDI1"));
        REQUIRE(upper.second().isOk());
        CHECK(upper.first() == VideoPortRef(VideoConnectorKind::Sdi, 1));

        Result<VideoPortRef> mixed = VideoPortRef::fromString(String("HdMi2"));
        REQUIRE(mixed.second().isOk());
        CHECK(mixed.first() == VideoPortRef(VideoConnectorKind::Hdmi, 2));
}

TEST_CASE("VideoPortRef: fromString rejects empty / unrecognised / missing-index input") {
        CHECK(VideoPortRef::fromString(String()).second() == Error::InvalidArgument);
        CHECK(VideoPortRef::fromString(String("")).second() == Error::InvalidArgument);
        CHECK(VideoPortRef::fromString(String("nope42")).second() == Error::InvalidArgument);
        CHECK(VideoPortRef::fromString(String("sdi")).second() == Error::InvalidArgument);
        CHECK(VideoPortRef::fromString(String("sdi0")).second() == Error::InvalidArgument);
        CHECK(VideoPortRef::fromString(String("sdi-1")).second() == Error::InvalidArgument);
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("VideoPortRef: DataStream operators round-trip a populated reference") {
        VideoPortRef   original(VideoConnectorKind::Sdi, 3);
        Buffer         storage(1024);
        BufferIODevice dev(&storage);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
                REQUIRE(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        VideoPortRef round;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                REQUIRE(r.status() == DataStream::Ok);
        }
        CHECK(round == original);
}

TEST_CASE("VideoPortRef: DataStream round-trip preserves the default 'auto' value") {
        VideoPortRef   original;
        Buffer         storage(1024);
        BufferIODevice dev(&storage);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
        }
        dev.seek(0);
        VideoPortRef round(VideoConnectorKind::Sdi, 7); // pre-populate so the reader has to overwrite
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                REQUIRE(r.status() == DataStream::Ok);
        }
        CHECK_FALSE(round.isValid());
        CHECK(round == original);
}

// ============================================================================
// Variant / DataType registry integration
// ============================================================================

TEST_CASE("VideoPortRef: DataType registry assigns DataTypeVideoPortRef = 0x64") {
        DataType dt = DataType::of<VideoPortRef>();
        REQUIRE(dt.isValid());
        CHECK(dt.id() == DataTypeVideoPortRef);
        CHECK(dt.version() == 1u);
        CHECK(String(dt.name()) == "VideoPortRef");
}

TEST_CASE("VideoPortRef: round-trips through Variant") {
        VideoPortRef original(VideoConnectorKind::Hdmi, 2);
        Variant      v;
        v.set(original);
        CHECK(v.type() == DataTypeVideoPortRef);
        VideoPortRef out = v.get<VideoPortRef>();
        CHECK(out == original);
}

TEST_CASE("VideoPortRef: round-trips through Variant <-> String converter") {
        VideoPortRef original(VideoConnectorKind::Sdi, 4);
        Variant      v(original);
        Error        err;
        String       s = v.toString(&err);
        REQUIRE(err.isOk());
        CHECK(s == String("sdi4"));

        Variant sv(s);
        Variant parsed = sv.convertTo(DataTypeVideoPortRef, &err);
        REQUIRE(err.isOk());
        CHECK(parsed.type() == DataTypeVideoPortRef);
        CHECK(parsed.get<VideoPortRef>() == original);
}
