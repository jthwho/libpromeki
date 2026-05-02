/**
 * @file      duration.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/duration.h>
#include <promeki/error.h>
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

Duration Duration::fromString(const String &str, Error *err) {
        const String s = str.trim();
        if (s.isEmpty()) {
                if (err) *err = Error::Invalid;
                return Duration();
        }

        // Walk past the magnitude — sign + digits + optional decimal —
        // to find the unit suffix.  We hand the magnitude prefix to
        // String::to<double>() so locale-independent decimal parsing
        // is consistent with the rest of the library.
        size_t       i      = 0;
        const size_t n      = s.length();
        if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
        bool sawDigit = false;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
                ++i;
                sawDigit = true;
        }
        if (i < n && s[i] == '.') {
                ++i;
                while (i < n && s[i] >= '0' && s[i] <= '9') {
                        ++i;
                        sawDigit = true;
                }
        }
        if (!sawDigit) {
                if (err) *err = Error::Invalid;
                return Duration();
        }

        // Optional whitespace between magnitude and unit.
        const String magStr = s.left(i);
        size_t       u      = i;
        while (u < n && (s[u] == ' ' || s[u] == '\t')) ++u;
        const String unit = s.mid(u).toLower();

        Error  pe;
        double mag = magStr.to<double>(&pe);
        if (pe.isError() || !std::isfinite(mag)) {
                if (err) *err = Error::Invalid;
                return Duration();
        }

        // Map the suffix to a nanosecond multiplier.  An empty suffix
        // is interpreted as seconds — see the doxygen on the
        // header-side declaration for the rationale.
        double nsPerUnit = 0.0;
        if (unit.isEmpty() || unit == "s")                              nsPerUnit = 1'000'000'000.0;
        else if (unit == "ms")                                          nsPerUnit = 1'000'000.0;
        else if (unit == "us" || unit == "\xc2\xb5s" /* µs UTF-8 */)    nsPerUnit = 1'000.0;
        else if (unit == "ns")                                          nsPerUnit = 1.0;
        else if (unit == "m")                                           nsPerUnit = 60.0 * 1'000'000'000.0;
        else if (unit == "h")                                           nsPerUnit = 3600.0 * 1'000'000'000.0;
        else {
                if (err) *err = Error::Invalid;
                return Duration();
        }

        const double ns = mag * nsPerUnit;
        return Duration::fromNanoseconds(static_cast<int64_t>(ns));
}

PROMEKI_NAMESPACE_END
