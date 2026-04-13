/**
 * @file      periodiccallback.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/periodiccallback.h>

PROMEKI_NAMESPACE_BEGIN

PeriodicCallback::PeriodicCallback(double intervalSeconds, Function func) :
        _func(std::move(func)),
        _intervalSeconds(intervalSeconds)
{
}

bool PeriodicCallback::service() {
        if(!_func || _intervalSeconds <= 0.0) return false;
        if(!_started) {
                _stamp = TimeStamp::now();
                _started = true;
                return false;
        }
        if(_stamp.elapsedSeconds() < _intervalSeconds) return false;
        _stamp = TimeStamp::now();
        _func();
        return true;
}

void PeriodicCallback::reset() {
        _stamp = TimeStamp::now();
        _started = false;
}

PROMEKI_NAMESPACE_END
