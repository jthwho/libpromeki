/**
 * @file      hevcbitstream.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/hevcbitstream.h>

#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /** @brief HEVC NAL unit type codes used by the parameter-set extractor. */
        enum HevcNalType : uint8_t {
                HevcNalTypeVps = 32,
                HevcNalTypeSps = 33,
                HevcNalTypePps = 34,
                HevcNalTypeIdrWRadl = 19,
                HevcNalTypeIdrNLp = 20,
        };

        /** @brief Returns the HEVC nal_unit_type (bits 1-6 of NAL header byte 0). */
        uint8_t hevcNalType(uint8_t header0) {
                return static_cast<uint8_t>((header0 >> 1) & 0x3f);
        }

        /** @brief Deep-copies a BufferView's bytes into a freshly allocated Buffer. */
        Buffer copyView(const BufferView &v) {
                Buffer buf = Buffer(v.size());
                if (!buf) return buf;
                if (v.size() > 0) std::memcpy(buf.data(), v.data(), v.size());
                buf.setSize(v.size());
                return buf;
        }

        /** @brief Reads a big-endian unsigned integer of @p n bytes from @p src. */
        uint64_t readBE(const uint8_t *src, size_t n) {
                uint64_t v = 0;
                for (size_t i = 0; i < n; ++i) v = (v << 8) | src[i];
                return v;
        }

        /** @brief Writes @p value as a big-endian integer of @p n bytes to @p dst. */
        void writeBE(uint8_t *dst, uint64_t value, size_t n) {
                for (size_t i = 0; i < n; ++i) {
                        dst[n - 1 - i] = static_cast<uint8_t>(value & 0xff);
                        value >>= 8;
                }
        }

        /**
 * @brief Extract profile/tier/level fixed fields from an HEVC SPS NAL.
 *
 * Expects the caller to hand in the SPS NAL payload (NAL header +
 * RBSP) as it appears in the Annex-B stream.  Fills the matching
 * fields in @p cfg on success, leaves them unchanged on failure.
 */
        Error extractHevcSpsProfile(const uint8_t *nal, size_t nalLen, HevcDecoderConfig &cfg) {
                // Minimum layout, in bytes relative to NAL start:
                //   0-1 : NAL header (2 bytes).
                //   2   : sps_vps_id (4) | sps_max_sub_layers_minus1 (3) |
                //         sps_temporal_id_nesting_flag (1).
                //   3   : general_profile_space (2) | general_tier_flag (1) |
                //         general_profile_idc (5).
                //   4-7 : general_profile_compatibility_flags (32 bits, BE).
                //   8-13: general_constraint_indicator_flags (48 bits, BE).
                //   14  : general_level_idc.
                // Total: 15 bytes.
                if (nalLen < 15) return Error::InvalidArgument;

                const uint8_t subLayersByte = nal[2];
                cfg.numTemporalLayers = static_cast<uint8_t>(((subLayersByte >> 1) & 0x07) + 1);
                cfg.temporalIdNested = static_cast<uint8_t>(subLayersByte & 0x01);

                const uint8_t ptlByte0 = nal[3];
                cfg.generalProfileSpace = static_cast<uint8_t>((ptlByte0 >> 6) & 0x03);
                cfg.generalTierFlag = static_cast<uint8_t>((ptlByte0 >> 5) & 0x01);
                cfg.generalProfileIdc = static_cast<uint8_t>(ptlByte0 & 0x1f);

                cfg.generalProfileCompatibilityFlags = static_cast<uint32_t>(readBE(nal + 4, 4));
                cfg.generalConstraintIndicatorFlags = readBE(nal + 8, 6);
                cfg.generalLevelIdc = nal[14];
                return Error::Ok;
        }

} // namespace

// ---------------------------------------------------------------------------
// HevcDecoderConfig
// ---------------------------------------------------------------------------

Error HevcDecoderConfig::fromAnnexB(const BufferView &au, HevcDecoderConfig &out) {
        out = HevcDecoderConfig{};

        bool  haveProfile = false;
        Error iterErr = H264Bitstream::forEachAnnexBNal(au, [&](const H264Bitstream::NalUnit &nal) -> Error {
                uint8_t     t = hevcNalType(nal.header0);
                Buffer copy;
                switch (t) {
                        case HevcNalTypeVps:
                                copy = copyView(nal.view);
                                if (!copy) return Error::NoMem;
                                out.vps.pushToBack(copy);
                                break;
                        case HevcNalTypeSps: {
                                if (!haveProfile) {
                                        Error pe = extractHevcSpsProfile(nal.view.data(), nal.view.size(), out);
                                        if (!pe.isError()) haveProfile = true;
                                }
                                copy = copyView(nal.view);
                                if (!copy) return Error::NoMem;
                                out.sps.pushToBack(copy);
                                break;
                        }
                        case HevcNalTypePps:
                                copy = copyView(nal.view);
                                if (!copy) return Error::NoMem;
                                out.pps.pushToBack(copy);
                                break;
                        default: break;
                }
                return Error::Ok;
        });
        if (iterErr.isError()) return iterErr;
        if (out.sps.isEmpty() || !haveProfile) return Error::InvalidArgument;
        return Error::Ok;
}

Error HevcDecoderConfig::parse(const BufferView &payload, HevcDecoderConfig &out) {
        out = HevcDecoderConfig{};

        const uint8_t *data = payload.data();
        size_t         len = payload.size();
        size_t         pos = 0;
        auto           need = [&](size_t n) -> bool {
                return pos + n <= len;
        };

        // 22-byte fixed header precedes the arrays.
        if (!need(23)) return Error::CorruptData;
        out.configurationVersion = data[0];
        out.generalProfileSpace = static_cast<uint8_t>((data[1] >> 6) & 0x03);
        out.generalTierFlag = static_cast<uint8_t>((data[1] >> 5) & 0x01);
        out.generalProfileIdc = static_cast<uint8_t>(data[1] & 0x1f);
        out.generalProfileCompatibilityFlags = static_cast<uint32_t>(readBE(data + 2, 4));
        out.generalConstraintIndicatorFlags = readBE(data + 6, 6);
        out.generalLevelIdc = data[12];
        out.minSpatialSegmentationIdc = static_cast<uint16_t>(((data[13] & 0x0f) << 8) | data[14]);
        out.parallelismType = static_cast<uint8_t>(data[15] & 0x03);
        out.chromaFormat = static_cast<uint8_t>(data[16] & 0x03);
        out.bitDepthLumaMinus8 = static_cast<uint8_t>(data[17] & 0x07);
        out.bitDepthChromaMinus8 = static_cast<uint8_t>(data[18] & 0x07);
        out.avgFrameRate = static_cast<uint16_t>(readBE(data + 19, 2));
        out.constantFrameRate = static_cast<uint8_t>((data[21] >> 6) & 0x03);
        out.numTemporalLayers = static_cast<uint8_t>((data[21] >> 3) & 0x07);
        out.temporalIdNested = static_cast<uint8_t>((data[21] >> 2) & 0x01);
        out.lengthSizeMinusOne = static_cast<uint8_t>(data[21] & 0x03);
        uint8_t numOfArrays = data[22];
        pos = 23;

        for (uint8_t a = 0; a < numOfArrays; ++a) {
                if (!need(3)) return Error::CorruptData;
                uint8_t  typeByte = data[pos + 0];
                uint8_t  nalType = static_cast<uint8_t>(typeByte & 0x3f);
                uint16_t numNalus = static_cast<uint16_t>(readBE(data + pos + 1, 2));
                pos += 3;

                List<Buffer> *bucket = nullptr;
                if (nalType == HevcNalTypeVps)
                        bucket = &out.vps;
                else if (nalType == HevcNalTypeSps)
                        bucket = &out.sps;
                else if (nalType == HevcNalTypePps)
                        bucket = &out.pps;

                for (uint16_t i = 0; i < numNalus; ++i) {
                        if (!need(2)) return Error::CorruptData;
                        uint16_t nalLen = static_cast<uint16_t>(readBE(data + pos, 2));
                        pos += 2;
                        if (!need(nalLen)) return Error::CorruptData;
                        if (bucket != nullptr) {
                                Buffer buf =
                                        copyView(BufferView(payload.buffer(), payload.offset() + pos, nalLen));
                                if (!buf) return Error::NoMem;
                                bucket->pushToBack(buf);
                        }
                        pos += nalLen;
                }
        }
        return Error::Ok;
}

Error HevcDecoderConfig::serialize(Buffer &outBuf) const {
        // Determine which arrays are populated so we can emit only
        // those (empty arrays are legal but wasteful).
        struct ArrayRef {
                        uint8_t                  nalType;
                        const List<Buffer> *nals;
        };
        ArrayRef arrays[] = {
                {HevcNalTypeVps, &vps},
                {HevcNalTypeSps, &sps},
                {HevcNalTypePps, &pps},
        };

        size_t  total = 23; // fixed header + numOfArrays byte
        uint8_t numArrays = 0;
        for (const ArrayRef &a : arrays) {
                if (a.nals->isEmpty()) continue;
                ++numArrays;
                total += 3; // per-array header (flags+type, numNalus BE16)
                for (const Buffer &n : *a.nals) {
                        if (!n) return Error::InvalidArgument;
                        if (n.size() > 0xffff) return Error::InvalidArgument;
                        total += 2 + n.size();
                }
                if (a.nals->size() > 0xffff) return Error::InvalidArgument;
        }

        Buffer buf = Buffer(total);
        if (!buf) return Error::NoMem;
        uint8_t *dst = static_cast<uint8_t *>(buf.data());

        dst[0] = configurationVersion;
        dst[1] = static_cast<uint8_t>(((generalProfileSpace & 0x03) << 6) | ((generalTierFlag & 0x01) << 5) |
                                      (generalProfileIdc & 0x1f));
        writeBE(dst + 2, generalProfileCompatibilityFlags, 4);
        writeBE(dst + 6, generalConstraintIndicatorFlags, 6);
        dst[12] = generalLevelIdc;
        dst[13] = static_cast<uint8_t>(0xf0 | ((minSpatialSegmentationIdc >> 8) & 0x0f));
        dst[14] = static_cast<uint8_t>(minSpatialSegmentationIdc & 0xff);
        dst[15] = static_cast<uint8_t>(0xfc | (parallelismType & 0x03));
        dst[16] = static_cast<uint8_t>(0xfc | (chromaFormat & 0x03));
        dst[17] = static_cast<uint8_t>(0xf8 | (bitDepthLumaMinus8 & 0x07));
        dst[18] = static_cast<uint8_t>(0xf8 | (bitDepthChromaMinus8 & 0x07));
        writeBE(dst + 19, avgFrameRate, 2);
        dst[21] = static_cast<uint8_t>(((constantFrameRate & 0x03) << 6) | ((numTemporalLayers & 0x07) << 3) |
                                       ((temporalIdNested & 0x01) << 2) | (lengthSizeMinusOne & 0x03));
        dst[22] = numArrays;

        size_t cursor = 23;
        for (const ArrayRef &a : arrays) {
                if (a.nals->isEmpty()) continue;
                // array_completeness=1 (we assert the set is complete),
                // reserved=0, NAL_unit_type in low 6 bits.
                dst[cursor++] = static_cast<uint8_t>(0x80 | (a.nalType & 0x3f));
                writeBE(dst + cursor, static_cast<uint64_t>(a.nals->size()), 2);
                cursor += 2;
                for (const Buffer &n : *a.nals) {
                        uint16_t nlen = static_cast<uint16_t>(n.size());
                        writeBE(dst + cursor, nlen, 2);
                        cursor += 2;
                        if (nlen > 0) std::memcpy(dst + cursor, n.data(), nlen);
                        cursor += nlen;
                }
        }

        buf.setSize(total);
        outBuf = buf;
        return Error::Ok;
}

Error HevcDecoderConfig::toAnnexB(Buffer &outBuf) const {
        List<BufferView> nals;
        auto             append = [&](const List<Buffer> &list) {
                for (const Buffer &p : list) {
                        nals.pushToBack(BufferView(p, 0, p ? p.size() : 0));
                }
        };
        append(vps);
        append(sps);
        append(pps);
        return H264Bitstream::wrapNalsAsAnnexB(nals, outBuf);
}

bool HevcDecoderConfig::isIrapAnnexB(const BufferView &au) {
        // HEVC IRAP NAL unit types: 16-23 (BLA / IDR / CRA + reserved
        // RAP slot).  See ISO/IEC 23008-2 Table 7-1.
        bool found = false;
        H264Bitstream::forEachAnnexBNal(au, [&](const H264Bitstream::NalUnit &nal) -> Error {
                uint8_t t = static_cast<uint8_t>((nal.header0 >> 1) & 0x3f);
                if (t >= 16 && t <= 23) {
                        found = true;
                }
                return Error::Ok;
        });
        return found;
}

namespace {

        // Same RBSP de-emulation as the H.264 path — moved into a
        // separate helper here so we don't tangle our cpp's lifetime
        // with H264Bitstream's anonymous namespace.
        Buffer hevcStripEmulationPrevention(const uint8_t *src, size_t len) {
                Buffer buf(len);
                buf.setSize(len);
                uint8_t *dst = static_cast<uint8_t *>(buf.data());
                size_t   w = 0;
                for (size_t i = 0; i < len; ++i) {
                        if (i + 2 < len && src[i] == 0x00 && src[i + 1] == 0x00 && src[i + 2] == 0x03) {
                                dst[w++] = src[i];
                                dst[w++] = src[i + 1];
                                i += 2;
                        } else {
                                dst[w++] = src[i];
                        }
                }
                buf.setSize(w);
                return buf;
        }

        // MSB-first bit reader for the HEVC SPS RBSP walk.  Same shape
        // as the H.264 reader; kept private here to avoid pulling in
        // a shared bit-reader translation unit just for these two
        // call sites.
        class HevcBitReader {
                public:
                        HevcBitReader(const uint8_t *data, size_t bytes) : _data(data), _bytes(bytes) {}

                        bool eof() const { return _bitOffset >= _bytes * 8; }

                        uint32_t readBits(int n) {
                                uint32_t v = 0;
                                for (int i = 0; i < n; ++i) {
                                        if (eof()) return v;
                                        const size_t byteIdx = _bitOffset / 8;
                                        const int    bitIdx = 7 - static_cast<int>(_bitOffset % 8);
                                        v = (v << 1) | ((_data[byteIdx] >> bitIdx) & 1u);
                                        _bitOffset++;
                                }
                                return v;
                        }

                        uint32_t readUe() {
                                int zeros = 0;
                                while (!eof() && readBits(1) == 0 && zeros < 32) zeros++;
                                if (zeros == 0) return 0;
                                const uint32_t suffix = readBits(zeros);
                                return (1u << zeros) - 1u + suffix;
                        }

                private:
                        const uint8_t *_data;
                        size_t         _bytes;
                        size_t         _bitOffset = 0;
        };

        // Skip an HEVC profile_tier_level structure with @p maxSubLayers
        // sub-layers (= sps_max_sub_layers_minus1).  See ISO/IEC 23008-2
        // §7.3.3.  Returns false if the underlying reader runs short.
        bool skipProfileTierLevel(HevcBitReader &r, uint32_t maxSubLayersMinus1) {
                // general_profile_space (2) | general_tier_flag (1) |
                // general_profile_idc (5)
                r.readBits(8);
                // general_profile_compatibility_flags (32)
                r.readBits(32);
                // general constraint indicator flags (48)
                r.readBits(32);
                r.readBits(16);
                // general_level_idc (8)
                r.readBits(8);

                if (maxSubLayersMinus1 == 0) return !r.eof() ? true : true;

                // Sub-layer profile / level present flags table — 2 bits
                // per sub-layer, then padded with reserved zero bits to a
                // byte boundary (§7.3.3 specifies the loop runs for
                // maxNumSubLayersMinus1 sub-layers, then up to 8 reserved
                // zero bits to align).
                uint32_t subProfilePresent[8] = {0};
                uint32_t subLevelPresent[8] = {0};
                for (uint32_t i = 0; i < maxSubLayersMinus1 && i < 8; ++i) {
                        subProfilePresent[i] = r.readBits(1);
                        subLevelPresent[i] = r.readBits(1);
                }
                if (maxSubLayersMinus1 > 0) {
                        const uint32_t pad = 2u * (8u - maxSubLayersMinus1);
                        if (pad > 0 && pad < 32) r.readBits(static_cast<int>(pad));
                }
                for (uint32_t i = 0; i < maxSubLayersMinus1 && i < 8; ++i) {
                        if (subProfilePresent[i] != 0) {
                                // Same 88-bit shape as the general profile
                                // structure (excluding general_level_idc).
                                r.readBits(8);
                                r.readBits(32);
                                r.readBits(32);
                                r.readBits(16);
                        }
                        if (subLevelPresent[i] != 0) {
                                r.readBits(8); // sub_layer_level_idc
                        }
                }
                return true;
        }

} // namespace

Error HevcDecoderConfig::parseSpsResolution(const BufferView &sps, SpsInfo &out) {
        out = SpsInfo{};
        if (sps.size() < 5) return Error::CorruptData;
        const uint8_t *raw = sps.data();
        const uint8_t  hdr0 = raw[0];
        const uint8_t  type = static_cast<uint8_t>((hdr0 >> 1) & 0x3f);
        if (type != HevcNalTypeSps) return Error::InvalidArgument;

        // HEVC NAL header is 2 bytes; RBSP starts at byte 2.
        Buffer rbsp = hevcStripEmulationPrevention(raw + 2, sps.size() - 2);
        if (rbsp.size() < 4) return Error::CorruptData;
        HevcBitReader r(static_cast<const uint8_t *>(rbsp.data()), rbsp.size());

        r.readBits(4); // sps_video_parameter_set_id
        const uint32_t maxSubLayersMinus1 = r.readBits(3);
        r.readBits(1); // sps_temporal_id_nesting_flag

        if (!skipProfileTierLevel(r, maxSubLayersMinus1)) return Error::CorruptData;

        r.readUe(); // sps_seq_parameter_set_id
        const uint32_t chromaFormatIdc = r.readUe();
        if (chromaFormatIdc > 3) return Error::CorruptData;
        if (chromaFormatIdc == 3) r.readBits(1); // separate_colour_plane_flag
        out.chromaFormatIdc = static_cast<uint8_t>(chromaFormatIdc);

        const uint32_t picWidth = r.readUe();
        const uint32_t picHeight = r.readUe();

        const bool conformanceWindowFlag = r.readBits(1) != 0;
        uint32_t   confLeft = 0, confRight = 0, confTop = 0, confBottom = 0;
        if (conformanceWindowFlag) {
                confLeft = r.readUe();
                confRight = r.readUe();
                confTop = r.readUe();
                confBottom = r.readUe();
        }

        const uint32_t bitDepthLumaMinus8 = r.readUe();
        const uint32_t bitDepthChromaMinus8 = r.readUe();
        if (bitDepthLumaMinus8 > 8 || bitDepthChromaMinus8 > 8) {
                // Out-of-range values usually mean we lost bit-stream
                // sync somewhere.  Carry the parsed dimensions but flag
                // the bit depths as unknown so the caller doesn't take
                // garbage too literally.
                out.bitDepthLumaMinus8 = 0;
                out.bitDepthChromaMinus8 = 0;
        } else {
                out.bitDepthLumaMinus8 = static_cast<uint8_t>(bitDepthLumaMinus8);
                out.bitDepthChromaMinus8 = static_cast<uint8_t>(bitDepthChromaMinus8);
        }

        // Per ISO/IEC 23008-2 §7.4.3.2.1: SubWidthC / SubHeightC come
        // from chroma_format_idc.  The conformance window crop is
        // expressed in the chroma sample grid (not luma), so the crop
        // shrinks from the boundary by SubWidthC × confX and
        // SubHeightC × confY.
        const uint32_t subWidthC = (chromaFormatIdc == 1 || chromaFormatIdc == 2) ? 2u : 1u;
        const uint32_t subHeightC = (chromaFormatIdc == 1) ? 2u : 1u;
        const uint32_t cropX = (confLeft + confRight) * subWidthC;
        const uint32_t cropY = (confTop + confBottom) * subHeightC;
        if (cropX > picWidth || cropY > picHeight) return Error::CorruptData;

        out.width = picWidth - cropX;
        out.height = picHeight - cropY;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
