/**
 * @file      duration.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/duration.h>
#include <promeki/string.h>
#include <promeki/units.h>

PROMEKI_NAMESPACE_BEGIN

String Duration::toString() const {
        int64_t total = nanoseconds();
        bool    neg = total < 0;
        if (neg) total = -total;

        int64_t h = total / 3600000000000LL;
        total %= 3600000000000LL;
        int64_t m = total / 60000000000LL;
        total %= 60000000000LL;
        int64_t s = total / 1000000000LL;
        total %= 1000000000LL;
        int64_t ms = total / 1000000LL;

        String result;
        if (neg) result += "-";
        if (h > 0) result += String::sprintf("%lldh ", (long long)h);
        if (m > 0) result += String::sprintf("%lldm ", (long long)m);
        if (ms > 0) {
                result += String::sprintf("%lld.%03llds", (long long)s, (long long)ms);
        } else {
                result += String::sprintf("%llds", (long long)s);
        }
        return result;
}

String Duration::toScaledString(int precision) const {
        return Units::fromDurationNs(static_cast<double>(nanoseconds()), precision);
}

PROMEKI_NAMESPACE_END
