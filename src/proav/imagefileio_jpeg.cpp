/**
 * @file      imagefileio_jpeg.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <csetjmp>
#include <cstring>
#include <jpeglib.h>
#include <promeki/logger.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/image.h>
#include <promeki/videoencoder.h>
#include <promeki/videopacket.h>
#include <promeki/videocodec.h>
#include <promeki/file.h>
#include <promeki/buffer.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(JPEG)

// ===========================================================================
// Header probing
// ===========================================================================
//
// libjpeg reads enough of the bitstream to report width, height, colour
// space and the per-component sampling factors without touching any
// scanlines.  We use that to decide which JPEG_xxx PixelFormat best
// describes the file on disk.  The PixelFormat is stored on the resulting
// Image so downstream consumers can decide whether to pass the bitstream
// through verbatim (JPEG → JPEG copy) or let Image::convert() route it
// through a JpegVideoDecoder session for rendering.

namespace {

struct ProbeErrorMgr {
        jpeg_error_mgr  pub;
        jmp_buf         jmpBuf;
};

static void probeErrorExit(j_common_ptr cinfo) {
        ProbeErrorMgr *mgr = reinterpret_cast<ProbeErrorMgr *>(cinfo->err);
        longjmp(mgr->jmpBuf, 1);
}

// Probes the JPEG bitstream at @p data for dimensions and the best-matching
// JPEG PixelFormat.  On success, fills @p widthOut / @p heightOut and returns
// the chosen PixelFormat::ID.  Returns PixelFormat::Invalid if the header can't
// be parsed.
static PixelFormat::ID probeJpegHeader(const void *data, size_t size,
                                     size_t &widthOut, size_t &heightOut) {
        jpeg_decompress_struct dinfo;
        ProbeErrorMgr jerr;
        dinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = probeErrorExit;
        if(setjmp(jerr.jmpBuf)) {
                jpeg_destroy_decompress(&dinfo);
                return PixelFormat::Invalid;
        }
        jpeg_create_decompress(&dinfo);
        jpeg_mem_src(&dinfo,
                     static_cast<const unsigned char *>(data),
                     static_cast<unsigned long>(size));
        if(jpeg_read_header(&dinfo, TRUE) != JPEG_HEADER_OK) {
                jpeg_destroy_decompress(&dinfo);
                return PixelFormat::Invalid;
        }

        widthOut  = dinfo.image_width;
        heightOut = dinfo.image_height;

        PixelFormat::ID id = PixelFormat::JPEG_RGB8_sRGB;
        switch(dinfo.jpeg_color_space) {
                case JCS_GRAYSCALE:
                case JCS_RGB:
                        id = PixelFormat::JPEG_RGB8_sRGB;
                        break;
                case JCS_YCbCr: {
                        // Component 0 carries the luma sampling factors.
                        // h=2,v=2 → 4:2:0; h=2,v=1 → 4:2:2; h=1,v=1 → 4:4:4.
                        // 4:4:4 has no dedicated JPEG PixelFormat today, so
                        // we tag it as 4:2:2 (the chroma siting is close
                        // enough for description purposes and the decode
                        // targets are a superset).
                        const int hs = dinfo.comp_info[0].h_samp_factor;
                        const int vs = dinfo.comp_info[0].v_samp_factor;
                        if(hs == 2 && vs == 2) {
                                id = PixelFormat::JPEG_YUV8_420_Rec709;
                        } else {
                                id = PixelFormat::JPEG_YUV8_422_Rec709;
                        }
                        break;
                }
                default:
                        // Unusual colour spaces (CMYK, YCCK, BG_RGB, ...):
                        // tag as generic JPEG RGB and let JpegVideoDecoder
                        // handle the conversion on decode.
                        id = PixelFormat::JPEG_RGB8_sRGB;
                        break;
        }
        jpeg_destroy_decompress(&dinfo);
        return id;
}

} // namespace

// ===========================================================================
// ImageFileIO_JPEG
// ===========================================================================

class ImageFileIO_JPEG : public ImageFileIO {
        public:
                ImageFileIO_JPEG() {
                        _id      = ImageFile::JPEG;
                        _canLoad = true;
                        _canSave = true;
                        _name    = "JPEG";
                        _description = "JPEG / JFIF image sequence";
                        _extensions = { "jpg", "jpeg", "jfif" };
                }

                Error load(ImageFile &imageFile, const MediaConfig &config) const override;
                Error save(ImageFile &imageFile, const MediaConfig &config) const override;
};
PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_JPEG);

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------
//
// JPEG load keeps the bitstream intact: the returned Image is compressed
// (isCompressed() == true) and its single plane points at the raw JPEG
// bytes.  Consumers that need uncompressed pixels run the image through
// Image::convert() — the dispatcher routes compressed inputs through a
// JpegVideoDecoder session automatically.  The pass-through MediaIO copy
// path (JPEG file → JPEG file) therefore avoids any re-encode.

Error ImageFileIO_JPEG::load(ImageFile &imageFile, const MediaConfig &config) const {
        // Load is a pure pass-through — the on-disk JPEG bytes flow into
        // the returned Image untouched, with no codec invocation.  No
        // config keys apply on the read path today (the future EXIF /
        // IPTC pickers will read from @ref MediaConfig once they exist).
        (void)config;
        const String &filename = imageFile.filename();

        File file(filename);
        Error err = file.open(File::ReadOnly);
        if(err.isError()) {
                promekiErr("JPEG load '%s': %s", filename.cstr(), err.name().cstr());
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
                promekiErr("JPEG load '%s': file too small (%lld bytes)",
                           filename.cstr(), (long long)fileSize);
                return Error::CorruptData;
        }

        // Single-buffer slurp.  JPEG headers can only be parsed from a
        // contiguous memory block, and the bitstream we want to hand to
        // Image::fromBuffer() has to live in one Buffer anyway.  Use
        // Buffer::Ptr::modify() so readBulk() sees a non-const
        // reference — the buffer has exclusive ownership at this point,
        // so modify() is just an unwrap.
        Buffer::Ptr fileBuf = Buffer::Ptr::create(static_cast<size_t>(fileSize));
        err = file.readBulk(*fileBuf.modify(), fileSize);
        file.close();
        if(err.isError()) {
                promekiErr("JPEG load '%s': read failed: %s",
                           filename.cstr(), err.name().cstr());
                return err;
        }
        // readBulk leaves size() reflecting the actual number of bytes
        // read.  If it came up short, downstream probes will catch it.
        if(fileBuf->size() < 4) {
                promekiErr("JPEG load '%s': short read (%zu bytes)",
                           filename.cstr(), fileBuf->size());
                return Error::CorruptData;
        }

        size_t width = 0;
        size_t height = 0;
        PixelFormat::ID pdId = probeJpegHeader(fileBuf->data(), fileBuf->size(),
                                             width, height);
        if(pdId == PixelFormat::Invalid) {
                promekiErr("JPEG load '%s': header probe failed", filename.cstr());
                return Error::CorruptData;
        }

        // Zero-copy wrap: the Image adopts fileBuf as plane 0 and the
        // compressedSize metadata is set from the buffer length.
        Image img = Image::fromBuffer(fileBuf, width, height, PixelFormat(pdId));
        if(!img.isValid()) {
                promekiErr("JPEG load '%s': Image::fromBuffer failed", filename.cstr());
                return Error::Invalid;
        }
        imageFile.setImage(img);
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------
//
// Three paths:
//
//   1. Input is already a compressed JPEG (isCompressed() && codecName()
//      == "JPEG"): write the payload bytes verbatim.  This is the fast
//      pass-through that MediaIO uses for zero-loss JPEG copies.
//
//   2. Input is uncompressed and JpegVideoEncoder accepts its PixelFormat
//      directly: Image::convert() dispatches to the codec without a
//      preparatory CSC.
//
//   3. Input is uncompressed but not in the target codec's encodeSources:
//      Image::convert() inserts a CSC to land on a supported format and
//      then encodes.
//
// Paths (2) and (3) are handled uniformly by calling Image::convert() with
// a JPEG PixelFormat target.  The target subtype is chosen to maximise the
// chance of avoiding an intermediate CSC for common source formats:
// JPEG_YUV8_422_Rec709 for YUV inputs, JPEG_RGB8_sRGB for RGB/mono
// inputs, falling back to RGB for anything else.
//
// MediaConfig::JpegQuality / MediaConfig::JpegSubsampling on @p config
// flow straight through Image::convert() into JpegVideoEncoder::configure(),
// so callers can write `mediaplay -oc JpegQuality:95 -o out.jpg` and have
// it take effect on the file backend exactly the same way it would on the
// converter backend.

Error ImageFileIO_JPEG::save(ImageFile &imageFile, const MediaConfig &config) const {
        const String &filename = imageFile.filename();
        Image image = imageFile.image();
        if(!image.isValid()) {
                promekiErr("JPEG save '%s': image is not valid", filename.cstr());
                return Error::Invalid;
        }

        // Pass-through: keep the existing JPEG bitstream exactly.
        if(image.isCompressed() && image.pixelFormat().videoCodec().id() == VideoCodec::JPEG) {
                const void *payload = image.data(0);
                const size_t payloadSize = image.compressedSize();
                if(payload == nullptr || payloadSize == 0) {
                        promekiErr("JPEG save '%s': empty compressed payload",
                                   filename.cstr());
                        return Error::Invalid;
                }

                File file(filename);
                Error err = file.open(File::WriteOnly,
                                      File::Create | File::Truncate);
                if(err.isError()) {
                        promekiErr("JPEG save '%s': %s",
                                   filename.cstr(), err.name().cstr());
                        return err;
                }
                const int64_t written = file.write(payload,
                                                   static_cast<int64_t>(payloadSize));
                file.close();
                if(written != static_cast<int64_t>(payloadSize)) {
                        promekiErr("JPEG save '%s': short write (%lld of %zu)",
                                   filename.cstr(),
                                   (long long)written, payloadSize);
                        return Error::IOError;
                }
                return Error::Ok;
        }

        // Compressed non-JPEG inputs need to be decoded to an uncompressed
        // format before we can re-encode as JPEG.  Image::convert() handles
        // the decode → re-encode chain but we detect the failure early for
        // a clearer error message.
        if(image.isCompressed()) {
                promekiErr("JPEG save '%s': unsupported compressed input codec '%s'",
                           filename.cstr(),
                           image.pixelFormat().videoCodec().name().cstr());
                return Error::NotSupported;
        }

        // Pick a JPEG subtype that matches the input colour family to
        // avoid an extra CSC hop where possible.
        PixelFormat::ID targetId = PixelFormat::JPEG_RGB8_sRGB;
        switch(image.pixelFormat().id()) {
                case PixelFormat::YUV8_422_Rec709:
                case PixelFormat::YUV8_422_UYVY_Rec709:
                case PixelFormat::YUV8_422_Planar_Rec709:
                        targetId = PixelFormat::JPEG_YUV8_422_Rec709;
                        break;
                case PixelFormat::YUV8_420_Planar_Rec709:
                case PixelFormat::YUV8_420_SemiPlanar_Rec709:
                        targetId = PixelFormat::JPEG_YUV8_420_Rec709;
                        break;
                case PixelFormat::RGBA8_sRGB:
                        targetId = PixelFormat::JPEG_RGBA8_sRGB;
                        break;
                default:
                        targetId = PixelFormat::JPEG_RGB8_sRGB;
                        break;
        }

        // Run the encode through a one-shot JpegVideoEncoder session.
        // Image::convert no longer dispatches to compression codecs;
        // each compressor lives behind the VideoEncoder contract so
        // pipelines can amortise its session state across frames.  For
        // a single-image file save we just create, configure, push,
        // pull, destroy.  Forward the caller's MediaConfig and force
        // the chosen JPEG sub-target so the encoder lands on the
        // format we picked above instead of its own default.
        MediaConfig encCfg = config;
        encCfg.set(MediaConfig::OutputPixelFormat, PixelFormat(targetId));
        auto encResult = VideoCodec(VideoCodec::JPEG).createEncoder(&encCfg);
        if(error(encResult).isError()) {
                promekiErr("JPEG save '%s': createEncoder failed: %s",
                           filename.cstr(), error(encResult).name().cstr());
                return Error::NotSupported;
        }
        VideoEncoder *enc = value(encResult);

        // The encoder happily accepts the source image as-is when its
        // PixelFormat is in supportedInputs(); otherwise we run a CSC to
        // the codec's preferred encode source (typically the YUV
        // variant matching the chosen target's chroma subsampling).
        Image encodeInput = image;
        const auto &sources = PixelFormat(targetId).encodeSources();
        bool sourceOk = sources.isEmpty();
        for(PixelFormat::ID s : sources) {
                if(image.pixelFormat().id() == s) { sourceOk = true; break; }
        }
        if(!sourceOk && !sources.isEmpty()) {
                encodeInput = image.convert(PixelFormat(sources[0]),
                                            image.metadata(), config);
                if(!encodeInput.isValid()) {
                        delete enc;
                        promekiErr("JPEG save '%s': prep CSC %s -> %s failed",
                                   filename.cstr(),
                                   image.pixelFormat().name().cstr(),
                                   PixelFormat(sources[0]).name().cstr());
                        return Error::ConversionFailed;
                }
        }

        if(Error e = enc->submitFrame(Image::Ptr::create(std::move(encodeInput))); e.isError()) {
                String msg = enc->lastErrorMessage();
                delete enc;
                promekiErr("JPEG save '%s': encode failed: %s",
                           filename.cstr(), msg.isEmpty() ? e.name().cstr() : msg.cstr());
                return e;
        }
        VideoPacket::Ptr pkt = enc->receivePacket();
        delete enc;
        if(!pkt) {
                promekiErr("JPEG save '%s': encoder produced no packet",
                           filename.cstr());
                return Error::EncodeFailed;
        }
        const void *payload = pkt->view().data();
        const size_t payloadSize = pkt->view().size();
        if(payload == nullptr || payloadSize == 0) {
                promekiErr("JPEG save '%s': empty encoded payload", filename.cstr());
                return Error::EncodeFailed;
        }

        File file(filename);
        Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
        if(err.isError()) {
                promekiErr("JPEG save '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }
        const int64_t written = file.write(payload, static_cast<int64_t>(payloadSize));
        file.close();
        if(written != static_cast<int64_t>(payloadSize)) {
                promekiErr("JPEG save '%s': short write (%lld of %zu)",
                           filename.cstr(), (long long)written, payloadSize);
                return Error::IOError;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
