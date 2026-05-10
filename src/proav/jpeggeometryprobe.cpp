/**
 * @file      jpeggeometryprobe.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/jpeggeometryprobe.h>

#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

JpegGeometryProbe::SofData JpegGeometryProbe::parseSof(const uint8_t *data, size_t size) {
        SofData out;
        if (data == nullptr || size < 2) return out;
        for (size_t i = 0; i + 1 < size; i++) {
                if (data[i] != 0xFFu) continue;
                // SOF0 (0xC0, baseline) and SOF2 (0xC2, progressive)
                // both carry the same field layout we need (length /
                // precision / height / width / component count /
                // per-component sampling).  Other SOFn variants are
                // rare in MJPEG over RTP and intentionally ignored.
                const uint8_t marker = data[i + 1];
                if (marker != 0xC0u && marker != 0xC2u) continue;
                if (i + 9 >= size) break;
                out.height = (static_cast<uint32_t>(data[i + 5]) << 8) |
                             static_cast<uint32_t>(data[i + 6]);
                out.width = (static_cast<uint32_t>(data[i + 7]) << 8) |
                            static_cast<uint32_t>(data[i + 8]);
                out.nf = data[i + 9];
                if (out.nf >= 1 && i + 11 < size) {
                        out.ySf = data[i + 11];
                }
                break;
        }
        return out;
}

void JpegGeometryProbe::reset() {
        _last = Result{};
        _cachedWidth = 0;
        _cachedHeight = 0;
        _cachedNf = 0;
        _cachedYsf = 0;
        _cachedRfc2435Type = 0xFFu;
        _cachedFmtp.clear();
        _hasCacheKey = false;
}

const JpegGeometryProbe::Result &JpegGeometryProbe::probe(const Buffer &reassembled, uint8_t rfc2435Type,
                                                          const String &fmtp) {
        if (!reassembled.isValid() || reassembled.size() == 0) return _last;
        const uint8_t *p = static_cast<const uint8_t *>(reassembled.data());
        const SofData  sof = parseSof(p, reassembled.size());
        if (!sof.isValid()) return _last;

        // Cache hit fast path — every input that drives the
        // PixelFormat resolution matches the last call's input, so we
        // can return the cached Result without re-running the
        // colorimetry parse / PixelFormat lookup.
        if (_hasCacheKey && _last.valid && _cachedWidth == sof.width &&
            _cachedHeight == sof.height && _cachedNf == sof.nf && _cachedYsf == sof.ySf &&
            _cachedRfc2435Type == rfc2435Type && _cachedFmtp == fmtp) {
                return _last;
        }

        // Decide subsampling / RGB-ness.  RFC 2435 Type ≥ 2 indicates
        // RGB encoding (some senders use Type 2/3 for ST 2110-style
        // RGB JPEG).  Otherwise the SOF sampling factor disambiguates
        // 4:2:0 (Hi/Vi == 0x22) from 4:2:2 (0x21).  For the
        // ambiguous-or-missing case we fall back to RFC 2435 Type 0/1.
        bool is420 = false;
        bool isRgb = false;
        if (sof.nf == 1) {
                // Grayscale — single-component JPEG.  Map to a 4:2:0
                // bucket because the receiver needs *some* PixelFormat
                // and the JPEG family registry doesn't have a
                // dedicated grayscale entry; the decoder will produce
                // monochrome regardless.
                is420 = true;
        } else if (sof.nf == 3 && sof.ySf == 0x11u && rfc2435Type >= 2u) {
                isRgb = true;
        } else if (sof.ySf == 0x22u) {
                is420 = true;
        } else if (sof.ySf == 0x21u) {
                is420 = false;
        } else {
                is420 = (rfc2435Type == 1u);
        }

        // Pull colorimetry / RANGE out of the SDP fmtp.  The fmtp is
        // a "key=value;key=value" string; we tolerate whitespace and
        // ignore unknown keys.  Empty fmtp falls back to JFIF
        // defaults (BT.601 full range) inside
        // @ref ImageDesc::jpegPixelFormatFromSdp.
        String colorimetry;
        String range;
        if (!fmtp.isEmpty()) {
                StringList parts = fmtp.split(";");
                for (size_t i = 0; i < parts.size(); i++) {
                        StringList kv = parts[i].split("=");
                        if (kv.size() < 2) continue;
                        String key = kv[0].trim();
                        String val = kv[1].trim();
                        if (key == "colorimetry") {
                                colorimetry = val;
                        } else if (key == "RANGE") {
                                range = val;
                        }
                }
        }

        const PixelFormat::ID pid =
                ImageDesc::jpegPixelFormatFromSdp(colorimetry, range, is420, isRgb);

        _last.valid = true;
        _last.size = Size2Du32(sof.width, sof.height);
        _last.pixelFormat = PixelFormat(pid);
        _last.is420 = is420;
        _last.isRgb = isRgb;

        _cachedWidth = sof.width;
        _cachedHeight = sof.height;
        _cachedNf = sof.nf;
        _cachedYsf = sof.ySf;
        _cachedRfc2435Type = rfc2435Type;
        _cachedFmtp = fmtp;
        _hasCacheKey = true;
        return _last;
}

PROMEKI_NAMESPACE_END
