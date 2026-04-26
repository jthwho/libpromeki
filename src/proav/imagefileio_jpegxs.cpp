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
#include <promeki/file.h>
#include <promeki/buffer.h>
#include <promeki/pixelformat.h>
#include <promeki/videoencoder.h>
#include <promeki/videocodec.h>
#include <promeki/metadata.h>
#include <promeki/videopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>

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
// correct compressed PixelFormat::ID for the loaded Image.  The
// decoder handle is closed immediately after probing — we only
// need the header information, not a full decode.

namespace {

        static PixelFormat::ID probeJpegXsHeader(const void *data, size_t size, size_t &widthOut, size_t &heightOut) {
                // JPEG XS codestreams begin with SOC marker 0xFF10.  Reject
                // obviously non-JPEG-XS data before touching SVT.
                if (size < 4) return PixelFormat::Invalid;
                const uint8_t *p = static_cast<const uint8_t *>(data);
                if (p[0] != 0xFF || p[1] != 0x10) return PixelFormat::Invalid;

                svt_jpeg_xs_decoder_api_t dec = {};
                dec.use_cpu_flags = CPU_FLAGS_ALL;
                dec.verbose = VERBOSE_NONE;
                dec.threads_num = 0;
                dec.packetization_mode = 0;
                dec.proxy_mode = proxy_mode_full;

                svt_jpeg_xs_image_config_t cfg = {};
                SvtJxsErrorType_t err = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
                                                                 &dec, static_cast<const uint8_t *>(data), size, &cfg);
                if (err != SvtJxsErrorNone) {
                        return PixelFormat::Invalid;
                }

                widthOut = cfg.width;
                heightOut = cfg.height;

                PixelFormat::ID id = PixelFormat::Invalid;
                const bool      is422 = (cfg.format == COLOUR_FORMAT_PLANAR_YUV422);
                const bool      is420 = (cfg.format == COLOUR_FORMAT_PLANAR_YUV420);

                if (is422) {
                        switch (cfg.bit_depth) {
                                case 8: id = PixelFormat::JPEG_XS_YUV8_422_Rec709; break;
                                case 10: id = PixelFormat::JPEG_XS_YUV10_422_Rec709; break;
                                case 12: id = PixelFormat::JPEG_XS_YUV12_422_Rec709; break;
                                default: break;
                        }
                } else if (is420) {
                        switch (cfg.bit_depth) {
                                case 8: id = PixelFormat::JPEG_XS_YUV8_420_Rec709; break;
                                case 10: id = PixelFormat::JPEG_XS_YUV10_420_Rec709; break;
                                case 12: id = PixelFormat::JPEG_XS_YUV12_420_Rec709; break;
                                default: break;
                        }
                } else if (cfg.format == COLOUR_FORMAT_PLANAR_YUV444_OR_RGB) {
                        // The decoder reports 4:4:4/RGB streams as planar.  We
                        // only have an 8-bit RGB PixelFormat today; 10/12-bit
                        // would need new entries.
                        if (cfg.bit_depth == 8) id = PixelFormat::JPEG_XS_RGB8_sRGB;
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
                        _id = ImageFile::JpegXS;
                        _canLoad = true;
                        _canSave = true;
                        _name = "JPEG XS";
                        _description = "JPEG XS (ISO/IEC 21122) image sequence";
                        _extensions = {"jxs"};
                        _mediaIoName = "ImgSeqJpegXS";
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
// returned video payload is a CompressedVideoPayload whose single
// plane points at the raw JPEG XS codestream bytes.  Consumers that
// need uncompressed pixels hand the payload to a JpegXsVideoDecoder
// session (or equivalent conversion entry point).  The pass-through
// path (JXS file → JXS file) avoids any re-encode.

Error ImageFileIO_JpegXS::load(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        File  file(filename);
        Error err = file.open(File::ReadOnly);
        if (err.isError()) {
                promekiErr("JPEG XS load '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        auto sizeResult = file.size();
        if (!isOk(sizeResult)) {
                file.close();
                return error(sizeResult);
        }
        const int64_t fileSize = value(sizeResult);
        if (fileSize < 4) {
                file.close();
                promekiErr("JPEG XS load '%s': file too small (%lld bytes)", filename.cstr(), (long long)fileSize);
                return Error::CorruptData;
        }

        // Allocate with DIO headroom so readBulk can align the
        // transfer internally.  Buffer::DefaultAlign is page-sized,
        // which satisfies every common filesystem's O_DIRECT
        // requirement.
        auto         alignResult = file.directIOAlignment();
        const size_t bufAlign = isOk(alignResult) ? value(alignResult) : Buffer::DefaultAlign;
        Buffer::Ptr  fileBuf = Buffer::Ptr::create(static_cast<size_t>(fileSize) + bufAlign, bufAlign);
        err = file.readBulk(*fileBuf.modify(), fileSize);
        file.close();
        if (err.isError()) {
                promekiErr("JPEG XS load '%s': read failed: %s", filename.cstr(), err.name().cstr());
                return err;
        }
        if (fileBuf->size() < 4) {
                promekiErr("JPEG XS load '%s': short read (%zu bytes)", filename.cstr(), fileBuf->size());
                return Error::CorruptData;
        }

        size_t          width = 0;
        size_t          height = 0;
        PixelFormat::ID pdId = probeJpegXsHeader(fileBuf->data(), fileBuf->size(), width, height);
        if (pdId == PixelFormat::Invalid) {
                promekiErr("JPEG XS load '%s': header probe failed", filename.cstr());
                return Error::CorruptData;
        }

        ImageDesc cdesc(Size2Du32(width, height), PixelFormat(pdId));
        auto      payload = CompressedVideoPayload::Ptr::create(cdesc, fileBuf);
        if (!payload->isValid()) {
                promekiErr("JPEG XS load '%s': compressed payload build failed", filename.cstr());
                return Error::Invalid;
        }
        Frame frame;
        frame.addPayload(payload);
        imageFile.setFrame(frame);
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
//   2. Input is uncompressed planar YUV that JpegXsVideoEncoder accepts
//      directly: UncompressedVideoPayload::convert() dispatches to the
//      codec without a preparatory CSC.
//
//   3. Input is uncompressed but not in the codec's encodeSources:
//      UncompressedVideoPayload::convert() inserts a CSC to land on a
//      supported format and then encodes.
//
// Paths (2) and (3) are handled uniformly by calling
// UncompressedVideoPayload::convert() with a JPEG XS PixelFormat target.
// The target subtype is chosen to match the input's bit depth and
// subsampling where possible; otherwise we fall back to
// JPEG_XS_YUV8_422_Rec709 which is the most common broadcast format.
//
// MediaConfig::JpegXsBpp / MediaConfig::JpegXsDecomposition on @p
// config flow straight through UncompressedVideoPayload::convert() into
// JpegXsVideoEncoder::configure().

Error ImageFileIO_JpegXS::save(ImageFile &imageFile, const MediaConfig &config) const {
        const String &filename = imageFile.filename();

        VideoPayload::PtrList vps = imageFile.frame().videoPayloads();
        if (vps.isEmpty() || !vps[0].isValid()) {
                promekiErr("JPEG XS save '%s': no video payload", filename.cstr());
                return Error::Invalid;
        }

        // Pass-through: keep the existing JPEG XS bitstream exactly.
        if (const auto *cvp = vps[0]->as<CompressedVideoPayload>()) {
                if (cvp->desc().pixelFormat().videoCodec().id() != VideoCodec::JPEG_XS) {
                        promekiErr("JPEG XS save '%s': unsupported compressed input codec '%s'", filename.cstr(),
                                   cvp->desc().pixelFormat().videoCodec().name().cstr());
                        return Error::NotSupported;
                }
                if (cvp->planeCount() == 0) {
                        promekiErr("JPEG XS save '%s': empty compressed payload", filename.cstr());
                        return Error::Invalid;
                }
                auto         cview = cvp->plane(0);
                const size_t payloadSize = cview.size();
                if (payloadSize == 0) {
                        promekiErr("JPEG XS save '%s': empty compressed payload", filename.cstr());
                        return Error::Invalid;
                }

                File  file(filename);
                Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
                if (err.isError()) {
                        promekiErr("JPEG XS save '%s': %s", filename.cstr(), err.name().cstr());
                        return err;
                }
                const int64_t written = file.writeBulk(cview.data(), static_cast<int64_t>(payloadSize));
                file.close();
                if (written != static_cast<int64_t>(payloadSize)) {
                        promekiErr("JPEG XS save '%s': short write (%lld of %zu)", filename.cstr(), (long long)written,
                                   payloadSize);
                        return Error::IOError;
                }
                return Error::Ok;
        }

        const auto *uvp = vps[0]->as<UncompressedVideoPayload>();
        if (uvp == nullptr || !uvp->desc().isValid() || uvp->planeCount() == 0) {
                promekiErr("JPEG XS save '%s': input payload not valid", filename.cstr());
                return Error::Invalid;
        }

        // Pick a JPEG XS subtype that matches the input's bit depth
        // and subsampling to avoid an extra CSC hop where possible.
        PixelFormat::ID targetId = PixelFormat::JPEG_XS_YUV8_422_Rec709;
        switch (uvp->desc().pixelFormat().id()) {
                // 4:2:2 planar inputs — match bit depth directly.
                case PixelFormat::YUV8_422_Planar_Rec709: targetId = PixelFormat::JPEG_XS_YUV8_422_Rec709; break;
                case PixelFormat::YUV10_422_Planar_LE_Rec709: targetId = PixelFormat::JPEG_XS_YUV10_422_Rec709; break;
                case PixelFormat::YUV12_422_Planar_LE_Rec709: targetId = PixelFormat::JPEG_XS_YUV12_422_Rec709; break;
                // 4:2:0 planar inputs — match bit depth directly.
                case PixelFormat::YUV8_420_Planar_Rec709: targetId = PixelFormat::JPEG_XS_YUV8_420_Rec709; break;
                case PixelFormat::YUV10_420_Planar_LE_Rec709: targetId = PixelFormat::JPEG_XS_YUV10_420_Rec709; break;
                case PixelFormat::YUV12_420_Planar_LE_Rec709: targetId = PixelFormat::JPEG_XS_YUV12_420_Rec709; break;
                // Interleaved 4:2:2 — encode as 8-bit 4:2:2.
                case PixelFormat::YUV8_422_Rec709:
                case PixelFormat::YUV8_422_UYVY_Rec709: targetId = PixelFormat::JPEG_XS_YUV8_422_Rec709; break;
                // 4:2:0 semi-planar — encode as 8-bit 4:2:0.
                case PixelFormat::YUV8_420_SemiPlanar_Rec709: targetId = PixelFormat::JPEG_XS_YUV8_420_Rec709; break;
                // RGB inputs — encode as packed RGB directly.  The SVT
                // encoder deinterleaves to planar internally with
                // AVX2/AVX512 fast paths.
                case PixelFormat::RGB8_sRGB:
                case PixelFormat::RGB8_Planar_sRGB: targetId = PixelFormat::JPEG_XS_RGB8_sRGB; break;
                default:
                        // RGBA, mono, and anything else: fall back to
                        // JPEG XS RGB when the input is an RGB-family
                        // format, otherwise YUV 4:2:2.
                        if (uvp->desc().pixelFormat().colorModel().id() == ColorModel::sRGB)
                                targetId = PixelFormat::JPEG_XS_RGB8_sRGB;
                        else
                                targetId = PixelFormat::JPEG_XS_YUV8_422_Rec709;
                        break;
        }

        // Same JpegVideoEncoder bridging used by ImageFileIO_JPEG —
        // one-shot session, configure with the chosen sub-target,
        // submit, pull the packet, write its bytes.
        MediaConfig encCfg = config;
        encCfg.set(MediaConfig::OutputPixelFormat, PixelFormat(targetId));
        auto encResult = VideoCodec(VideoCodec::JPEG_XS).createEncoder(&encCfg);
        if (error(encResult).isError()) {
                promekiErr("JPEG XS save '%s': createEncoder failed: %s", filename.cstr(),
                           error(encResult).name().cstr());
                return Error::NotSupported;
        }
        VideoEncoder *enc = value(encResult);

        UncompressedVideoPayload::Ptr inPayload = sharedPointerCast<UncompressedVideoPayload>(vps[0]);
        const auto                   &sources = PixelFormat(targetId).encodeSources();
        bool                          sourceOk = sources.isEmpty();
        for (PixelFormat::ID s : sources) {
                if (uvp->desc().pixelFormat().id() == s) {
                        sourceOk = true;
                        break;
                }
        }
        if (!sourceOk && !sources.isEmpty()) {
                inPayload = uvp->convert(PixelFormat(sources[0]), uvp->desc().metadata(), config);
                if (!inPayload.isValid()) {
                        delete enc;
                        promekiErr("JPEG XS save '%s': prep CSC %s -> %s failed", filename.cstr(),
                                   uvp->desc().pixelFormat().name().cstr(), PixelFormat(sources[0]).name().cstr());
                        return Error::ConversionFailed;
                }
        }

        if (!inPayload.isValid()) {
                delete enc;
                promekiErr("JPEG XS save '%s': payload build failed", filename.cstr());
                return Error::ConversionFailed;
        }
        if (Error e = enc->submitPayload(inPayload); e.isError()) {
                String msg = enc->lastErrorMessage();
                delete enc;
                promekiErr("JPEG XS save '%s': encode failed: %s", filename.cstr(),
                           msg.isEmpty() ? e.name().cstr() : msg.cstr());
                return e;
        }
        CompressedVideoPayload::Ptr outPayload = enc->receiveCompressedPayload();
        delete enc;
        if (!outPayload.isValid()) {
                promekiErr("JPEG XS save '%s': encoder produced no payload", filename.cstr());
                return Error::EncodeFailed;
        }
        auto         encView = outPayload->plane(0);
        const void  *payload = encView.data();
        const size_t payloadSize = encView.size();
        if (payload == nullptr || payloadSize == 0) {
                promekiErr("JPEG XS save '%s': empty encoded payload", filename.cstr());
                return Error::EncodeFailed;
        }

        File  file(filename);
        Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
        if (err.isError()) {
                promekiErr("JPEG XS save '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }
        const int64_t written = file.writeBulk(payload, static_cast<int64_t>(payloadSize));
        file.close();
        if (written != static_cast<int64_t>(payloadSize)) {
                promekiErr("JPEG XS save '%s': short write (%lld of %zu)", filename.cstr(), (long long)written,
                           payloadSize);
                return Error::IOError;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
