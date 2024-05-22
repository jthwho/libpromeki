/**
 * @file      framerate.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

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
        _rate = item.id;
    }
}

FrameRate::FrameRate(const RationalType &r) : 
    _fps(r) 
{
    // Can we match the fps rational to any of the well known rates?
    for(const auto &pair : db.database()) {
        if(pair.second.fps == _fps) {
            _rate = pair.first;
            break;
        }
    }
}


PROMEKI_NAMESPACE_END

