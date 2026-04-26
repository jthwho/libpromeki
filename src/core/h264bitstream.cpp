/**
 * @file      h264bitstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/h264bitstream.h>

#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /** @brief Returns true if @p sz is a valid AVCC @c lengthSizeMinusOne+1. */
        bool isValidLenSize(uint8_t sz) {
                return sz == 1 || sz == 2 || sz == 4;
        }

        /**
 * @brief Locate the next Annex-B start code in @p data, beginning at
 *        offset @p from.
 *
 * Recognizes both the 3-byte form (@c 00 00 01) and the 4-byte form
 * (@c 00 00 00 01).  On return, @p outCodeStart points to the first
 * zero byte of the start code and @p outCodeLen is 3 or 4.  Returns
 * @c false if no start code is found before end-of-buffer.
 */
        bool findNextStartCode(const uint8_t *data, size_t len, size_t from, size_t &outCodeStart, size_t &outCodeLen) {
                // Walk the buffer looking for 00 00 01 or 00 00 00 01.  The
                // short form is searched first on each position; this is
                // sufficient because a 4-byte code begins with the same 00 00
                // 01 pattern at offset+1, and we prefer the position-zero
                // match so the preceding 00 is counted as part of the code.
                for (size_t i = from; i + 2 < len; ++i) {
                        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
                                if (i > 0 && data[i - 1] == 0x00) {
                                        outCodeStart = i - 1;
                                        outCodeLen = 4;
                                } else {
                                        outCodeStart = i;
                                        outCodeLen = 3;
                                }
                                return true;
                        }
                }
                return false;
        }

        /** @brief Write @p value as a big-endian integer of @p lenSize bytes. */
        void writeBE(uint8_t *dst, uint64_t value, uint8_t lenSize) {
                for (int i = lenSize - 1; i >= 0; --i) {
                        dst[i] = static_cast<uint8_t>(value & 0xff);
                        value >>= 8;
                }
        }

        /** @brief Read a big-endian integer of @p lenSize bytes from @p src. */
        uint64_t readBE(const uint8_t *src, uint8_t lenSize) {
                uint64_t v = 0;
                for (uint8_t i = 0; i < lenSize; ++i) {
                        v = (v << 8) | src[i];
                }
                return v;
        }

} // namespace

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

Error H264Bitstream::forEachAnnexBNal(const BufferView &in, const Visitor &visit) {
        // H.264 bitstreams are a single contiguous byte run — we operate
        // on the first slice of @p in.  Callers typically pass a
        // single-slice BufferView built from their compressed payload.
        if (in.isEmpty()) return Error::Ok;
        auto slice = in[0];
        if (!slice.isValid() || slice.size() == 0) return Error::Ok;

        const uint8_t *data = slice.data();
        size_t         len = slice.size();

        size_t codeStart = 0;
        size_t codeLen = 0;
        if (!findNextStartCode(data, len, 0, codeStart, codeLen)) {
                // Input is non-empty but contains no start code anywhere:
                // the caller handed us a buffer that is not an Annex-B
                // stream.  Flag structural corruption rather than silently
                // returning success.
                return Error::CorruptData;
        }

        // Walk NAL-by-NAL.  Each iteration starts with codeStart
        // pointing at the current NAL's leading start code; the NAL
        // payload runs from codeStart+codeLen to the next start code
        // (or to end-of-buffer for the last NAL).
        while (true) {
                size_t nalStart = codeStart + codeLen;
                size_t nextCodeStart = 0;
                size_t nextCodeLen = 0;
                bool   haveNext = findNextStartCode(data, len, nalStart, nextCodeStart, nextCodeLen);
                size_t nalEnd = haveNext ? nextCodeStart : len;
                if (nalEnd > nalStart) {
                        NalUnit nal;
                        size_t  offsetIntoBuffer = slice.offset() + nalStart;
                        size_t  nalLen = nalEnd - nalStart;
                        nal.view = BufferView(slice.buffer(), offsetIntoBuffer, nalLen);
                        nal.header0 = data[nalStart];
                        nal.header1 = (nalLen > 1) ? data[nalStart + 1] : 0;
                        Error e = visit(nal);
                        if (e.isError()) return e;
                }
                if (!haveNext) break;
                codeStart = nextCodeStart;
                codeLen = nextCodeLen;
        }
        return Error::Ok;
}

Error H264Bitstream::forEachAvccNal(const BufferView &in, uint8_t lenSize, const Visitor &visit) {
        if (!isValidLenSize(lenSize)) return Error::InvalidArgument;
        if (in.isEmpty()) return Error::Ok;
        auto slice = in[0];
        if (!slice.isValid() || slice.size() == 0) return Error::Ok;

        const uint8_t *data = slice.data();
        size_t         len = slice.size();
        size_t         pos = 0;

        while (pos < len) {
                if (pos + lenSize > len) return Error::CorruptData;
                uint64_t nalLen = readBE(data + pos, lenSize);
                pos += lenSize;
                if (nalLen > len - pos) return Error::CorruptData;
                if (nalLen > 0) {
                        NalUnit nal;
                        nal.view = BufferView(slice.buffer(), slice.offset() + pos, static_cast<size_t>(nalLen));
                        nal.header0 = data[pos];
                        nal.header1 = (nalLen > 1) ? data[pos + 1] : 0;
                        Error e = visit(nal);
                        if (e.isError()) return e;
                }
                pos += static_cast<size_t>(nalLen);
        }
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

Error H264Bitstream::annexBToAvcc(const BufferView &in, uint8_t lenSize, Buffer::Ptr &outBuf) {
        if (!isValidLenSize(lenSize)) return Error::InvalidArgument;

        // Two-pass: first walk the input to compute the total output
        // size (lenSize + payload per NAL), then allocate once and
        // copy.  Avoids a growing-buffer branch and keeps the output
        // tightly sized.
        size_t         totalSize = 0;
        const uint64_t maxPayload = (lenSize == 4) ? 0xffffffffull : (lenSize == 2) ? 0xffffull : 0xffull;
        auto           sizingVisitor = [&](const NalUnit &nal) -> Error {
                if (nal.view.size() > maxPayload) return Error::CorruptData;
                totalSize += lenSize + nal.view.size();
                return Error::Ok;
        };
        Error err = forEachAnnexBNal(in, sizingVisitor);
        if (err.isError()) return err;

        Buffer::Ptr buf = Buffer::Ptr::create(totalSize);
        if (!buf) return Error::NoMem;
        uint8_t *dst = static_cast<uint8_t *>(buf->data());
        size_t   cursor = 0;
        auto     copyVisitor = [&](const NalUnit &nal) -> Error {
                writeBE(dst + cursor, static_cast<uint64_t>(nal.view.size()), lenSize);
                cursor += lenSize;
                if (nal.view.size() > 0) {
                        std::memcpy(dst + cursor, nal.view.data(), nal.view.size());
                        cursor += nal.view.size();
                }
                return Error::Ok;
        };
        err = forEachAnnexBNal(in, copyVisitor);
        if (err.isError()) return err;
        buf->setSize(totalSize);
        outBuf = buf;
        return Error::Ok;
}

Error H264Bitstream::annexBToAvccFiltered(const BufferView &in, uint8_t lenSize,
                                          const std::function<bool(const NalUnit &)> &keep, Buffer::Ptr &outBuf) {
        if (!isValidLenSize(lenSize)) return Error::InvalidArgument;

        size_t         totalSize = 0;
        const uint64_t maxPayload = (lenSize == 4) ? 0xffffffffull : (lenSize == 2) ? 0xffffull : 0xffull;
        auto           sizingVisitor = [&](const NalUnit &nal) -> Error {
                if (!keep(nal)) return Error::Ok;
                if (nal.view.size() > maxPayload) return Error::CorruptData;
                totalSize += lenSize + nal.view.size();
                return Error::Ok;
        };
        Error err = forEachAnnexBNal(in, sizingVisitor);
        if (err.isError()) return err;

        Buffer::Ptr buf = Buffer::Ptr::create(totalSize);
        if (!buf) return Error::NoMem;
        uint8_t *dst = static_cast<uint8_t *>(buf->data());
        size_t   cursor = 0;
        auto     copyVisitor = [&](const NalUnit &nal) -> Error {
                if (!keep(nal)) return Error::Ok;
                writeBE(dst + cursor, static_cast<uint64_t>(nal.view.size()), lenSize);
                cursor += lenSize;
                if (nal.view.size() > 0) {
                        std::memcpy(dst + cursor, nal.view.data(), nal.view.size());
                        cursor += nal.view.size();
                }
                return Error::Ok;
        };
        err = forEachAnnexBNal(in, copyVisitor);
        if (err.isError()) return err;
        buf->setSize(totalSize);
        outBuf = buf;
        return Error::Ok;
}

Error H264Bitstream::avccToAnnexB(const BufferView &in, uint8_t lenSize, Buffer::Ptr &outBuf) {
        if (!isValidLenSize(lenSize)) return Error::InvalidArgument;

        // Output size = sum over NALs of (4 start-code bytes + NAL payload).
        size_t totalSize = 0;
        auto   sizingVisitor = [&](const NalUnit &nal) -> Error {
                totalSize += 4 + nal.view.size();
                return Error::Ok;
        };
        Error err = forEachAvccNal(in, lenSize, sizingVisitor);
        if (err.isError()) return err;

        Buffer::Ptr buf = Buffer::Ptr::create(totalSize);
        if (!buf) return Error::NoMem;
        uint8_t *dst = static_cast<uint8_t *>(buf->data());
        size_t   cursor = 0;
        auto     copyVisitor = [&](const NalUnit &nal) -> Error {
                dst[cursor + 0] = 0x00;
                dst[cursor + 1] = 0x00;
                dst[cursor + 2] = 0x00;
                dst[cursor + 3] = 0x01;
                cursor += 4;
                if (nal.view.size() > 0) {
                        std::memcpy(dst + cursor, nal.view.data(), nal.view.size());
                        cursor += nal.view.size();
                }
                return Error::Ok;
        };
        err = forEachAvccNal(in, lenSize, copyVisitor);
        if (err.isError()) return err;
        buf->setSize(totalSize);
        outBuf = buf;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// AvcDecoderConfig
// ---------------------------------------------------------------------------

namespace {

        /** @brief H.264 NAL unit type codes used by the parameter-set extractor. */
        enum H264NalType : uint8_t {
                H264NalTypeSliceIdr = 5,
                H264NalTypeSps = 7,
                H264NalTypePps = 8,
        };

        /** @brief Returns the H.264 nal_unit_type (low 5 bits of NAL header). */
        uint8_t h264NalType(uint8_t header0) {
                return header0 & 0x1f;
        }

        /** @brief Deep-copies a BufferView's single-slice bytes into a freshly allocated Buffer. */
        Buffer::Ptr copyView(const BufferView &v) {
                auto        slice = v[0];
                Buffer::Ptr buf = Buffer::Ptr::create(slice.size());
                if (!buf) return buf;
                if (slice.size() > 0) std::memcpy(buf->data(), slice.data(), slice.size());
                buf->setSize(slice.size());
                return buf;
        }

} // namespace

Error AvcDecoderConfig::fromAnnexB(const BufferView &au, AvcDecoderConfig &out) {
        out.sps.clear();
        out.pps.clear();
        out.configurationVersion = 1;
        out.lengthSizeMinusOne = 3;
        out.avcProfileIndication = 0;
        out.profileCompatibility = 0;
        out.avcLevelIndication = 0;

        bool  haveProfile = false;
        Error iterErr = H264Bitstream::forEachAnnexBNal(au, [&](const H264Bitstream::NalUnit &nal) {
                uint8_t t = h264NalType(nal.header0);
                auto    nalSlice = nal.view[0];
                if (t == H264NalTypeSps) {
                        // Profile / compat / level are at SPS payload
                        // bytes 1-3 (byte 0 is the NAL header).
                        if (!haveProfile && nalSlice.size() >= 4) {
                                out.avcProfileIndication = nalSlice.data()[1];
                                out.profileCompatibility = nalSlice.data()[2];
                                out.avcLevelIndication = nalSlice.data()[3];
                                haveProfile = true;
                        }
                        Buffer::Ptr copy = copyView(nal.view);
                        if (!copy) return Error(Error::NoMem);
                        out.sps.pushToBack(copy);
                } else if (t == H264NalTypePps) {
                        Buffer::Ptr copy = copyView(nal.view);
                        if (!copy) return Error(Error::NoMem);
                        out.pps.pushToBack(copy);
                }
                return Error(Error::Ok);
        });
        if (iterErr.isError()) return iterErr;
        if (out.sps.isEmpty()) return Error::InvalidArgument;
        return Error::Ok;
}

Error AvcDecoderConfig::parse(const BufferView &payload, AvcDecoderConfig &out) {
        out.sps.clear();
        out.pps.clear();

        auto           slice = payload[0];
        const uint8_t *data = slice.data();
        size_t         len = slice.size();
        size_t         pos = 0;
        auto           need = [&](size_t n) -> bool {
                return pos + n <= len;
        };

        if (!need(6)) return Error::CorruptData;
        out.configurationVersion = data[pos + 0];
        out.avcProfileIndication = data[pos + 1];
        out.profileCompatibility = data[pos + 2];
        out.avcLevelIndication = data[pos + 3];
        out.lengthSizeMinusOne = data[pos + 4] & 0x03;
        uint8_t numSps = data[pos + 5] & 0x1f;
        pos += 6;

        for (uint8_t i = 0; i < numSps; ++i) {
                if (!need(2)) return Error::CorruptData;
                uint16_t spsLen = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
                pos += 2;
                if (!need(spsLen)) return Error::CorruptData;
                Buffer::Ptr buf = copyView(BufferView(slice.buffer(), slice.offset() + pos, spsLen));
                if (!buf) return Error::NoMem;
                out.sps.pushToBack(buf);
                pos += spsLen;
        }

        if (!need(1)) return Error::CorruptData;
        uint8_t numPps = data[pos++];
        for (uint8_t i = 0; i < numPps; ++i) {
                if (!need(2)) return Error::CorruptData;
                uint16_t ppsLen = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
                pos += 2;
                if (!need(ppsLen)) return Error::CorruptData;
                Buffer::Ptr buf = copyView(BufferView(slice.buffer(), slice.offset() + pos, ppsLen));
                if (!buf) return Error::NoMem;
                out.pps.pushToBack(buf);
                pos += ppsLen;
        }
        return Error::Ok;
}

Error AvcDecoderConfig::serialize(Buffer::Ptr &outBuf) const {
        // Reject oversize parameter-set lists up front — the avcC
        // record uses a 5-bit SPS count and an 8-bit PPS count.
        if (sps.size() > 0x1f) return Error::InvalidArgument;
        if (pps.size() > 0xff) return Error::InvalidArgument;

        // Compute output size: 6-byte header, then per-SPS (2 + len),
        // 1-byte numPps, per-PPS (2 + len).
        size_t total = 6;
        for (const Buffer::Ptr &s : sps) {
                if (!s) return Error::InvalidArgument;
                if (s->size() > 0xffff) return Error::InvalidArgument;
                total += 2 + s->size();
        }
        total += 1;
        for (const Buffer::Ptr &p : pps) {
                if (!p) return Error::InvalidArgument;
                if (p->size() > 0xffff) return Error::InvalidArgument;
                total += 2 + p->size();
        }

        Buffer::Ptr buf = Buffer::Ptr::create(total);
        if (!buf) return Error::NoMem;
        uint8_t *dst = static_cast<uint8_t *>(buf->data());
        size_t   cursor = 0;

        dst[cursor++] = configurationVersion;
        dst[cursor++] = avcProfileIndication;
        dst[cursor++] = profileCompatibility;
        dst[cursor++] = avcLevelIndication;
        dst[cursor++] = static_cast<uint8_t>(0xfc | (lengthSizeMinusOne & 0x03));
        dst[cursor++] = static_cast<uint8_t>(0xe0 | (sps.size() & 0x1f));
        for (const Buffer::Ptr &s : sps) {
                uint16_t n = static_cast<uint16_t>(s->size());
                dst[cursor++] = static_cast<uint8_t>((n >> 8) & 0xff);
                dst[cursor++] = static_cast<uint8_t>(n & 0xff);
                if (n > 0) std::memcpy(dst + cursor, s->data(), n);
                cursor += n;
        }
        dst[cursor++] = static_cast<uint8_t>(pps.size());
        for (const Buffer::Ptr &p : pps) {
                uint16_t n = static_cast<uint16_t>(p->size());
                dst[cursor++] = static_cast<uint8_t>((n >> 8) & 0xff);
                dst[cursor++] = static_cast<uint8_t>(n & 0xff);
                if (n > 0) std::memcpy(dst + cursor, p->data(), n);
                cursor += n;
        }
        buf->setSize(total);
        outBuf = buf;
        return Error::Ok;
}

Error AvcDecoderConfig::toAnnexB(Buffer::Ptr &outBuf) const {
        List<BufferView> nals;
        for (const Buffer::Ptr &s : sps) {
                nals.pushToBack(BufferView(s, 0, s ? s->size() : 0));
        }
        for (const Buffer::Ptr &p : pps) {
                nals.pushToBack(BufferView(p, 0, p ? p->size() : 0));
        }
        return H264Bitstream::wrapNalsAsAnnexB(nals, outBuf);
}

Error H264Bitstream::wrapNalsAsAnnexB(const List<BufferView> &nals, Buffer::Ptr &outBuf) {
        size_t totalSize = 0;
        for (const BufferView &v : nals) {
                totalSize += 4 + v.size();
        }
        Buffer::Ptr buf = Buffer::Ptr::create(totalSize);
        if (!buf) return Error::NoMem;
        uint8_t *dst = static_cast<uint8_t *>(buf->data());
        size_t   cursor = 0;
        for (const BufferView &v : nals) {
                dst[cursor + 0] = 0x00;
                dst[cursor + 1] = 0x00;
                dst[cursor + 2] = 0x00;
                dst[cursor + 3] = 0x01;
                cursor += 4;
                if (v.size() > 0) {
                        std::memcpy(dst + cursor, v.data(), v.size());
                        cursor += v.size();
                }
        }
        buf->setSize(totalSize);
        outBuf = buf;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
