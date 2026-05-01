/**
 * @file      units.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <limits>
#include <promeki/units.h>

using namespace promeki;

// ============================================================================
// fromByteCount — metric (default)
// ============================================================================

TEST_CASE("Units: fromByteCount zero") {
        CHECK(Units::fromByteCount(0) == "0 B");
}

TEST_CASE("Units: fromByteCount sub-kilo") {
        CHECK(Units::fromByteCount(999) == "999 B");
}

TEST_CASE("Units: fromByteCount KB") {
        CHECK(Units::fromByteCount(1500) == "1.5 KB");
}

TEST_CASE("Units: fromByteCount KB zero decimals") {
        CHECK(Units::fromByteCount(1500, 0) == "2 KB");
}

TEST_CASE("Units: fromByteCount MB") {
        CHECK(Units::fromByteCount(1500000, 2) == "1.5 MB");
}

TEST_CASE("Units: fromByteCount TB") {
        CHECK(Units::fromByteCount(1234567890123ULL, 2) == "1.23 TB");
}

TEST_CASE("Units: fromByteCount trailing zeros trimmed") {
        CHECK(Units::fromByteCount(1000000) == "1 MB");
}

// ============================================================================
// fromByteCount — binary
// ============================================================================

TEST_CASE("Units: fromByteCount binary MiB") {
        CHECK(Units::fromByteCount(1048576, 3, ByteCountStyle::Binary) == "1 MiB");
}

TEST_CASE("Units: fromByteCount binary GiB") {
        CHECK(Units::fromByteCount(1073741824ULL, 3, ByteCountStyle::Binary) == "1 GiB");
}

TEST_CASE("Units: fromByteCount metric explicit") {
        CHECK(Units::fromByteCount(1024, 3, ByteCountStyle::Metric) == "1.024 KB");
}

// ============================================================================
// fromDuration
// ============================================================================

TEST_CASE("Units: fromDuration nanoseconds") {
        CHECK(Units::fromDuration(0.0000000005) == "0.5 ns");
}

TEST_CASE("Units: fromDuration microseconds") {
        CHECK(Units::fromDuration(0.0000015) == "1.5 us");
}

TEST_CASE("Units: fromDuration milliseconds") {
        CHECK(Units::fromDuration(0.0015) == "1.5 ms");
}

TEST_CASE("Units: fromDuration seconds") {
        CHECK(Units::fromDuration(1.5) == "1.5 s");
}

TEST_CASE("Units: fromDuration minutes") {
        CHECK(Units::fromDuration(90.0) == "1.5 m");
}

TEST_CASE("Units: fromDuration hours") {
        CHECK(Units::fromDuration(3600.0) == "1 h");
}

TEST_CASE("Units: fromDuration custom precision") {
        CHECK(Units::fromDuration(0.001234, 4) == "1.234 ms");
}

// ============================================================================
// fromDurationNs
// ============================================================================

TEST_CASE("Units: fromDurationNs nanoseconds") {
        CHECK(Units::fromDurationNs(500) == "500 ns");
}

TEST_CASE("Units: fromDurationNs microseconds") {
        CHECK(Units::fromDurationNs(8970) == "8.97 us");
}

TEST_CASE("Units: fromDurationNs milliseconds") {
        CHECK(Units::fromDurationNs(1500000) == "1.5 ms");
}

TEST_CASE("Units: fromDurationNs seconds") {
        CHECK(Units::fromDurationNs(1500000000) == "1.5 s");
}

// ============================================================================
// fromDurationMs
// ============================================================================

TEST_CASE("Units: fromDurationMs sub-microsecond rounds to ns") {
        // 0.0005 ms = 500 ns — verifies the ms→ns scale carries
        // sub-microsecond inputs into the ns bucket of the
        // fromDurationNs auto-scale table.
        CHECK(Units::fromDurationMs(0.0005) == "500 ns");
}

TEST_CASE("Units: fromDurationMs microseconds") {
        CHECK(Units::fromDurationMs(0.05) == "50 us");
}

TEST_CASE("Units: fromDurationMs milliseconds") {
        CHECK(Units::fromDurationMs(4.2) == "4.2 ms");
}

TEST_CASE("Units: fromDurationMs seconds") {
        CHECK(Units::fromDurationMs(1500.0) == "1.5 s");
}

TEST_CASE("Units: fromDurationMs minutes") {
        // 90 000 ms = 90 s = 1.5 m — exercises the minute crossover.
        CHECK(Units::fromDurationMs(90000.0) == "1.5 m");
}

TEST_CASE("Units: fromDurationMs zero is ns") {
        // Zero falls into the lowest scale bucket (ns) since the
        // auto-scale table picks the highest entry whose threshold
        // does not exceed the absolute value.
        CHECK(Units::fromDurationMs(0.0) == "0 ns");
}

// ============================================================================
// fromCount
// ============================================================================

TEST_CASE("Units: fromCount zero renders as '0'") {
        // Distinct from fromItemsPerSec which renders zero as "-".
        // fromCount is intended for rows where every slot is always
        // populated, so zero must render as a real number.
        CHECK(Units::fromCount(0) == "0");
}

TEST_CASE("Units: fromCount sub-kilo renders as integer") {
        CHECK(Units::fromCount(127) == "127");
        CHECK(Units::fromCount(999) == "999");
}

TEST_CASE("Units: fromCount kilo") {
        CHECK(Units::fromCount(1500) == "1.5k");
}

TEST_CASE("Units: fromCount mega trims trailing zeros") {
        // 2 340 000 → "2.34M" — verifies that the trailing-zero
        // trimmer leaves significant digits intact.
        CHECK(Units::fromCount(2340000ULL) == "2.34M");
}

TEST_CASE("Units: fromCount giga") {
        CHECK(Units::fromCount(2500000000ULL) == "2.5G");
}

TEST_CASE("Units: fromCount tera") {
        CHECK(Units::fromCount(1500000000000ULL) == "1.5T");
}

TEST_CASE("Units: fromCount precision controls fractional digits") {
        // Default precision (2) would render 1234 as "1.23k"; a
        // precision of 0 forces no fractional digits.
        CHECK(Units::fromCount(1234, 0) == "1k");
}

// ============================================================================
// fromFramesPerSec
// ============================================================================

TEST_CASE("Units: fromFramesPerSec integer rate trims fraction") {
        CHECK(Units::fromFramesPerSec(30.0) == "30 fps");
}

TEST_CASE("Units: fromFramesPerSec NDF preserves precision") {
        CHECK(Units::fromFramesPerSec(29.97) == "29.97 fps");
}

TEST_CASE("Units: fromFramesPerSec custom precision") {
        // 23.976 with default precision (2) rounds to 23.98; bumping
        // precision to 3 keeps the full value.
        CHECK(Units::fromFramesPerSec(23.976, 3) == "23.976 fps");
}

TEST_CASE("Units: fromFramesPerSec non-finite renders as '-'") {
        // Telemetry that has not yet observed a frame may pass NaN/inf.
        CHECK(Units::fromFramesPerSec(std::numeric_limits<double>::quiet_NaN()) == "-");
        CHECK(Units::fromFramesPerSec(std::numeric_limits<double>::infinity()) == "-");
}

// ============================================================================
// fromFrequency
// ============================================================================

TEST_CASE("Units: fromFrequency Hz") {
        CHECK(Units::fromFrequency(440.0) == "440 Hz");
}

TEST_CASE("Units: fromFrequency kHz") {
        CHECK(Units::fromFrequency(48000.0) == "48 kHz");
}

TEST_CASE("Units: fromFrequency MHz") {
        CHECK(Units::fromFrequency(27000000.0) == "27 MHz");
}

TEST_CASE("Units: fromFrequency GHz") {
        CHECK(Units::fromFrequency(2400000000.0) == "2.4 GHz");
}

TEST_CASE("Units: fromFrequency custom precision") {
        CHECK(Units::fromFrequency(44100.0, 1) == "44.1 kHz");
}

// ============================================================================
// fromSampleCount
// ============================================================================

TEST_CASE("Units: fromSampleCount one second") {
        CHECK(Units::fromSampleCount(48000, 48000) == "1 s");
}

TEST_CASE("Units: fromSampleCount two seconds") {
        CHECK(Units::fromSampleCount(96000, 48000) == "2 s");
}

TEST_CASE("Units: fromSampleCount sub-millisecond") {
        CHECK(Units::fromSampleCount(24, 48000) == "500 us");
}

TEST_CASE("Units: fromSampleCount zero rate") {
        CHECK(Units::fromSampleCount(1000, 0) == "0 s");
}

// ============================================================================
// fromBytesPerSec
// ============================================================================

TEST_CASE("Units: fromBytesPerSec zero") {
        CHECK(Units::fromBytesPerSec(0.0) == "-");
}

TEST_CASE("Units: fromBytesPerSec B/s") {
        CHECK(Units::fromBytesPerSec(500.0) == "500 B/s");
}

TEST_CASE("Units: fromBytesPerSec MB/s") {
        CHECK(Units::fromBytesPerSec(1048576.0) == "1 MB/s");
}

TEST_CASE("Units: fromBytesPerSec GB/s") {
        CHECK(Units::fromBytesPerSec(1073741824.0) == "1 GB/s");
}

// ============================================================================
// fromBitsPerSec
// ============================================================================

TEST_CASE("Units: fromBitsPerSec zero") {
        CHECK(Units::fromBitsPerSec(0.0) == "-");
}

TEST_CASE("Units: fromBitsPerSec bps") {
        CHECK(Units::fromBitsPerSec(500.0) == "500 bps");
}

TEST_CASE("Units: fromBitsPerSec Mbps") {
        CHECK(Units::fromBitsPerSec(1000000.0) == "1 Mbps");
}

TEST_CASE("Units: fromBitsPerSec Gbps") {
        CHECK(Units::fromBitsPerSec(10000000000.0) == "10 Gbps");
}

// ============================================================================
// fromItemsPerSec
// ============================================================================

TEST_CASE("Units: fromItemsPerSec zero") {
        CHECK(Units::fromItemsPerSec(0.0) == "-");
}

TEST_CASE("Units: fromItemsPerSec sub-kilo") {
        CHECK(Units::fromItemsPerSec(500.0) == "500");
}

TEST_CASE("Units: fromItemsPerSec kilo") {
        CHECK(Units::fromItemsPerSec(1500.0) == "1.5k");
}

TEST_CASE("Units: fromItemsPerSec mega") {
        CHECK(Units::fromItemsPerSec(2500000.0) == "2.5M");
}

TEST_CASE("Units: fromItemsPerSec giga") {
        CHECK(Units::fromItemsPerSec(1000000000.0) == "1G");
}

TEST_CASE("Units: fromItemsPerSec tera") {
        CHECK(Units::fromItemsPerSec(1500000000000.0) == "1.5T");
}
