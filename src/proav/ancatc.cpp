/**
 * @file      ancatc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ancatc.h>
#include <promeki/ancformat.h>
#include <promeki/datastream.h>
#include <promeki/json.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// HFR alternation helper (D1f / ST 12-3:2016 §6)
// ============================================================================

int AncAtc::atcVitcFormatForFrame(uint32_t frameRateFps, uint64_t frameIndex) {
        if (!ancAtcIsHfrRate(frameRateFps)) {
                return static_cast<int>(AncFormat::AtcVitc1);
        }
        // HFR: alternate per physical frame so each frame-pair carries
        // one VITC1 (field-mark=0) followed by one VITC2 (field-mark=1).
        return static_cast<int>((frameIndex & 1u) == 0u ? AncFormat::AtcVitc1
                                                       : AncFormat::AtcVitc2);
}

// ============================================================================
// Diagnostics
// ============================================================================

String AncAtc::toString() const {
        // Lead with the timecode in canonical SMPTE form so log lines
        // are scannable; suffix any non-default extras.
        String s = "AncAtc(tc=";
        s += _tc.toString();
        bool anyUserBit = false;
        for (size_t i = 0; i < UserBitCount; ++i) {
                if (_userBits[i] != 0) {
                        anyUserBit = true;
                        break;
                }
        }
        if (anyUserBit) {
                s += ", userBits={";
                for (size_t i = 0; i < UserBitCount; ++i) {
                        if (i > 0) s += ",";
                        s += String::number(static_cast<int>(_userBits[i]));
                }
                s += "}";
        }
        if (_flags != 0) {
                s += ", flags=0x";
                s += String::number(static_cast<int>(_flags), 16);
        }
        if (_payloadType != Ltc) {
                s += ", payloadType=0x";
                s += String::number(static_cast<int>(_payloadType), 16);
        }
        if (_dbb2 != 0) {
                s += ", dbb2=0x";
                s += String::number(static_cast<int>(_dbb2), 16);
        }
        s += ")";
        return s;
}

JsonObject AncAtc::toJson() const {
        JsonObject obj;
        obj.set("timeCode", _tc.toString());
        JsonArray ub;
        for (size_t i = 0; i < UserBitCount; ++i) {
                ub.add(static_cast<int64_t>(_userBits[i]));
        }
        obj.set("userBits", ub);
        obj.set("flags", static_cast<int64_t>(_flags));
        obj.set("colorFrame", colorFrame());
        obj.set("polarity", polarity());
        obj.set("bgf0", bgf0());
        obj.set("bgf1", bgf1());
        obj.set("bgf2", bgf2());
        obj.set("payloadType", static_cast<int64_t>(_payloadType));
        obj.set("dbb2", static_cast<int64_t>(_dbb2));
        return obj;
}

// ============================================================================
// DataStream wire format (v1: timecode + 8 user-bit bytes + flags +
// payloadType + dbb2).
// ============================================================================

Error AncAtc::writeToStream(DataStream &s) const {
        s << _tc;
        // Eight uint8_t user-bit nibbles, then flags + payloadType +
        // dbb2.  Each is tagged via DataStream operator<< so a reader
        // can refuse a malformed prefix without dropping into the
        // trailer.
        for (size_t i = 0; i < UserBitCount; ++i) {
                s << static_cast<uint8_t>(_userBits[i]);
        }
        s << static_cast<uint8_t>(_flags);
        s << static_cast<uint8_t>(_payloadType);
        s << static_cast<uint8_t>(_dbb2);
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<AncAtc> AncAtc::readFromStream<1>(DataStream &s) {
        AncAtc out;
        Timecode tc;
        s >> tc;
        if (s.status() != DataStream::Ok) return makeError<AncAtc>(s.toError());
        out.setTimecode(tc);
        UserBits ub{};
        for (size_t i = 0; i < UserBitCount; ++i) {
                uint8_t b = 0;
                s >> b;
                if (s.status() != DataStream::Ok) return makeError<AncAtc>(s.toError());
                ub[i] = static_cast<uint8_t>(b & 0x0F);
        }
        out.setUserBits(ub);
        uint8_t flags = 0;
        uint8_t payloadType = 0;
        uint8_t dbb2 = 0;
        s >> flags >> payloadType >> dbb2;
        if (s.status() != DataStream::Ok) return makeError<AncAtc>(s.toError());
        out.setFlags(flags);
        out.setPayloadType(payloadType);
        out.setDbb2(dbb2);
        return makeResult<AncAtc>(std::move(out));
}

PROMEKI_NAMESPACE_END
