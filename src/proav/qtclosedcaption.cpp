/**
 * @file      qtclosedcaption.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/qtclosedcaption.h>
#if PROMEKI_ENABLE_PROAV

#include <cstring>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        constexpr uint32_t kCdat = 0x63646174; // 'cdat' — CEA-608 field 1
        constexpr uint32_t kCdt2 = 0x63647432; // 'cdt2' — CEA-608 field 2

        // Appends a big-endian UInt32 to the byte list.
        void putU32(List<uint8_t> &out, uint32_t v) {
                out.pushToBack(static_cast<uint8_t>(v >> 24));
                out.pushToBack(static_cast<uint8_t>(v >> 16));
                out.pushToBack(static_cast<uint8_t>(v >> 8));
                out.pushToBack(static_cast<uint8_t>(v));
        }

        // Appends one [size][type][pairs] atom for the given field's pairs.
        void appendAtom(List<uint8_t> &out, uint32_t type, const List<uint8_t> &pairs) {
                putU32(out, static_cast<uint32_t>(8 + pairs.size()));
                putU32(out, type);
                for (size_t i = 0; i < pairs.size(); ++i) out.pushToBack(pairs[i]);
        }

        uint32_t getU32(const uint8_t *p) {
                return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        }

} // namespace

Buffer QtClosedCaption::encode608(const Cea708Cdp::CcDataList &ccData) {
        List<uint8_t> field1, field2;
        for (size_t i = 0; i < ccData.size(); ++i) {
                const Cea708Cdp::CcData &cc = ccData[i];
                if (!cc.valid) continue;
                if (cc.type == 0) {
                        field1.pushToBack(cc.b1);
                        field1.pushToBack(cc.b2);
                } else if (cc.type == 1) {
                        field2.pushToBack(cc.b1);
                        field2.pushToBack(cc.b2);
                }
        }

        List<uint8_t> out;
        // A cdat atom is always present (empty when there is no field-1
        // data), matching ffmpeg's MOV muxer; cdt2 only when field 2 has data.
        appendAtom(out, kCdat, field1);
        if (!field2.isEmpty()) appendAtom(out, kCdt2, field2);

        Buffer result(out.size());
        if (out.size() > 0) std::memcpy(result.data(), out.data(), out.size());
        result.setSize(out.size());
        return result;
}

Cea708Cdp::CcDataList QtClosedCaption::decode608(const Buffer &sample) {
        Cea708Cdp::CcDataList ccData;
        if (!sample || sample.size() < 8) return ccData;

        const uint8_t *p = static_cast<const uint8_t *>(sample.data());
        size_t         remain = sample.size();
        size_t         pos = 0;

        while (remain >= 8) {
                const uint32_t atomSize = getU32(p + pos);
                const uint32_t atomType = getU32(p + pos + 4);
                if (atomSize < 8 || atomSize > remain) break;
                const uint32_t dataSize = atomSize - 8;

                uint8_t ccType = 0xFF;
                if (atomType == kCdat) ccType = 0;
                else if (atomType == kCdt2) ccType = 1;

                if (ccType != 0xFF && (dataSize % 2) == 0) {
                        for (uint32_t i = 0; i + 1 < dataSize; i += 2) {
                                Cea708Cdp::CcData cc;
                                cc.valid = true;
                                cc.type = ccType;
                                cc.b1 = p[pos + 8 + i];
                                cc.b2 = p[pos + 8 + i + 1];
                                ccData.pushToBack(cc);
                        }
                }
                pos += atomSize;
                remain -= atomSize;
        }
        return ccData;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
