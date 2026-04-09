/**
 * @file      imagefileio_png.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <spng.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <promeki/logger.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/file.h>
#include <promeki/buffer.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(PNG)

// ===========================================================================
// Constants
// ===========================================================================

// Fallback alignment when the filesystem cannot tell us its O_DIRECT
// requirement. 4096 covers every common Linux filesystem.
static constexpr size_t PNG_DIO_FALLBACK_ALIGN = 4096;

// ===========================================================================
// PixelDesc <-> PNG IHDR mapping
// ===========================================================================
//
// PNG natively encodes only the byte-order-natural color types listed below.
// Multibyte (16-bit) values are big-endian on the wire, so we can pass our
// `_BE_sRGB` PixelDescs straight through to libspng. The `_LE_sRGB` variants
// require a 16-bit byte swap into a temporary buffer before encoding.
//
// PixelDescs that PNG cannot represent (BGR/ARGB/ABGR component orders,
// 10/12-bit packed formats, planar/semi-planar/YCbCr, float, palette as
// output) are intentionally rejected here. Callers should run
// Image::convert() upstream to land in one of the supported descs.

struct PngFormat {
        uint8_t colorType;     ///< spng_color_type value
        uint8_t bitDepth;      ///< 8 or 16
        uint8_t channels;      ///< Channel count (used to compute packed stride)
        bool    swap16;        ///< true → input is little-endian 16-bit, swap to big-endian before encoding
};

static bool pngFormatFromPixelDesc(PixelDesc::ID id, PngFormat &out) {
        switch(id) {
                case PixelDesc::Mono8_sRGB:     out = { SPNG_COLOR_TYPE_GRAYSCALE,       8,  1, false }; return true;
                case PixelDesc::Mono16_BE_sRGB: out = { SPNG_COLOR_TYPE_GRAYSCALE,       16, 1, false }; return true;
                case PixelDesc::Mono16_LE_sRGB: out = { SPNG_COLOR_TYPE_GRAYSCALE,       16, 1, true  }; return true;
                case PixelDesc::RGB8_sRGB:      out = { SPNG_COLOR_TYPE_TRUECOLOR,       8,  3, false }; return true;
                case PixelDesc::RGB16_BE_sRGB:  out = { SPNG_COLOR_TYPE_TRUECOLOR,       16, 3, false }; return true;
                case PixelDesc::RGB16_LE_sRGB:  out = { SPNG_COLOR_TYPE_TRUECOLOR,       16, 3, true  }; return true;
                case PixelDesc::RGBA8_sRGB:     out = { SPNG_COLOR_TYPE_TRUECOLOR_ALPHA, 8,  4, false }; return true;
                case PixelDesc::RGBA16_BE_sRGB: out = { SPNG_COLOR_TYPE_TRUECOLOR_ALPHA, 16, 4, false }; return true;
                case PixelDesc::RGBA16_LE_sRGB: out = { SPNG_COLOR_TYPE_TRUECOLOR_ALPHA, 16, 4, true  }; return true;
                default: return false;
        }
}

// Map an IHDR to the PixelDesc we should allocate plus the libspng decode
// format we need to ask for. SPNG_FMT_RAW gives us PNG-natural byte order
// (matches the *_BE_sRGB variants directly). For sub-byte / palette /
// gray+alpha sources we ask libspng to expand to a supported representation:
//   - 1/2/4-bit grayscale  → expanded to G8           → Mono8_sRGB
//   - 8-bit gray+alpha     → expanded to RGBA8        → RGBA8_sRGB
//   - 8-bit palette        → expanded to RGBA8        → RGBA8_sRGB (covers tRNS)
// 16-bit gray+alpha is intentionally refused — there's no native PixelDesc
// and the demand is essentially zero.
static PixelDesc::ID pixelDescFromIhdr(const spng_ihdr &ihdr, int &spngFmtOut) {
        switch(ihdr.color_type) {
                case SPNG_COLOR_TYPE_GRAYSCALE:
                        if(ihdr.bit_depth <= 8) {
                                spngFmtOut = SPNG_FMT_G8;
                                return PixelDesc::Mono8_sRGB;
                        }
                        if(ihdr.bit_depth == 16) {
                                spngFmtOut = SPNG_FMT_RAW;
                                return PixelDesc::Mono16_BE_sRGB;
                        }
                        break;
                case SPNG_COLOR_TYPE_TRUECOLOR:
                        if(ihdr.bit_depth == 8) {
                                spngFmtOut = SPNG_FMT_RAW;
                                return PixelDesc::RGB8_sRGB;
                        }
                        if(ihdr.bit_depth == 16) {
                                spngFmtOut = SPNG_FMT_RAW;
                                return PixelDesc::RGB16_BE_sRGB;
                        }
                        break;
                case SPNG_COLOR_TYPE_TRUECOLOR_ALPHA:
                        if(ihdr.bit_depth == 8) {
                                spngFmtOut = SPNG_FMT_RAW;
                                return PixelDesc::RGBA8_sRGB;
                        }
                        if(ihdr.bit_depth == 16) {
                                spngFmtOut = SPNG_FMT_RAW;
                                return PixelDesc::RGBA16_BE_sRGB;
                        }
                        break;
                case SPNG_COLOR_TYPE_GRAYSCALE_ALPHA:
                        if(ihdr.bit_depth <= 8) {
                                spngFmtOut = SPNG_FMT_RGBA8;
                                return PixelDesc::RGBA8_sRGB;
                        }
                        // 16-bit gray+alpha falls through to refusal.
                        break;
                case SPNG_COLOR_TYPE_INDEXED:
                        spngFmtOut = SPNG_FMT_RGBA8;
                        return PixelDesc::RGBA8_sRGB;
        }
        return PixelDesc::Invalid;
}

// In-place 16-bit byte swap. Used for the LE → BE conversion on save.
static void swap16InPlace(void *data, size_t pixelCount) {
        uint16_t *p = static_cast<uint16_t *>(data);
        for(size_t i = 0; i < pixelCount; ++i) {
                uint16_t v = p[i];
                p[i] = static_cast<uint16_t>((v >> 8) | (v << 8));
        }
}

// ===========================================================================
// ImageFileIO_PNG
// ===========================================================================

class ImageFileIO_PNG : public ImageFileIO {
        public:
                ImageFileIO_PNG() {
                        _id = ImageFile::PNG;
                        _canLoad = true;
                        _canSave = true;
                        _name = "PNG";
                }

                Error load(ImageFile &imageFile, const MediaConfig &config) const override;
                Error save(ImageFile &imageFile, const MediaConfig &config) const override;
};
PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_PNG);

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

Error ImageFileIO_PNG::save(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const Image image = imageFile.image();
        const String &filename = imageFile.filename();

        if(!image.isValid()) {
                promekiErr("PNG save '%s': image is not valid", filename.cstr());
                return Error::Invalid;
        }

        PngFormat pf;
        if(!pngFormatFromPixelDesc(image.pixelDesc().id(), pf)) {
                promekiErr("PNG save '%s': pixel format '%s' not supported",
                           filename.cstr(), image.pixelDesc().name().cstr());
                return Error::PixelFormatNotSupported;
        }

        const size_t width        = image.width();
        const size_t height       = image.height();
        const size_t bytesPerPx   = static_cast<size_t>(pf.bitDepth / 8) * pf.channels;
        const size_t packedStride = width * bytesPerPx;
        const size_t packedSize   = packedStride * height;
        const size_t lineStride   = image.lineStride(0);

        // libspng's encoder reads its input as one tightly-packed buffer.
        // If the source image has row padding (lineStride != packedStride)
        // or we need a 16-bit byte swap, repack into a scratch buffer.
        // Otherwise, encode directly from the image plane.
        Buffer packedBuf;
        const void *pixelData = image.data(0);
        if(pf.swap16 || lineStride != packedStride) {
                packedBuf = Buffer(packedSize);
                const uint8_t *src = static_cast<const uint8_t *>(image.data(0));
                uint8_t *dst       = static_cast<uint8_t *>(packedBuf.data());
                for(size_t y = 0; y < height; ++y) {
                        std::memcpy(dst + y * packedStride,
                                    src + y * lineStride,
                                    packedStride);
                }
                if(pf.swap16) {
                        swap16InPlace(packedBuf.data(), packedSize / 2);
                }
                pixelData = packedBuf.data();
        }

        spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
        if(!ctx) {
                promekiErr("PNG save '%s': spng_ctx_new failed", filename.cstr());
                return Error::NoMem;
        }

        int sret = spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);
        if(sret) {
                promekiErr("PNG save '%s': spng_set_option(ENCODE_TO_BUFFER) failed: %s",
                           filename.cstr(), spng_strerror(sret));
                spng_ctx_free(ctx);
                return Error::EncodeFailed;
        }

        spng_ihdr ihdr = {};
        ihdr.width              = static_cast<uint32_t>(width);
        ihdr.height             = static_cast<uint32_t>(height);
        ihdr.bit_depth          = pf.bitDepth;
        ihdr.color_type         = pf.colorType;
        ihdr.compression_method = 0;
        ihdr.filter_method      = 0;
        ihdr.interlace_method   = SPNG_INTERLACE_NONE;
        sret = spng_set_ihdr(ctx, &ihdr);
        if(sret) {
                promekiErr("PNG save '%s': spng_set_ihdr failed: %s",
                           filename.cstr(), spng_strerror(sret));
                spng_ctx_free(ctx);
                return Error::EncodeFailed;
        }

        // Color management: prefer an explicit gAMA chunk if metadata
        // carries one, otherwise tag as sRGB (perceptual intent). Every
        // PNG-supported PixelDesc in the table above is sRGB-tagged.
        if(image.metadata().contains(Metadata::Gamma)) {
                double gamma = image.metadata().get(Metadata::Gamma).get<double>();
                if(gamma > 0.0) spng_set_gama(ctx, gamma);
        } else {
                spng_set_srgb(ctx, 0); // 0 = perceptual rendering intent
        }

        // SPNG_FMT_RAW tells libspng "input is already in PNG wire format"
        // — i.e. big-endian for 16-bit samples, no conversion. SPNG_FMT_PNG
        // would treat the input as host-endian and byte-swap 16-bit data
        // into BE on its way out, which would corrupt our BE PixelDescs and
        // unwind the explicit LE→BE swap we just did for the LE variants.
        sret = spng_encode_image(ctx, pixelData, packedSize, SPNG_FMT_RAW, SPNG_ENCODE_FINALIZE);
        if(sret) {
                promekiErr("PNG save '%s': spng_encode_image failed: %s",
                           filename.cstr(), spng_strerror(sret));
                spng_ctx_free(ctx);
                return Error::EncodeFailed;
        }

        // Take ownership of the encoded buffer. We must std::free() it.
        size_t pngLen = 0;
        int    getErr = 0;
        void  *pngBuf = spng_get_png_buffer(ctx, &pngLen, &getErr);
        if(!pngBuf || getErr) {
                promekiErr("PNG save '%s': spng_get_png_buffer failed: %s",
                           filename.cstr(), spng_strerror(getErr));
                if(pngBuf) std::free(pngBuf);
                spng_ctx_free(ctx);
                return Error::EncodeFailed;
        }
        // The encoder context is no longer needed.
        spng_ctx_free(ctx);

        // Write to disk via promeki::File. For image-sequence workloads we
        // want O_DIRECT so back-to-back frames don't churn the page cache.
        // O_DIRECT requires aligned buffer + aligned size, so we copy the
        // libspng output into an aligned Buffer padded up to alignment, do
        // one big aligned write, then truncate the trailing zero pad.
        File file(filename);
        file.setDirectIO(true);
        Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
        if(err.isError()) {
                promekiDebug("PNG save '%s': DIO open failed, falling back to normal I/O", filename.cstr());
                file.setDirectIO(false);
                err = file.open(File::WriteOnly, File::Create | File::Truncate);
                if(err.isError()) {
                        promekiErr("PNG save '%s': %s", filename.cstr(), err.name().cstr());
                        std::free(pngBuf);
                        return err;
                }
        }

        size_t bufAlign = PNG_DIO_FALLBACK_ALIGN;
        if(file.isDirectIO()) {
                auto alignResult = file.directIOAlignment();
                if(isOk(alignResult)) bufAlign = value(alignResult);
        }
        const size_t paddedLen = (pngLen + bufAlign - 1) & ~(bufAlign - 1);
        Buffer alignedBuf(paddedLen, bufAlign);
        if(paddedLen > pngLen) {
                std::memset(static_cast<uint8_t *>(alignedBuf.data()) + pngLen, 0, paddedLen - pngLen);
        }
        std::memcpy(alignedBuf.data(), pngBuf, pngLen);
        std::free(pngBuf);

        int64_t written = file.write(alignedBuf.data(), static_cast<int64_t>(paddedLen));
        if(written != static_cast<int64_t>(paddedLen)) {
                promekiErr("PNG save '%s': short write (%lld of %zu)",
                           filename.cstr(), (long long)written, paddedLen);
                file.close();
                return Error::IOError;
        }

        if(paddedLen > pngLen) {
                err = file.truncate(static_cast<int64_t>(pngLen));
                if(err.isError()) {
                        promekiErr("PNG save '%s': truncate to %zu failed: %s",
                                   filename.cstr(), pngLen, err.name().cstr());
                        file.close();
                        return err;
                }
        }

        file.close();
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

Error ImageFileIO_PNG::load(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        // Read the entire file in one shot via the bulk-direct-I/O pattern.
        // PNGs are small relative to bulk media payloads, so the cost is
        // negligible and the codec gets to operate on a contiguous buffer.
        File file(filename);
        Error err = file.open(File::ReadOnly);
        if(err.isError()) {
                promekiErr("PNG load '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        auto sizeResult = file.size();
        if(!isOk(sizeResult)) {
                file.close();
                return error(sizeResult);
        }
        int64_t fileSize = value(sizeResult);
        if(fileSize <= 8) { // PNG signature alone is 8 bytes
                file.close();
                promekiErr("PNG load '%s': file too small (%lld bytes)",
                           filename.cstr(), (long long)fileSize);
                return Error::CorruptData;
        }

        auto alignResult = file.directIOAlignment();
        size_t bufAlign = isOk(alignResult) ? value(alignResult) : PNG_DIO_FALLBACK_ALIGN;
        Buffer fileBuf(static_cast<size_t>(fileSize) + bufAlign, bufAlign);

        err = file.readBulk(fileBuf, fileSize);
        file.close();
        if(err.isError()) {
                promekiErr("PNG load '%s': read failed: %s",
                           filename.cstr(), err.name().cstr());
                return err;
        }

        spng_ctx *ctx = spng_ctx_new(0);
        if(!ctx) {
                promekiErr("PNG load '%s': spng_ctx_new failed", filename.cstr());
                return Error::NoMem;
        }

        // Validate critical-chunk CRCs (the IDAT and IHDR chunks); skip the
        // ancillary CRCs for speed. SPNG_CRC_USE on ancillaries also skips
        // checksum validation in the deflate stream itself.
        spng_set_crc_action(ctx, SPNG_CRC_ERROR, SPNG_CRC_USE);

        int sret = spng_set_png_buffer(ctx, fileBuf.data(), fileBuf.size());
        if(sret) {
                promekiErr("PNG load '%s': spng_set_png_buffer failed: %s",
                           filename.cstr(), spng_strerror(sret));
                spng_ctx_free(ctx);
                return Error::CorruptData;
        }

        spng_ihdr ihdr;
        sret = spng_get_ihdr(ctx, &ihdr);
        if(sret) {
                promekiErr("PNG load '%s': spng_get_ihdr failed: %s",
                           filename.cstr(), spng_strerror(sret));
                spng_ctx_free(ctx);
                return Error::CorruptData;
        }

        int spngFmt = 0;
        PixelDesc::ID pdId = pixelDescFromIhdr(ihdr, spngFmt);
        if(pdId == PixelDesc::Invalid) {
                promekiErr("PNG load '%s': unsupported PNG format (color_type=%d, bit_depth=%d)",
                           filename.cstr(), ihdr.color_type, ihdr.bit_depth);
                spng_ctx_free(ctx);
                return Error::PixelFormatNotSupported;
        }

        size_t decodedSize = 0;
        sret = spng_decoded_image_size(ctx, spngFmt, &decodedSize);
        if(sret) {
                promekiErr("PNG load '%s': spng_decoded_image_size failed: %s",
                           filename.cstr(), spng_strerror(sret));
                spng_ctx_free(ctx);
                return Error::DecodeFailed;
        }

        Image image(ihdr.width, ihdr.height, PixelDesc(pdId));
        if(!image.isValid()) {
                spng_ctx_free(ctx);
                promekiErr("PNG load '%s': failed to allocate %ux%u image",
                           filename.cstr(), ihdr.width, ihdr.height);
                return Error::NoMem;
        }

        // libspng decodes into a tightly-packed contiguous buffer. The
        // proav Image plane is also tightly packed (no row padding) for
        // every PixelDesc we map here, so the sizes must match exactly.
        const size_t planeBytes = image.lineStride(0) * ihdr.height;
        if(decodedSize != planeBytes) {
                spng_ctx_free(ctx);
                promekiErr("PNG load '%s': decoded size %zu != plane size %zu (stride mismatch)",
                           filename.cstr(), decodedSize, planeBytes);
                return Error::BufferTooSmall;
        }

        sret = spng_decode_image(ctx, image.data(0), decodedSize, spngFmt, 0);
        if(sret) {
                promekiErr("PNG load '%s': spng_decode_image failed: %s",
                           filename.cstr(), spng_strerror(sret));
                spng_ctx_free(ctx);
                return Error::DecodeFailed;
        }

        // Color management: an sRGB chunk wins (the PixelDesc already
        // carries that color model). Otherwise, if a gAMA chunk is
        // present, stash it in metadata. With neither chunk present we
        // assume sRGB per the PNG spec recommendation, which matches the
        // PixelDesc default — nothing to do.
        uint8_t srgbIntent = 0;
        if(spng_get_srgb(ctx, &srgbIntent) != SPNG_OK) {
                double gamma = 0.0;
                if(spng_get_gama(ctx, &gamma) == SPNG_OK && gamma > 0.0) {
                        image.metadata().set(Metadata::Gamma, gamma);
                }
        }

        spng_ctx_free(ctx);

        imageFile.setImage(image);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
