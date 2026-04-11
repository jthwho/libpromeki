/**
 * @file      statsaccumulator.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/statsaccumulator.h>
#include <cmath>

using namespace promeki;

TEST_CASE("StatsAccumulator: default state is empty") {
        StatsAccumulator acc;
        CHECK(acc.isEmpty());
        CHECK(acc.count() == 0);
        CHECK(acc.sum() == 0.0);
        CHECK(acc.sumSq() == 0.0);
        CHECK(acc.min() == 0.0);
        CHECK(acc.max() == 0.0);
        CHECK(acc.mean() == 0.0);
        CHECK(acc.variance() == 0.0);
        CHECK(acc.stddev() == 0.0);
}

TEST_CASE("StatsAccumulator: single sample") {
        StatsAccumulator acc;
        acc.add(5.0);
        CHECK_FALSE(acc.isEmpty());
        CHECK(acc.count() == 1);
        CHECK(acc.sum() == doctest::Approx(5.0));
        CHECK(acc.sumSq() == doctest::Approx(25.0));
        CHECK(acc.min() == doctest::Approx(5.0));
        CHECK(acc.max() == doctest::Approx(5.0));
        CHECK(acc.mean() == doctest::Approx(5.0));
        // Variance/stddev undefined with n=1; should return 0.
        CHECK(acc.variance() == 0.0);
        CHECK(acc.stddev() == 0.0);
}

TEST_CASE("StatsAccumulator: known samples compute expected mean and stddev") {
        StatsAccumulator acc;
        acc.add(2.0);
        acc.add(4.0);
        acc.add(4.0);
        acc.add(4.0);
        acc.add(5.0);
        acc.add(5.0);
        acc.add(7.0);
        acc.add(9.0);
        // Classic textbook example: {2,4,4,4,5,5,7,9}. Mean = 5. The sum
        // of squared deviations from the mean is 32. Sample variance uses
        // the n-1 divisor, so variance = 32/7 and stddev = sqrt(32/7).
        CHECK(acc.count() == 8);
        CHECK(acc.mean() == doctest::Approx(5.0));
        CHECK(acc.variance() == doctest::Approx(32.0 / 7.0));
        CHECK(acc.stddev() == doctest::Approx(std::sqrt(32.0 / 7.0)));
        CHECK(acc.min() == doctest::Approx(2.0));
        CHECK(acc.max() == doctest::Approx(9.0));
}

TEST_CASE("StatsAccumulator: min and max track extremes") {
        StatsAccumulator acc;
        acc.add(10.0);
        acc.add(-5.0);
        acc.add(100.0);
        acc.add(0.0);
        acc.add(50.0);
        CHECK(acc.min() == doctest::Approx(-5.0));
        CHECK(acc.max() == doctest::Approx(100.0));
}

TEST_CASE("StatsAccumulator: reset returns to empty state") {
        StatsAccumulator acc;
        acc.add(1.0);
        acc.add(2.0);
        acc.add(3.0);
        REQUIRE_FALSE(acc.isEmpty());
        acc.reset();
        CHECK(acc.isEmpty());
        CHECK(acc.count() == 0);
        CHECK(acc.sum() == 0.0);
        CHECK(acc.mean() == 0.0);
}

TEST_CASE("StatsAccumulator: merge combines counts and moments") {
        StatsAccumulator a;
        a.add(1.0);
        a.add(2.0);
        a.add(3.0);

        StatsAccumulator b;
        b.add(4.0);
        b.add(5.0);
        b.add(6.0);

        StatsAccumulator merged;
        merged.merge(a);
        merged.merge(b);

        CHECK(merged.count() == 6);
        CHECK(merged.sum() == doctest::Approx(21.0));
        CHECK(merged.mean() == doctest::Approx(3.5));
        CHECK(merged.min() == doctest::Approx(1.0));
        CHECK(merged.max() == doctest::Approx(6.0));

        // Reference stddev computed independently: sqrt(variance of {1..6}) = sqrt(3.5).
        CHECK(merged.stddev() == doctest::Approx(std::sqrt(3.5)));
}

TEST_CASE("StatsAccumulator: merging empty accumulator is a no-op") {
        StatsAccumulator a;
        a.add(1.0);
        a.add(2.0);
        StatsAccumulator empty;
        a.merge(empty);
        CHECK(a.count() == 2);
        CHECK(a.mean() == doctest::Approx(1.5));
}

TEST_CASE("StatsAccumulator: merging into empty accumulator copies state") {
        StatsAccumulator source;
        source.add(10.0);
        source.add(20.0);
        StatsAccumulator target;
        target.merge(source);
        CHECK(target.count() == 2);
        CHECK(target.mean() == doctest::Approx(15.0));
        CHECK(target.min() == doctest::Approx(10.0));
        CHECK(target.max() == doctest::Approx(20.0));
}

TEST_CASE("StatsAccumulator: negative variance from numerical error clamps to 0") {
        // When all samples are identical, the sumSq - sum*sum/n formula
        // can produce a tiny negative value due to floating-point error.
        // StatsAccumulator must clamp this to 0, not sqrt a negative.
        StatsAccumulator acc;
        for(int i = 0; i < 1000; i++) acc.add(1e-9);
        CHECK(acc.variance() >= 0.0);
        CHECK(acc.stddev() >= 0.0);
}
