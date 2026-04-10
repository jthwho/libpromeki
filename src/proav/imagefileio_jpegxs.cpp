/**
 * @file      imagefileio_jpegxs.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <promeki/logger.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/image.h>
#include <promeki/file.h>
#include <promeki/buffer.h>
#include <promeki/pixeldesc.h>
#include <promeki/jpegxsimagecodec.h>
#include <promeki/metadata.h>

#include <SvtJpegxs.h>
#include <SvtJpegxsDec.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(JPEGXS)

// ===========================================================================
// Header probing
// ===========================================================================
//
// SVT-JPEG-XS parses the JPEG XS codestream header during
// svt_jpeg_xs_decoder_init, filling an svt_jpeg_xs_image_config_t
// with the intrinsic frame geometry (width, height, bit_depth,
// colour_format, components_num).  We use that to identify the
// correct compressed PixelDesc::ID for the loaded Image.  The
// decoder handle is closed immediately after probing — we only
// need the header information, not a full decode.

namespace {

static PixelDesc::ID probeJpegXsHeader(const void *data, size_t size,
                                       size_t &widthOut, size_t &heightOut) {
        // JPEG XS codestreams begin with SOC marker 0xFF10.  Reject
        // obviously non-JPEG-XS data before touching SVT.
        if(size < 4) return PixelDesc::Invalid;
        const uint8_t *p = static_cast<const uint8_t *>(data);
        if(p[0] != 0xFF || p[1] != 0x10) return PixelDesc::Invalid;

        svt_jpeg_xs_decoder_api_t dec = {};
        dec.use_cpu_flags      = CPU_FLAGS_ALL;
        dec.verbose            = VERBOSE_NONE;
        dec.threads_num        = 0;
        dec.packetization_mode = 0;
        dec.proxy_mode         = proxy_mode_full;

        svt_jpeg_xs_image_config_t cfg = {};
        SvtJxsErrorType_t err = svt_jpeg_xs_decoder_init(
                SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
                &dec,
                static_cast<const uint8_t *>(data),
                size,
                &cfg);
        if(err != SvtJxsErrorNone) {
                return PixelDesc::Invalid;
        }

        widthOut  = cfg.width;
        heightOut = cfg.height;

        PixelDesc::ID id = PixelDesc::Invalid;
        const bool is422 = (cfg.format == COLOUR_FORMAT_PLANAR_YUV422);
        const bool is420 = (cfg.format == COLOUR_FORMAT_PLANAR_YUV420);

        if(is422) {
                switch(cfg.bit_depth) {
                        case 8:  id = PixelDesc::JPEG_XS_YUV8_422_Rec709;  break;
                        case 10: id = PixelDesc::JPEG_XS_YUV10_422_Rec709; break;
                        case 12: id = PixelDesc::JPEG_XS_YUV12_422_Rec709; break;
                        default: break;
                }
        } else if(is420) {
                switch(cfg.bit_depth) {
                        case 8:  id = PixelDesc::JPEG_XS_YUV8_420_Rec709;  break;
                        case 10: id = PixelDesc::JPEG_XS_YUV10_420_Rec709; break;
                        case 12: id = PixelDesc::JPEG_XS_YUV12_420_Rec709; break;
                        default: break;
                }
        } else if(cfg.format == COLOUR_FORMAT_PLANAR_YUV444_OR_RGB) {
                // The decoder reports 4:4:4/RGB streams as planar.  We
                // only have an 8-bit RGB PixelDesc today; 10/12-bit
                // would need new entries.
                if(cfg.bit_depth == 8) id = PixelDesc::JPEG_XS_RGB8_sRGB;
        }

        svt_jpeg_xs_decoder_close(&dec);
        return id;
}

} // namespace

// ===========================================================================
// ImageFileIO_JpegXS
// ===========================================================================

class ImageFileIO_JpegXS : public ImageFileIO {
        public:
                ImageFileIO_JpegXS() {
                        _id      = ImageFile::JpegXS;
                        _canLoad = true;
                        _canSave = true;
                        _name    = "JPEG XS";
                }

                Error load(ImageFile &imageFile, const MediaConfig &config) const override;
                Error save(ImageFile &imageFile, const MediaConfig &config) const override;
};
PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_JpegXS);

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------
//
// Like the JPEG backend, JPEG XS load keeps the bitstream intact: the
// returned Image is compressed (isCompressed() == true) and its single
// plane points at the raw JPEG XS codestream bytes.  Consumers that
// need uncompressed pixels run the image through Image::convert() —
// the dispatcher routes compressed inputs to JpegXsImageCodec::decode()
// automatically.  The pass-through path (JXS file → JXS file) avoids
// any re-encode.

Error ImageFileIO_JpegXS::load(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        File file(filename);
        Error err = file.open(File::ReadOnly);
        if(err.isError()) {
                promekiErr("JPEG XS load '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        auto sizeResult = file.size();
        if(!isOk(sizeResult)) {
                file.close();
                return error(sizeResult);
        }
        const int64_t fileSize = value(sizeResult);
        if(fileSize < 4) {
                file.close();
                promekiErr("JPEG XS load '%s': file too small (%lld bytes)",
                           filename.cstr(), (long long)fileSize);
                return Error::CorruptData;
        }

        // Allocate with DIO headroom so readBulk can align the
        // transfer internally.  Buffer::DefaultAlign is page-sized,
        // which satisfies every common filesystem's O_DIRECT
        // requirement.
        auto alignResult = file.directIOAlignment();
        const size_t bufAlign = isOk(alignResult) ? value(alignResult)
                                                  : Buffer::DefaultAlign;
        Buffer::Ptr fileBuf = Buffer::Ptr::create(
                static_cast<size_t>(fileSize) + bufAlign, bufAlign);
        err = file.readBulk(*fileBuf.modify(), fileSize);
        file.close();
        if(err.isError()) {
                promekiErr("JPEG XS load '%s': read failed: %s",
                           filename.cstr(), err.name().cstr());
                return err;
        }
        if(fileBuf->size() < 4) {
                promekiErr("JPEG XS load '%s': short read (%zu bytes)",
                           filename.cstr(), fileBuf->size());
                return Error::CorruptData;
        }

        size_t width = 0;
        size_t height = 0;
        PixelDesc::ID pdId = probeJpegXsHeader(fileBuf->data(), fileBuf->size(),
                                               width, height);
        if(pdId == PixelDesc::Invalid) {
                promekiErr("JPEG XS load '%s': header probe failed", filename.cstr());
                return Error::CorruptData;
        }

        Image img = Image::fromBuffer(fileBuf, width, height, PixelDesc(pdId));
        if(!img.isValid()) {
                promekiErr("JPEG XS load '%s': Image::fromBuffer failed", filename.cstr());
                return Error::Invalid;
        }
        imageFile.setImage(img);
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------
//
// Three paths, mirroring the JPEG backend:
//
//   1. Input is already a compressed JPEG XS bitstream: write the
//      payload bytes verbatim (zero-loss pass-through).
//
//   2. Input is uncompressed planar YUV that JpegXsImageCodec accepts
//      directly: Image::convert() dispatches to the codec without a
//      preparatory CSC.
//
//   3. Input is uncompressed but not in the codec's encodeSources:
//      Image::convert() inserts a CSC to land on a supported format
//      and then encodes.
//
// Paths (2) and (3) are handled uniformly by calling Image::convert()
// with a JPEG XS PixelDesc target.  The target subtype is chosen to
// match the input's bit depth and subsampling where possible; otherwise
// we fall back to JPEG_XS_YUV8_422_Rec709 which is the most common
// broadcast format.
//
// MediaConfig::JpegXsBpp / MediaConfig::JpegXsDecomposition on @p
// config flow straight through Image::convert() into
// JpegXsImageCodec::configure().

Error ImageFileIO_JpegXS::save(ImageFile &imageFile, const MediaConfig &config) const {
        const String &filename = imageFile.filename();
        Image image = imageFile.image();
        if(!image.isValid()) {
                promekiErr("JPEG XS save '%s': image is not valid", filename.cstr());
                return Error::Invalid;
        }

        // Pass-through: keep the existing JPEG XS bitstream exactly.
        if(image.isCompressed() && image.pixelDesc().codecName() == "jpegxs") {
                const void *payload = image.data(0);
                const size_t payloadSize = image.compressedSize();
                if(payload == nullptr || payloadSize == 0) {
                        promekiErr("JPEG XS save '%s': empty compressed payload",
                                   filename.cstr());
                        return Error::Invalid;
                }

                File file(filename);
                Error err = file.open(File::WriteOnly,
                                      File::Create | File::Truncate);
                if(err.isError()) {
                        promekiErr("JPEG XS save '%s': %s",
                                   filename.cstr(), err.name().cstr());
                        return err;
                }
                // writeBulk uses direct I/O for the aligned interior
                // of the payload, falling back to normal I/O for any
                // unaligned head/tail and when the source pointer is
                // not page-aligned.
                const int64_t written = file.writeBulk(payload,
                                                       static_cast<int64_t>(payloadSize));
                file.close();
                if(written != static_cast<int64_t>(payloadSize)) {
                        promekiErr("JPEG XS save '%s': short write (%lld of %zu)",
                                   filename.cstr(),
                                   (long long)written, payloadSize);
                        return Error::IOError;
                }
                return Error::Ok;
        }

        // Compressed non-JPEG-XS inputs need to be decoded first.
        if(image.isCompressed()) {
                promekiErr("JPEG XS save '%s': unsupported compressed input codec '%s'",
                           filename.cstr(),
                           image.pixelDesc().codecName().cstr());
                return Error::NotSupported;
        }

        // Pick a JPEG XS subtype that matches the input's bit depth
        // and subsampling to avoid an extra CSC hop where possible.
        PixelDesc::ID targetId = PixelDesc::JPEG_XS_YUV8_422_Rec709;
        switch(image.pixelDesc().id()) {
                // 4:2:2 planar inputs — match bit depth directly.
                case PixelDesc::YUV8_422_Planar_Rec709:
                        targetId = PixelDesc::JPEG_XS_YUV8_422_Rec709;
                        break;
                case PixelDesc::YUV10_422_Planar_LE_Rec709:
                        targetId = PixelDesc::JPEG_XS_YUV10_422_Rec709;
                        break;
                case PixelDesc::YUV12_422_Planar_LE_Rec709:
                        targetId = PixelDesc::JPEG_XS_YUV12_422_Rec709;
                        break;
                // 4:2:0 planar inputs — match bit depth directly.
                case PixelDesc::YUV8_420_Planar_Rec709:
                        targetId = PixelDesc::JPEG_XS_YUV8_420_Rec709;
                        break;
                case PixelDesc::YUV10_420_Planar_LE_Rec709:
                        targetId = PixelDesc::JPEG_XS_YUV10_420_Rec709;
                        break;
                case PixelDesc::YUV12_420_Planar_LE_Rec709:
                        targetId = PixelDesc::JPEG_XS_YUV12_420_Rec709;
                        break;
                // Interleaved 4:2:2 — encode as 8-bit 4:2:2.
                case PixelDesc::YUV8_422_Rec709:
                case PixelDesc::YUV8_422_UYVY_Rec709:
                        targetId = PixelDesc::JPEG_XS_YUV8_422_Rec709;
                        break;
                // 4:2:0 semi-planar — encode as 8-bit 4:2:0.
                case PixelDesc::YUV8_420_SemiPlanar_Rec709:
                        targetId = PixelDesc::JPEG_XS_YUV8_420_Rec709;
                        break;
                // RGB inputs — encode as packed RGB directly.  The SVT
                // encoder deinterleaves to planar internally with
                // AVX2/AVX512 fast paths.
                case PixelDesc::RGB8_sRGB:
                case PixelDesc::RGB8_Planar_sRGB:
                        targetId = PixelDesc::JPEG_XS_RGB8_sRGB;
                        break;
                default:
                        // RGBA, mono, and anything else: fall back to
                        // JPEG XS RGB when the input is an RGB-family
                        // format, otherwise YUV 4:2:2.
                        if(image.pixelDesc().colorModel().id() == ColorModel::sRGB)
                                targetId = PixelDesc::JPEG_XS_RGB8_sRGB;
                        else
                                targetId = PixelDesc::JPEG_XS_YUV8_422_Rec709;
                        break;
        }

        Image encoded = image.convert(PixelDesc(targetId), image.metadata(), config);
        if(!encoded.isValid() || !encoded.isCompressed()) {
                promekiErr("JPEG XS save '%s': encode of '%s' failed",
                           filename.cstr(),
                           image.pixelDesc().name().cstr());
                return Error::EncodeFailed;
        }

        const void *payload = encoded.data(0);
        const size_t payloadSize = encoded.compressedSize();
        if(payload == nullptr || payloadSize == 0) {
                promekiErr("JPEG XS save '%s': empty encoded payload", filename.cstr());
                return Error::EncodeFailed;
        }

        File file(filename);
        Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
        if(err.isError()) {
                promekiErr("JPEG XS save '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }
        const int64_t written = file.writeBulk(payload, static_cast<int64_t>(payloadSize));
        file.close();
        if(written != static_cast<int64_t>(payloadSize)) {
                promekiErr("JPEG XS save '%s': short write (%lld of %zu)",
                           filename.cstr(), (long long)written, payloadSize);
                return Error::IOError;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
