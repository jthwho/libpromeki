/**
 * @file      imagefileio_tga.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/logger.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/file.h>
#include <promeki/buffer.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/videopayload.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(TGA)

// ===========================================================================
// TGA header (18 bytes)
// ===========================================================================

#pragma pack(push, 1)
struct TGAHeader {
                uint8_t  idlen;     // Image ID field length
                uint8_t  cmaptype;  // 0 = no colormap
                uint8_t  imgtype;   // 2 = uncompressed true-color
                uint16_t cmapstart; // Colormap first entry index
                uint16_t cmaplen;   // Colormap entry count
                uint8_t  cmapsz;    // Colormap entry size in bits
                uint16_t xorigin;   // Image X origin
                uint16_t yorigin;   // Image Y origin
                uint16_t width;     // Image width
                uint16_t height;    // Image height
                uint8_t  bpp;       // Bits per pixel (24 or 32)
                uint8_t  imgdesc;   // Image descriptor
};
#pragma pack(pop)

static_assert(sizeof(TGAHeader) == 18, "TGA header must be 18 bytes");

// imgdesc flags
static constexpr uint8_t TGA_ORIGIN_TOP = 0x20;
// static constexpr uint8_t TGA_ORIGIN_RIGHT = 0x10;  // Not commonly used

// ===========================================================================
// ImageFileIO_TGA
// ===========================================================================

class ImageFileIO_TGA : public ImageFileIO {
        public:
                ImageFileIO_TGA() {
                        _id = ImageFile::TGA;
                        _canLoad = true;
                        _canSave = true;
                        _name = "TGA";
                        _description = "Truevision TGA image sequence";
                        _extensions = {"tga"};
                }
                Error load(ImageFile &imageFile, const MediaConfig &config) const override;
                Error save(ImageFile &imageFile, const MediaConfig &config) const override;
};
PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_TGA);

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

Error ImageFileIO_TGA::load(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        File  file(filename);
        Error err = file.open(File::ReadOnly);
        if (err.isError()) {
                promekiErr("TGA load '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        // Read header
        TGAHeader hdr;
        int64_t   n = file.read(&hdr, sizeof(TGAHeader));
        if (n != sizeof(TGAHeader)) {
                promekiErr("TGA load '%s': short header read", filename.cstr());
                file.close();
                return Error::IOError;
        }

        // Validate
        if (hdr.imgtype != 2) {
                promekiErr("TGA load '%s': unsupported image type %d (only uncompressed true-color supported)",
                           filename.cstr(), hdr.imgtype);
                file.close();
                return Error::NotSupported;
        }
        if (hdr.cmaptype != 0) {
                promekiErr("TGA load '%s': colormapped images not supported", filename.cstr());
                file.close();
                return Error::NotSupported;
        }
        if (hdr.bpp != 24 && hdr.bpp != 32) {
                promekiErr("TGA load '%s': unsupported bit depth %d", filename.cstr(), hdr.bpp);
                file.close();
                return Error::PixelFormatNotSupported;
        }

        // Skip image ID field
        if (hdr.idlen > 0) {
                file.seek(sizeof(TGAHeader) + hdr.idlen);
        }

        size_t w = hdr.width;
        size_t h = hdr.height;
        size_t srcBpp = hdr.bpp / 8; // 3 or 4
        size_t rawSize = w * h * srcBpp;

        // Read raw pixel data
        Buffer rawBuf(rawSize);
        n = file.read(rawBuf.data(), static_cast<int64_t>(rawSize));
        file.close();
        if (n != static_cast<int64_t>(rawSize)) {
                promekiErr("TGA load '%s': short data read", filename.cstr());
                return Error::IOError;
        }

        // Allocate RGBA8 payload
        ImageDesc idesc(w, h, PixelFormat::RGBA8_sRGB);
        auto      payload = UncompressedVideoPayload::allocate(idesc);
        if (!payload.isValid()) {
                promekiErr("TGA load '%s': failed to allocate payload", filename.cstr());
                return Error::NoMem;
        }

        const uint8_t *src = static_cast<const uint8_t *>(rawBuf.data());
        uint8_t       *dst = payload.modify()->data()[0].data();
        bool           needFlip = !(hdr.imgdesc & TGA_ORIGIN_TOP); // bottom-up → flip
        bool           hasAlpha = (srcBpp == 4);

        if (needFlip) {
                // Process bottom-to-top: write scanlines in reverse order
                for (size_t y = 0; y < h; ++y) {
                        const uint8_t *srcRow = src + (h - 1 - y) * w * srcBpp;
                        uint8_t       *dstRow = dst + y * w * 4;
                        for (size_t x = 0; x < w; ++x) {
                                dstRow[x * 4 + 0] = srcRow[x * srcBpp + 2]; // B → R
                                dstRow[x * 4 + 1] = srcRow[x * srcBpp + 1]; // G → G
                                dstRow[x * 4 + 2] = srcRow[x * srcBpp + 0]; // R → B
                                dstRow[x * 4 + 3] = hasAlpha ? srcRow[x * srcBpp + 3] : 0xFF;
                        }
                }
        } else {
                // Top-down: process in order
                size_t pixelCount = w * h;
                for (size_t i = 0; i < pixelCount; ++i) {
                        dst[i * 4 + 0] = src[i * srcBpp + 2]; // B → R
                        dst[i * 4 + 1] = src[i * srcBpp + 1]; // G → G
                        dst[i * 4 + 2] = src[i * srcBpp + 0]; // R → B
                        dst[i * 4 + 3] = hasAlpha ? src[i * srcBpp + 3] : 0xFF;
                }
        }

        Frame frame;
        frame.addPayload(payload);
        imageFile.setFrame(frame);
        promekiDebug("TGA load '%s': %zux%zu RGBA8 (%d-bit%s)", filename.cstr(), w, h, hdr.bpp,
                     needFlip ? ", flipped" : "");
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

Error ImageFileIO_TGA::save(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        VideoPayload::PtrList           vps = imageFile.frame().videoPayloads();
        const UncompressedVideoPayload *uvp = nullptr;
        if (!vps.isEmpty() && vps[0].isValid()) uvp = vps[0]->as<UncompressedVideoPayload>();
        if (uvp == nullptr || !uvp->desc().isValid() || uvp->planeCount() == 0) {
                promekiErr("TGA save '%s': no uncompressed video payload", filename.cstr());
                return Error::Invalid;
        }
        const ImageDesc &idesc = uvp->desc();
        if (idesc.pixelFormat().id() != PixelFormat::RGBA8_sRGB) {
                promekiErr("TGA save '%s': only RGBA8 supported, got '%s'", filename.cstr(),
                           idesc.pixelFormat().name().cstr());
                return Error::PixelFormatNotSupported;
        }

        size_t w = idesc.size().width();
        size_t h = idesc.size().height();
        auto   plane0 = uvp->plane(0);

        // Build header
        TGAHeader hdr;
        std::memset(&hdr, 0, sizeof(hdr));
        hdr.imgtype = 2; // Uncompressed true-color
        hdr.width = static_cast<uint16_t>(w);
        hdr.height = static_cast<uint16_t>(h);
        hdr.bpp = 32;
        hdr.imgdesc = TGA_ORIGIN_TOP | 0x08; // Top-left origin, 8 alpha bits

        // Convert RGBA → BGRA
        size_t         pixelCount = w * h;
        Buffer         outBuf(pixelCount * 4);
        const uint8_t *src = plane0.data();
        uint8_t       *dst = static_cast<uint8_t *>(outBuf.data());
        for (size_t i = 0; i < pixelCount; ++i) {
                dst[i * 4 + 0] = src[i * 4 + 2]; // R → B
                dst[i * 4 + 1] = src[i * 4 + 1]; // G → G
                dst[i * 4 + 2] = src[i * 4 + 0]; // B → R
                dst[i * 4 + 3] = src[i * 4 + 3]; // A → A
        }

        File  file(filename);
        Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
        if (err.isError()) {
                promekiErr("TGA save '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        int64_t written = file.write(&hdr, sizeof(hdr));
        if (written != sizeof(hdr)) {
                file.close();
                return Error::IOError;
        }

        written = file.write(outBuf.data(), static_cast<int64_t>(pixelCount * 4));
        if (written != static_cast<int64_t>(pixelCount * 4)) {
                file.close();
                return Error::IOError;
        }

        file.close();
        promekiDebug("TGA save '%s': %zux%zu RGBA8", filename.cstr(), w, h);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
