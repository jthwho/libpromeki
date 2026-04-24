/**
 * @file      mediatimestamp.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediatimestamp.h>
#include <promeki/variant.h>
#include <promeki/metadata.h>
#include <promeki/datastream.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>

using namespace promeki;

TEST_CASE("MediaTimeStamp: default is invalid") {
        MediaTimeStamp mts;
        CHECK_FALSE(mts.isValid());
        CHECK(mts.toString().isEmpty());
}

TEST_CASE("MediaTimeStamp: construction with TimeStamp and ClockDomain") {
        TimeStamp ts = TimeStamp::now();
        MediaTimeStamp mts(ts, ClockDomain::SystemMonotonic);
        CHECK(mts.isValid());
        CHECK(mts.domain() == ClockDomain::SystemMonotonic);
        CHECK(mts.timeStamp() == ts);
        CHECK(mts.offset().isZero());
}

TEST_CASE("MediaTimeStamp: construction with offset") {
        TimeStamp ts = TimeStamp::now();
        Duration offset = Duration::fromMilliseconds(50);
        MediaTimeStamp mts(ts, ClockDomain::Synthetic, offset);
        CHECK(mts.isValid());
        CHECK(mts.offset() == offset);
}

TEST_CASE("MediaTimeStamp: toString format") {
        // Construct with a known time point so we can predict the output
        TimeStamp::Duration d = TimeStamp::secondsToDuration(1.5);
        TimeStamp ts{TimeStamp::Value{d}};
        MediaTimeStamp mts(ts, ClockDomain::Synthetic);
        String s = mts.toString();
        CHECK(s.startsWith("Synthetic "));
        CHECK(s.endsWith(" +0"));
}

TEST_CASE("MediaTimeStamp: toString with positive offset") {
        TimeStamp::Duration d = TimeStamp::secondsToDuration(1.0);
        TimeStamp ts{TimeStamp::Value{d}};
        Duration offset = Duration::fromMicroseconds(500);
        MediaTimeStamp mts(ts, ClockDomain::Synthetic, offset);
        String s = mts.toString();
        CHECK(s.endsWith("+500000"));
}

TEST_CASE("MediaTimeStamp: toString with negative offset") {
        TimeStamp::Duration d = TimeStamp::secondsToDuration(1.0);
        TimeStamp ts{TimeStamp::Value{d}};
        Duration offset = Duration::fromNanoseconds(-1000);
        MediaTimeStamp mts(ts, ClockDomain::SystemMonotonic, offset);
        String s = mts.toString();
        CHECK(s.contains("-1000"));
}

TEST_CASE("MediaTimeStamp: fromString round-trip with zero offset") {
        TimeStamp::Duration d = TimeStamp::secondsToDuration(42.0);
        TimeStamp ts{TimeStamp::Value{d}};
        MediaTimeStamp original(ts, ClockDomain::Synthetic);
        String s = original.toString();

        auto [parsed, err] = MediaTimeStamp::fromString(s);
        CHECK(err.isOk());
        CHECK(parsed.isValid());
        CHECK(parsed.domain() == ClockDomain::Synthetic);
        CHECK(parsed.domain().epoch() == ClockEpoch::PerStream);
        CHECK(parsed.offset().isZero());
}

TEST_CASE("MediaTimeStamp: fromString round-trip with offset") {
        TimeStamp::Duration d = TimeStamp::secondsToDuration(10.0);
        TimeStamp ts{TimeStamp::Value{d}};
        Duration offset = Duration::fromMilliseconds(25);
        MediaTimeStamp original(ts, ClockDomain::SystemMonotonic, offset);
        String s = original.toString();

        auto [parsed, err] = MediaTimeStamp::fromString(s);
        CHECK(err.isOk());
        CHECK(parsed.isValid());
        CHECK(parsed.domain() == ClockDomain::SystemMonotonic);
        CHECK(parsed.offset() == offset);
}

TEST_CASE("MediaTimeStamp: fromString with invalid input") {
        auto [mts, err] = MediaTimeStamp::fromString("invalid");
        CHECK(err.isError());
        CHECK_FALSE(mts.isValid());
}

TEST_CASE("MediaTimeStamp: fromString with too few tokens") {
        auto [mts, err] = MediaTimeStamp::fromString("Synthetic 1.0");
        CHECK(err.isError());
}

TEST_CASE("MediaTimeStamp: fromString with unknown domain") {
        auto [mts, err] = MediaTimeStamp::fromString("UnknownClock 1.0 +0");
        CHECK(err.isError());
        CHECK_FALSE(mts.isValid());
}

TEST_CASE("MediaTimeStamp: Variant round-trip") {
        TimeStamp ts = TimeStamp::now();
        MediaTimeStamp original(ts, ClockDomain::Synthetic);
        Variant v = original;
        CHECK(v.type() == Variant::TypeMediaTimeStamp);

        MediaTimeStamp retrieved = v.get<MediaTimeStamp>();
        CHECK(retrieved.isValid());
        CHECK(retrieved.domain() == original.domain());
        CHECK(retrieved.timeStamp() == original.timeStamp());
}

TEST_CASE("MediaTimeStamp: Variant to String conversion") {
        TimeStamp::Duration d = TimeStamp::secondsToDuration(5.0);
        TimeStamp ts{TimeStamp::Value{d}};
        MediaTimeStamp mts(ts, ClockDomain::Synthetic);
        Variant v = mts;
        String s = v.get<String>();
        CHECK(s.startsWith("Synthetic "));
}

TEST_CASE("MediaTimeStamp: DataStream round-trip") {
        TimeStamp::Duration d = TimeStamp::secondsToDuration(100.0);
        TimeStamp ts{TimeStamp::Value{d}};
        Duration offset = Duration::fromMicroseconds(-250);
        MediaTimeStamp original(ts, ClockDomain::SystemMonotonic, offset);

        Buffer buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << original;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream rs = DataStream::createReader(&dev);
                MediaTimeStamp loaded;
                rs >> loaded;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(loaded.isValid());
                CHECK(loaded.domain() == ClockDomain::SystemMonotonic);
                CHECK(loaded.offset() == offset);
        }
}

TEST_CASE("MediaTimeStamp: Metadata set/get") {
        TimeStamp ts = TimeStamp::now();
        MediaTimeStamp mts(ts, ClockDomain::Synthetic);

        // CaptureTime is a MediaTimeStamp-typed Metadata ID; use it to
        // exercise Variant round-trip through Metadata.
        Metadata meta;
        meta.set(Metadata::CaptureTime, mts);

        MediaTimeStamp retrieved = meta.get(Metadata::CaptureTime)
                .get<MediaTimeStamp>();
        CHECK(retrieved.isValid());
        CHECK(retrieved.domain() == ClockDomain::Synthetic);
        CHECK(retrieved.timeStamp() == ts);
}

TEST_CASE("MediaTimeStamp: equality") {
        TimeStamp::Duration d = TimeStamp::secondsToDuration(1.0);
        TimeStamp ts{TimeStamp::Value{d}};
        Duration offset = Duration::fromMilliseconds(10);

        MediaTimeStamp a(ts, ClockDomain::Synthetic, offset);
        MediaTimeStamp b(ts, ClockDomain::Synthetic, offset);
        CHECK(a == b);

        MediaTimeStamp c(ts, ClockDomain::SystemMonotonic, offset);
        CHECK(a != c);
}
