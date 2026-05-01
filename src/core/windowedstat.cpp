/**
 * @file      windowedstat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <cstdio>
#include <promeki/duration.h>
#include <promeki/framecount.h>
#include <promeki/variant.h>
#include <promeki/windowedstat.h>

PROMEKI_NAMESPACE_BEGIN

WindowedStat::WindowedStat(int capacity) {
        setCapacity(capacity);
}

void WindowedStat::setCapacity(int capacity) {
        if (capacity < 0) capacity = 0;
        if (capacity == _capacity) return;
        if (capacity == 0) {
                clear();
                _capacity = 0;
                return;
        }
        // Snapshot the current samples in oldest-first order so we
        // preserve the user-visible window across the resize.  When
        // shrinking, drop the oldest entries first so the most recent
        // window survives.
        Samples ordered = values();
        int     keep = static_cast<int>(ordered.size());
        if (keep > capacity) {
                Samples trimmed;
                for (int i = keep - capacity; i < keep; ++i) {
                        trimmed.pushToBack(ordered[i]);
                }
                ordered = std::move(trimmed);
        }
        _samples = std::move(ordered);
        _capacity = capacity;
        _full = static_cast<int>(_samples.size()) == _capacity;
        _head = _full ? 0 : static_cast<int>(_samples.size());
}

int WindowedStat::count() const {
        return static_cast<int>(_samples.size());
}

void WindowedStat::push(double value) {
        if (_capacity <= 0) return;
        if (!_full) {
                _samples.pushToBack(value);
                if (static_cast<int>(_samples.size()) >= _capacity) {
                        _full = true;
                        _head = 0;
                }
                return;
        }
        _samples[_head] = value;
        _head = (_head + 1) % _capacity;
}

bool WindowedStat::push(const Variant &v) {
        switch (v.type()) {
                case Variant::TypeBool:
                case Variant::TypeU8:
                case Variant::TypeS8:
                case Variant::TypeU16:
                case Variant::TypeS16:
                case Variant::TypeU32:
                case Variant::TypeS32:
                case Variant::TypeU64:
                case Variant::TypeS64:
                case Variant::TypeFloat:
                case Variant::TypeDouble: push(v.get<double>()); return true;
                case Variant::TypeDuration: push(static_cast<double>(v.get<Duration>().nanoseconds())); return true;
                case Variant::TypeFrameCount: {
                        FrameCount fc = v.get<FrameCount>();
                        if (!fc.isFinite()) return false;
                        push(static_cast<double>(fc.value()));
                        return true;
                }
                default: return false;
        }
}

void WindowedStat::clear() {
        _samples = Samples();
        _head = 0;
        _full = false;
}

WindowedStat::Stats WindowedStat::stats() const {
        Stats out;
        out.capacity = _capacity;
        const size_t n = _samples.size();
        out.count = static_cast<int>(n);
        if (n == 0) return out;

        // First pass: min, max, and the running sum that feeds the
        // mean.  Avoids a re-scan inside @ref average so the second
        // pass only sees the deviations.
        double mn = _samples[0];
        double mx = _samples[0];
        double sum = _samples[0];
        for (size_t i = 1; i < n; ++i) {
                const double v = _samples[i];
                if (v < mn) mn = v;
                if (v > mx) mx = v;
                sum += v;
        }
        out.min = mn;
        out.max = mx;
        out.sum = sum;
        out.average = sum / static_cast<double>(n);

        // Population standard deviation requires the mean from above,
        // so it has to be a second pass.  Skip it for n<2 because the
        // result collapses to zero anyway.
        if (n >= 2) {
                double acc = 0.0;
                for (size_t i = 0; i < n; ++i) {
                        const double d = _samples[i] - out.average;
                        acc += d * d;
                }
                out.stddev = std::sqrt(acc / static_cast<double>(n));
        }
        return out;
}

double WindowedStat::min() const {
        return stats().min;
}

double WindowedStat::max() const {
        return stats().max;
}

double WindowedStat::sum() const {
        return stats().sum;
}

double WindowedStat::average() const {
        return stats().average;
}

double WindowedStat::stddev() const {
        return stats().stddev;
}

WindowedStat::Samples WindowedStat::values() const {
        if (!_full) return _samples;
        Samples out;
        out.reserve(_capacity);
        for (int i = 0; i < _capacity; ++i) {
                out.pushToBack(_samples[(_head + i) % _capacity]);
        }
        return out;
}

String WindowedStat::toString(const ValueFormatter &formatter) const {
        const Stats s = stats();
        const auto  fmt = [&formatter](double v) -> String {
                if (formatter) return formatter(v);
                return String::number(v);
        };
        String out = "Avg: ";
        out += fmt(s.average);
        out += " StdDev: ";
        out += fmt(s.stddev);
        out += " Min: ";
        out += fmt(s.min);
        out += " Max: ";
        out += fmt(s.max);
        out += " WinSz: ";
        out += String::number(s.count);
        return out;
}

String WindowedStat::toSerializedString() const {
        String out = "cap=";
        out += String::number(_capacity);
        out += ":[";
        const Samples ordered = values();
        for (size_t i = 0; i < ordered.size(); ++i) {
                if (i > 0) out += ',';
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%g", ordered[i]);
                out += buf;
        }
        out += ']';
        return out;
}

Result<WindowedStat> WindowedStat::fromString(const String &s) {
        // Accept "cap=N:[v1,v2,...]" (canonical) and "[v1,v2,...]"
        // (legacy bare-list — capacity inferred from sample count).
        String body = s.trim();
        int    capacity = -1;
        if (body.startsWith("cap=")) {
                size_t colon = body.find(':');
                if (colon == String::npos) {
                        return Result<WindowedStat>(WindowedStat(), Error::Invalid);
                }
                Error  capErr;
                String capStr = body.mid(4, colon - 4);
                capacity = capStr.to<int>(&capErr);
                if (capErr.isError()) {
                        return Result<WindowedStat>(WindowedStat(), Error::Invalid);
                }
                body = body.mid(colon + 1).trim();
        }

        if (body.isEmpty() || body[0] != '[' || body[body.byteCount() - 1] != ']') {
                return Result<WindowedStat>(WindowedStat(), Error::Invalid);
        }
        String inner = body.mid(1, body.byteCount() - 2).trim();

        WindowedStat out(capacity >= 0 ? capacity : 0);
        if (inner.isEmpty()) return Result<WindowedStat>(out, Error::Ok);

        StringList parts = inner.split(",");
        if (capacity < 0) {
                out.setCapacity(static_cast<int>(parts.size()));
        }
        for (size_t i = 0; i < parts.size(); ++i) {
                Error  ve;
                String tok = parts[i].trim();
                double v = tok.to<double>(&ve);
                if (ve.isError()) {
                        return Result<WindowedStat>(WindowedStat(), Error::Invalid);
                }
                out.push(v);
        }
        return Result<WindowedStat>(out, Error::Ok);
}

bool WindowedStat::operator==(const WindowedStat &other) const {
        if (_capacity != other._capacity) return false;
        const Samples a = values();
        const Samples b = other.values();
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
                if (a[i] != b[i]) return false;
        }
        return true;
}

DataStream &operator<<(DataStream &stream, const WindowedStat &val) {
        stream.writeTag(DataStream::TypeWindowedStat);
        stream << static_cast<uint32_t>(val.capacity());
        const WindowedStat::Samples ordered = val.values();
        stream << static_cast<uint32_t>(ordered.size());
        for (size_t i = 0; i < ordered.size(); ++i) {
                stream << ordered[i];
        }
        return stream;
}

DataStream &operator>>(DataStream &stream, WindowedStat &val) {
        if (!stream.readTag(DataStream::TypeWindowedStat)) {
                val = WindowedStat();
                return stream;
        }
        uint32_t capacity = 0;
        uint32_t count = 0;
        stream >> capacity;
        stream >> count;
        if (stream.status() != DataStream::Ok) {
                val = WindowedStat();
                return stream;
        }
        val = WindowedStat(static_cast<int>(capacity));
        for (uint32_t i = 0; i < count && stream.status() == DataStream::Ok; ++i) {
                double v = 0.0;
                stream >> v;
                val.push(v);
        }
        return stream;
}

PROMEKI_NAMESPACE_END
