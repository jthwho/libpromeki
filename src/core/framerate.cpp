/**
 * @file      framerate.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <cstring>
#include <promeki/framerate.h>
#include <promeki/structdatabase.h>

PROMEKI_NAMESPACE_BEGIN

struct WellKnownFrameRate {
    FrameRate::WellKnownRate    id;
    String                      name;
    FrameRate::RationalType     fps;
    double                      fpsd;
};

#define X(ID, NAME, NUM, DEN) { \
        .id = FrameRate::ID, .name = NAME, \
        .fps = FrameRate::RationalType(NUM, DEN), \
        .fpsd = static_cast<double>(NUM) / static_cast<double>(DEN) \
    },
static StructDatabase<FrameRate::WellKnownRate, WellKnownFrameRate> db = {
    PROMEKI_WELL_KNOWN_FRAME_RATES
};
#undef X

FrameRate::FrameRate(WellKnownRate rate) {
    const WellKnownFrameRate &item = db.get(rate);
    if(item.id != FPS_Invalid) {
        _fps = item.fps;
    }
}

FrameRate::FrameRate(const RationalType &r) :
    _fps(r)
{
}

FrameRate::WellKnownRate FrameRate::wellKnownRate() const {
    if(!isValid()) return FPS_NotWellKnown;
    for(const auto &pair : db.database()) {
        if(pair.first == FPS_Invalid) continue;
        if(pair.second.fps == _fps) return pair.first;
    }
    return FPS_NotWellKnown;
}


Result<FrameRate> FrameRate::fromString(const String &str) {
        // Try well-known rate names first
        for(const auto &pair : db.database()) {
                if(pair.second.name == str) {
                        return makeResult(FrameRate(pair.first));
                }
        }

        // Also accept "23.976" as an alias for "23.98"
        if(str == "23.976") return makeResult(FrameRate(FPS_2398));

        // Try fraction form: num/den
        const char *slash = strchr(str.cstr(), '/');
        if(slash) {
                unsigned int num = static_cast<unsigned int>(atoi(str.cstr()));
                unsigned int den = static_cast<unsigned int>(atoi(slash + 1));
                if(num > 0 && den > 0) {
                        return makeResult(FrameRate(RationalType(num, den)));
                }
        }

        return makeError<FrameRate>(Error::Invalid);
}

PROMEKI_NAMESPACE_END

