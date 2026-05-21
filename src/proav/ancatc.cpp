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
        // ≤30 fps: every physical frame carries its own ATC packet; the
        // pair-rate alternation rule doesn't apply.
        if (!ancAtcIsHfrRate(frameRateFps)) {
                return static_cast<int>(AncFormat::AtcVitc1);
        }
        // At HFRTC rates (72/96/100/120) the alternation result is
        // informational only — receivers at those rates must use
        // ATC_HFRTC (SDID=0x61) for conformant per-physical-frame
        // recovery (a single field-mark bit can't encode the N>2
        // sub-frame phase).  Returning the same alternation as
        // pair-rates lets callers that build a "best-effort" VITC1/
        // VITC2 stream at HFRTC rates still get a deterministic
        // payload-type sequence, but the result is lossy.
        return static_cast<int>((frameIndex & 1u) == 0u ? AncFormat::AtcVitc1
                                                       : AncFormat::AtcVitc2);
}

// ============================================================================
// Diagnostics
// ============================================================================

String AncAtc::toString() const {
        String s = "AncAtc(tc=";
        s += _tc.toString();
        if (_tc.colorFrame()) {
                s += ", colorFrame";
        }
        if (_tc.userbits().toUint32() != 0u ||
            _tc.userbits().mode() != TimecodeUserbits::Unspecified) {
                s += ", ";
                s += _tc.userbits().toString();
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
        obj.set("colorFrame", _tc.colorFrame());
        obj.set("userbits", _tc.userbits().toJson());
        obj.set("payloadType", static_cast<int64_t>(_payloadType));
        obj.set("dbb2", static_cast<int64_t>(_dbb2));
        // Surface a decoded view of DBB2 keyed on payload type.  Callers that
        // want the raw byte still see @c dbb2; the @c dbb2Decoded sub-object
        // is intentionally redundant — a debugging convenience.
        JsonObject decoded;
        if (isHfrtcPayload()) {
                HfrtcDbb2 d   = dbb2DecodeHfrtc(_dbb2);
                decoded.set("kind", String("hfrtc"));
                decoded.set("superFrameCount", static_cast<int64_t>(d.superFrameCount));
                decoded.set("n", static_cast<int64_t>(d.n));
        } else {
                VitcDbb2 d = dbb2DecodeVitc(_dbb2);
                decoded.set("kind", String("vitc"));
                decoded.set("line", static_cast<int64_t>(d.line));
                decoded.set("duplicate", d.duplicate);
                decoded.set("valid", d.valid);
                decoded.set("processed", d.processed);
        }
        obj.set("dbb2Decoded", decoded);
        return obj;
}

// ============================================================================
// DataStream wire format
// ============================================================================
//
// v1: timecode + 8 user-bit bytes + flags (CF/Polarity/BGF0/1/2) +
//     payloadType + dbb2.  Phase 4 dropped the user-bits / flags fields
//     from AncAtc onto Timecode; the v1 reader keeps reading them off the
//     wire and (a) stamps userBits + colorFrame onto the Timecode (so the
//     captured info is preserved) and (b) drops the polarity flag (libvtc
//     recomputes it at LTC pack time).
//
// v2 (current): timecode + payloadType + dbb2.  All user-bit / color-frame
//     / BGF info rides on the embedded @ref Timecode.

Error AncAtc::writeToStream(DataStream &s) const {
        s << _tc;
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
        // 8 user-bit nibbles (each as a uint8_t).
        TimecodeUserbits::Nibbles ubNibs{};
        for (size_t i = 0; i < 8; ++i) {
                uint8_t b = 0;
                s >> b;
                if (s.status() != DataStream::Ok) return makeError<AncAtc>(s.toError());
                ubNibs[i] = static_cast<uint8_t>(b & 0x0Fu);
        }
        uint8_t flags = 0;
        uint8_t payloadType = 0;
        uint8_t dbb2 = 0;
        s >> flags >> payloadType >> dbb2;
        if (s.status() != DataStream::Ok) return makeError<AncAtc>(s.toError());

        // Replay v1 flags onto Timecode.  The Phase 4 AncAtc enum used:
        //   bit 0 = ColorFrame, bit 1 = Polarity, bit 2 = BGF0,
        //   bit 3 = BGF1,       bit 4 = BGF2.
        // We drop the Polarity bit (recomputed by libvtc).
        if (flags & 0x01u) tc.setColorFrame(true);
        TimecodeUserbits::Mode mode = TimecodeUserbits::Unspecified;
        uint8_t modeBits = 0;
        if (flags & 0x04u) modeBits |= 0x01u; // BGF0
        if (flags & 0x08u) modeBits |= 0x02u; // BGF1
        if (flags & 0x10u) modeBits |= 0x04u; // BGF2
        mode = static_cast<TimecodeUserbits::Mode>(modeBits);
        tc.setUserbits(TimecodeUserbits::fromNibbles(ubNibs, mode));

        out.setTimecode(tc);
        out.setPayloadType(payloadType);
        out.setDbb2(dbb2);
        return makeResult<AncAtc>(std::move(out));
}

template <>
Result<AncAtc> AncAtc::readFromStream<2>(DataStream &s) {
        AncAtc   out;
        Timecode tc;
        uint8_t  payloadType = 0;
        uint8_t  dbb2 = 0;
        s >> tc >> payloadType >> dbb2;
        if (s.status() != DataStream::Ok) return makeError<AncAtc>(s.toError());
        out.setTimecode(tc);
        out.setPayloadType(payloadType);
        out.setDbb2(dbb2);
        return makeResult<AncAtc>(std::move(out));
}

PROMEKI_NAMESPACE_END
