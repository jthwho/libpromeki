/**
 * @file      pcapreader.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pcapreader.h>
#if PROMEKI_ENABLE_NETWORK
#include <chrono>
#include <cmath>
#include <promeki/file.h>
#include <promeki/iodevice.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Read a big- or little-endian unsigned integer out of a byte pointer.
// The capture's byte order is fixed per file (classic) or per section
// (pcapng), so the order is threaded through as a bool rather than
// detected per read.
uint16_t rd16(const uint8_t *p, bool be) {
        return be ? static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1])
                  : static_cast<uint16_t>((static_cast<uint16_t>(p[1]) << 8) | p[0]);
}

uint32_t rd32(const uint8_t *p, bool be) {
        return be ? (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                            (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3])
                  : (static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[2]) << 16) |
                            (static_cast<uint32_t>(p[1]) << 8) | static_cast<uint32_t>(p[0]);
}

// Compose a pcapng 64-bit timestamp from its high/low 32-bit halves.
uint64_t rd64hl(uint32_t hi, uint32_t lo) {
        return (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
}

// Convert a pcapng tick count to nanoseconds using the interface's
// if_tsresol byte: high bit set selects a 2^-n s/tick resolution,
// otherwise 10^-n s/tick.  The decimal n<=9 path (covering the
// near-universal microsecond=6 and nanosecond=9 cases) is exact; the
// exotic high-resolution paths fall back to long double.
int64_t pcapngTicksToNanos(uint64_t ticks, uint8_t code) {
        if(code & 0x80) {
                const unsigned exp = code & 0x7f;
                const long double sec = static_cast<long double>(ticks) / powl(2.0L, static_cast<long double>(exp));
                return static_cast<int64_t>(sec * 1e9L);
        }
        if(code <= 9) {
                static const int64_t nsPerTick[10] = {1000000000LL, 100000000LL, 10000000LL, 1000000LL, 100000LL,
                                                      10000LL,       1000LL,      100LL,      10LL,      1LL};
                return static_cast<int64_t>(ticks) * nsPerTick[code];
        }
        const long double sec = static_cast<long double>(ticks) / powl(10.0L, static_cast<long double>(code));
        return static_cast<int64_t>(sec * 1e9L);
}

// Build a wall-clock DateTime from nanoseconds since the Unix epoch.
// system_clock is the Unix-epoch clock under C++20, so this lands the
// capture's arrival time on the right scale (distinct from the RTP
// media clock carried inside the payload).
DateTime unixNanosToDateTime(int64_t ns) {
        using SC = std::chrono::system_clock;
        const auto dur = std::chrono::duration_cast<SC::duration>(std::chrono::nanoseconds(ns));
        return DateTime(SC::time_point(dur));
}

} // namespace

Error PcapReader::open(IODevice &device) {
        close();
        auto [total, sizeErr] = device.size();
        if(sizeErr.isError() || total <= 0) {
                // A non-seekable / unsized source (pipe, socket) is out of
                // scope for this revision; the streamed fallback is future
                // work tracked in devplan/network/pcap.md.
                return Error::NotSupported;
        }
        if(static_cast<uint64_t>(total) > MaxRecordLength * 64ull) {
                // Refuse absurd sizes outright rather than attempt a huge
                // allocation on a corrupt or hostile header.
                return Error::OutOfRange;
        }
        Buffer buf(static_cast<size_t>(total));
        if(!buf.isValid()) return Error::NoMem;
        if(device.seek(0).isError()) return Error::NotSupported;
        uint8_t *p = static_cast<uint8_t *>(buf.data());
        int64_t got = 0;
        while(got < total) {
                const int64_t n = device.read(p + got, total - got);
                if(n < 0) return Error::IOError;
                if(n == 0) break; // short file; parse what we have
                got += n;
        }
        buf.setSize(static_cast<size_t>(got));
        return openBuffer(buf);
}

Error PcapReader::openFile(const String &path) {
        File file(path);
        const Error err = file.open(IODevice::ReadOnly);
        if(err.isError()) return Error::OpenFailed;
        const Error rerr = open(file);
        file.close();
        return rerr;
}

Error PcapReader::openBuffer(const Buffer &buf) {
        close();
        _backing = buf;
        _size = buf.size();
        _pos = 0;
        return parseHeader();
}

void PcapReader::close() {
        _backing = Buffer();
        _pos = 0;
        _size = 0;
        _format = PcapFileFormat::Unknown;
        _byteOrder = PcapByteOrder::Unknown;
        _be = false;
        _nanoTs = false;
        _snaplen = 0;
        _classicLink = PcapLinkType::Ethernet;
        _interfaces.clear();
}

Error PcapReader::parseHeader() {
        if(_size < 4) return Error::TruncatedData;
        const uint8_t *d = static_cast<const uint8_t *>(_backing.data());
        const uint32_t leMagic = rd32(d, false);
        switch(leMagic) {
                case MagicMicros: _format = PcapFileFormat::ClassicPcap; _be = false; _nanoTs = false; break;
                case MagicNanos:  _format = PcapFileFormat::ClassicPcap; _be = false; _nanoTs = true;  break;
                // Byte-swapped classic magics (file written big-endian).
                case 0xd4c3b2a1u: _format = PcapFileFormat::ClassicPcap; _be = true;  _nanoTs = false; break;
                case 0x4d3cb2a1u: _format = PcapFileFormat::ClassicPcap; _be = true;  _nanoTs = true;  break;
                case PngBlockShb: _format = PcapFileFormat::Pcapng; break;
                default: return Error::CorruptData;
        }
        if(_format == PcapFileFormat::ClassicPcap) {
                _byteOrder = _be ? PcapByteOrder::BigEndian : PcapByteOrder::LittleEndian;
                return parseClassicHeader();
        }
        return parsePcapngFirstSection();
}

Error PcapReader::parseClassicHeader() {
        // Global header: magic(4) major(2) minor(2) thiszone(4) sigfigs(4)
        // snaplen(4) network(4) = 24 bytes.
        if(_size < 24) return Error::TruncatedData;
        const uint8_t *d = static_cast<const uint8_t *>(_backing.data());
        _snaplen = rd32(d + 16, _be);
        _classicLink = PcapLinkType(static_cast<int>(rd32(d + 20, _be)));
        _interfaces.clear();
        Interface itf;
        itf.linkType = _classicLink;
        itf.snapLength = _snaplen;
        itf.tsResolCode = _nanoTs ? 9 : 6;
        _interfaces.pushToBack(itf);
        _pos = 24;
        return Error::Ok;
}

Error PcapReader::parsePcapngFirstSection() {
        // The leading block is the Section Header Block; its body opens
        // with the byte-order magic, which fixes this section's endianness.
        if(_size < 12) return Error::TruncatedData;
        const uint8_t *d = static_cast<const uint8_t *>(_backing.data());
        const uint32_t bomLE = rd32(d + 8, false);
        if(bomLE == PngByteOrderMagic) {
                _be = false;
        } else if(rd32(d + 8, true) == PngByteOrderMagic) {
                _be = true;
        } else {
                return Error::CorruptData;
        }
        _byteOrder = _be ? PcapByteOrder::BigEndian : PcapByteOrder::LittleEndian;
        _pos = 0;

        // Pre-scan the first section's Interface Description Blocks so
        // linkType() / snapLength() / interfaceCount() report something
        // meaningful before iteration begins.  next() walks from _pos == 0
        // and rebuilds this same table as it goes (clearing on each SHB),
        // so the pre-scan does not perturb iteration.
        const uint8_t *base = d;
        size_t pos = 0;
        while(pos + 8 <= _size) {
                const uint32_t btype = rd32(base + pos, _be);
                const uint32_t blen = rd32(base + pos + 4, _be);
                if(blen < 12 || (blen & 3u) != 0 || pos + blen > _size) break;
                if(btype == PngBlockIdb) {
                        consumePcapngIdb(pos + 8, blen - 12);
                } else if(btype == PngBlockEpb || btype == PngBlockSpb) {
                        break; // first packet block reached; metadata scan complete
                } else if(btype == PngBlockShb && pos != 0) {
                        break; // next section; stop scanning
                }
                pos += blen;
        }
        return Error::Ok;
}

Error PcapReader::consumePcapngIdb(size_t bodyOff, size_t bodyLen) {
        // IDB body: LinkType(2) Reserved(2) SnapLen(4) then options.
        if(bodyLen < 8) return Error::CorruptData;
        const uint8_t *b = static_cast<const uint8_t *>(_backing.data()) + bodyOff;
        Interface itf;
        itf.linkType = PcapLinkType(static_cast<int>(rd16(b, _be)));
        itf.snapLength = rd32(b + 4, _be);
        itf.tsResolCode = 6; // default: microseconds
        size_t optOff = 8;
        while(optOff + 4 <= bodyLen) {
                const uint16_t code = rd16(b + optOff, _be);
                const uint16_t len = rd16(b + optOff + 2, _be);
                optOff += 4;
                if(code == 0) break;                  // opt_endofopt
                if(optOff + len > bodyLen) break;     // malformed option run
                if(code == 9 && len >= 1) {           // if_tsresol
                        itf.tsResolCode = b[optOff];
                }
                optOff += len;
                optOff = (optOff + 3) & ~static_cast<size_t>(3); // pad to 32-bit boundary
        }
        _interfaces.pushToBack(itf);
        return Error::Ok;
}

PcapLinkType PcapReader::linkType() const {
        if(_format == PcapFileFormat::ClassicPcap) return _classicLink;
        if(!_interfaces.isEmpty()) return _interfaces[0].linkType;
        return PcapLinkType::Null;
}

uint32_t PcapReader::snapLength() const {
        if(_format == PcapFileFormat::ClassicPcap) return _snaplen;
        if(!_interfaces.isEmpty()) return _interfaces[0].snapLength;
        return 0;
}

Result<PcapRecord> PcapReader::next() {
        if(_format == PcapFileFormat::ClassicPcap) return nextClassic();
        if(_format == PcapFileFormat::Pcapng) return nextPcapng();
        return makeError<PcapRecord>(Error::NotOpen);
}

Result<PcapRecord> PcapReader::nextClassic() {
        if(_pos >= _size) return makeError<PcapRecord>(Error::EndOfFile);
        if(_pos + 16 > _size) return makeError<PcapRecord>(Error::TruncatedData);
        const uint8_t *d = static_cast<const uint8_t *>(_backing.data()) + _pos;
        const uint32_t tsSec = rd32(d, _be);
        const uint32_t tsFrac = rd32(d + 4, _be);
        const uint32_t inclLen = rd32(d + 8, _be);
        const uint32_t origLen = rd32(d + 12, _be);
        if(inclLen > MaxRecordLength) return makeError<PcapRecord>(Error::CorruptData);
        if(_pos + 16 + static_cast<size_t>(inclLen) > _size) return makeError<PcapRecord>(Error::TruncatedData);
        const int64_t ns =
                static_cast<int64_t>(tsSec) * 1000000000LL + (_nanoTs ? static_cast<int64_t>(tsFrac)
                                                                       : static_cast<int64_t>(tsFrac) * 1000LL);
        PcapRecord rec;
        rec.captureTime = unixNanosToDateTime(ns);
        rec.linkType = _classicLink;
        rec.frame = BufferView(_backing, _pos + 16, inclLen);
        rec.originalLength = origLen;
        rec.snapTruncated = inclLen < origLen;
        _pos += 16 + static_cast<size_t>(inclLen);
        return makeResult(rec);
}

Result<PcapRecord> PcapReader::nextPcapng() {
        const uint8_t *base = static_cast<const uint8_t *>(_backing.data());
        for(;;) {
                if(_pos >= _size) return makeError<PcapRecord>(Error::EndOfFile);
                if(_pos + 8 > _size) return makeError<PcapRecord>(Error::TruncatedData);
                const uint32_t btype = rd32(base + _pos, _be);
                const uint32_t blen = rd32(base + _pos + 4, _be);
                if(blen < 12 || (blen & 3u) != 0) return makeError<PcapRecord>(Error::CorruptData);
                if(_pos + blen > _size) return makeError<PcapRecord>(Error::TruncatedData);
                const size_t bodyOff = _pos + 8;
                const size_t bodyLen = blen - 12; // excludes 8-byte header + 4-byte trailing length

                if(btype == PngBlockShb) {
                        // New section: re-establish byte order and reset the
                        // interface table.  (A section may use the opposite
                        // endianness from the previous one.)
                        if(bodyLen >= 4) {
                                if(rd32(base + bodyOff, false) == PngByteOrderMagic) {
                                        _be = false;
                                } else if(rd32(base + bodyOff, true) == PngByteOrderMagic) {
                                        _be = true;
                                } else {
                                        return makeError<PcapRecord>(Error::CorruptData);
                                }
                                _byteOrder = _be ? PcapByteOrder::BigEndian : PcapByteOrder::LittleEndian;
                        }
                        _interfaces.clear();
                } else if(btype == PngBlockIdb) {
                        const Error e = consumePcapngIdb(bodyOff, bodyLen);
                        if(e.isError()) return makeError<PcapRecord>(e);
                } else if(btype == PngBlockEpb) {
                        // interface_id(4) ts_high(4) ts_low(4) cap_len(4) orig_len(4) data...
                        if(bodyLen < 20) return makeError<PcapRecord>(Error::CorruptData);
                        const uint32_t ifid = rd32(base + bodyOff, _be);
                        const uint32_t tsHi = rd32(base + bodyOff + 4, _be);
                        const uint32_t tsLo = rd32(base + bodyOff + 8, _be);
                        const uint32_t cap = rd32(base + bodyOff + 12, _be);
                        const uint32_t orig = rd32(base + bodyOff + 16, _be);
                        if(cap > MaxRecordLength || 20 + static_cast<size_t>(cap) > bodyLen) {
                                return makeError<PcapRecord>(Error::CorruptData);
                        }
                        if(ifid >= _interfaces.size()) return makeError<PcapRecord>(Error::CorruptData);
                        const Interface &itf = _interfaces[ifid];
                        PcapRecord rec;
                        rec.captureTime = unixNanosToDateTime(pcapngTicksToNanos(rd64hl(tsHi, tsLo), itf.tsResolCode));
                        rec.linkType = itf.linkType;
                        rec.frame = BufferView(_backing, bodyOff + 20, cap);
                        rec.originalLength = orig;
                        rec.snapTruncated = cap < orig;
                        _pos += blen;
                        return makeResult(rec);
                } else if(btype == PngBlockSpb) {
                        // orig_len(4) data... — no timestamp, no interface id
                        // (interface 0 by definition).
                        if(bodyLen < 4) return makeError<PcapRecord>(Error::CorruptData);
                        if(_interfaces.isEmpty()) return makeError<PcapRecord>(Error::CorruptData);
                        const uint32_t orig = rd32(base + bodyOff, _be);
                        const Interface &itf = _interfaces[0];
                        size_t cap = orig;
                        if(itf.snapLength != 0 && cap > itf.snapLength) cap = itf.snapLength;
                        if(cap > bodyLen - 4) cap = bodyLen - 4;
                        PcapRecord rec;
                        rec.captureTime = DateTime(); // SPB carries no timestamp
                        rec.linkType = itf.linkType;
                        rec.frame = BufferView(_backing, bodyOff + 4, cap);
                        rec.originalLength = orig;
                        rec.snapTruncated = cap < orig;
                        _pos += blen;
                        return makeResult(rec);
                }
                // Unknown / uninteresting block (e.g. NRB, ISB): skip it.
                _pos += blen;
        }
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
