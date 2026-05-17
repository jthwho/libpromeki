/**
 * @file      framerate.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <promeki/framerate.h>
#include <promeki/datastream.h>
#include <promeki/structdatabase.h>

PROMEKI_NAMESPACE_BEGIN

struct WellKnownFrameRate {
                FrameRate::WellKnownRate id;
                String                   name;
                FrameRate::RationalType  fps;
                double                   fpsd;
};

#define X(ID, NAME, NUM, DEN)                                                                                          \
        {.id = FrameRate::ID,                                                                                          \
         .name = NAME,                                                                                                 \
         .fps = FrameRate::RationalType(NUM, DEN),                                                                     \
         .fpsd = static_cast<double>(NUM) / static_cast<double>(DEN)},
static StructDatabase<FrameRate::WellKnownRate, WellKnownFrameRate> db = {PROMEKI_WELL_KNOWN_FRAME_RATES};
#undef X

List<FrameRate::WellKnown> FrameRate::wellKnownRates() {
        // The internal db is populated by PROMEKI_WELL_KNOWN_FRAME_RATES,
        // which lists entries from fastest to slowest with FPS_Invalid
        // first.  Build the public list in the opposite order so the
        // resulting dropdown reads naturally from low to high (23.98 →
        // 120) and skip the invalid sentinel.
        List<FrameRate::WellKnown> out;
        const auto                &all = db.database();
        for (auto it = all.crbegin(); it != all.crend(); ++it) {
                if (it->first == FPS_Invalid) continue;
                FrameRate::WellKnown entry;
                entry.label = it->second.name;
                entry.rate = FrameRate(it->first);
                out.pushToBack(entry);
        }
        return out;
}

FrameRate::FrameRate(WellKnownRate rate) {
        const WellKnownFrameRate &item = db.get(rate);
        if (item.id != FPS_Invalid) {
                _fps = item.fps;
        }
}

FrameRate::FrameRate(const RationalType &r) : _fps(r) {}

FrameRate::WellKnownRate FrameRate::wellKnownRate() const {
        if (!isValid()) return FPS_NotWellKnown;
        for (const auto &pair : db.database()) {
                if (pair.first == FPS_Invalid) continue;
                if (pair.second.fps == _fps) return pair.first;
        }
        return FPS_NotWellKnown;
}


Result<FrameRate> FrameRate::fromDouble(double val) {
        if (std::isnan(val) || std::isinf(val) || val <= 0.0) {
                return makeError<FrameRate>(Error::ParseFailed);
        }
        // Snap to a well-known rate when we're within 0.01 fps — that's
        // tight enough to disambiguate 23.976 (24000/1001 ≈ 23.976024)
        // from neighbouring entries (24.0, 25.0) and loose enough to
        // catch the common "23.98" rounding.
        constexpr double kTolerance = 0.01;
        for (const auto &pair : db.database()) {
                if (pair.first == FPS_Invalid) continue;
                if (std::abs(pair.second.fpsd - val) <= kTolerance) {
                        return makeResult(FrameRate(pair.first));
                }
        }
        // Positive integer exact match — construct the (N, 1)
        // rational directly so e.g. 90.0 fps round-trips through
        // toDouble() exactly even though it isn't in the well-known
        // table.
        if (val < static_cast<double>(std::numeric_limits<unsigned int>::max())) {
                double rounded = std::nearbyint(val);
                if (rounded == val && rounded >= 1.0) {
                        return makeResult(FrameRate(FrameRate::RationalType(
                                static_cast<unsigned int>(rounded), 1u)));
                }
        }
        return makeError<FrameRate>(Error::ParseFailed);
}

Result<FrameRate> FrameRate::fromString(const String &str) {
        // Canonical sentinel forms — the default-constructed
        // FrameRate() carries a 0/1 Rational, which Rational::toString
        // emits as "0/1".  An empty input from JSON tooling also
        // routes here.  Accept both so defaults declared as
        // setDefault(FrameRate()) survive a toString → fromString
        // round-trip cleanly.
        if (str.isEmpty() || str == "0/0" || str == "0/1") {
                return makeResult(FrameRate());
        }

        // Try well-known rate names first
        for (const auto &pair : db.database()) {
                if (pair.second.name == str) {
                        return makeResult(FrameRate(pair.first));
                }
        }

        // Also accept "23.976" as an alias for "23.98"
        if (str == "23.976") return makeResult(FrameRate(FPS_23_98));

        // Try fraction form: num/den
        const char *slash = strchr(str.cstr(), '/');
        if (slash) {
                unsigned int num = static_cast<unsigned int>(atoi(str.cstr()));
                unsigned int den = static_cast<unsigned int>(atoi(slash + 1));
                if (num > 0 && den > 0) {
                        return makeResult(FrameRate(RationalType(num, den)));
                }
        }

        return makeError<FrameRate>(Error::Invalid);
}

// ============================================================================
// DataStream wire format (v1: two tagged uint32 fields, numerator/denominator).
// ============================================================================

Error FrameRate::writeToStream(DataStream &s) const {
        s << static_cast<uint32_t>(_fps.numerator());
        s << static_cast<uint32_t>(_fps.denominator());
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<FrameRate> FrameRate::readFromStream<1>(DataStream &s) {
        uint32_t num = 0, den = 1;
        s >> num >> den;
        if (s.status() != DataStream::Ok) return makeError<FrameRate>(s.toError());
        if (den == 0) return makeError<FrameRate>(Error::CorruptData);
        return makeResult(FrameRate(RationalType(num, den)));
}

PROMEKI_NAMESPACE_END
