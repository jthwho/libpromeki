/**
 * @file      windowedstat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cmath>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/duration.h>
#include <promeki/framecount.h>
#include <promeki/string.h>
#include <promeki/variant.h>
#include <promeki/windowedstat.h>

using namespace promeki;

TEST_CASE("WindowedStat_DefaultIsEmpty") {
        WindowedStat ws;
        CHECK(ws.capacity() == 0);
        CHECK(ws.count() == 0);
        CHECK(ws.isEmpty());
        CHECK_FALSE(ws.isFull());
        CHECK(ws.min() == 0.0);
        CHECK(ws.max() == 0.0);
        CHECK(ws.average() == 0.0);
        CHECK(ws.stddev() == 0.0);
        CHECK(ws.sum() == 0.0);
}

TEST_CASE("WindowedStat_PushPartialFill") {
        WindowedStat ws(8);
        ws.push(1.0);
        ws.push(2.0);
        ws.push(3.0);

        CHECK(ws.capacity() == 8);
        CHECK(ws.count() == 3);
        CHECK_FALSE(ws.isFull());
        CHECK(ws.min() == 1.0);
        CHECK(ws.max() == 3.0);
        CHECK(ws.sum() == doctest::Approx(6.0));
        CHECK(ws.average() == doctest::Approx(2.0));

        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 3);
        CHECK(values[0] == 1.0);
        CHECK(values[1] == 2.0);
        CHECK(values[2] == 3.0);
}

TEST_CASE("WindowedStat_RingWrapKeepsRecentSamples") {
        WindowedStat ws(4);
        for (int i = 1; i <= 7; ++i) ws.push(static_cast<double>(i));
        // Pushed 1..7 into a ring of 4 — only 4..7 should survive
        // and they should appear in oldest-first order.
        CHECK(ws.isFull());
        CHECK(ws.count() == 4);
        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 4);
        CHECK(values[0] == 4.0);
        CHECK(values[1] == 5.0);
        CHECK(values[2] == 6.0);
        CHECK(values[3] == 7.0);
        CHECK(ws.min() == 4.0);
        CHECK(ws.max() == 7.0);
        CHECK(ws.average() == doctest::Approx(5.5));
}

TEST_CASE("WindowedStat_StddevPopulationFormula") {
        WindowedStat ws(8);
        for (int i = 0; i < 5; ++i) ws.push(static_cast<double>(i));
        // Population stddev of 0..4: mean=2, var=2.0, stddev=sqrt(2).
        CHECK(ws.stddev() == doctest::Approx(std::sqrt(2.0)));
}

TEST_CASE("WindowedStat_SetCapacityShrinkDropsOldest") {
        WindowedStat ws(8);
        for (int i = 1; i <= 6; ++i) ws.push(static_cast<double>(i));
        ws.setCapacity(3);
        CHECK(ws.capacity() == 3);
        CHECK(ws.isFull());
        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 3);
        CHECK(values[0] == 4.0);
        CHECK(values[1] == 5.0);
        CHECK(values[2] == 6.0);
}

TEST_CASE("WindowedStat_SetCapacityGrowKeepsAllSamples") {
        WindowedStat ws(3);
        for (int i = 1; i <= 5; ++i) ws.push(static_cast<double>(i));
        // Ring is full at [3,4,5] — growing capacity preserves them in order.
        ws.setCapacity(8);
        CHECK(ws.capacity() == 8);
        CHECK_FALSE(ws.isFull());
        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 3);
        CHECK(values[0] == 3.0);
        CHECK(values[1] == 4.0);
        CHECK(values[2] == 5.0);
        ws.push(6.0);
        ws.push(7.0);
        CHECK(ws.count() == 5);
        CHECK(ws.values()[4] == 7.0);
}

TEST_CASE("WindowedStat_ZeroCapacityDiscardsPushes") {
        WindowedStat ws(0);
        ws.push(1.0);
        ws.push(2.0);
        CHECK(ws.count() == 0);
        CHECK(ws.isEmpty());
}

TEST_CASE("WindowedStat_Clear") {
        WindowedStat ws(4);
        ws.push(1.0);
        ws.push(2.0);
        ws.clear();
        CHECK(ws.count() == 0);
        CHECK_FALSE(ws.isFull());
        CHECK(ws.capacity() == 4);
}

TEST_CASE("WindowedStat_StringRoundTrip") {
        WindowedStat ws(6);
        ws.push(1.5);
        ws.push(-2.25);
        ws.push(3.75);

        const String s = ws.toSerializedString();
        // Canonical "cap=N:[v1,v2,...]" form.
        CHECK(s.startsWith("cap=6:["));
        CHECK(s.endsWith("]"));

        auto r = WindowedStat::fromString(s);
        REQUIRE(error(r).isOk());
        const WindowedStat round = value(r);
        CHECK(round == ws);
}

TEST_CASE("WindowedStat_ToStringIsHumanSummary") {
        WindowedStat ws(8);
        ws.push(1.0);
        ws.push(2.0);
        ws.push(3.0);

        // Default (no formatter) renders raw doubles via String::number;
        // every field is space-separated, no commas.
        const String summary = ws.toString();
        CHECK(summary.contains("Avg: "));
        CHECK(summary.contains("StdDev: "));
        CHECK(summary.contains("Min: "));
        CHECK(summary.contains("Max: "));
        CHECK(summary.contains("WinSz: 3"));
        CHECK_FALSE(summary.contains(","));

        // A custom formatter is used for every numeric field; the
        // sample-count field stays as the raw integer.
        const String custom =
                ws.toString([](double v) { return String("[") + String::number(v) + String("]"); });
        CHECK(custom.contains("Avg: [2"));
        CHECK(custom.contains("Min: [1"));
        CHECK(custom.contains("Max: [3"));
        CHECK(custom.contains("WinSz: 3"));
}

TEST_CASE("WindowedStat_StatsSinglePassAgreesWithAccessors") {
        WindowedStat ws(8);
        for (int i = 0; i < 5; ++i) ws.push(static_cast<double>(i));
        const WindowedStat::Stats s = ws.stats();
        CHECK(s.count == ws.count());
        CHECK(s.capacity == ws.capacity());
        CHECK(s.min == doctest::Approx(ws.min()));
        CHECK(s.max == doctest::Approx(ws.max()));
        CHECK(s.sum == doctest::Approx(ws.sum()));
        CHECK(s.average == doctest::Approx(ws.average()));
        CHECK(s.stddev == doctest::Approx(ws.stddev()));
}

TEST_CASE("WindowedStat_FromString_BareListInfersCapacity") {
        auto r = WindowedStat::fromString("[1,2,3,4]");
        REQUIRE(error(r).isOk());
        WindowedStat ws = value(r);
        CHECK(ws.count() == 4);
        CHECK(ws.capacity() == 4);
        CHECK(ws.isFull());
        CHECK(ws.average() == doctest::Approx(2.5));
}

TEST_CASE("WindowedStat_FromString_RejectsMalformed") {
        CHECK(error(WindowedStat::fromString("not a list")).isError());
        CHECK(error(WindowedStat::fromString("cap=4")).isError());
        CHECK(error(WindowedStat::fromString("cap=abc:[1,2]")).isError());
        CHECK(error(WindowedStat::fromString("[1,oops,3]")).isError());
}

TEST_CASE("WindowedStat_DataStreamRoundTrip") {
        WindowedStat ws(5);
        ws.push(10.0);
        ws.push(20.0);
        ws.push(30.0);

        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << ws;
                CHECK(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        WindowedStat round;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                CHECK(r.status() == DataStream::Ok);
        }
        CHECK(round == ws);
}

TEST_CASE("WindowedStat_VariantRoundTrip") {
        // The Variant integration is exercised indirectly via JSON
        // and DataStream paths in the MediaIOStats / pipeline tests;
        // here verify the get<>() conversions in both directions and
        // operator== across types.
        WindowedStat ws(3);
        ws.push(1.0);
        ws.push(2.0);
        Variant v(ws);
        CHECK(v.type() == Variant::TypeWindowedStat);

        const String s = v.get<String>();
        CHECK(s.startsWith("cap=3:["));

        Variant      v2(s);
        WindowedStat reconstructed = v2.get<WindowedStat>();
        CHECK(reconstructed == ws);
}

TEST_CASE("WindowedStat_VariantDataStreamRoundTrip") {
        // Round-trip through Variant's DataStream operators so the
        // tag-dispatch path (writeVariantValue + readVariantPayload's
        // TypeWindowedStat case) exercises both ends.
        WindowedStat ws(4);
        ws.push(0.5);
        ws.push(1.5);
        ws.push(-2.0);
        ws.push(7.25);
        Variant orig(ws);

        Buffer         buf(256);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << orig;
                CHECK(w.status() == DataStream::Ok);
        }
        dev.seek(0);
        Variant round;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> round;
                CHECK(r.status() == DataStream::Ok);
        }
        CHECK(round.type() == Variant::TypeWindowedStat);
        CHECK(round.get<WindowedStat>() == ws);
}

// ============================================================================
// push(const Variant&) — Variant promotion overload.
//
// Exercises the numeric-promotion table and the non-numeric / non-finite
// rejection paths, plus the corner case where the Variant is numeric
// but the destination window has zero capacity (the call still reports
// "pushed" since the variant was numeric — the underlying push() is a
// no-op for capacity 0).
// ============================================================================

TEST_CASE("WindowedStat_PushVariant_IntegerTypes") {
        WindowedStat ws(4);
        CHECK(ws.push(Variant(static_cast<int8_t>(-5))));
        CHECK(ws.push(Variant(static_cast<uint8_t>(7))));
        CHECK(ws.push(Variant(static_cast<int16_t>(-300))));
        CHECK(ws.push(Variant(static_cast<int32_t>(42))));

        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 4);
        CHECK(values[0] == doctest::Approx(-5.0));
        CHECK(values[1] == doctest::Approx(7.0));
        CHECK(values[2] == doctest::Approx(-300.0));
        CHECK(values[3] == doctest::Approx(42.0));
}

TEST_CASE("WindowedStat_PushVariant_64BitIntegers") {
        WindowedStat ws(2);
        CHECK(ws.push(Variant(static_cast<int64_t>(-9'000'000'000LL))));
        CHECK(ws.push(Variant(static_cast<uint64_t>(9'000'000'000ULL))));

        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 2);
        CHECK(values[0] == doctest::Approx(-9'000'000'000.0));
        CHECK(values[1] == doctest::Approx(9'000'000'000.0));
}

TEST_CASE("WindowedStat_PushVariant_FloatAndDouble") {
        WindowedStat ws(2);
        CHECK(ws.push(Variant(1.5f)));
        CHECK(ws.push(Variant(2.25)));

        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 2);
        CHECK(values[0] == doctest::Approx(1.5));
        CHECK(values[1] == doctest::Approx(2.25));
}

TEST_CASE("WindowedStat_PushVariant_BoolPushesAsZeroOrOne") {
        WindowedStat ws(2);
        CHECK(ws.push(Variant(true)));
        CHECK(ws.push(Variant(false)));

        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 2);
        CHECK(values[0] == 1.0);
        CHECK(values[1] == 0.0);
}

TEST_CASE("WindowedStat_PushVariant_DurationConvertsToNanoseconds") {
        // Duration → nanoseconds — matches the unit submit() writes
        // internally for QueueWaitDuration / ExecuteDuration so the
        // windowed values stay comparable across strategies.
        WindowedStat ws(3);
        CHECK(ws.push(Variant(Duration::fromMilliseconds(1))));
        CHECK(ws.push(Variant(Duration::fromMicroseconds(500))));
        CHECK(ws.push(Variant(Duration::fromNanoseconds(123))));

        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 3);
        CHECK(values[0] == doctest::Approx(1'000'000.0));
        CHECK(values[1] == doctest::Approx(500'000.0));
        CHECK(values[2] == doctest::Approx(123.0));
}

TEST_CASE("WindowedStat_PushVariant_FrameCountFinitePushesValue") {
        WindowedStat ws(2);
        CHECK(ws.push(Variant(FrameCount(0))));
        CHECK(ws.push(Variant(FrameCount(120))));

        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 2);
        CHECK(values[0] == 0.0);
        CHECK(values[1] == 120.0);
}

TEST_CASE("WindowedStat_PushVariant_FrameCountUnknownIsRejected") {
        // Sentinel states must not push a misleading negative or
        // sentinel value into the ring — the contract is "skip and
        // return false" so the caller can react if needed.
        WindowedStat ws(2);
        ws.push(7.5); // baseline sample so we can prove non-finite did not push
        CHECK_FALSE(ws.push(Variant(FrameCount::unknown())));
        CHECK_FALSE(ws.push(Variant(FrameCount::infinity())));

        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 1);
        CHECK(values[0] == 7.5);
}

TEST_CASE("WindowedStat_PushVariant_NonNumericTypesAreRejected") {
        WindowedStat ws(4);
        ws.push(0.0); // baseline so we can verify non-numeric pushes did nothing

        // Strings, default-constructed (Invalid), and object-typed
        // variants all fall through the default branch.
        CHECK_FALSE(ws.push(Variant(String("hello"))));
        CHECK_FALSE(ws.push(Variant()));

        // A Variant carrying another WindowedStat is not numeric per
        // the promotion table — it should not push.
        WindowedStat inner(2);
        inner.push(1.0);
        CHECK_FALSE(ws.push(Variant(inner)));

        // Only the baseline sample should remain.
        const WindowedStat::Samples values = ws.values();
        REQUIRE(values.size() == 1);
        CHECK(values[0] == 0.0);
}

TEST_CASE("WindowedStat_PushVariant_ZeroCapacityReturnsTrueButRingStaysEmpty") {
        // Numeric promotion succeeds (the variant *was* numeric), so
        // the documented contract — "true if the variant was numeric"
        // — returns true even though the underlying push is a no-op
        // for a zero-capacity ring.  Verifies that the bool overload
        // is not gated on whether a sample actually materialised.
        WindowedStat ws(0);
        CHECK(ws.push(Variant(static_cast<int32_t>(42))));
        CHECK(ws.count() == 0);
        CHECK(ws.isEmpty());
}
