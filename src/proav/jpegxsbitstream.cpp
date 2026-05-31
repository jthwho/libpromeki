/**
 * @file      jpegxsbitstream.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/jpegxsbitstream.h>

#include <cstring>
#include <promeki/error.h>
#include <promeki/pixelformat.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        inline uint16_t readBe16(const uint8_t *p) {
                return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
        }

        // Returns @c true when @p marker carries a 16-bit length+payload;
        // @c false for the pure-delimiter markers (SOC / EOC).
        inline bool markerHasPayload(uint16_t marker) {
                return marker != JpegXsBitstream::MarkerSoc && marker != JpegXsBitstream::MarkerEoc;
        }

} // namespace

Error JpegXsBitstream::parsePictureHeader(const BufferView &view, PictureInfo &out) {
        if (!view.isValid() || view.size() < 2) return Error::InvalidArgument;
        const uint8_t *base = view.data();
        const size_t   len = view.size();
        size_t         off = 0;

        bool gotPih = false;

        // Walk markers until we have both PIH and (optionally) CDT, or
        // run out of input / hit EOC.  CDT is required to learn
        // per-component bit depth + subsampling — without it we
        // populate @c hSubsampling / @c vSubsampling with the
        // per-component defaults of 1 (i.e. report 4:4:4-equivalent).
        while (off + 2 <= len) {
                const uint16_t marker = readBe16(base + off);
                off += 2;
                if (marker == MarkerEoc) break;
                if (!markerHasPayload(marker)) {
                        // Pure delimiter (e.g. SOC) — no length / payload.
                        continue;
                }
                if (off + 2 > len) return Error::CorruptData;
                const uint16_t segLen = readBe16(base + off);
                // segLen includes its own 2 bytes but excludes the
                // marker.  Validate before slicing.
                if (segLen < 2) return Error::CorruptData;
                if (off + segLen > len) return Error::CorruptData;
                const uint8_t *segPayload = base + off + 2;
                const size_t   payloadLen = static_cast<size_t>(segLen - 2);
                off += segLen;

                if (marker == MarkerPih) {
                        // PIH payload (post-Lpih) is 23 bytes:
                        //   Lcod (4) Ppih (2) Plev (2) Wf (2) Hf (2)
                        //   Cw (2) Hsl (2) Nc (1) Ng (1) Ss (1) Bw (1)
                        //   Fq+Br (1) Fslc+Ppoc+Cpih (1) Nlx+Nly (1)
                        if (payloadLen < 23) return Error::CorruptData;
                        out.profile = readBe16(segPayload + 4);
                        out.level = readBe16(segPayload + 6);
                        out.width = readBe16(segPayload + 8);
                        out.height = readBe16(segPayload + 10);
                        out.numComponents = segPayload[16];
                        // Bw is bit precision of weights / raw bytes —
                        // not the picture bit depth, but a useful
                        // fallback when the CDT marker is missing.
                        out.bitDepth = segPayload[19];
                        gotPih = true;
                } else if (marker == MarkerCdt && gotPih && out.numComponents > 0) {
                        // CDT payload: 2 bytes per component
                        //   Bc (8)        — bit depth minus 1
                        //   Sx (4) | Sy (4) — horizontal / vertical subsampling
                        const unsigned nc = out.numComponents;
                        if (nc > 4) return Error::NotSupported;
                        const size_t need = static_cast<size_t>(nc) * 2u;
                        if (payloadLen < need) return Error::CorruptData;
                        for (unsigned i = 0; i < nc; ++i) {
                                const uint8_t bc = segPayload[i * 2 + 0];
                                const uint8_t sxsy = segPayload[i * 2 + 1];
                                out.perComponentBitDepth[i] = static_cast<uint8_t>(bc + 1u);
                                out.hSubsampling[i] = static_cast<uint8_t>((sxsy >> 4) & 0x0F);
                                out.vSubsampling[i] = static_cast<uint8_t>(sxsy & 0x0F);
                                if (out.hSubsampling[i] == 0) out.hSubsampling[i] = 1;
                                if (out.vSubsampling[i] == 0) out.vSubsampling[i] = 1;
                        }
                        // Picture bit depth = first component's bit
                        // depth (luma in YUV, R in RGB).  Cleaner than
                        // the Bw fallback we set from PIH.
                        out.bitDepth = out.perComponentBitDepth[0];
                        out.hasCdt = true;
                }

                if (gotPih && out.hasCdt) break;
        }

        if (!gotPih) return Error::NotFound;
        return Error::Ok;
}

PixelFormat::ID JpegXsBitstream::pixelFormatFor(const PictureInfo &info) {
        if (!info.hasCdt || info.numComponents == 0) return PixelFormat::Invalid;

        // RGB 8-bit (3 components, no subsampling).  JPEG XS doesn't
        // carry the colour-space hint in the codestream the way JPEG
        // 2000 does — RGB vs YUV is determined out-of-band (RTP SDP /
        // MP4 sample entry) — so we use the subsampling pattern as a
        // pragmatic proxy: 4:4:4 + 8-bit components → RGB sRGB.
        if (info.numComponents == 3 && info.hSubsampling[0] == 1 && info.hSubsampling[1] == 1 &&
            info.hSubsampling[2] == 1 && info.vSubsampling[0] == 1 && info.vSubsampling[1] == 1 &&
            info.vSubsampling[2] == 1 && info.bitDepth == 8) {
                return PixelFormat::JPEG_XS_RGB8_sRGB;
        }

        // YUV variants — 3 components, luma full-rate.
        if (info.numComponents == 3 && info.hSubsampling[0] == 1 && info.vSubsampling[0] == 1) {
                const bool h422 = (info.hSubsampling[1] == 2 && info.hSubsampling[2] == 2 &&
                                   info.vSubsampling[1] == 1 && info.vSubsampling[2] == 1);
                const bool h420 = (info.hSubsampling[1] == 2 && info.hSubsampling[2] == 2 &&
                                   info.vSubsampling[1] == 2 && info.vSubsampling[2] == 2);
                if (h422) {
                        switch (info.bitDepth) {
                                case 8:  return PixelFormat::JPEG_XS_YUV8_422_Rec709;
                                case 10: return PixelFormat::JPEG_XS_YUV10_422_Rec709;
                                case 12: return PixelFormat::JPEG_XS_YUV12_422_Rec709;
                                default: return PixelFormat::Invalid;
                        }
                }
                if (h420) {
                        switch (info.bitDepth) {
                                case 8:  return PixelFormat::JPEG_XS_YUV8_420_Rec709;
                                case 10: return PixelFormat::JPEG_XS_YUV10_420_Rec709;
                                case 12: return PixelFormat::JPEG_XS_YUV12_420_Rec709;
                                default: return PixelFormat::Invalid;
                        }
                }
        }

        return PixelFormat::Invalid;
}

PROMEKI_NAMESPACE_END
