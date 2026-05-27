/**
 * @file      ancop47sdp.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ancop47sdp.h>
#include <promeki/buffer.h>
#include <promeki/datastream.h>
#include <promeki/json.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Diagnostics
// ============================================================================

String AncOp47Sdp::toString() const {
        String s = "AncOp47Sdp(fsc=";
        s += String::number(static_cast<int>(_fsc));
        s += ", packets=";
        s += String::number(static_cast<int>(_packets.size()));
        s += ")";
        return s;
}

JsonObject AncOp47Sdp::toJson() const {
        JsonObject obj;
        obj.set("footerSequenceCounter", static_cast<int64_t>(_fsc));
        JsonArray pkts;
        for (const VbiPacket &p : _packets) {
                JsonObject po;
                po.set("lineNumber", static_cast<int64_t>(p.lineNumber));
                po.set("fieldOne", p.fieldOne);
                po.set("reservedBits", static_cast<int64_t>(p.reservedBits));
                // WST bytes are surfaced as a hex string so a captured
                // teletext payload is human-inspectable in inspector
                // JSON without bloating it into a 45-element array.
                String hex;
                for (size_t i = 0; i < WstPacketSize; ++i) {
                        if (i > 0) hex += " ";
                        uint8_t b = p.wstData[i];
                        if (b < 0x10) hex += "0";
                        hex += String::number(static_cast<int>(b), 16);
                }
                po.set("wstHex", hex);
                pkts.add(po);
        }
        obj.set("packets", pkts);
        return obj;
}

// ============================================================================
// DataStream wire format (v1).
//
// Bundled into a single Buffer so the per-element DataStream tag
// header overhead does not multiply with the 45-byte WST payload.
// Layout:
//   bytes 0..1 : FSC (big-endian uint16)
//   byte  2    : packet count (uint8, 0..MaxVbiPackets)
//   per packet : 1 byte lineNumber, 1 byte fieldOne, 1 byte reservedBits,
//                WstPacketSize (45) bytes wstData
// ============================================================================

Error AncOp47Sdp::writeToStream(DataStream &s) const {
        const size_t headerBytes = 3;
        const size_t perPacketBytes = 3 + WstPacketSize;
        const size_t totalBytes = headerBytes + _packets.size() * perPacketBytes;
        Buffer       buf(totalBytes);
        buf.setSize(totalBytes);
        uint8_t *    p = static_cast<uint8_t *>(buf.data());
        p[0] = static_cast<uint8_t>((_fsc >> 8) & 0xFF);
        p[1] = static_cast<uint8_t>(_fsc & 0xFF);
        p[2] = static_cast<uint8_t>(_packets.size());
        size_t off = headerBytes;
        for (const VbiPacket &pkt : _packets) {
                p[off + 0] = pkt.lineNumber;
                p[off + 1] = pkt.fieldOne ? uint8_t(1) : uint8_t(0);
                p[off + 2] = pkt.reservedBits;
                for (size_t i = 0; i < WstPacketSize; ++i) {
                        p[off + 3 + i] = pkt.wstData[i];
                }
                off += perPacketBytes;
        }
        s << buf;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<AncOp47Sdp> AncOp47Sdp::readFromStream<1>(DataStream &s) {
        Buffer buf;
        s >> buf;
        if (s.status() != DataStream::Ok) return makeError<AncOp47Sdp>(s.toError());
        if (buf.size() < 3) return makeError<AncOp47Sdp>(Error::CorruptData);

        const uint8_t *p = static_cast<const uint8_t *>(buf.data());
        AncOp47Sdp     out;
        out.setFooterSequenceCounter(static_cast<uint16_t>(
                (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1])));
        uint8_t count = p[2];
        const size_t perPacketBytes = 3 + WstPacketSize;
        if (buf.size() != 3 + static_cast<size_t>(count) * perPacketBytes) {
                return makeError<AncOp47Sdp>(Error::CorruptData);
        }
        size_t off = 3;
        for (uint8_t i = 0; i < count; ++i) {
                VbiPacket pkt;
                pkt.lineNumber = p[off + 0];
                pkt.fieldOne = (p[off + 1] != 0);
                pkt.reservedBits = p[off + 2];
                for (size_t b = 0; b < WstPacketSize; ++b) {
                        pkt.wstData[b] = p[off + 3 + b];
                }
                out.addPacket(pkt);
                off += perPacketBytes;
        }
        return makeResult<AncOp47Sdp>(std::move(out));
}

PROMEKI_NAMESPACE_END
