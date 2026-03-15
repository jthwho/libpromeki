/**
 * @file      pidcontroller.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/pidcontroller.h>

using namespace promeki;

TEST_CASE("PIDController: construction") {
        double time = 0.0;
        double value = 0.0;
        PIDController<double, double> pid(
                [&]() { return value; },
                [&]() { return time; }
        );
        CHECK(pid.getGainP() == 1.0);
        CHECK(pid.getGainI() == 0.0);
        CHECK(pid.getGainD() == 0.0);
}

TEST_CASE("PIDController: set and get gains") {
        double time = 0.0;
        double value = 0.0;
        PIDController<double, double> pid(
                [&]() { return value; },
                [&]() { return time; }
        );
        pid.setGainP(2.0);
        pid.setGainI(0.5);
        pid.setGainD(0.1);
        CHECK(pid.getGainP() == 2.0);
        CHECK(pid.getGainI() == 0.5);
        CHECK(pid.getGainD() == 0.1);
}

TEST_CASE("PIDController: proportional-only step") {
        double time = 0.0;
        double value = 0.0;
        PIDController<double, double> pid(
                [&]() { return value; },
                [&]() { return time; }
        );
        pid.setGainP(1.0);
        pid.setGainI(0.0);
        pid.setGainD(0.0);
        pid.updateSetPoint(10.0);

        time = 1.0;
        double output = pid.step();
        // error = 10 - 0 = 10, P output = 1.0 * 10 = 10
        CHECK(output == doctest::Approx(10.0));
}

TEST_CASE("PIDController: convergence with P control") {
        double time = 0.0;
        double value = 0.0;
        PIDController<double, double> pid(
                [&]() { return value; },
                [&]() { return time; }
        );
        pid.setGainP(0.5);
        pid.setGainI(0.0);
        pid.setGainD(0.0);
        pid.updateSetPoint(100.0);

        // Simulate several steps
        for(int i = 0; i < 20; ++i) {
                time = (i + 1) * 0.1;
                double output = pid.step();
                value += output * 0.1; // Simple integration
        }
        // Value should be moving towards 100
        CHECK(value > 0.0);
}

TEST_CASE("PIDController: reset") {
        double time = 0.0;
        double value = 0.0;
        PIDController<double, double> pid(
                [&]() { return value; },
                [&]() { return time; }
        );
        pid.updateSetPoint(10.0);
        time = 1.0;
        pid.step();
        pid.reset();
        // After reset, step should behave as if starting fresh
        time = 2.0;
        double output = pid.step();
        CHECK(output == doctest::Approx(10.0));
}

TEST_CASE("PIDController: zero error produces zero output with P-only") {
        double time = 0.0;
        double value = 50.0;
        PIDController<double, double> pid(
                [&]() { return value; },
                [&]() { return time; }
        );
        pid.setGainP(1.0);
        pid.setGainI(0.0);
        pid.setGainD(0.0);
        pid.updateSetPoint(50.0); // set point equals current value

        time = 1.0;
        double output = pid.step();
        CHECK(output == doctest::Approx(0.0));
}
