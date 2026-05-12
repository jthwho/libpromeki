/**
 * @file      st291packet.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/st291packet.h>
#include <promeki/ancmeta.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// 10-bit packing helpers
//
// ST 291 ancillary packets store DID, SDID, DataCount, every UDW, and the
// trailing Checksum as 10-bit words.  The library's canonical storage
// format is to pack these words tightly into a byte stream starting from
// the most-significant bit of byte 0.  RFC 8331 §2.1 uses the same packing
// inside its per-packet record (the surrounding line / h-offset / stream
// header is *not* stored in the byte buffer — those framing fields live
// in @ref AncPacket::meta via @ref AncMeta::St291).
//
// Each 10-bit word splits into:
//   - bits 0..7: 8-bit data byte
//   - bit  8:    parity of bits 0..7 (XOR of those eight bits)
//   - bit  9:    NOT bit 8
//
// Word values supplied with non-zero parity bits are passed through
// unchanged; word values with zero parity bits have parity computed
// automatically.
// ---------------------------------------------------------------------------

namespace {

        constexpr uint16_t parityBits(uint8_t dataByte) {
                uint8_t  b = dataByte;
                unsigned p = 0;
                for (unsigned i = 0; i < 8; ++i) p ^= (b >> i) & 1u;
                // bit 8 = even-parity over data bits; bit 9 = NOT bit 8.
                return static_cast<uint16_t>((p << 8) | ((p ^ 1u) << 9));
        }

        constexpr uint16_t makeWord(uint16_t in) {
                // Pass-through for callers that supply complete 10-bit
                // words (any non-zero bits above bit 7 are taken as
                // intent to preserve parity bits verbatim).
                if ((in & 0x300u) != 0) return in & 0x3FFu;
                return static_cast<uint16_t>(in & 0xFFu) | parityBits(static_cast<uint8_t>(in & 0xFFu));
        }

        // Packs an array of 10-bit words into bytes MSB-first.  Returns
        // the number of bytes written; the caller pre-sizes the buffer
        // to ceil((wordCount * 10) / 8).
        size_t pack10bit(const uint16_t *words, size_t wordCount, uint8_t *out) {
                size_t bitPos = 0;
                for (size_t i = 0; i < wordCount; ++i) {
                        uint16_t w = words[i] & 0x3FFu;
                        for (unsigned b = 0; b < 10; ++b) {
                                unsigned bit = (w >> (9 - b)) & 1u;
                                size_t   byteIdx = bitPos >> 3;
                                unsigned bitIdx = 7 - (bitPos & 7);
                                if (bit) {
                                        out[byteIdx] |= static_cast<uint8_t>(1u << bitIdx);
                                } else {
                                        out[byteIdx] &= static_cast<uint8_t>(~(1u << bitIdx));
                                }
                                ++bitPos;
                        }
                }
                return (bitPos + 7) >> 3;
        }

        // Unpacks @p wordCount 10-bit words from @p in into @p out.
        // Returns @c true when the buffer is large enough.
        bool unpack10bit(const uint8_t *in, size_t inSize, size_t wordCount, uint16_t *out) {
                const size_t bitsNeeded = wordCount * 10;
                if (inSize * 8 < bitsNeeded) return false;
                size_t bitPos = 0;
                for (size_t i = 0; i < wordCount; ++i) {
                        uint16_t w = 0;
                        for (unsigned b = 0; b < 10; ++b) {
                                size_t   byteIdx = bitPos >> 3;
                                unsigned bitIdx = 7 - (bitPos & 7);
                                unsigned bit = (in[byteIdx] >> bitIdx) & 1u;
                                w = static_cast<uint16_t>((w << 1) | bit);
                                ++bitPos;
                        }
                        out[i] = w;
                }
                return true;
        }

        // Computes the ST 291 §6.4 checksum from a list of 10-bit
        // words.  The sum is over the lower 9 bits of every input word;
        // the result's bit 9 is the inverse of bit 8 so the checksum
        // word is itself a well-formed 10-bit ST 291 word.
        uint16_t checksum291(const uint16_t *words, size_t count) {
                uint16_t s = 0;
                for (size_t i = 0; i < count; ++i) {
                        s = static_cast<uint16_t>((s + (words[i] & 0x1FFu)) & 0x1FFu);
                }
                // Final bit 9 = NOT bit 8.
                uint16_t b8 = (s >> 8) & 1u;
                return static_cast<uint16_t>(s | ((b8 ^ 1u) << 9));
        }

        // Packs (DID, SDID, DataCount, UDW0..UDWn-1, Checksum) into a
        // freshly allocated Buffer in the canonical 10-bit storage form.
        Buffer buildWireBytes(uint8_t did, uint8_t sdid, const List<uint16_t> &udw) {
                const size_t           dc = udw.size();
                List<uint16_t>         words;
                words.pushToBack(makeWord(did));
                words.pushToBack(makeWord(sdid));
                words.pushToBack(makeWord(static_cast<uint16_t>(dc & 0xFFu)));
                for (size_t i = 0; i < dc; ++i) words.pushToBack(makeWord(udw.at(i)));
                // Checksum runs over DID, SDID, DC, UDW (not over itself).
                uint16_t cs = checksum291(words.data(), words.size());
                words.pushToBack(cs);

                const size_t byteCount = (words.size() * 10 + 7) / 8;
                Buffer       buf(byteCount);
                // Zero-init before packing so unused trailing bits of the
                // last byte are deterministic across builds.
                List<uint8_t> tmp(byteCount, uint8_t(0));
                pack10bit(words.data(), words.size(), tmp.data());
                Error err = buf.copyFrom(tmp.data(), byteCount, 0);
                if (err.isError()) return Buffer();
                // The Buffer holds @c byteCount bytes of meaningful
                // content; stamp the logical size so downstream readers
                // (Buffer::size, the AncPacket diagnostic toString, the
                // RFC 8331 packetiser) see the right value.
                buf.setSize(byteCount);
                return buf;
        }

        // Returns the host-readable byte view of a Buffer.  All
        // captured ST 291 packets live in MemSpace::Host; this helper
        // is a thin shorthand for the common "read these bytes" path
        // and assumes the Buffer is already host-mapped (the default
        // for the constructors used to build ST 291 packets).
        const uint8_t *hostBytes(const Buffer &buf) { return static_cast<const uint8_t *>(buf.data()); }

} // namespace

// ---------------------------------------------------------------------------
// Static factories
// ---------------------------------------------------------------------------

Result<St291Packet> St291Packet::from(const AncPacket &pkt) {
        if (pkt.transport() != AncTransport::St291) {
                return makeError<St291Packet>(Error::InvalidArgument);
        }
        // Need at least DID + SDID + DC + Checksum = 4 ten-bit words = 5 bytes.
        if (pkt.data().size() < 5) {
                return makeError<St291Packet>(Error::InvalidArgument);
        }
        return makeResult(St291Packet(pkt));
}

St291Packet St291Packet::build(const AncFormat &fmt, const List<uint16_t> &udw, uint16_t line, uint16_t hOffset,
                               bool fieldB, bool cBit, uint8_t streamNum) {
        // Caller must use a format that has a non-wildcard ST 291
        // SDID; wildcard-SDID formats (Smpte2020Audio) must go through
        // @ref buildRaw with a discriminating SDID byte.
        return buildRaw(fmt.st291Did(), fmt.st291Sdid(), udw, line, hOffset, fieldB, cBit, streamNum);
}

St291Packet St291Packet::buildRaw(uint8_t did, uint8_t sdid, const List<uint16_t> &udw, uint16_t line, uint16_t hOffset,
                                  bool fieldB, bool cBit, uint8_t streamNum) {
        Buffer    wire = buildWireBytes(did, sdid, udw);
        AncFormat fmt = AncFormat::fromSt291DidSdid(did, sdid);

        Metadata meta;
        meta.set(AncMeta::St291::Line, line);
        meta.set(AncMeta::St291::HOffset, hOffset);
        meta.set(AncMeta::St291::FieldB, fieldB);
        meta.set(AncMeta::St291::CBit, cBit);
        meta.set(AncMeta::St291::StreamNum, streamNum);

        AncPacket pkt(fmt, AncTransport::St291, std::move(wire), std::move(meta));
        return St291Packet(pkt);
}

// ---------------------------------------------------------------------------
// Wire-byte accessors (extract DID, SDID, DC, UDW, CS from packed bytes)
// ---------------------------------------------------------------------------

uint8_t St291Packet::did() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        if (_pkt.data().size() < 2 || bytes == nullptr) return 0;
        uint16_t word = 0;
        unpack10bit(bytes, _pkt.data().size(), 1, &word);
        return static_cast<uint8_t>(word & 0xFFu);
}

uint8_t St291Packet::sdid() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        if (_pkt.data().size() < 3 || bytes == nullptr) return 0;
        uint16_t words[2];
        unpack10bit(bytes, _pkt.data().size(), 2, words);
        return static_cast<uint8_t>(words[1] & 0xFFu);
}

uint8_t St291Packet::dataCount() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        if (_pkt.data().size() < 4 || bytes == nullptr) return 0;
        uint16_t words[3];
        unpack10bit(bytes, _pkt.data().size(), 3, words);
        return static_cast<uint8_t>(words[2] & 0xFFu);
}

List<uint16_t> St291Packet::udw() const {
        List<uint16_t> ret;
        const uint8_t *bytes = hostBytes(_pkt.data());
        if (_pkt.data().size() < 4 || bytes == nullptr) return ret;
        uint8_t dc = dataCount();
        if (dc == 0) return ret;
        const size_t   totalWords = 3 + dc;
        List<uint16_t> words(totalWords, uint16_t(0));
        if (!unpack10bit(bytes, _pkt.data().size(), totalWords, words.data())) return ret;
        ret.reserve(dc);
        for (size_t i = 0; i < dc; ++i) ret.pushToBack(words.at(3 + i));
        return ret;
}

uint16_t St291Packet::checksum() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        if (bytes == nullptr) return 0;
        uint8_t      dc = dataCount();
        const size_t totalWords = 4 + dc; // DID + SDID + DC + UDW + CS
        if (_pkt.data().size() * 8 < totalWords * 10) return 0;
        List<uint16_t> words(totalWords, uint16_t(0));
        if (!unpack10bit(bytes, _pkt.data().size(), totalWords, words.data())) return 0;
        return words.at(totalWords - 1) & 0x3FFu;
}

uint16_t St291Packet::computedChecksum() const {
        // Recompute over (DID, SDID, DC, UDW) — *not* over the stored
        // checksum word.  Walk the wire bytes and stop one short.
        const uint8_t *bytes = hostBytes(_pkt.data());
        if (bytes == nullptr) return 0;
        uint8_t        dc = dataCount();
        const size_t   coveredWords = 3 + dc; // DID + SDID + DC + UDW
        List<uint16_t> words(coveredWords, uint16_t(0));
        if (!unpack10bit(bytes, _pkt.data().size(), coveredWords, words.data())) return 0;
        return checksum291(words.data(), coveredWords);
}

bool St291Packet::checksumValid() const { return checksum() == computedChecksum(); }

// ---------------------------------------------------------------------------
// Meta-sidecar accessors
// ---------------------------------------------------------------------------

uint16_t St291Packet::line() const { return _pkt.meta().get(AncMeta::St291::Line).get<uint16_t>(); }

uint16_t St291Packet::hOffset() const { return _pkt.meta().get(AncMeta::St291::HOffset).get<uint16_t>(); }

bool St291Packet::fieldB() const { return _pkt.meta().get(AncMeta::St291::FieldB).get<bool>(); }

bool St291Packet::cBit() const { return _pkt.meta().get(AncMeta::St291::CBit).get<bool>(); }

uint8_t St291Packet::streamNum() const { return _pkt.meta().get(AncMeta::St291::StreamNum).get<uint8_t>(); }

bool St291Packet::isValid() const { return _pkt.transport() == AncTransport::St291 && _pkt.data().size() >= 5; }

// ---------------------------------------------------------------------------
// Mutators (rewrite wire bytes / sidecar; CoW via AncPacket::dataMut)
// ---------------------------------------------------------------------------

void St291Packet::setUdw(const List<uint16_t> &udw) {
        const uint8_t curDid = did();
        const uint8_t curSdid = sdid();
        Buffer        wire = buildWireBytes(curDid, curSdid, udw);
        _pkt.setData(std::move(wire));
}

void St291Packet::setLine(uint16_t line) { _pkt.metaMut().set(AncMeta::St291::Line, line); }

void St291Packet::setHOffset(uint16_t hOffset) { _pkt.metaMut().set(AncMeta::St291::HOffset, hOffset); }

void St291Packet::setFieldB(bool fieldB) { _pkt.metaMut().set(AncMeta::St291::FieldB, fieldB); }

void St291Packet::setCBit(bool cBit) { _pkt.metaMut().set(AncMeta::St291::CBit, cBit); }

void St291Packet::setStreamNum(uint8_t streamNum) { _pkt.metaMut().set(AncMeta::St291::StreamNum, streamNum); }

PROMEKI_NAMESPACE_END
