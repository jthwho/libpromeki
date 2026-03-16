/**
 * @file      core/pidcontroller.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <functional>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Discrete Proportional-Integral-Derivative (PID) controller.
 * @ingroup core_math
 *
 * Implements a standard PID control loop parameterized on value and time types.
 * The controller requires two callbacks: one that returns the current measured
 * value of the process variable (for closed-loop control) or the last requested
 * output (for open-loop control), and one that returns the current time as a
 * monotonically increasing value.
 *
 * @tparam ValueType The numeric type for gains, set point, and control output (default: double).
 * @tparam TimeType  The numeric type for time values (default: double).
 */
template <typename ValueType = double, typename TimeType = double>
class PIDController {
 public:
     /** @brief Callback type that returns the current time. */
     using CurrentTimeFunc = std::function<TimeType()>;
     /** @brief Callback type that returns the current measured process value. */
     using CurrentValueFunc = std::function<ValueType()>;

     /**
      * @brief Constructs a PID controller with the given feedback callbacks.
      * @param currentValueFunc Callback returning the current process value.
      * @param currentTimeFunc  Callback returning the current time.
      */
     PIDController(CurrentValueFunc currentValueFunc, CurrentTimeFunc currentTimeFunc)
         : _gainP(1), _gainI(0), _gainD(0),
           _setPoint(0), _integral(0), _prevError(0),
           _currentValue(currentValueFunc), _currentTime(currentTimeFunc),
           _prevUpdate(_currentTime()) {}

     /** @brief Returns the proportional gain. */
     ValueType getGainP() const { return _gainP; }

     /**
      * @brief Sets the proportional gain.
      * @param val The new proportional gain value.
      */
     void setGainP(const ValueType &val) { _gainP = val; }

     /** @brief Returns the integral gain. */
     ValueType getGainI() const { return _gainI; }

     /**
      * @brief Sets the integral gain.
      * @param val The new integral gain value.
      */
     void setGainI(const ValueType &val) { _gainI = val; }

     /** @brief Returns the derivative gain. */
     ValueType getGainD() const { return _gainD; }

     /**
      * @brief Sets the derivative gain.
      * @param val The new derivative gain value.
      */
     void setGainD(const ValueType &val) { _gainD = val; }

     /**
      * @brief Updates the set point (target value the PID should converge on).
      * @param val The desired target value.
      */
     void updateSetPoint(const ValueType &val) { _setPoint = val; }

     /**
      * @brief Advances the PID by one time step and returns the control output.
      *
      * Should be called at regular intervals as frequently as possible.
      * If called too infrequently, the controller may not respond to
      * changes effectively.
      *
      * @return The computed control value to apply to the controlled device.
      */
     ValueType step() {
         TimeType now = _currentTime();
         TimeType dt = now - _prevUpdate;
         _prevUpdate = now;

         ValueType pv = _currentValue();
         ValueType error = _setPoint - pv;
         _integral += error * dt;
         ValueType derivative = (error - _prevError) / dt;
         _prevError = error;

         return _gainP * error + _gainI * _integral + _gainD * derivative;
     }

     /** @brief Resets the integral accumulator, previous error, and update timestamp. */
     void reset() {
         _integral = {};
         _prevError = {};
         _prevUpdate = _currentTime();
         return;
     }

private:
     ValueType _gainP = 1;
     ValueType _gainI = 1;
     ValueType _gainD = 1;
     ValueType _integral = 0;
     ValueType _prevError = 0;
     ValueType _setPoint = 0;
     CurrentValueFunc _currentValue;
     CurrentTimeFunc _currentTime;
     TimeType _prevUpdate = 0;
};


PROMEKI_NAMESPACE_END


