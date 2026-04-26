/**
 * @file      units.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/units.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        struct ScaleEntry {
                        double      threshold;
                        double      divisor;
                        const char *suffix;
        };

        String trimTrailingZeros(const String &s) {
                size_t dotPos = s.size();
                for (size_t i = 0; i < s.size(); ++i) {
                        if (s.charAt(i) == '.') {
                                dotPos = i;
                                break;
                        }
                }
                if (dotPos == s.size()) return s;
                size_t lastKeep = s.size();
                while (lastKeep > dotPos + 1 && s.charAt(lastKeep - 1) == '0') {
                        --lastKeep;
                }
                if (lastKeep == dotPos + 1) --lastKeep;
                if (lastKeep < s.size()) {
                        String ret = s;
                        ret.erase(lastKeep, s.size() - lastKeep);
                        return ret;
                }
                return s;
        }

        String autoScale(double value, const ScaleEntry *table, int count, int precision) {
                bool              neg = value < 0.0;
                double            abs = neg ? -value : value;
                const ScaleEntry *entry = &table[0];
                for (int i = count - 1; i >= 0; --i) {
                        if (abs >= table[i].threshold) {
                                entry = &table[i];
                                break;
                        }
                }
                double scaled = (neg ? -abs : abs) / entry->divisor;
                return trimTrailingZeros(String::number(scaled, precision)) + " " + entry->suffix;
        }

        String formatByteCount(uint64_t bytes, int maxDecimals, bool binary) {
                static const char *metricUnits[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
                static const char *binaryUnits[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
                constexpr size_t   unitCount = sizeof(metricUnits) / sizeof(metricUnits[0]);

                if (maxDecimals < 0) maxDecimals = 0;
                if (maxDecimals > 9) maxDecimals = 9;

                const double       base = binary ? 1024.0 : 1000.0;
                const char *const *units = binary ? binaryUnits : metricUnits;

                if (bytes < static_cast<uint64_t>(base)) {
                        return String::sprintf("%llu %s", (unsigned long long)bytes, units[0]);
                }

                double value = static_cast<double>(bytes);
                size_t unit = 0;
                while (value >= base && unit + 1 < unitCount) {
                        value /= base;
                        ++unit;
                }

                String ret = String::sprintf("%.*f %s", maxDecimals, value, units[unit]);
                if (maxDecimals > 0) {
                        const size_t len = ret.size();
                        size_t       spacePos = len;
                        for (size_t i = 0; i < len; ++i) {
                                if (ret.charAt(i) == ' ') {
                                        spacePos = i;
                                        break;
                                }
                        }
                        size_t dotPos = spacePos;
                        for (size_t i = 0; i < spacePos; ++i) {
                                if (ret.charAt(i) == '.') {
                                        dotPos = i;
                                        break;
                                }
                        }
                        if (dotPos < spacePos) {
                                size_t lastKeep = spacePos;
                                while (lastKeep > dotPos + 1 && ret.charAt(lastKeep - 1) == '0') {
                                        --lastKeep;
                                }
                                if (lastKeep == dotPos + 1) --lastKeep;
                                if (lastKeep < spacePos) {
                                        ret.erase(lastKeep, spacePos - lastKeep);
                                }
                        }
                }
                return ret;
        }

} // namespace

String Units::fromByteCount(uint64_t bytes, int maxDecimals) {
        return ::promeki::formatByteCount(bytes, maxDecimals, false);
}

String Units::fromByteCount(uint64_t bytes, int maxDecimals, const ByteCountStyle &style) {
        return ::promeki::formatByteCount(bytes, maxDecimals, style.value() == ByteCountStyle::Binary.value());
}

String Units::fromDurationNs(double ns, int precision) {
        static const ScaleEntry table[] = {
                {0.0, 1.0, "ns"},    {1.0e3, 1.0e3, "us"},  {1.0e6, 1.0e6, "ms"},
                {1.0e9, 1.0e9, "s"}, {60.0e9, 60.0e9, "m"}, {3.6e12, 3600.0e9, "h"},
        };
        return autoScale(ns, table, 6, precision);
}

String Units::fromDuration(double seconds, int precision) {
        return fromDurationNs(seconds * 1.0e9, precision);
}

String Units::fromFrequency(double hz, int precision) {
        static const ScaleEntry table[] = {
                {0.0, 1.0, "Hz"},
                {1.0e3, 1.0e3, "kHz"},
                {1.0e6, 1.0e6, "MHz"},
                {1.0e9, 1.0e9, "GHz"},
        };
        return autoScale(hz, table, 4, precision);
}

String Units::fromSampleCount(uint64_t samples, uint32_t sampleRate, int precision) {
        if (sampleRate == 0) return String("0 s");
        double seconds = static_cast<double>(samples) / static_cast<double>(sampleRate);
        return fromDuration(seconds, precision);
}

String Units::fromBytesPerSec(double bps, int precision) {
        if (bps <= 0.0) return String("-");
        static const ScaleEntry table[] = {
                {0.0, 1.0, "B/s"},
                {1024.0, 1024.0, "KB/s"},
                {1048576.0, 1048576.0, "MB/s"},
                {1.073741824e9, 1073741824.0, "GB/s"},
        };
        return autoScale(bps, table, 4, precision);
}

String Units::fromBitsPerSec(double bps, int precision) {
        if (bps <= 0.0) return String("-");
        static const ScaleEntry table[] = {
                {0.0, 1.0, "bps"},
                {1.0e3, 1.0e3, "Kbps"},
                {1.0e6, 1.0e6, "Mbps"},
                {1.0e9, 1.0e9, "Gbps"},
        };
        return autoScale(bps, table, 4, precision);
}

String Units::fromItemsPerSec(double ips, int precision) {
        if (ips <= 0.0) return String("-");
        if (ips < 1.0e3) return trimTrailingZeros(String::number(ips, precision));
        if (ips < 1.0e6) return trimTrailingZeros(String::number(ips / 1.0e3, precision)) + "k";
        if (ips < 1.0e9) return trimTrailingZeros(String::number(ips / 1.0e6, precision)) + "M";
        if (ips < 1.0e12) return trimTrailingZeros(String::number(ips / 1.0e9, precision)) + "G";
        return trimTrailingZeros(String::number(ips / 1.0e12, precision)) + "T";
}

PROMEKI_NAMESPACE_END
