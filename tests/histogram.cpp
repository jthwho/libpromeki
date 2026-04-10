/**
 * @file      histogram.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/histogram.h>
#include <promeki/duration.h>

using namespace promeki;

TEST_CASE("Histogram") {

        SUBCASE("default state") {
                Histogram h;
                CHECK(h.count() == 0);
                CHECK(h.min() == 0);
                CHECK(h.max() == 0);
                CHECK(h.mean() == doctest::Approx(0.0));
                CHECK(h.percentile(0.5) == 0);
                String s = h.toString();
                CHECK(s.contains("no samples"));
        }

        SUBCASE("single sample") {
                Histogram h;
                h.addSample(42);
                CHECK(h.count() == 1);
                CHECK(h.min() == 42);
                CHECK(h.max() == 42);
                CHECK(h.mean() == doctest::Approx(42.0));
                // Percentile is clamped to [min, max] so a
                // single-sample histogram returns the sample value
                // exactly regardless of which sub-bucket it landed
                // in.
                CHECK(h.percentile(0.5) == 42);
        }

        SUBCASE("multiple samples in same octave") {
                Histogram h;
                for(int i = 32; i < 64; i++) {
                        h.addSample(i);
                }
                CHECK(h.count() == 32);
                CHECK(h.min() == 32);
                CHECK(h.max() == 63);
                // Mean of 32..63 is 47.5
                CHECK(h.mean() == doctest::Approx(47.5));
                // The 16 sub-buckets in octave 5 give a per-bucket
                // width of 2, so the percentile estimate should be
                // within roughly 2 of the true median.
                int64_t p50 = h.percentile(0.5);
                CHECK(p50 >= 44);
                CHECK(p50 <= 50);
        }

        SUBCASE("multiple samples across octaves") {
                Histogram h;
                // 10 small samples in octave 0 (value 1)
                for(int i = 0; i < 10; i++) h.addSample(1);
                // 10 medium samples in octave 9 (value 800 ∈ [512, 1024))
                for(int i = 0; i < 10; i++) h.addSample(800);
                // 10 large samples in octave 19 (value 800000 ∈ [524288, 1048576))
                for(int i = 0; i < 10; i++) h.addSample(800000);

                CHECK(h.count() == 30);
                CHECK(h.min() == 1);
                CHECK(h.max() == 800000);

                // Median should land in the middle group (octave 9 — value 800).
                int64_t p50 = h.percentile(0.50);
                CHECK(p50 >= 512);
                CHECK(p50 < 1024);

                // 95th percentile should land in the largest group.
                int64_t p95 = h.percentile(0.95);
                CHECK(p95 >= 524288);
        }

        SUBCASE("zero-valued samples land in bucket 0") {
                Histogram h;
                h.addSample(0);
                h.addSample(0);
                h.addSample(0);
                CHECK(h.count() == 3);
                CHECK(h.min() == 0);
                CHECK(h.max() == 0);
                CHECK(h.mean() == doctest::Approx(0.0));
        }

        SUBCASE("negative samples are clamped to zero") {
                Histogram h;
                h.addSample(-100);
                h.addSample(50);
                CHECK(h.count() == 2);
                CHECK(h.min() == 0);
                CHECK(h.max() == 50);
        }

        SUBCASE("Duration overload") {
                Histogram h;
                h.addSample(Duration::fromMicroseconds(33333));
                CHECK(h.count() == 1);
                CHECK(h.min() == 33333000);  // 33333us == 33,333,000ns
        }

        SUBCASE("reset clears state") {
                Histogram h;
                for(int i = 0; i < 100; i++) h.addSample(i * 10);
                CHECK(h.count() == 100);
                h.reset();
                CHECK(h.count() == 0);
                CHECK(h.min() == 0);
                CHECK(h.max() == 0);
                CHECK(h.mean() == doctest::Approx(0.0));
        }

        SUBCASE("name and unit appear in toString") {
                Histogram h;
                h.setName("frame-interval");
                h.setUnit("us");
                h.addSample(33333);
                String s = h.toString();
                CHECK(s.contains("frame-interval"));
                CHECK(s.contains("us"));
                CHECK(s.contains("count=1"));
        }

        SUBCASE("percentile bounds: p<=0 returns min, p>=1 returns max") {
                Histogram h;
                for(int i = 1; i <= 100; i++) h.addSample(i * 100);
                CHECK(h.percentile(0.0) == 100);
                CHECK(h.percentile(-0.5) == 100);
                CHECK(h.percentile(1.0) == 10000);
                CHECK(h.percentile(1.5) == 10000);
        }

        SUBCASE("toString format includes the expected fields") {
                Histogram h;
                h.setName("test");
                h.setUnit("ns");
                for(int i = 1; i <= 1000; i++) h.addSample(i);
                String s = h.toString();
                CHECK(s.contains("test"));
                CHECK(s.contains("count=1000"));
                CHECK(s.contains("min="));
                CHECK(s.contains("mean="));
                CHECK(s.contains("p50="));
                CHECK(s.contains("p95="));
                CHECK(s.contains("p99="));
                CHECK(s.contains("max="));
        }

        SUBCASE("monotone increasing samples produce monotone percentiles") {
                Histogram h;
                for(int i = 1; i <= 1000; i++) h.addSample(i * 1000);
                int64_t p25 = h.percentile(0.25);
                int64_t p50 = h.percentile(0.50);
                int64_t p75 = h.percentile(0.75);
                int64_t p99 = h.percentile(0.99);
                CHECK(p25 <= p50);
                CHECK(p50 <= p75);
                CHECK(p75 <= p99);
        }
}
