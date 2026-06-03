/**
 * @file      st291packet.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/st291packet.h>
#include <promeki/logger.h>

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
// as direct accessors on AncPacket (st291Line, st291HOffset, st291FieldB,
// st291CBit, st291StreamNum)).
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

        // Returns @c true when @p word's upper 2 bits encode a valid
        // ST 291 parity (bit 9 = NOT bit 8 — i.e. upper-2-bit pattern
        // 01 or 10).  Upper bits @c 11 indicate either a §9.1
        // protected code (0x3FC-0x3FF) or a parity-violation value
        // (0x300-0x3FB); both are wire-illegal and the build path
        // refuses to emit them.  Upper bits @c 00 means the parity
        // bits weren't set — the caller is supplying an 8-bit data
        // byte and @ref parityBits will compute the encoding.
        constexpr bool isValidPassThroughEncoding(uint16_t word) {
                const uint16_t upper = (word >> 8) & 0x3u;
                return upper == 0x1u || upper == 0x2u;
        }

        // Packs (DID, SDID, DataCount, UDW0..UDWn-1, Checksum) into a
        // freshly allocated Buffer in the canonical 10-bit storage form.
        // Returns an empty Buffer on validation failure (DC > 255 or any
        // caller-supplied 10-bit word that lies in the §9.1 protected
        // range / has invalid parity encoding).
        Buffer buildWireBytes(uint8_t did, uint8_t sdid, const List<uint16_t> &udw) {
                const size_t dc = udw.size();

                // ST 291-1 §6.5: DataCount range is 0..255.  Silently
                // truncating dc to a uint8 would leave the wire bytes
                // internally inconsistent (DC says N but the buffer holds
                // M > N UDWs); refuse the build.
                if (dc > 255) {
                        promekiErr("St291Packet::build: UDW count %zu exceeds ST 291 §6.5 max of 255", dc);
                        return Buffer();
                }

                // ST 291-1 §9.1: 10-bit values 000h-003h and 3FCh-3FFh
                // shall not appear in any ANC packet payload.  The
                // parity-compute path can never produce such a value
                // (its output always has bit 9 = NOT bit 8, giving
                // upper-2-bit pattern 01 or 10).  The pass-through path
                // (caller supplied a complete 10-bit word) can — refuse
                // such inputs rather than emit spec-illegal wire bytes
                // (loud failure preserves the byte-exact replay contract).
                for (size_t i = 0; i < dc; ++i) {
                        const uint16_t in = udw.at(i);
                        // Upper-2-bit pattern 00 = parity-compute path; the parityBits()
                        // helper will produce a valid (01 or 10) encoding from the
                        // 8-bit data byte.  Skip the check in that case.
                        if ((in & 0x300u) == 0) continue;
                        if (!isValidPassThroughEncoding(in)) {
                                promekiErr("St291Packet::build: UDW[%zu] = 0x%03X is a §9.1 protected "
                                           "code or has invalid parity encoding (upper-2-bit "
                                           "pattern must be 01 or 10)",
                                           i, static_cast<unsigned>(in & 0x3FFu));
                                return Buffer();
                        }
                }

                List<uint16_t> words;
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

Result<St291Packet> St291Packet::from(const AncPacket &pkt, AncChecksumPolicy policy) {
        if (pkt.transport() != AncTransport::St291) {
                return makeError<St291Packet>(Error::InvalidArgument);
        }
        // Need at least DID + (SDID or DBN) + DC + Checksum = 4 ten-bit words = 5 bytes.
        const size_t bufSize = pkt.data().size();
        if (bufSize < 5) {
                return makeError<St291Packet>(Error::InvalidArgument);
        }
        // The header through DC fits in the first 5 bytes; once we have DC we
        // can require the buffer cover (4 + DC) ten-bit words = ceil((4+DC)*10/8)
        // bytes for the full packet (header + UDW + CS).  A short buffer is a
        // truncated capture and we reject rather than let downstream readers
        // walk off the end of the buffer (ST 291-1 §6.5 / RFC 8331 §2.2 — the
        // declared DC is authoritative for the on-wire payload length).
        const uint8_t *bytes = static_cast<const uint8_t *>(pkt.data().data());
        if (bytes == nullptr) return makeError<St291Packet>(Error::InvalidArgument);
        uint16_t header[3] = {0, 0, 0};
        if (!unpack10bit(bytes, bufSize, 3, header)) {
                return makeError<St291Packet>(Error::InvalidArgument);
        }
        const uint8_t dc = static_cast<uint8_t>(header[2] & 0xFFu);
        const size_t  totalWords = 4u + static_cast<size_t>(dc);
        const size_t  requiredBytes = (totalWords * 10u + 7u) / 8u;
        if (bufSize < requiredBytes) {
                return makeError<St291Packet>(Error::InvalidArgument);
        }
        St291Packet wrapped(pkt);
        // RFC 8331 §7 SHOULD-check: under StrictValidate we verify the
        // stored Checksum_Word matches the value computed over
        // (DID, SDID, DataCount, UDW) per ST 291-1 §6.4.  The buffer
        // length check above already covers the §7 Data_Count guard
        // (declared DC vs available bytes); under StrictValidate the
        // checksum is the remaining wire-integrity check.
        //
        // PreserveOrRecompute and AlwaysRecompute accept the packet
        // as-is on the parse path — they govern emission, not ingest.
        // The byte-exact replay contract requires captured packets
        // with occasional bit errors to survive promotion; downstream
        // codecs decide whether to tolerate them.
        if (policy == AncChecksumPolicy::StrictValidate && !wrapped.checksumValid()) {
                return makeError<St291Packet>(Error::InvalidChecksum);
        }
        return makeResult(wrapped);
}

St291Packet St291Packet::build(const AncFormat &fmt, const List<uint16_t> &udw, uint16_t line, uint16_t hOffset,
                               bool fieldB, bool cBit, uint8_t streamNum) {
        // Caller must use a format that has a non-wildcard ST 291
        // SDID; wildcard-SDID formats (Smpte2020Audio) must go through
        // @ref buildRaw with a discriminating SDID byte.
        //
        // Preserve the caller's @p fmt verbatim on the resulting packet
        // rather than running the wire (DID,SDID)→ID lookup that
        // @ref buildRaw uses.  This matters for families where multiple
        // @c AncFormat::ID values share a single (DID,SDID) slot
        // (ST 12-2:2014 ATC: AtcLtc / AtcVitc1 / AtcVitc2 all share
        // (0x60, 0x60) and the lookup would otherwise collapse all
        // three onto AtcLtc).
        const uint8_t did = fmt.st291Did();
        const uint8_t sdid = fmt.st291Sdid();
        if (did == 0) {
                promekiErr("St291Packet::build: format '%s' has no ST 291 DID assigned (DID 0x00 is reserved)",
                           fmt.name().cstr());
                return St291Packet();
        }
        Buffer    wire = buildWireBytes(did, sdid, udw);
        AncPacket pkt(fmt, AncTransport::St291, std::move(wire));
        pkt.setSt291Line(line);
        pkt.setSt291HOffset(hOffset);
        pkt.setSt291FieldB(fieldB);
        pkt.setSt291CBit(cBit);
        pkt.setSt291StreamNum(streamNum);
        return St291Packet(pkt);
}

namespace {

// Reserved Type-2 DIDs per ST 291-1:2011 §6.1 Figure 4b.
// Returns nullptr when the DID is permitted; otherwise a static
// reason string for the diagnostic.
inline const char *type2DidReservedReason(uint8_t did) {
        if (did == 0x00) return "0x00 is reserved";
        if (did >= 0x01 && did <= 0x03) return "0x01-0x03 are reserved";
        // 0x04-0x0F "Reserved for 8-bit Application": only 0x04, 0x08,
        // 0x0C are valid (the truncation pattern documented in §6.1).
        if (did >= 0x04 && did <= 0x0F &&
            did != 0x04 && did != 0x08 && did != 0x0C) {
                return "0x04-0x0F are reserved for 8-bit applications (only 0x04/0x08/0x0C valid)";
        }
        return nullptr;
}

// Reserved Type-1 DIDs per ST 291-1:2011 §6.1 Figure 4a.
// Returns nullptr when the DID is permitted; otherwise a static
// reason string for the diagnostic.
inline const char *type1DidReservedReason(uint8_t did) {
        // 0x80: Packet Marked for Deletion (§6.3 — valid).
        // 0x81-0x9F: Reserved (figure 4a).
        // 0xA0-0xFF: SMPTE registered / User Application (valid).
        if (did >= 0x81 && did <= 0x9F) return "0x81-0x9F are reserved";
        return nullptr;
}

} // namespace

St291Packet St291Packet::buildRaw(uint8_t did, uint8_t sdid, const List<uint16_t> &udw, uint16_t line, uint16_t hOffset,
                                  bool fieldB, bool cBit, uint8_t streamNum) {
        // ST 291-1:2011 §6.1 Figure 4a/4b: validate the DID against the
        // reserved-range table.  ST 291-1 §6.2: Type-2 SDID 0x00 is
        // also reserved.  Refuse to emit a packet with a reserved
        // identifier rather than produce wire bytes a conforming
        // receiver will discard.  The return signal is an invalid
        // @c St291Packet (`isValid() == false`) plus a diagnostic.
        if ((did & 0x80u) != 0u) {
                if (const char *reason = type1DidReservedReason(did)) {
                        promekiErr("St291Packet::buildRaw: Type-1 DID 0x%02X reserved (ST 291-1 §6.1, %s)",
                                   did, reason);
                        return St291Packet();
                }
        } else {
                if (const char *reason = type2DidReservedReason(did)) {
                        promekiErr("St291Packet::buildRaw: Type-2 DID 0x%02X reserved (ST 291-1 §6.1, %s)",
                                   did, reason);
                        return St291Packet();
                }
                // §6.2: SDID 0x00 reserved on Type-2 packets only.  On
                // Type-1 packets the second-byte slot carries a DBN
                // value of 0x00 = "DBN inactive" per §6.4, which is
                // legal; @ref buildRawType1 routes through this same
                // function with a DBN argument we therefore must not
                // reject.
                if (sdid == 0x00) {
                        promekiErr("St291Packet::buildRaw: Type-2 SDID 0x00 reserved (ST 291-1 §6.2)");
                        return St291Packet();
                }
        }

        Buffer    wire = buildWireBytes(did, sdid, udw);
        // Pass the UDWs so the ST 12-2 ATC trio (0x60/0x60) resolves to
        // its true flavour (LTC / VITC1 / VITC2) from the DBB1 byte rather
        // than anchoring to the lowest-ID AtcLtc.
        AncFormat fmt = AncFormat::fromSt291DidSdid(did, sdid, &udw);

        AncPacket pkt(fmt, AncTransport::St291, std::move(wire));
        pkt.setSt291Line(line);
        pkt.setSt291HOffset(hOffset);
        pkt.setSt291FieldB(fieldB);
        pkt.setSt291CBit(cBit);
        pkt.setSt291StreamNum(streamNum);
        return St291Packet(pkt);
}

St291Packet St291Packet::buildRawType1(uint8_t did, uint8_t dbn, const List<uint16_t> &udw, uint16_t line,
                                       uint16_t hOffset, bool fieldB, bool cBit, uint8_t streamNum) {
        // Type-1 packets are defined by DID's high bit being set
        // (ST 291-1 §5.1).  Refuse a Type-2 DID — callers that mean
        // SDID should use @ref buildRaw, where the second-byte
        // parameter is the SDID byte rather than the DBN.
        if ((did & 0x80u) == 0u) {
                promekiErr("St291Packet::buildRawType1: DID 0x%02X is Type-2 (high bit clear); "
                           "use buildRaw() for SDID-based packets",
                           did);
                return St291Packet();
        }
        // Word layout is identical between Type-1 and Type-2 — the
        // only difference is whether the second byte is SDID or DBN.
        // Reuse buildRaw and feed it the DBN as the second-byte
        // parameter; the registry's wildcard-SDID lookup catches the
        // (DID, *) match for registered Type-1 formats
        // (e.g. PacketForDeletion under DID 0x80) so format() resolves
        // correctly regardless of DBN value.
        return buildRaw(did, dbn, udw, line, hOffset, fieldB, cBit, streamNum);
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
        const uint8_t didByte = static_cast<uint8_t>(words[0] & 0xFFu);
        // Type-1 packets (DID high bit set) carry a DBN in word 1, not
        // an SDID.  Per RFC 8331 §3.1 the SDID slot for Type-1 packets
        // is reported as 0x00; expose that here so callers reasoning
        // about SDID semantics never see a DBN value mis-labelled as
        // SDID.  Use @ref dbn to read the actual second-word byte for
        // Type-1 packets.
        if ((didByte & 0x80u) != 0u) return 0;
        return static_cast<uint8_t>(words[1] & 0xFFu);
}

uint8_t St291Packet::dbn() const {
        const uint8_t *bytes = hostBytes(_pkt.data());
        if (_pkt.data().size() < 3 || bytes == nullptr) return 0;
        uint16_t words[2];
        unpack10bit(bytes, _pkt.data().size(), 2, words);
        const uint8_t didByte = static_cast<uint8_t>(words[0] & 0xFFu);
        // Mirror of sdid(): only meaningful for Type-1 packets, zero
        // for Type-2.
        if ((didByte & 0x80u) == 0u) return 0;
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
        // udw() strips parity bits — see the @ref udwRaw companion for
        // the parity-preserving variant used by byte-exact replay.
        List<uint16_t> ret;
        const uint8_t *bytes = hostBytes(_pkt.data());
        if (_pkt.data().size() < 4 || bytes == nullptr) return ret;
        uint8_t dc = dataCount();
        if (dc == 0) return ret;
        const size_t   totalWords = 3 + dc;
        List<uint16_t> words(totalWords, uint16_t(0));
        if (!unpack10bit(bytes, _pkt.data().size(), totalWords, words.data())) return ret;
        ret.reserve(dc);
        for (size_t i = 0; i < dc; ++i) ret.pushToBack(static_cast<uint16_t>(words.at(3 + i) & 0xFFu));
        return ret;
}

List<uint16_t> St291Packet::udwRaw() const {
        // Same walk as @ref udw, but preserve the upper-2 parity bits
        // verbatim.  Callers verifying byte-exact replay against a
        // captured stream need the original parity encoding (which
        // can differ from the canonical one parityBits() would
        // compute when the sender hand-built parity).
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

uint16_t St291Packet::line() const { return _pkt.st291Line(); }

uint16_t St291Packet::hOffset() const { return _pkt.st291HOffset(); }

bool St291Packet::fieldB() const { return _pkt.st291FieldB(); }

bool St291Packet::cBit() const { return _pkt.st291CBit(); }

uint8_t St291Packet::streamNum() const { return _pkt.st291StreamNum(); }

bool St291Packet::isValid() const { return _pkt.transport() == AncTransport::St291 && _pkt.data().size() >= 5; }

// ---------------------------------------------------------------------------
// Mutators (rewrite wire bytes / sidecar; CoW via AncPacket::dataMut)
// ---------------------------------------------------------------------------

void St291Packet::setUdw(const List<uint16_t> &udw) {
        const uint8_t curDid = did();
        // For Type-2 packets the second-word byte is SDID; for Type-1
        // packets it is DBN.  Preserve whichever applies so the
        // re-pack does not silently zero a DBN on a Type-1 mutator.
        const uint8_t curSecond = (curDid & 0x80u) != 0u ? dbn() : sdid();
        Buffer        wire = buildWireBytes(curDid, curSecond, udw);
        _pkt.setData(std::move(wire));
}

void St291Packet::setLine(uint16_t line) { _pkt.setSt291Line(line); }

void St291Packet::setHOffset(uint16_t hOffset) { _pkt.setSt291HOffset(hOffset); }

void St291Packet::setFieldB(bool fieldB) { _pkt.setSt291FieldB(fieldB); }

void St291Packet::setCBit(bool cBit) { _pkt.setSt291CBit(cBit); }

void St291Packet::setStreamNum(uint8_t streamNum) { _pkt.setSt291StreamNum(streamNum); }

PROMEKI_NAMESPACE_END
