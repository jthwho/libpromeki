/**
 * @file      cea708service.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708service.h>
#include <promeki/error.h>
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
                        // Null block: terminator.  Spec says the
                        // remainder of the packet is padding 0xFF — we
                        // stop parsing here.
                        out.pushToBack(Cea708Service());
                        return makeResult<List<Cea708Service>>(std::move(out));
                }
                uint8_t effectiveServiceNum = serviceNum;
                if (serviceNum == 0x07) {
                        // Extended header: next byte carries the full
                        // 6-bit service_number_ext.
                        if (i >= size) {
                                promekiWarn("Cea708DtvccPacket::parsePayloadBytes: truncated "
                                            "extended block header");
                                return makeError<List<Cea708Service>>(Error::ParseFailed);
                        }
                        effectiveServiceNum = static_cast<uint8_t>(p[i++] & 0x3F);
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
        // packet_size_code carries (payload_size + 1) mod 128 — spec
        // value 0 means 128.  For our payloads <= 127 bytes we just
        // emit payload_size + 1; if payload_size == 0 we still emit
        // a header (size_code=1) plus a null filler byte.
        const uint8_t packetSizeCode =
                static_cast<uint8_t>((payloadSize == 0) ? 1u : ((payloadSize + 1u) & 0x3F));
        const uint8_t header = static_cast<uint8_t>((_sequenceNumber & 0x03) << 6 | packetSizeCode);

        const auto *p = static_cast<const uint8_t *>(payload.data());
        bool        first = true;
        // The wire layout for the per-packet triples is:
        //   triple 0: cc_type=2  (start)   bytes = [header, p[0]]
        //   triple k: cc_type=3  (data)    bytes = [p[2k-1], p[2k]]
        // for total ceil((payloadSize + 1) / 2) triples.
        size_t i = 0;
        while (true) {
                if (first) {
                        const uint8_t b1 = header;
                        const uint8_t b2 = (i < payloadSize) ? p[i++] : 0xFF;
                        out.pushToBack(Cea708Cdp::CcData{true, CcTypePacketStart, b1, b2});
                        first = false;
                } else {
                        const uint8_t b1 = (i < payloadSize) ? p[i++] : 0xFF;
                        const uint8_t b2 = (i < payloadSize) ? p[i++] : 0xFF;
                        out.pushToBack(Cea708Cdp::CcData{true, CcTypePacketData, b1, b2});
                }
                if (i >= payloadSize) break;
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
        const size_t  packetSize = (sizeCode == 0) ? 128u : sizeCode;
        if (packetSize == 0 || packetSize > bytes.size()) {
                promekiWarn("Cea708DtvccPacket::fromCcData: packet size %zu (size_code=%u) "
                            "exceeds available %zu bytes",
                            packetSize, static_cast<unsigned>(sizeCode), bytes.size());
                return makeError<Cea708DtvccPacket>(Error::ParseFailed);
        }
        // Payload bytes follow the header byte; payloadSize = packetSize - 1.
        const size_t payloadSize = packetSize - 1;
        Result<List<Cea708Service>> blocksR =
                parsePayloadBytes(bytes.data() + 1, payloadSize);
        if (blocksR.second().isError()) {
                return makeError<Cea708DtvccPacket>(blocksR.second());
        }
        Cea708DtvccPacket pkt(seq, blocksR.first());
        return makeResult<Cea708DtvccPacket>(std::move(pkt));
}

PROMEKI_NAMESPACE_END
