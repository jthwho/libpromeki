/**
 * @file      ntv2format.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/ntv2format.h>

#include <ntv2enums.h>

PROMEKI_NAMESPACE_BEGIN

namespace Ntv2Format {

// ============================================================================
// Pixel format
// ============================================================================

int toNtv2PixelFormat(PixelFormat::ID id) {
        switch (id) {
                case PixelFormat::YUV8_422_UYVY_Rec709:           return NTV2_FBF_8BIT_YCBCR;
                case PixelFormat::YUV8_422_Rec709:                return NTV2_FBF_8BIT_YCBCR_YUY2;
                case PixelFormat::RGB8_sRGB:                      return NTV2_FBF_24BIT_RGB;
                case PixelFormat::BGR8_sRGB:                      return NTV2_FBF_24BIT_BGR;
                case PixelFormat::ARGB8_sRGB:                     return NTV2_FBF_ARGB;
                case PixelFormat::ABGR8_sRGB:                     return NTV2_FBF_ABGR;
                case PixelFormat::RGBA8_sRGB:                     return NTV2_FBF_RGBA;
                // 10-bit RGB / 48-bit RGB / 10-bit YCbCr (V210) map to
                // NTV2_FBF_10BIT_RGB, NTV2_FBF_48BIT_RGB,
                // NTV2_FBF_10BIT_YCBCR respectively.  Phase 5 of the
                // NTV2 devplan promotes them once the endianness +
                // V210 first-class PixelFormat questions are resolved.
                default:                                          return NTV2_FBF_INVALID;
        }
}

PixelFormat::ID fromNtv2PixelFormat(int fbf) {
        switch (fbf) {
                case NTV2_FBF_8BIT_YCBCR:                         return PixelFormat::YUV8_422_UYVY_Rec709;
                case NTV2_FBF_8BIT_YCBCR_YUY2:                    return PixelFormat::YUV8_422_Rec709;
                case NTV2_FBF_24BIT_RGB:                          return PixelFormat::RGB8_sRGB;
                case NTV2_FBF_24BIT_BGR:                          return PixelFormat::BGR8_sRGB;
                case NTV2_FBF_ARGB:                               return PixelFormat::ARGB8_sRGB;
                case NTV2_FBF_ABGR:                               return PixelFormat::ABGR8_sRGB;
                case NTV2_FBF_RGBA:                               return PixelFormat::RGBA8_sRGB;
                default:                                          return PixelFormat::Invalid;
        }
}

// ============================================================================
// Video format
// ============================================================================

namespace {

        // Common broadcast format coverage for Phase 1.  Each row is
        // matched on (width, height, scan-is-interlaced, rate
        // numerator/denominator).  4K / 8K / 2K-DCI rasters land in
        // Phase 5; we report NTV2_FORMAT_UNKNOWN for them today so the
        // open path fails fast with a clear error.
        struct VideoFormatRow {
                        uint32_t width;
                        uint32_t height;
                        bool     interlaced;
                        unsigned int num;
                        unsigned int den;
                        int      ntv2Format;
        };

        constexpr VideoFormatRow kVideoFormatTable[] = {
                // ---- HD 1080p ----
                {1920, 1080, false, 60000, 1001, NTV2_FORMAT_1080p_5994_A},
                {1920, 1080, false, 60,    1,    NTV2_FORMAT_1080p_6000_A},
                {1920, 1080, false, 50,    1,    NTV2_FORMAT_1080p_5000_A},
                {1920, 1080, false, 30000, 1001, NTV2_FORMAT_1080p_2997},
                {1920, 1080, false, 30,    1,    NTV2_FORMAT_1080p_3000},
                {1920, 1080, false, 25,    1,    NTV2_FORMAT_1080p_2500},
                {1920, 1080, false, 24000, 1001, NTV2_FORMAT_1080p_2398},
                {1920, 1080, false, 24,    1,    NTV2_FORMAT_1080p_2400},
                // ---- HD 1080i ----
                {1920, 1080, true,  30000, 1001, NTV2_FORMAT_1080i_5994},
                {1920, 1080, true,  30,    1,    NTV2_FORMAT_1080i_6000},
                {1920, 1080, true,  25,    1,    NTV2_FORMAT_1080i_5000},
                // ---- HD 720p ----
                {1280, 720,  false, 60000, 1001, NTV2_FORMAT_720p_5994},
                {1280, 720,  false, 60,    1,    NTV2_FORMAT_720p_6000},
                {1280, 720,  false, 50,    1,    NTV2_FORMAT_720p_5000},
                {1280, 720,  false, 24000, 1001, NTV2_FORMAT_720p_2398},
                {1280, 720,  false, 25,    1,    NTV2_FORMAT_720p_2500},
                // ---- SD ----
                {720,  486,  true,  30000, 1001, NTV2_FORMAT_525_5994},
                {720,  576,  true,  25,    1,    NTV2_FORMAT_625_5000},
        };

} // namespace

int toNtv2VideoFormat(const ImageDesc &image, const FrameRate &rate) {
        if (!image.size().isValid() || !rate.isValid()) return NTV2_FORMAT_UNKNOWN;

        const uint32_t          w        = image.size().width();
        const uint32_t          h        = image.size().height();
        const bool              isInter  = image.videoScanMode().isInterlaced();
        const FrameRate::RationalType r  = rate.rational();
        const unsigned int      num      = r.numerator();
        const unsigned int      den      = r.denominator();

        for (const auto &row : kVideoFormatTable) {
                if (row.width != w || row.height != h) continue;
                if (row.interlaced != isInter) continue;
                if (row.num != num || row.den != den) continue;
                return row.ntv2Format;
        }
        return NTV2_FORMAT_UNKNOWN;
}

Error fromNtv2VideoFormat(int fmt, Size2Du32 *outSize, FrameRate *outRate, VideoScanMode *outScan) {
        if (fmt == NTV2_FORMAT_UNKNOWN) return Error::InvalidArgument;
        for (const auto &row : kVideoFormatTable) {
                if (row.ntv2Format != fmt) continue;
                if (outSize) *outSize = Size2Du32(row.width, row.height);
                if (outRate) *outRate = FrameRate(FrameRate::RationalType(row.num, row.den));
                if (outScan) *outScan = row.interlaced ? VideoScanMode::Interlaced : VideoScanMode::Progressive;
                return Error::Ok;
        }
        return Error::InvalidArgument;
}

// ============================================================================
// Channel / input source / reference
// ============================================================================

int toNtv2Channel(int channel) {
        // The SDK's NTV2_CHANNEL1..NTV2_CHANNEL8 are sequential 0..7.
        // Reject anything outside 1..8.
        if (channel < 1 || channel > NTV2_MAX_NUM_CHANNELS) return NTV2_CHANNEL_INVALID;
        return NTV2_CHANNEL1 + (channel - 1);
}

int fromNtv2Channel(int ntv2Ch) {
        if (ntv2Ch < NTV2_CHANNEL1 || ntv2Ch >= NTV2_MAX_NUM_CHANNELS) return 0;
        return (ntv2Ch - NTV2_CHANNEL1) + 1;
}

int portToInputSource(const VideoPortRef &port) {
        if (!port.isValid()) return NTV2_INPUTSOURCE_INVALID;
        const int idx = port.index();
        if (port.kind() == VideoConnectorKind::Sdi) {
                // NTV2_INPUTSOURCE_SDI1..SDI8 are sequential.
                if (idx < 1 || idx > 8) return NTV2_INPUTSOURCE_INVALID;
                return NTV2_INPUTSOURCE_SDI1 + (idx - 1);
        }
        if (port.kind() == VideoConnectorKind::Hdmi) {
                if (idx < 1 || idx > 4) return NTV2_INPUTSOURCE_INVALID;
                return NTV2_INPUTSOURCE_HDMI1 + (idx - 1);
        }
        return NTV2_INPUTSOURCE_INVALID;
}

int referenceFor(const VideoReferenceConfig &ref) {
        const VideoReferenceSource src = ref.source();
        if (src == VideoReferenceSource::FreeRun)  return NTV2_REFERENCE_FREERUN;
        if (src == VideoReferenceSource::External) return NTV2_REFERENCE_EXTERNAL;
        if (src == VideoReferenceSource::Genlock)  return NTV2_REFERENCE_EXTERNAL;
        if (src == VideoReferenceSource::Ptp)      return NTV2_REFERENCE_SFP1_PTP;
        if (src == VideoReferenceSource::FromSignal) {
                const VideoPortRef &port = ref.signalPort();
                if (!port.isValid()) return NTV2_REFERENCE_FREERUN;
                const int idx = port.index();
                if (port.kind() == VideoConnectorKind::Sdi) {
                        switch (idx) {
                                case 1: return NTV2_REFERENCE_INPUT1;
                                case 2: return NTV2_REFERENCE_INPUT2;
                                case 3: return NTV2_REFERENCE_INPUT3;
                                case 4: return NTV2_REFERENCE_INPUT4;
                                case 5: return NTV2_REFERENCE_INPUT5;
                                case 6: return NTV2_REFERENCE_INPUT6;
                                case 7: return NTV2_REFERENCE_INPUT7;
                                case 8: return NTV2_REFERENCE_INPUT8;
                                default: return NTV2_REFERENCE_FREERUN;
                        }
                }
                if (port.kind() == VideoConnectorKind::Hdmi && idx == 1) {
                        return NTV2_REFERENCE_HDMI_INPUT1;
                }
        }
        return NTV2_REFERENCE_FREERUN;
}

// ============================================================================
// SDI link standard helpers
// ============================================================================

bool standardFitsCableCount(const SdiLinkStandard &standard, int availableCables) {
        if (standard == SdiLinkStandard::Auto) return true;
        return cablesFor(standard) <= availableCables;
}

} // namespace Ntv2Format

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
