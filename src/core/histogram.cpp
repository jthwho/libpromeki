/**
 * @file      histogram.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <climits>
#include <promeki/histogram.h>

PROMEKI_NAMESPACE_BEGIN

Histogram::Histogram() {
        reset();
}

void Histogram::setName(const String &name) {
        _name = name;
}

void Histogram::setUnit(const String &unit) {
        _unit = unit;
}

void Histogram::reset() {
        _count = 0;
        _sum   = 0;
        _min   = INT64_MAX;
        _max   = INT64_MIN;
        for(size_t i = 0; i < BucketCount; i++) _buckets[i] = 0;
}

void Histogram::addSample(int64_t value) {
        if(value < 0) value = 0;
        _count++;
        _sum += value;
        if(value < _min) _min = value;
        if(value > _max) _max = value;

        // Bucket index = octave * SubBucketsPerOctave + sub-bucket.
        //
        // Octave is floor(log2(value)) for value >= 1, with value 0
        // mapping to octave 0.  __builtin_clzll on a non-zero
        // unsigned value returns the count of leading zeros, and
        // 63 - clzll(v) is the position of the most significant set
        // bit — exactly floor(log2(v)).
        //
        // Within an octave the sub-bucket is computed as
        // ((v - 2^O) * SubBucketsPerOctave) / 2^O, which is just
        // the upper @c log2(SubBucketsPerOctave) bits of v immediately
        // below the leading bit.  We extract that with a shift so
        // there's no division on the hot path.
        size_t bucket = 0;
        if(value > 0) {
                const uint64_t v = static_cast<uint64_t>(value);
                const int octave = 63 - __builtin_clzll(v);
                size_t subBucket = 0;
                constexpr int subBits =
                        __builtin_ctz(SubBucketsPerOctave); // 4 for 16
                if(octave >= subBits) {
                        // Drop the leading bit (always 1 for non-zero v)
                        // and keep the next subBits bits.
                        const int shift = octave - subBits;
                        subBucket = static_cast<size_t>(
                                (v >> shift) & (SubBucketsPerOctave - 1));
                } else {
                        // Octaves smaller than the sub-bucket count
                        // fit entirely in the low end of the linear
                        // bucket region; the sub-bucket index is
                        // just the value scaled to the sub range.
                        subBucket = static_cast<size_t>(v) & (SubBucketsPerOctave - 1);
                }
                bucket = static_cast<size_t>(octave) * SubBucketsPerOctave + subBucket;
        }
        if(bucket >= BucketCount) bucket = BucketCount - 1;
        _buckets[bucket]++;
}

double Histogram::mean() const {
        if(_count <= 0) return 0.0;
        return static_cast<double>(_sum) / static_cast<double>(_count);
}

int64_t Histogram::percentile(double p) const {
        if(_count <= 0) return 0;
        if(p <= 0.0) return _min;
        if(p >= 1.0) return _max;

        // Walk the cumulative bucket counts until we land on the
        // first non-empty bucket whose running total covers the
        // requested rank.  The target rank is computed as a
        // floating-point fraction of the total count so the
        // single-sample / low-count cases don't truncate to zero
        // (e.g. count=1, p=0.5 truncates to int 0 and would
        // otherwise return the first bucket regardless of where
        // the actual sample lives).  Empty buckets are skipped so
        // the answer is always pulled from a bucket that contains
        // at least one observation.  The returned value is the
        // midpoint of the matched sub-bucket, clamped to the
        // observed [min, max] range so the percentile estimate
        // never claims a value outside what was actually recorded.
        const double target = static_cast<double>(_count) * p;
        int64_t cumulative = 0;
        for(size_t i = 0; i < BucketCount; i++) {
                cumulative += _buckets[i];
                if(_buckets[i] > 0 &&
                   static_cast<double>(cumulative) >= target) {
                        const size_t octave = i / SubBucketsPerOctave;
                        const size_t sub    = i % SubBucketsPerOctave;
                        const int64_t octaveLow = (octave == 0) ?
                                0 :
                                (static_cast<int64_t>(1) << octave);
                        // Sub-bucket width = 2^octave / SubBucketsPerOctave
                        // = 1 << (octave - log2(SubBucketsPerOctave))
                        // for octaves big enough that the bit shift
                        // is non-negative; otherwise the sub-bucket
                        // is one integer wide.
                        constexpr int subBits =
                                __builtin_ctz(SubBucketsPerOctave);
                        int64_t subWidth;
                        if(static_cast<int>(octave) >= subBits) {
                                subWidth = static_cast<int64_t>(1)
                                        << (octave - subBits);
                        } else {
                                subWidth = 1;
                        }
                        const int64_t subLow =
                                octaveLow + static_cast<int64_t>(sub) * subWidth;
                        const int64_t subHigh = subLow + subWidth;
                        int64_t mid = subLow + (subHigh - subLow) / 2;
                        // Clamp to observed range so percentile
                        // estimates never lie outside [min, max].
                        if(mid < _min) mid = _min;
                        if(mid > _max) mid = _max;
                        return mid;
                }
        }
        return _max;
}

String Histogram::toString() const {
        String s;
        if(!_name.isEmpty()) {
                s += _name;
                s += ": ";
        }
        if(_count <= 0) {
                s += "no samples";
                return s;
        }
        // Use a small inline buffer to compose the summary; the
        // bucket-quantised percentile estimates are appended after
        // the exact running min/mean/max so the bucket error is
        // visually obvious when interpreting the line.
        const char *unit = _unit.cstr();
        s += String::sprintf(
                "count=%lld min=%lld%s mean=%.1f%s "
                "p50=%lld%s p95=%lld%s p99=%lld%s max=%lld%s",
                static_cast<long long>(_count),
                static_cast<long long>(_min), unit,
                mean(), unit,
                static_cast<long long>(percentile(0.50)), unit,
                static_cast<long long>(percentile(0.95)), unit,
                static_cast<long long>(percentile(0.99)), unit,
                static_cast<long long>(_max), unit);
        return s;
}

PROMEKI_NAMESPACE_END
