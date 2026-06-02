/**
 * @file      v4l2captionsei.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_V4L2

#include <cstring>
#include <utility>

#include <promeki/v4l2captionsei.h>
#include <promeki/h264bitstream.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // H.264: nal_unit_type = header0 & 0x1f, VCL = 1..5.
        // HEVC:  nal_unit_type = (header0 >> 1) & 0x3f, VCL = 0..31.
        int nalType(uint8_t header0, bool hevc) {
                return hevc ? ((header0 >> 1) & 0x3f) : (header0 & 0x1f);
        }
        bool isVclNal(uint8_t header0, bool hevc) {
                const int t = nalType(header0, hevc);
                return hevc ? (t < 32) : (t >= 1 && t <= 5);
        }
        bool isSeiNal(uint8_t header0, bool hevc) {
                const int t = nalType(header0, hevc);
                return hevc ? (t == 39 || t == 40) : (t == 6);
        }

        // Appends `v` to `out` as a chain of 0xFF bytes plus a final remainder,
        // the SEI payloadType / payloadSize coding.
        void putFFCoded(List<uint8_t> &out, size_t v) {
                while (v >= 255) {
                        out.pushToBack(0xFF);
                        v -= 255;
                }
                out.pushToBack(static_cast<uint8_t>(v));
        }

        // RBSP → EBSP: insert an emulation_prevention_three_byte (0x03) after any
        // 0x00 0x00 followed by a byte <= 0x03.
        void escapeEmulation(const List<uint8_t> &rbsp, List<uint8_t> &out) {
                int zeros = 0;
                for (uint8_t b : rbsp) {
                        if (zeros >= 2 && b <= 0x03) {
                                out.pushToBack(0x03);
                                zeros = 0;
                        }
                        out.pushToBack(b);
                        zeros = (b == 0x00) ? (zeros + 1) : 0;
                }
        }

        // EBSP → RBSP: drop the 0x03 in any 0x00 0x00 0x03 sequence.
        void unescapeEmulation(const uint8_t *p, size_t n, List<uint8_t> &out) {
                int zeros = 0;
                for (size_t i = 0; i < n; ++i) {
                        const uint8_t b = p[i];
                        if (zeros >= 2 && b == 0x03 && i + 1 < n && p[i + 1] <= 0x03) {
                                zeros = 0; // skip the emulation-prevention byte
                                continue;
                        }
                        out.pushToBack(b);
                        zeros = (b == 0x00) ? (zeros + 1) : 0;
                }
        }

} // namespace

Buffer v4l2BuildSeiNal(int payloadType, const BufferView &payloadBody, bool hevc) {
        // --- sei_message + rbsp_trailing_bits (the RBSP) ---
        List<uint8_t> rbsp;
        putFFCoded(rbsp, static_cast<size_t>(payloadType));
        putFFCoded(rbsp, payloadBody.size());
        const uint8_t *body = static_cast<const uint8_t *>(payloadBody.data());
        for (size_t i = 0; i < payloadBody.size(); ++i) rbsp.pushToBack(body[i]);
        rbsp.pushToBack(0x80); // rbsp_trailing_bits (stop bit + alignment).

        // --- NAL header + emulation-prevented RBSP ---
        List<uint8_t> nal;
        if (hevc) {
                nal.pushToBack(static_cast<uint8_t>(39 << 1)); // prefix SEI, layer 0
                nal.pushToBack(0x01);                          // temporal_id_plus1 = 1
        } else {
                nal.pushToBack(0x06); // nal_ref_idc 0, type 6 (SEI)
        }
        escapeEmulation(rbsp, nal);

        Buffer out(nal.size());
        std::memcpy(out.data(), nal.data(), nal.size());
        out.setSize(nal.size());
        return out;
}

Error v4l2InjectSeiNals(const BufferView &codedIn, const List<Buffer> &seiNals, bool hevc, Buffer &out) {
        if (seiNals.isEmpty()) {
                out = Buffer(codedIn.size());
                if (codedIn.size()) std::memcpy(out.data(), codedIn.data(), codedIn.size());
                out.setSize(codedIn.size());
                return Error::Ok;
        }

        // Collect the original NALs and the index of the first VCL NAL.
        List<BufferView> nals;
        int              firstVcl = -1;
        H264Bitstream::forEachAnnexBNal(codedIn, [&](const H264Bitstream::NalUnit &nal) -> Error {
                if (firstVcl < 0 && isVclNal(nal.header0, hevc)) {
                        firstVcl = static_cast<int>(nals.size());
                }
                nals.pushToBack(nal.view);
                return Error::Ok;
        });

        // Rebuild: everything before the first VCL, then the SEI NALs, then the
        // rest.  No VCL NAL (unexpected) → append the SEI after the existing NALs.
        const int insertAt = (firstVcl >= 0) ? firstVcl : static_cast<int>(nals.size());
        List<BufferView> rebuilt;
        for (int i = 0; i < insertAt; ++i) rebuilt.pushToBack(nals[i]);
        for (const Buffer &sei : seiNals) rebuilt.pushToBack(BufferView(sei, 0, sei.size()));
        for (int i = insertAt; i < static_cast<int>(nals.size()); ++i) rebuilt.pushToBack(nals[i]);

        return H264Bitstream::wrapNalsAsAnnexB(rebuilt, out);
}

List<Buffer> v4l2ExtractSeiPayloads(const BufferView &codedIn, int payloadType, bool hevc) {
        List<Buffer> bodies;
        H264Bitstream::forEachAnnexBNal(codedIn, [&](const H264Bitstream::NalUnit &nal) -> Error {
                if (!isSeiNal(nal.header0, hevc)) return Error::Ok;

                const size_t   headerBytes = hevc ? 2 : 1;
                const uint8_t *raw = static_cast<const uint8_t *>(nal.view.data());
                if (nal.view.size() <= headerBytes) return Error::Ok;

                // De-emulate the RBSP (everything after the NAL header).
                List<uint8_t> rbsp;
                unescapeEmulation(raw + headerBytes, nal.view.size() - headerBytes, rbsp);

                // Walk sei_message entries until rbsp_trailing_bits.
                size_t i = 0;
                while (i < rbsp.size()) {
                        if (rbsp[i] == 0x80 && i + 1 >= rbsp.size()) break; // trailing bits
                        size_t type = 0;
                        while (i < rbsp.size() && rbsp[i] == 0xFF) {
                                type += 255;
                                ++i;
                        }
                        if (i >= rbsp.size()) break;
                        type += rbsp[i++];
                        size_t size = 0;
                        while (i < rbsp.size() && rbsp[i] == 0xFF) {
                                size += 255;
                                ++i;
                        }
                        if (i >= rbsp.size()) break;
                        size += rbsp[i++];
                        if (i + size > rbsp.size()) break; // malformed
                        if (static_cast<int>(type) == payloadType && size > 0) {
                                Buffer b(size);
                                std::memcpy(b.data(), rbsp.data() + i, size);
                                b.setSize(size);
                                bodies.pushToBack(std::move(b));
                        }
                        i += size;
                }
                return Error::Ok;
        });
        return bodies;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
