/**
 * @file      pidcontroller.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#pragma once

#include <promeki/namespace.h>
#include <functional>

PROMEKI_NAMESPACE_BEGIN

/** Discrete Proportional-Integral-Derivative (D-PID) controller
 * This class implements a PID 
 * To use it, you'll need the following:
 * 
 * - A function that returns a ValueType.  This would be the type of the
 *   value that you're feeding into your device/object/whatever under control.
 *   Most likely it'll be some form of a floating point value, but doesn't have
 *   to be if the object implements all the required math used by this function
 *   (i.e. could be give an object that abstracts integer math for systems w/o
 *    floating point)
 *   
 *   This function should return the currently measured value of the device/object
 *   under control.  In a closed-loop use case, this would return the instantanious
 *   measurement of that value (i.e. the actual speed of the car as measured by the
 *   speedometer).  However, you can also simply provide the last value that was
 *   requested by the step() function.  This would effectively be using the PID in
 *   open-loop mode.  Which, still gives you the benifits of filtering of set point,
 *   but doesn't actually react to actual device behavior.
 *
 * - A function that returns a TimeType.  This would typically be a floating point
 *   continuously linear incrementing number of seconds, but technically doesn't have to be
 *   in seconds, just that it represents some linear time progression.  Note that a single
 *   integer step is assumed to be the integration and derivative time.
 */

template <typename ValueType = double, typename TimeType = double>
class PIDController {
 public:
     using CurrentTimeFunc = std::function<TimeType()>;
     using CurrentValueFunc = std::function<ValueType()>;

     PIDController(CurrentValueFunc currentValueFunc, CurrentTimeFunc currentTimeFunc)
         : _gainP(1), _gainI(0), _gainD(0), 
           _setPoint(0), _integral(0), _prevError(0), 
           _currentValue(currentValueFunc), _currentTime(currentTimeFunc),
           _prevUpdate(_currentTime()) {}

     // Get and set the proportional gain
     ValueType getGainP() const { return _gainP; }
     void setGainP(const ValueType &val) { _gainP = val; }

     // Get and set the integral gain
     ValueType getGainI() const { return _gainI; }
     void setGainI(const ValueType &val) { _gainI = val; }

     // Get and set the derivative gain
     ValueType getGainD() const { return _gainD; }
     void setGainD(const ValueType &val) { _gainD = val; }

     // Updates the set point (the value you'd like the PID to converge on)
     void updateSetPoint(const ValueType &val) { _setPoint = val; }

     // Steps the PID in time and returns the control value that should be sent to whatever the
     // PID is controlling.  Typically you'd call this in equal time slices and as often as possible 
     // and as frequently as possible.  If this is called too infrequently, the PID may not be able
     // to cope with changes effectively.
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

     // Resets the PID internal state
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
     TimeType _prevUpdate = 0;
     CurrentValueFunc _currentValue;
     CurrentTimeFunc _currentTime;
};


PROMEKI_NAMESPACE_END


