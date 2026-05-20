/**
 * @file      ancafd.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ancafd.h>
#include <promeki/datastream.h>
#include <promeki/json.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Diagnostics
// ============================================================================

String AncAfd::toString() const {
        String s = "AncAfd(code=0x";
        s += String::number(static_cast<int>(_afdCode), 16);
        s += ", ar=";
        s += _arFlag ? "1" : "0";
        if (hasBarData()) {
                s += ", barFlags=0x";
                s += String::number(static_cast<int>(_barFlags), 16);
                s += ", v1=";
                s += String::number(static_cast<int>(_barValue1));
                s += ", v2=";
                s += String::number(static_cast<int>(_barValue2));
        }
        s += ")";
        return s;
}

JsonObject AncAfd::toJson() const {
        JsonObject obj;
        obj.set("afdCode", static_cast<int64_t>(_afdCode));
        obj.set("ar", _arFlag);
        obj.set("topBar", topBar());
        obj.set("bottomBar", bottomBar());
        obj.set("leftBar", leftBar());
        obj.set("rightBar", rightBar());
        obj.set("barValue1", static_cast<int64_t>(_barValue1));
        obj.set("barValue2", static_cast<int64_t>(_barValue2));
        return obj;
}

// ============================================================================
// DataStream wire format (v1: afdCode + ar + barFlags + barValue1 + barValue2).
// ============================================================================

Error AncAfd::writeToStream(DataStream &s) const {
        s << static_cast<uint8_t>(_afdCode);
        s << static_cast<uint8_t>(_arFlag ? 1 : 0);
        s << static_cast<uint8_t>(_barFlags);
        s << static_cast<uint16_t>(_barValue1);
        s << static_cast<uint16_t>(_barValue2);
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<AncAfd> AncAfd::readFromStream<1>(DataStream &s) {
        AncAfd  out;
        uint8_t code = 0;
        uint8_t ar = 0;
        uint8_t flags = 0;
        uint16_t v1 = 0;
        uint16_t v2 = 0;
        s >> code >> ar >> flags >> v1 >> v2;
        if (s.status() != DataStream::Ok) return makeError<AncAfd>(s.toError());
        out.setAfdCode(code);
        out.setArFlag(ar != 0);
        out.setBarFlags(flags);
        out.setBarValue1(v1);
        out.setBarValue2(v2);
        return makeResult<AncAfd>(std::move(out));
}

PROMEKI_NAMESPACE_END
