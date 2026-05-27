/**
 * @file      cea708service.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708service.h>
#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// @brief Returns the wire size in bytes a service block would
        ///        occupy: header (1 byte for service 1..6, 2 bytes for
        ///        the extended form 7..63) + data length.  Data length
        ///        is capped at 31 bytes (the spec's 5-bit @c block_size
        ///        limit) — matches the truncation @ref
        ///        Cea708DtvccPacket::toPayloadBytes performs when a
        ///        caller pushes oversized block data.
        size_t blockWireSize(const Cea708Service &block) {
                if (block.isNull()) return 1;
                constexpr size_t kMaxBlockData = 31;
                size_t           header = block.isExtended() ? 2 : 1;
                size_t           data = block.data().size();
                if (data > kMaxBlockData) data = kMaxBlockData;
                return header + data;
        }

} // namespace

// ============================================================================
// Cea708Service
// ============================================================================

Cea708Service Cea708Service::fromText(uint8_t serviceNumber, const String &text) {
        const char *cp = text.cstr();
        size_t      n = text.byteCount();
        Buffer      buf(n);
        buf.setSize(n);
        size_t outLen = 0;
        if (n > 0) {
                // Build a small staging copy then copy in one shot.
                List<uint8_t> bytes;
                bytes.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                        uint8_t raw = static_cast<uint8_t>(cp[i]);
                        // G0 range: pass through.  Anything else
                        // (control / non-ASCII / UTF-8 continuation)
                        // substitutes with space.  Full G1 / extended
                        // mapping is a future task.
                        if (raw >= 0x20 && raw <= 0x7F) {
                                bytes.pushToBack(raw);
                        } else {
                                bytes.pushToBack(0x20);
                        }
                }
                if (!bytes.isEmpty()) {
                        List<uint8_t> contig(bytes.size());
                        for (size_t i = 0; i < bytes.size(); ++i) contig[i] = bytes[i];
                        buf.setSize(contig.size());
                        outLen = contig.size();
                        Error err = buf.copyFrom(contig.data(), contig.size(), 0);
                        if (err.isError()) {
                                promekiWarn("Cea708Service::fromText: buffer copy failed: %s",
                                            err.name().cstr());
                        }
                }
        }
        if (outLen != n) buf.setSize(outLen);
        return Cea708Service(serviceNumber, std::move(buf));
}

String Cea708Service::text() const {
        String out;
        const auto *p = static_cast<const uint8_t *>(_data.data());
        size_t      n = _data.size();
        for (size_t i = 0; i < n; ++i) {
                uint8_t c = p[i];
                if (c >= 0x20 && c <= 0x7F) {
                        char tmp[2] = {static_cast<char>(c), 0};
                        out += tmp;
                } else if (c == 0x10 || c == 0x18) {
                        // C2 / C3 extended-control prefix — the next
                        // byte is the extended control code.  Skip the
                        // pair for now.
                        if (i + 1 < n) ++i;
                } else if (c == 0x11 || c == 0x19) {
                        // G2 / G3 extended-character prefix — the
                        // next byte is the extended character.  Skip
                        // for now (full table map is a future task).
                        if (i + 1 < n) ++i;
                }
                // Other C0 / C1 bytes are dropped silently — they
                // carry cursor / window state, not text.
        }
        return out;
}

bool Cea708Service::operator==(const Cea708Service &o) const {
        if (_serviceNumber != o._serviceNumber) return false;
        const size_t n = _data.size();
        if (n != o._data.size()) return false;
        if (n == 0) return true;
        const auto *a = static_cast<const uint8_t *>(_data.data());
        const auto *b = static_cast<const uint8_t *>(o._data.data());
        return std::memcmp(a, b, n) == 0;
}

JsonObject Cea708Service::toJson() const {
        JsonObject obj;
        obj.set("serviceNumber", static_cast<int64_t>(_serviceNumber));
        obj.set("dataSize", static_cast<int64_t>(_data.size()));
        obj.set("dataHex", _data.toHex());
        return obj;
}

String Cea708Service::toString() const {
        String s = "Cea708Service(svc=";
        s += String::number(static_cast<int64_t>(_serviceNumber));
        s += ", bytes=";
        s += String::number(static_cast<int64_t>(_data.size()));
        s += ")";
        return s;
}

// ============================================================================
// Cea708DtvccPacket
// ============================================================================

size_t Cea708DtvccPacket::payloadByteCount() const {
        size_t total = 0;
        for (size_t i = 0; i < _serviceBlocks.size(); ++i) {
                total += blockWireSize(_serviceBlocks[i]);
        }
        return total;
}

Buffer Cea708DtvccPacket::toPayloadBytes() const {
        // Per CEA-708 the @c block_size header field is 5 bits, so a
        // single service block can carry at most 31 data bytes.  The
        // encoder's chunker (Cea708Encoder::wrapInDtvccPacket) is
        // expected to enforce this; we additionally cap here so that
        // a caller building a packet by hand can't silently corrupt
        // the wire by pushing oversized data — the parser would read
        // a wrapped (mod-32) block size and misinterpret subsequent
        // data bytes as a fresh block header.
        constexpr size_t       kMaxBlockData = 31;
        List<uint8_t>   bytes;
        for (size_t i = 0; i < _serviceBlocks.size(); ++i) {
                const Cea708Service &b = _serviceBlocks[i];
                if (b.isNull()) {
                        // Null block: header byte 0x00.
                        bytes.pushToBack(0x00);
                        continue;
                }
                size_t blockBytes = b.data().size();
                if (blockBytes > kMaxBlockData) {
                        promekiWarn("Cea708DtvccPacket::toPayloadBytes: service %u block has "
                                    "%zu data bytes; max is %zu — truncating",
                                    static_cast<unsigned>(b.serviceNumber()), blockBytes,
                                    kMaxBlockData);
                        blockBytes = kMaxBlockData;
                }
                if (b.isExtended()) {
                        // Extended form:
                        //   byte 0: 0b111 (service_number==7) + block_size (5 bits)
                        //   byte 1: 0b00 + service_number_ext (6 bits)
                        const uint8_t bsz = static_cast<uint8_t>(blockBytes & 0x1F);
                        bytes.pushToBack(static_cast<uint8_t>((7u << 5) | bsz));
                        bytes.pushToBack(static_cast<uint8_t>(b.serviceNumber() & 0x3F));
                } else {
                        const uint8_t bsz = static_cast<uint8_t>(blockBytes & 0x1F);
                        bytes.pushToBack(static_cast<uint8_t>(((b.serviceNumber() & 0x07) << 5) | bsz));
                }
                const auto *dp = static_cast<const uint8_t *>(b.data().data());
                for (size_t k = 0; k < blockBytes; ++k) bytes.pushToBack(dp[k]);
        }
        Buffer out(bytes.size());
        out.setSize(bytes.size());
        if (bytes.isEmpty()) return out;
        Error err = out.copyFrom(bytes.data(), bytes.size(), 0);
        if (err.isError()) {
                promekiWarn("Cea708DtvccPacket::toPayloadBytes: buffer copy failed: %s",
                            err.name().cstr());
        }
        return out;
}

Result<List<Cea708Service>> Cea708DtvccPacket::parsePayloadBytes(const void *data, size_t size) {
        List<Cea708Service> out;
        if (data == nullptr || size == 0) return makeResult<List<Cea708Service>>(std::move(out));
        const auto *p = static_cast<const uint8_t *>(data);
        size_t      i = 0;
        while (i < size) {
                const uint8_t hdr = p[i++];
                const uint8_t serviceNum = static_cast<uint8_t>((hdr >> 5) & 0x07);
                const uint8_t blockSize = static_cast<uint8_t>(hdr & 0x1F);
                if (serviceNum == 0 && blockSize == 0) {
                        // Null block: terminator per §6.2.5.  Remainder
                        // of the packet is padding (0x00 or 0xFF) and
                        // should not appear in the parsed block list.
                        return makeResult<List<Cea708Service>>(std::move(out));
                }
                uint8_t effectiveServiceNum = serviceNum;
                if (serviceNum == 0x07) {
                        // Extended header (CEA-708-E §6.2.2): next byte
                        // carries 2 reserved (null_fill) bits in bits 7..6
                        // followed by the 6-bit service_number_extension
                        // in bits 5..0.  service_number_extension must be
                        // >= 7 because services 1..6 use the standard
                        // (non-extended) header form.
                        if (i >= size) {
                                promekiWarn("Cea708DtvccPacket::parsePayloadBytes: truncated "
                                            "extended block header");
                                return makeError<List<Cea708Service>>(Error::ParseFailed);
                        }
                        const uint8_t extByte = p[i++];
                        // Warn (don't fail) on non-zero reserved bits —
                        // some encoders leave them set; the data bits are
                        // still meaningful.
                        if ((extByte & 0xC0) != 0) {
                                promekiWarn("Cea708DtvccPacket::parsePayloadBytes: extended "
                                            "service header reserved bits (b7..b6) = 0x%X, "
                                            "expected 0x0",
                                            static_cast<unsigned>((extByte >> 6) & 0x03));
                        }
                        effectiveServiceNum = static_cast<uint8_t>(extByte & 0x3F);
                        if (effectiveServiceNum < 7) {
                                promekiWarn("Cea708DtvccPacket::parsePayloadBytes: extended "
                                            "header reports service_number=%u, which should "
                                            "use the standard (non-extended) header form",
                                            static_cast<unsigned>(effectiveServiceNum));
                                return makeError<List<Cea708Service>>(Error::ParseFailed);
                        }
                }
                if (i + blockSize > size) {
                        promekiWarn("Cea708DtvccPacket::parsePayloadBytes: block size %u "
                                    "exceeds remaining %zu bytes",
                                    static_cast<unsigned>(blockSize), size - i);
                        return makeError<List<Cea708Service>>(Error::ParseFailed);
                }
                Buffer blockData(blockSize);
                blockData.setSize(blockSize);
                if (blockSize > 0) {
                        Error err = blockData.copyFrom(p + i, blockSize, 0);
                        if (err.isError()) {
                                return makeError<List<Cea708Service>>(err);
                        }
                        i += blockSize;
                }
                out.pushToBack(Cea708Service(effectiveServiceNum, std::move(blockData)));
        }
        return makeResult<List<Cea708Service>>(std::move(out));
}

Cea708Cdp::CcDataList Cea708DtvccPacket::toCcData() const {
        Cea708Cdp::CcDataList out;
        const Buffer          payload = toPayloadBytes();
        const size_t          payloadSize = payload.size();
        if (payloadSize > MaxPayloadBytes) {
                promekiWarn("Cea708DtvccPacket::toCcData: payload %zu bytes exceeds spec max "
                            "%u — emitted unchanged but the wire format will fail to parse",
                            payloadSize, MaxPayloadBytes);
        }
        // Header byte: sequence_number (2 bits) << 6 | packet_size_code (6 bits).
        //
        // Per CEA-708-E §5.1, packet_size_code encodes the number of
        // byte PAIRS in the CCP including the header byte:
        //   if (packet_size_code == 0): packet_data_size = 127 (n=128)
        //   else:                       packet_data_size = (psc*2) - 1
        // So total wire byte count n = packet_size_code * 2 (with the
        // special case psc=0 meaning n=128).  Since each cc_data triple
        // carries 2 wire bytes, ceil((1 + payloadSize) / 2) triples
        // are needed; pad with 0xFF when payloadSize doesn't fill the
        // last triple.
        size_t  ccpTotalBytes = 1 + payloadSize;
        if (ccpTotalBytes & 1) ++ccpTotalBytes; // pad to even
        uint8_t packetSizeCode;
        if (ccpTotalBytes >= 128) {
                packetSizeCode = 0; // spec's max-encoded value
        } else if (ccpTotalBytes <= 2) {
                // Minimum legal CCP is 1 header + 1 byte = 2 wire bytes
                // (psc=1, packet_data_size=1).  Even a zero-payload
                // packet emits a single 0xFF padding byte so the wire
                // form remains parseable.
                packetSizeCode = 1;
        } else {
                packetSizeCode = static_cast<uint8_t>((ccpTotalBytes / 2) & 0x3F);
        }
        const uint8_t header = static_cast<uint8_t>((_sequenceNumber & 0x03) << 6 | packetSizeCode);

        const auto *p = static_cast<const uint8_t *>(payload.data());
        // The wire layout for the per-packet triples is:
        //   triple 0: cc_type=2  (start)   bytes = [header, p[0]]
        //   triple k: cc_type=3  (data)    bytes = [p[2k-1], p[2k]]
        // for total ceil((payloadSize + 1) / 2) triples — derived from
        // ccpTotalBytes above so the wire byte count matches the
        // packet_size_code we just stamped into the header.
        size_t i = 0;
        // Pad with 0x00 (null service block header) per §6.2.5 ("A
        // Null Service Block Header shall be inserted as the last
        // Service Block in the Caption Channel Packet if space
        // permits").  This lets a spec-compliant service-block parser
        // terminate cleanly when it hits the padding bytes instead of
        // misinterpreting 0xFF as a malformed extended service header.
        const size_t totalTriples = ccpTotalBytes / 2;
        for (size_t t = 0; t < totalTriples; ++t) {
                if (t == 0) {
                        const uint8_t b1 = header;
                        const uint8_t b2 = (i < payloadSize) ? p[i++] : 0x00;
                        out.pushToBack(Cea708Cdp::CcData{true, CcTypePacketStart, b1, b2});
                } else {
                        const uint8_t b1 = (i < payloadSize) ? p[i++] : 0x00;
                        const uint8_t b2 = (i < payloadSize) ? p[i++] : 0x00;
                        out.pushToBack(Cea708Cdp::CcData{true, CcTypePacketData, b1, b2});
                }
        }
        return out;
}

Result<Cea708DtvccPacket> Cea708DtvccPacket::fromCcData(const Cea708Cdp::CcDataList &triples) {
        if (triples.isEmpty()) return makeResult<Cea708DtvccPacket>(Cea708DtvccPacket());
        if (triples[0].type != CcTypePacketStart) {
                promekiWarn("Cea708DtvccPacket::fromCcData: first triple has cc_type %u, "
                            "expected %u (DTVCC_PACKET_START)",
                            static_cast<unsigned>(triples[0].type),
                            static_cast<unsigned>(CcTypePacketStart));
                return makeError<Cea708DtvccPacket>(Error::ParseFailed);
        }
        // Reassemble the wire byte stream from the triples.
        List<uint8_t> bytes;
        bytes.reserve(triples.size() * 2);
        for (size_t i = 0; i < triples.size(); ++i) {
                const Cea708Cdp::CcData &t = triples[i];
                const uint8_t            wantType = (i == 0) ? CcTypePacketStart : CcTypePacketData;
                if (t.type != wantType) {
                        // Mixed-cc_type stream — stop reading at the
                        // boundary; caller is responsible for slicing
                        // triple lists by packet.
                        break;
                }
                bytes.pushToBack(t.b1);
                bytes.pushToBack(t.b2);
        }
        if (bytes.isEmpty()) return makeError<Cea708DtvccPacket>(Error::ParseFailed);
        const uint8_t header = bytes[0];
        const uint8_t seq = static_cast<uint8_t>((header >> 6) & 0x03);
        uint8_t       sizeCode = static_cast<uint8_t>(header & 0x3F);
        // Per CEA-708-E §5.1: packet_size_code is in byte PAIRS,
        // including the header byte.  packet_data_size = (psc*2) - 1
        // except psc=0 which means 127 (full 128-byte packet).
        const size_t packetSize = (sizeCode == 0) ? 128u : static_cast<size_t>(sizeCode) * 2u;
        const size_t payloadSize = packetSize - 1;
        if (packetSize > bytes.size()) {
                promekiWarn("Cea708DtvccPacket::fromCcData: packet size %zu (size_code=%u) "
                            "exceeds available %zu bytes",
                            packetSize, static_cast<unsigned>(sizeCode), bytes.size());
                return makeError<Cea708DtvccPacket>(Error::ParseFailed);
        }
        Result<List<Cea708Service>> blocksR =
                parsePayloadBytes(bytes.data() + 1, payloadSize);
        if (blocksR.second().isError()) {
                return makeError<Cea708DtvccPacket>(blocksR.second());
        }
        Cea708DtvccPacket pkt(seq, blocksR.first());
        return makeResult<Cea708DtvccPacket>(std::move(pkt));
}

JsonObject Cea708DtvccPacket::toJson() const {
        JsonObject obj;
        obj.set("sequenceNumber", static_cast<int64_t>(_sequenceNumber));
        obj.set("payloadByteCount", static_cast<int64_t>(payloadByteCount()));
        JsonArray arr;
        for (size_t i = 0; i < _serviceBlocks.size(); ++i) {
                arr.add(_serviceBlocks[i].toJson());
        }
        obj.set("serviceBlocks", arr);
        return obj;
}

String Cea708DtvccPacket::toString() const {
        String s = "Cea708DtvccPacket(seq=";
        s += String::number(static_cast<int64_t>(_sequenceNumber));
        s += ", blocks=";
        s += String::number(static_cast<int64_t>(_serviceBlocks.size()));
        s += ", payload=";
        s += String::number(static_cast<int64_t>(payloadByteCount()));
        s += "B)";
        return s;
}

// ============================================================================
// DataStream operators
// ============================================================================
//
// Cea708Service wire layout:
//   uint8_t   service_number   (TypeU8)
//   Buffer    data             (TypeBuffer — length-prefixed bytes)
//
// Cea708DtvccPacket wire layout:
//   uint8_t   sequence_number  (TypeU8)
//   uint32_t  count            (TypeU32 — number of service blocks)
//   for each service block:
//     Cea708Service (tagged TypeCea708Service)

Error Cea708Service::writeToStream(DataStream &s) const {
        s << serviceNumber();
        s << data();
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<Cea708Service> Cea708Service::readFromStream<1>(DataStream &s) {
        uint8_t svcNum = 0;
        Buffer  data;
        s >> svcNum >> data;
        if (s.status() != DataStream::Ok) return makeError<Cea708Service>(s.toError());
        return makeResult(Cea708Service(svcNum, std::move(data)));
}

Error Cea708DtvccPacket::writeToStream(DataStream &s) const {
        s << sequenceNumber();
        const auto &blocks = serviceBlocks();
        s << static_cast<uint32_t>(blocks.size());
        for (size_t i = 0; i < blocks.size(); ++i) s << blocks[i];
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<Cea708DtvccPacket> Cea708DtvccPacket::readFromStream<1>(DataStream &s) {
        uint8_t  seq   = 0;
        uint32_t count = 0;
        s >> seq >> count;
        if (s.status() != DataStream::Ok) return makeError<Cea708DtvccPacket>(s.toError());
        List<Cea708Service> blocks;
        blocks.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
                Cea708Service svc;
                s >> svc;
                if (s.status() != DataStream::Ok) return makeError<Cea708DtvccPacket>(s.toError());
                blocks.pushToBack(std::move(svc));
        }
        return makeResult(Cea708DtvccPacket(seq, std::move(blocks)));
}

PROMEKI_NAMESPACE_END
