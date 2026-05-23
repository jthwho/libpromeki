/**
 * @file      st2110video.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/st2110video.h>
#include <promeki/logger.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/colormodel.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#endif

PROMEKI_NAMESPACE_BEGIN

namespace {

// Categorises an @ref St2110Sampling into one of five "shapes" that
// share a pgroup table column.  RGB shares the 4:4:4 triplet shape
// (one sample triplet per pixel, depths 8/10/12/16/16f); XYZ uses
// the same triplet layout but skips depths 8 and 10 per §6.2.3.
// Key is its own shape (single component, depths 8/10/12/16/16f).
enum class SamplingShape {
        Invalid,
        Group444Triplet, // YCbCr444 / CLYCbCr444 / ICtCp444 / RGB
        Group422Pair,    // YCbCr422 / CLYCbCr422 / ICtCp422
        Group420,        // YCbCr420 / CLYCbCr420 / ICtCp420
        GroupXyz,        // XYZ (4:4:4-like, depths 12/16/16f only)
        GroupKey,        // KEY
};

SamplingShape shapeOf(const St2110Sampling &sampling) {
        const int v = sampling.value();
        if (v == St2110Sampling::YCbCr444.value() || v == St2110Sampling::CLYCbCr444.value()
            || v == St2110Sampling::ICtCp444.value() || v == St2110Sampling::Rgb.value()) {
                return SamplingShape::Group444Triplet;
        }
        if (v == St2110Sampling::YCbCr422.value() || v == St2110Sampling::CLYCbCr422.value()
            || v == St2110Sampling::ICtCp422.value()) {
                return SamplingShape::Group422Pair;
        }
        if (v == St2110Sampling::YCbCr420.value() || v == St2110Sampling::CLYCbCr420.value()
            || v == St2110Sampling::ICtCp420.value()) {
                return SamplingShape::Group420;
        }
        if (v == St2110Sampling::Xyz.value()) return SamplingShape::GroupXyz;
        if (v == St2110Sampling::Key.value()) return SamplingShape::GroupKey;
        return SamplingShape::Invalid;
}

} // namespace

int St2110Video::bitsPerSample(const St2110Depth &depth) {
        const int v = depth.value();
        if (v == St2110Depth::Bits8.value()) return 8;
        if (v == St2110Depth::Bits10.value()) return 10;
        if (v == St2110Depth::Bits12.value()) return 12;
        if (v == St2110Depth::Bits16.value()) return 16;
        if (v == St2110Depth::Bits16f.value()) return 16;
        return 0;
}

bool St2110Video::isFloatDepth(const St2110Depth &depth) {
        return depth == St2110Depth::Bits16f;
}

St2110Video::Pgroup St2110Video::pgroup(const St2110Sampling &sampling, const St2110Depth &depth) {
        const SamplingShape shape = shapeOf(sampling);
        if (shape == SamplingShape::Invalid) return Pgroup{};
        const int dv = depth.value();
        const bool is8 = (dv == St2110Depth::Bits8.value());
        const bool is10 = (dv == St2110Depth::Bits10.value());
        const bool is12 = (dv == St2110Depth::Bits12.value());
        const bool is16 = (dv == St2110Depth::Bits16.value());
        const bool is16f = (dv == St2110Depth::Bits16f.value());
        // §6.2 Tables 1-4 — exhaustively enumerated.  Any
        // (shape, depth) combo not listed below stays at the
        // default {0,0,0} which @ref isSupported reports as
        // unsupported.
        switch (shape) {
                case SamplingShape::Group444Triplet:
                        if (is8) return {3, 1, 3};
                        if (is10) return {15, 4, 12};
                        if (is12) return {9, 2, 6};
                        if (is16 || is16f) return {6, 1, 3};
                        break;
                case SamplingShape::Group422Pair:
                        if (is8) return {4, 2, 4};
                        if (is10) return {5, 2, 4};
                        if (is12) return {6, 2, 4};
                        if (is16 || is16f) return {8, 2, 4};
                        break;
                case SamplingShape::Group420:
                        // §6.2.5 — 4:2:0 listed only at depths
                        // 8/10/12.  16 and 16f are deliberately
                        // absent.
                        if (is8) return {6, 4, 6};
                        if (is10) return {15, 8, 12};
                        if (is12) return {9, 4, 6};
                        break;
                case SamplingShape::GroupXyz:
                        // §6.2.3 — XYZ listed only at depths
                        // 12/16/16f.  8 and 10 are deliberately
                        // absent.
                        if (is12) return {9, 2, 6};
                        if (is16 || is16f) return {6, 1, 3};
                        break;
                case SamplingShape::GroupKey:
                        if (is8) return {1, 1, 1};
                        if (is10) return {5, 4, 4};
                        if (is12) return {3, 2, 2};
                        if (is16 || is16f) return {2, 1, 1};
                        break;
                default:
                        break;
        }
        return Pgroup{};
}

bool St2110Video::isSupported(const St2110Sampling &sampling, const St2110Depth &depth) {
        return pgroup(sampling, depth).octets > 0;
}

size_t St2110Video::packSamplesBE(uint8_t *dst, size_t dstCap,
                                  const uint16_t *samples, size_t nSamples,
                                  int depthBits) {
        if (dst == nullptr || samples == nullptr) return 0;
        if (depthBits < 1 || depthBits > 16) return 0;
        if (nSamples == 0) return 0;
        const size_t totalBits = nSamples * static_cast<size_t>(depthBits);
        const size_t needBytes = (totalBits + 7) / 8;
        if (needBytes > dstCap) return 0;

        // §6.1.1 — big-endian on the wire.  Byte-aligned depths get
        // a tight loop; the general path packs 10/12-bit samples
        // through a bit-stream accumulator that keeps the MSB-first
        // sample order required by §6.2.
        if (depthBits == 8) {
                for (size_t i = 0; i < nSamples; i++) {
                        dst[i] = static_cast<uint8_t>(samples[i] & 0xFFu);
                }
                return nSamples;
        }
        if (depthBits == 16) {
                for (size_t i = 0; i < nSamples; i++) {
                        dst[2u * i + 0u] = static_cast<uint8_t>((samples[i] >> 8) & 0xFFu);
                        dst[2u * i + 1u] = static_cast<uint8_t>(samples[i] & 0xFFu);
                }
                return 2u * nSamples;
        }

        uint64_t       accum = 0;
        int            accumBits = 0;
        size_t         out = 0;
        const uint32_t mask = (1u << depthBits) - 1u;
        for (size_t i = 0; i < nSamples; i++) {
                accum = (accum << depthBits) | (static_cast<uint32_t>(samples[i]) & mask);
                accumBits += depthBits;
                while (accumBits >= 8) {
                        accumBits -= 8;
                        dst[out++] = static_cast<uint8_t>((accum >> accumBits) & 0xFFu);
                }
        }
        if (accumBits > 0) {
                // §6.2.1 — "the sender shall fill the remaining
                // sample positions of the final pgroup with zero".
                // Shift the trailing bits to the MSB side of the
                // final octet; the low bits stay zero.
                dst[out++] = static_cast<uint8_t>((accum << (8 - accumBits)) & 0xFFu);
        }
        return out;
}

size_t St2110Video::unpackSamplesBE(const uint8_t *src, size_t srcSize,
                                    uint16_t *samples, size_t nSamples,
                                    int depthBits) {
        if (src == nullptr || samples == nullptr) return 0;
        if (depthBits < 1 || depthBits > 16) return 0;
        if (nSamples == 0) return 0;
        const size_t totalBits = nSamples * static_cast<size_t>(depthBits);
        const size_t needBytes = (totalBits + 7) / 8;
        if (needBytes > srcSize) return 0;

        if (depthBits == 8) {
                for (size_t i = 0; i < nSamples; i++) samples[i] = src[i];
                return nSamples;
        }
        if (depthBits == 16) {
                for (size_t i = 0; i < nSamples; i++) {
                        samples[i] = static_cast<uint16_t>((static_cast<uint16_t>(src[2u * i]) << 8)
                                                           | src[2u * i + 1u]);
                }
                return 2u * nSamples;
        }

        uint64_t       accum = 0;
        int            accumBits = 0;
        size_t         in = 0;
        const uint32_t mask = (1u << depthBits) - 1u;
        for (size_t i = 0; i < nSamples; i++) {
                while (accumBits < depthBits) {
                        accum = (accum << 8) | static_cast<uint64_t>(src[in++]);
                        accumBits += 8;
                }
                accumBits -= depthBits;
                samples[i] = static_cast<uint16_t>((accum >> accumBits) & mask);
        }
        return in;
}

size_t St2110Video::rowOctets(const St2110Sampling &sampling, const St2110Depth &depth, size_t nPixels) {
        const Pgroup pg = pgroup(sampling, depth);
        if (pg.octets == 0 || pg.pixels == 0) return 0;
        if ((nPixels % pg.pixels) != 0) return 0;
        return pg.octets * (nPixels / pg.pixels);
}

size_t St2110Video::rowSamples(const St2110Sampling &sampling, const St2110Depth &depth, size_t nPixels) {
        const Pgroup pg = pgroup(sampling, depth);
        if (pg.octets == 0 || pg.pixels == 0) return 0;
        if ((nPixels % pg.pixels) != 0) return 0;
        return pg.samples * (nPixels / pg.pixels);
}

size_t St2110Video::packRow(const St2110Sampling &sampling, const St2110Depth &depth,
                            size_t nPixels, const uint16_t *samples,
                            uint8_t *dst, size_t dstCap) {
        const Pgroup pg = pgroup(sampling, depth);
        if (pg.octets == 0 || pg.pixels == 0) return 0;
        if ((nPixels % pg.pixels) != 0) return 0;
        const size_t nGroups = nPixels / pg.pixels;
        const size_t nSamples = pg.samples * nGroups;
        return packSamplesBE(dst, dstCap, samples, nSamples, bitsPerSample(depth));
}

size_t St2110Video::unpackRow(const St2110Sampling &sampling, const St2110Depth &depth,
                              size_t nPixels, const uint8_t *src, size_t srcSize,
                              uint16_t *samples) {
        const Pgroup pg = pgroup(sampling, depth);
        if (pg.octets == 0 || pg.pixels == 0) return 0;
        if ((nPixels % pg.pixels) != 0) return 0;
        const size_t nGroups = nPixels / pg.pixels;
        const size_t nSamples = pg.samples * nGroups;
        return unpackSamplesBE(src, srcSize, samples, nSamples, bitsPerSample(depth));
}

// ============================================================================
// Wire form ↔ project form for the §7 fmtp parameter values.
// ============================================================================

String St2110Video::samplingWire(const St2110Sampling &sampling) {
        const int v = sampling.value();
        if (v == St2110Sampling::YCbCr444.value()) return String("YCbCr-4:4:4");
        if (v == St2110Sampling::YCbCr422.value()) return String("YCbCr-4:2:2");
        if (v == St2110Sampling::YCbCr420.value()) return String("YCbCr-4:2:0");
        if (v == St2110Sampling::CLYCbCr444.value()) return String("CLYCbCr-4:4:4");
        if (v == St2110Sampling::CLYCbCr422.value()) return String("CLYCbCr-4:2:2");
        if (v == St2110Sampling::CLYCbCr420.value()) return String("CLYCbCr-4:2:0");
        if (v == St2110Sampling::ICtCp444.value()) return String("ICtCp-4:4:4");
        if (v == St2110Sampling::ICtCp422.value()) return String("ICtCp-4:2:2");
        if (v == St2110Sampling::ICtCp420.value()) return String("ICtCp-4:2:0");
        if (v == St2110Sampling::Rgb.value()) return String("RGB");
        if (v == St2110Sampling::Xyz.value()) return String("XYZ");
        if (v == St2110Sampling::Key.value()) return String("KEY");
        return String();
}

St2110Sampling St2110Video::samplingFromWire(const String &s) {
        if (s == "YCbCr-4:4:4") return St2110Sampling::YCbCr444;
        if (s == "YCbCr-4:2:2") return St2110Sampling::YCbCr422;
        if (s == "YCbCr-4:2:0") return St2110Sampling::YCbCr420;
        if (s == "CLYCbCr-4:4:4") return St2110Sampling::CLYCbCr444;
        if (s == "CLYCbCr-4:2:2") return St2110Sampling::CLYCbCr422;
        if (s == "CLYCbCr-4:2:0") return St2110Sampling::CLYCbCr420;
        if (s == "ICtCp-4:4:4") return St2110Sampling::ICtCp444;
        if (s == "ICtCp-4:2:2") return St2110Sampling::ICtCp422;
        if (s == "ICtCp-4:2:0") return St2110Sampling::ICtCp420;
        if (s == "RGB") return St2110Sampling::Rgb;
        if (s == "XYZ") return St2110Sampling::Xyz;
        if (s == "KEY") return St2110Sampling::Key;
        return St2110Sampling::Invalid;
}

String St2110Video::depthWire(const St2110Depth &depth) {
        const int v = depth.value();
        if (v == St2110Depth::Bits8.value()) return String("8");
        if (v == St2110Depth::Bits10.value()) return String("10");
        if (v == St2110Depth::Bits12.value()) return String("12");
        if (v == St2110Depth::Bits16.value()) return String("16");
        if (v == St2110Depth::Bits16f.value()) return String("16f");
        return String();
}

St2110Depth St2110Video::depthFromWire(const String &s) {
        if (s == "8") return St2110Depth::Bits8;
        if (s == "10") return St2110Depth::Bits10;
        if (s == "12") return St2110Depth::Bits12;
        if (s == "16") return St2110Depth::Bits16;
        if (s == "16f") return St2110Depth::Bits16f;
        return St2110Depth::Invalid;
}

String St2110Video::colorimetryWire(const St2110Colorimetry &c) {
        const int v = c.value();
        if (v == St2110Colorimetry::Bt601.value()) return String("BT601");
        if (v == St2110Colorimetry::Bt709.value()) return String("BT709");
        if (v == St2110Colorimetry::Bt2020.value()) return String("BT2020");
        if (v == St2110Colorimetry::Bt2100.value()) return String("BT2100");
        if (v == St2110Colorimetry::St2065_1.value()) return String("ST2065-1");
        if (v == St2110Colorimetry::St2065_3.value()) return String("ST2065-3");
        if (v == St2110Colorimetry::Unspecified.value()) return String("UNSPECIFIED");
        if (v == St2110Colorimetry::Xyz.value()) return String("XYZ");
        if (v == St2110Colorimetry::Alpha.value()) return String("ALPHA");
        return String();
}

St2110Colorimetry St2110Video::colorimetryFromWire(const String &s) {
        if (s == "BT601") return St2110Colorimetry::Bt601;
        if (s == "BT709") return St2110Colorimetry::Bt709;
        if (s == "BT2020") return St2110Colorimetry::Bt2020;
        if (s == "BT2100") return St2110Colorimetry::Bt2100;
        if (s == "ST2065-1") return St2110Colorimetry::St2065_1;
        if (s == "ST2065-3") return St2110Colorimetry::St2065_3;
        if (s == "UNSPECIFIED") return St2110Colorimetry::Unspecified;
        if (s == "XYZ") return St2110Colorimetry::Xyz;
        if (s == "ALPHA") return St2110Colorimetry::Alpha;
        return St2110Colorimetry::Invalid;
}

String St2110Video::tcsWire(const St2110Tcs &t) {
        const int v = t.value();
        if (v == St2110Tcs::Sdr.value()) return String("SDR");
        if (v == St2110Tcs::Pq.value()) return String("PQ");
        if (v == St2110Tcs::Hlg.value()) return String("HLG");
        if (v == St2110Tcs::Linear.value()) return String("LINEAR");
        if (v == St2110Tcs::Bt2100LinPq.value()) return String("BT2100LINPQ");
        if (v == St2110Tcs::Bt2100LinHlg.value()) return String("BT2100LINHLG");
        if (v == St2110Tcs::St2065_1.value()) return String("ST2065-1");
        if (v == St2110Tcs::St428_1.value()) return String("ST428-1");
        if (v == St2110Tcs::Density.value()) return String("DENSITY");
        if (v == St2110Tcs::St2115LogS3.value()) return String("ST2115LOGS3");
        if (v == St2110Tcs::Unspecified.value()) return String("UNSPECIFIED");
        return String();
}

St2110Tcs St2110Video::tcsFromWire(const String &s) {
        if (s == "SDR") return St2110Tcs::Sdr;
        if (s == "PQ") return St2110Tcs::Pq;
        if (s == "HLG") return St2110Tcs::Hlg;
        if (s == "LINEAR") return St2110Tcs::Linear;
        if (s == "BT2100LINPQ") return St2110Tcs::Bt2100LinPq;
        if (s == "BT2100LINHLG") return St2110Tcs::Bt2100LinHlg;
        if (s == "ST2065-1") return St2110Tcs::St2065_1;
        if (s == "ST428-1") return St2110Tcs::St428_1;
        if (s == "DENSITY") return St2110Tcs::Density;
        if (s == "ST2115LOGS3") return St2110Tcs::St2115LogS3;
        if (s == "UNSPECIFIED") return St2110Tcs::Unspecified;
        return St2110Tcs::Invalid;
}

String St2110Video::rangeWire(const St2110Range &r) {
        const int v = r.value();
        if (v == St2110Range::Narrow.value()) return String("NARROW");
        if (v == St2110Range::Full.value()) return String("FULL");
        if (v == St2110Range::FullProtect.value()) return String("FULLPROTECT");
        return String();
}

St2110Range St2110Video::rangeFromWire(const String &s) {
        if (s == "NARROW") return St2110Range::Narrow;
        if (s == "FULL") return St2110Range::Full;
        if (s == "FULLPROTECT") return St2110Range::FullProtect;
        return St2110Range::Invalid;
}

String St2110Video::packingModeWire(const St2110PackingMode &p) {
        const int v = p.value();
        if (v == St2110PackingMode::Gpm.value()) return String("2110GPM");
        if (v == St2110PackingMode::Bpm.value()) return String("2110BPM");
        return String();
}

St2110PackingMode St2110Video::packingModeFromWire(const String &s) {
        if (s == "2110GPM") return St2110PackingMode::Gpm;
        if (s == "2110BPM") return St2110PackingMode::Bpm;
        return St2110PackingMode::Invalid;
}

String St2110Video::ssnFor(const St2110Colorimetry &colorimetry, const St2110Tcs &tcs) {
        // §7.2: "Senders implementing this standard shall signal the
        // value ST2110-20:2017 unless the colorimetry value ALPHA or
        // the TCS value ST2115LOGS3 are used, in which case the value
        // ST2110-20:2022 shall be signaled."
        const bool needs2022 = (colorimetry == St2110Colorimetry::Alpha)
                            || (tcs == St2110Tcs::St2115LogS3);
        return needs2022 ? String("ST2110-20:2022") : String("ST2110-20:2017");
}

// ============================================================================
// §7 a=fmtp body builder / parser.
// ============================================================================

String St2110Video::frameRateToWire(const FrameRate &fr) {
        if (!fr.isValid()) return String();
        if (fr.denominator() == 1u) {
                return String::number(static_cast<int64_t>(fr.numerator()));
        }
        return String::number(static_cast<int64_t>(fr.numerator())) + String("/")
               + String::number(static_cast<int64_t>(fr.denominator()));
}

String St2110Video::toFmtp(const Fmtp &fmtp) {
        if (samplingWire(fmtp.sampling).isEmpty()) return String();
        if (depthWire(fmtp.depth).isEmpty()) return String();
        if (colorimetryWire(fmtp.colorimetry).isEmpty()) return String();
        if (fmtp.width == 0 || fmtp.height == 0) return String();
        if (!fmtp.exactFrameRate.isValid()) return String();

        // §7.1: parameters separated by "; " (semicolon + whitespace).
        // We emit required parameters first in canonical order, then
        // default-valued parameters only when they differ from the
        // §7.3 defaults.
        String body;
        const auto append = [&](const String &kv) {
                if (kv.isEmpty()) return;
                if (!body.isEmpty()) body += String("; ");
                body += kv;
        };

        // §7.2 — Required Media Type Parameters.
        append(String("sampling=") + samplingWire(fmtp.sampling));
        append(String("depth=") + depthWire(fmtp.depth));
        append(String("width=") + String::number(static_cast<int64_t>(fmtp.width)));
        append(String("height=") + String::number(static_cast<int64_t>(fmtp.height)));
        append(String("exactframerate=") + frameRateToWire(fmtp.exactFrameRate));
        append(String("colorimetry=") + colorimetryWire(fmtp.colorimetry));
        append(String("PM=") + packingModeWire(fmtp.pm));
        // §7.2 SSN: derived unless the caller explicitly overrode.
        const String ssn = fmtp.ssnOverride.isEmpty() ? ssnFor(fmtp.colorimetry, fmtp.tcs) : fmtp.ssnOverride;
        append(String("SSN=") + ssn);

        // §7.3 — Default-valued parameters; emit only when non-default.
        if (fmtp.interlace) append(String("interlace"));
        if (fmtp.segmented) append(String("segmented"));
        if (fmtp.tcs != St2110Tcs::Sdr && !tcsWire(fmtp.tcs).isEmpty()) {
                append(String("TCS=") + tcsWire(fmtp.tcs));
        }
        if (fmtp.range != St2110Range::Narrow && !rangeWire(fmtp.range).isEmpty()) {
                append(String("RANGE=") + rangeWire(fmtp.range));
        }
        if (fmtp.maxUdp > 0) append(String("MAXUDP=") + String::number(static_cast<int64_t>(fmtp.maxUdp)));
        if (fmtp.par.isValid() && !fmtp.par.isSquare()) {
                append(String("PAR=") + fmtp.par.toString());
        }

        return body;
}

St2110Video::Fmtp St2110Video::fromFmtp(const Map<String, String> &params) {
        Fmtp out;
        auto lookup = [&](const String &k) -> String {
                auto it = params.find(k);
                return (it != params.end()) ? it->second : String();
        };

        const String sampling = lookup("sampling");
        if (!sampling.isEmpty()) out.sampling = samplingFromWire(sampling);

        const String depth = lookup("depth");
        if (!depth.isEmpty()) out.depth = depthFromWire(depth);

        const String width = lookup("width");
        if (!width.isEmpty()) out.width = static_cast<uint32_t>(width.toInt());

        const String height = lookup("height");
        if (!height.isEmpty()) out.height = static_cast<uint32_t>(height.toInt());

        const String fps = lookup("exactframerate");
        if (!fps.isEmpty()) {
                // §7.2: integer frame rates as a bare decimal; non-integer
                // as "num/den".  @ref FrameRate::fromString accepts both
                // forms but is stricter about the well-known table — feed
                // the wire string through directly so any integer rate
                // round-trips ("25" → 25/1).  We synthesise the "N/1"
                // form for the integer case because FrameRate::fromString
                // expects either a well-known name or a "/"-separated
                // fraction.
                const Result<FrameRate> fr = (fps.find('/') == String::npos)
                                                     ? FrameRate::fromString(fps + String("/1"))
                                                     : FrameRate::fromString(fps);
                if (isOk(fr)) out.exactFrameRate = value(fr);
        }

        const String colorimetry = lookup("colorimetry");
        if (!colorimetry.isEmpty()) out.colorimetry = colorimetryFromWire(colorimetry);

        const String pm = lookup("PM");
        if (!pm.isEmpty()) out.pm = packingModeFromWire(pm);

        const String ssn = lookup("SSN");
        if (!ssn.isEmpty()) out.ssnOverride = ssn;

        // §7.3 — presence-only and defaulted parameters.
        if (params.find("interlace") != params.end()) out.interlace = true;
        if (params.find("segmented") != params.end()) out.segmented = true;
        // §7.3: `segmented` is only meaningful as a sub-flag of
        // `interlace` (interlace + segmented = PsF).  A bare
        // `segmented` is invalid.  Clear it with a one-shot warning so
        // the rest of the parse stays usable for permissive interop.
        if (out.segmented && !out.interlace) {
                promekiWarnOnce("St2110Video::fromFmtp: `segmented` without `interlace` is forbidden (§7.3); "
                                "clearing `segmented`");
                out.segmented = false;
        }
        // §6.2.5: 4:2:0 sampling is progressive-only.  Reject the
        // interlace/segmented flags when combined with 4:2:0.
        if (out.sampling == St2110Sampling::YCbCr420 && (out.interlace || out.segmented)) {
                promekiWarnOnce("St2110Video::fromFmtp: 4:2:0 sampling is progressive-only per §6.2.5; "
                                "clearing `interlace` / `segmented`");
                out.interlace = false;
                out.segmented = false;
        }

        const String tcs = lookup("TCS");
        if (!tcs.isEmpty()) out.tcs = tcsFromWire(tcs);

        const String range = lookup("RANGE");
        if (!range.isEmpty()) out.range = rangeFromWire(range);

        const String maxUdp = lookup("MAXUDP");
        if (!maxUdp.isEmpty()) out.maxUdp = static_cast<uint32_t>(maxUdp.toInt());

        const String par = lookup("PAR");
        if (!par.isEmpty()) {
                const Result<PixelAspect> pa = PixelAspect::fromString(par);
                if (isOk(pa)) out.par = value(pa);
        }

        return out;
}

VideoScanMode St2110Video::fmtpScanMode(const Fmtp &fmtp) {
        if (fmtp.interlace && fmtp.segmented) return VideoScanMode::PsF;
        if (fmtp.interlace) return VideoScanMode::Interlaced;
        return VideoScanMode::Progressive;
}

void St2110Video::setFmtpScanMode(Fmtp &fmtp, VideoScanMode mode) {
        const int v = mode.value();
        if (v == VideoScanMode::PsF.value()) {
                fmtp.interlace = true;
                fmtp.segmented = true;
        } else if (mode.isInterlaced()) {
                fmtp.interlace = true;
                fmtp.segmented = false;
        } else {
                // Progressive / Unknown both serialise as "no flags".
                fmtp.interlace = false;
                fmtp.segmented = false;
        }
}

#if PROMEKI_ENABLE_PROAV

// Map a ColorModel to the ST 2110-20 (colorimetry, tcs) pair it most
// directly signals.  ColorModel encodes both colorimetry and transfer
// characteristics, so this is the single place where the project's
// colour model identity flows into wire-format colorimetry + tcs.
// Returns (Invalid, Invalid) for ColorModels with no ST 2110-20
// counterpart.
static void colorModelToSt2110(const ColorModel &cm, St2110Colorimetry *colorimetry, St2110Tcs *tcs) {
        *tcs = St2110Tcs::Sdr;
        switch (cm.id()) {
                case ColorModel::YCbCr_Rec601:
                case ColorModel::Rec601_PAL:
                case ColorModel::Rec601_NTSC:
                        *colorimetry = St2110Colorimetry::Bt601;
                        return;
                case ColorModel::YCbCr_Rec709:
                case ColorModel::Rec709:
                case ColorModel::sRGB:
                        *colorimetry = St2110Colorimetry::Bt709;
                        return;
                case ColorModel::Rec2020:
                case ColorModel::YCbCr_Rec2020:
                        *colorimetry = St2110Colorimetry::Bt2020;
                        return;
                case ColorModel::CIEXYZ:
                        // §7.5 — XYZ samples carry the XYZ colorimetry
                        // tag.  TCS for XYZ-tagged streams is Linear or
                        // ST428-1 per §7.6; the project ColorModel
                        // doesn't yet distinguish, so leave at Sdr and
                        // let an explicit @ref MediaConfig::RtpVideoTcs
                        // override declare it.
                        *colorimetry = St2110Colorimetry::Xyz;
                        return;
                default: break;
        }
        *colorimetry = St2110Colorimetry::Invalid;
}

St2110Video::PixelFormatBridge St2110Video::bridgeForPixelFormat(const PixelFormat &pd) {
        PixelFormatBridge br;
        if (!pd.isValid() || pd.isCompressed()) return br;

        const PixelMemLayout &pf = pd.memLayout();
        const ColorModel     &cm = pd.colorModel();

        // Colour space + transfer characteristic → §7.5 colorimetry +
        // §7.6 TCS.  Fall out for any ColorModel without an ST 2110-20
        // counterpart.
        colorModelToSt2110(cm, &br.colorimetry, &br.tcs);
        if (!br.colorimetry.isValid()) return br;

        // Sampling — RGB vs YCbCr vs XYZ vs Key.  The chroma ratio is
        // on the PixelMemLayout; the ColorModel and component count
        // distinguish RGB, XYZ, and KEY (single-component intensity).
        const bool isRgb = (cm.type() == ColorModel::TypeRGB);
        const bool isXyz = (cm.id() == ColorModel::CIEXYZ);
        if (isXyz) {
                // §7.4.1 lists XYZ as a discrete sampling label.
                br.sampling = St2110Sampling::Xyz;
        } else if (pf.compCount() == 1) {
                // Single-component formats map to KEY (§7.4.1 KEY +
                // §7.5 ALPHA colorimetry).  Overrides any prior RGB/
                // YCbCr inference.
                br.sampling = St2110Sampling::Key;
                br.colorimetry = St2110Colorimetry::Alpha;
        } else if (isRgb) {
                br.sampling = St2110Sampling::Rgb;
        } else {
                switch (pf.sampling()) {
                        case PixelMemLayout::Sampling444: br.sampling = St2110Sampling::YCbCr444; break;
                        case PixelMemLayout::Sampling422: br.sampling = St2110Sampling::YCbCr422; break;
                        case PixelMemLayout::Sampling420: br.sampling = St2110Sampling::YCbCr420; break;
                        // §7.4.1 does not list 4:1:1; the PixelFormat
                        // is then non-2110-conformant and we report
                        // sampling=Invalid so the caller can fall back
                        // to a non-2110 SDP path (or reject the stream).
                        default: return br;
                }
        }

        // Depth — read the first component's bit width.  Float (F16)
        // layouts surface as a 16-bit width with the isFloat flag set
        // on the PixelMemLayout's component name — the project's
        // float layouts are F16 / F32; ST 2110-20 §7.4.2 only lists
        // 16f for float depths so any other float width drops to
        // Invalid here.
        const int bits = (pf.compCount() > 0) ? static_cast<int>(pf.compDesc(0).bits) : 0;
        const String lname = pf.name();
        const bool   isFloat = lname.contains("F16") || lname.contains("F32");
        if (isFloat) {
                if (bits == 16) br.depth = St2110Depth::Bits16f;
                else br.depth = St2110Depth::Invalid;
        } else {
                switch (bits) {
                        case 8:  br.depth = St2110Depth::Bits8; break;
                        case 10: br.depth = St2110Depth::Bits10; break;
                        case 12: br.depth = St2110Depth::Bits12; break;
                        case 16: br.depth = St2110Depth::Bits16; break;
                        default: br.depth = St2110Depth::Invalid; break;
                }
        }

        // Range — RGB is always FULL on ST 2110-20 unless the
        // PixelFormat carries a non-full luma floor; YCbCr defaults
        // to NARROW unless the first component's range starts at 0.
        const PixelFormat::CompSemantic &cs = pd.compSemantic(0);
        if (isRgb) {
                br.range = St2110Range::Full;
        } else {
                br.range = (cs.rangeMin > 0.0f) ? St2110Range::Narrow : St2110Range::Full;
        }

        // Tcs — derive from the ColorModel for the HDR-eligible
        // ColorModels.  For SDR ColorModels (Rec.709 / Rec.601 /
        // sRGB / Rec.2020 SDR) the colorModelToSt2110 helper above
        // already set tcs to Sdr.  HDR ColorModels would override
        // here — currently none of the registered ColorModels carry
        // a PQ/HLG identity directly, so Sdr is the only outcome
        // until [[project_color_refactor]] surfaces HDR ColorModels.

        // Reject combos the standard does not define (e.g. 4:2:0
        // depth=16, XYZ depth=8) before returning.
        if (!isSupported(br.sampling, br.depth)) {
                br.sampling = St2110Sampling::Invalid;
                br.depth = St2110Depth::Invalid;
        }
        return br;
}

PixelFormat St2110Video::wirePixelFormatFor(const St2110Sampling   &sampling,
                                            const St2110Depth      &depth,
                                            const St2110Colorimetry &colorimetry,
                                            const St2110Range       &range) {
        if (!isSupported(sampling, depth)) return PixelFormat(PixelFormat::Invalid);

        const int samp = sampling.value();
        const int dv = depth.value();
        const int color = colorimetry.value();
        // Default range: Full for RGB / XYZ / Key, Narrow for YCbCr.
        const bool rangeFull = range.isValid()
                                       ? (range == St2110Range::Full)
                                       : (samp == St2110Sampling::Rgb.value() || samp == St2110Sampling::Xyz.value() ||
                                          samp == St2110Sampling::Key.value());
        (void)rangeFull; // c-2 only registers limited-range YCbCr / full-range RGB; range routing lands with c-3.

        // KEY — single-component intensity.  KEY uses Colorimetry=Alpha
        // per §7.5 (the bridge maps that automatically); here we just
        // pick the layout-matching wire-format PixelFormat.
        if (samp == St2110Sampling::Key.value()) {
                if (dv == St2110Depth::Bits8.value()) return PixelFormat(PixelFormat::Mono8_sRGB);
                if (dv == St2110Depth::Bits10.value()) return PixelFormat(PixelFormat::Mono10_BE_2110_sRGB);
                if (dv == St2110Depth::Bits12.value()) return PixelFormat(PixelFormat::Mono12_BE_2110_sRGB);
                if (dv == St2110Depth::Bits16.value()) return PixelFormat(PixelFormat::Mono16_BE_sRGB);
                if (dv == St2110Depth::Bits16f.value()) return PixelFormat(PixelFormat::MonoF16_BE_LinearRec709);
                return PixelFormat(PixelFormat::Invalid);
        }

        // XYZ — cinema, §6.2.3 depths 12 / 16 / 16f only.  The standard
        // pairs XYZ samples with the XYZ colorimetry tag.
        if (samp == St2110Sampling::Xyz.value()) {
                if (dv == St2110Depth::Bits12.value()) return PixelFormat(PixelFormat::XYZ12_BE_2110);
                if (dv == St2110Depth::Bits16.value() || dv == St2110Depth::Bits16f.value())
                        return PixelFormat(PixelFormat::XYZ16_BE_2110);
                return PixelFormat(PixelFormat::Invalid);
        }

        // RGB — colorimetry routing.  Only sRGB / Rec.709 wired in c-2;
        // Rec.2020 + HDR variants land in c-3 alongside their kernels.
        if (samp == St2110Sampling::Rgb.value()) {
                if (dv == St2110Depth::Bits8.value()) return PixelFormat(PixelFormat::RGB8_sRGB);
                if (dv == St2110Depth::Bits10.value()) return PixelFormat(PixelFormat::RGB10_BE_2110_sRGB);
                if (dv == St2110Depth::Bits12.value()) return PixelFormat(PixelFormat::RGB12_BE_2110_sRGB);
                if (dv == St2110Depth::Bits16.value()) return PixelFormat(PixelFormat::RGB16_BE_sRGB);
                if (dv == St2110Depth::Bits16f.value()) return PixelFormat(PixelFormat::RGBF16_BE_LinearRec709);
                return PixelFormat(PixelFormat::Invalid);
        }

        // YCbCr — colorimetry routes between Rec.601 / 709 / 2020.
        // c-2 only registers the Rec.709 line; the others land in c-3.
        const bool isRec709 = (color == St2110Colorimetry::Bt709.value() || !colorimetry.isValid());
        if (!isRec709) return PixelFormat(PixelFormat::Invalid);

        if (samp == St2110Sampling::YCbCr444.value()) {
                if (dv == St2110Depth::Bits8.value()) return PixelFormat(PixelFormat::YUV8_Rec709);
                if (dv == St2110Depth::Bits10.value()) return PixelFormat(PixelFormat::YUV10_2110_Rec709);
                if (dv == St2110Depth::Bits12.value()) return PixelFormat(PixelFormat::YUV12_2110_Rec709);
                if (dv == St2110Depth::Bits16.value()) return PixelFormat(PixelFormat::YUV16_BE_Rec709);
                return PixelFormat(PixelFormat::Invalid);
        }
        if (samp == St2110Sampling::YCbCr422.value()) {
                if (dv == St2110Depth::Bits8.value()) return PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709);
                if (dv == St2110Depth::Bits10.value()) return PixelFormat(PixelFormat::YUV10_422_2110_Rec709);
                if (dv == St2110Depth::Bits12.value()) return PixelFormat(PixelFormat::YUV12_422_2110_Rec709);
                if (dv == St2110Depth::Bits16.value()) return PixelFormat(PixelFormat::YUV16_422_UYVY_BE_Rec709);
                return PixelFormat(PixelFormat::Invalid);
        }
        if (samp == St2110Sampling::YCbCr420.value()) {
                // §6.2.5 — 4:2:0 only at depths 8 / 10 / 12.  The wire
                // format is single-plane pgroup-interleaved (NOT NV12);
                // see PixelMemLayout::I_420_BE_2110_8/10/12.
                if (dv == St2110Depth::Bits8.value()) return PixelFormat(PixelFormat::YUV8_420_2110_Rec709);
                if (dv == St2110Depth::Bits10.value()) return PixelFormat(PixelFormat::YUV10_420_2110_Rec709);
                if (dv == St2110Depth::Bits12.value()) return PixelFormat(PixelFormat::YUV12_420_2110_Rec709);
                return PixelFormat(PixelFormat::Invalid);
        }
        return PixelFormat(PixelFormat::Invalid);
}

#endif // PROMEKI_ENABLE_PROAV

PROMEKI_NAMESPACE_END
