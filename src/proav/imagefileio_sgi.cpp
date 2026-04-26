/**
 * @file      imagefileio_sgi.cpp
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

PROMEKI_DEBUG(SGI)

// ===========================================================================
// SGI header (512 bytes)
// ===========================================================================

static constexpr int16_t SGI_MAGIC = 474;

#pragma pack(push, 1)
struct SGIHeader {
                int16_t  magic;   // 474
                uint8_t  storage; // 0=raw, 1=RLE
                uint8_t  bpc;     // Bytes per channel (1 or 2)
                uint16_t dim;     // Number of dimensions
                uint16_t xSize;   // Width
                uint16_t ySize;   // Height
                uint16_t zSize;   // Channels
                int32_t  pixmin;
                int32_t  pixmax;
                char     _pad1[4];
                char     imageName[80];
                int32_t  colorMap;
                char     _pad2[404];
};
#pragma pack(pop)

static_assert(sizeof(SGIHeader) == 512, "SGI header must be 512 bytes");

// ===========================================================================
// Endian helpers (SGI is big-endian)
// ===========================================================================

static inline void endflip2(char *to, const char *from, int off) {
        to[off] = from[off + 1];
        to[off + 1] = from[off];
}

static inline void endflip4(char *to, const char *from, int off) {
        to[off] = from[off + 3];
        to[off + 1] = from[off + 2];
        to[off + 2] = from[off + 1];
        to[off + 3] = from[off];
}

static void sgiFlipHeader(SGIHeader *dst, const SGIHeader *src) {
        char       *t = reinterpret_cast<char *>(dst);
        const char *f = reinterpret_cast<const char *>(src);
        std::memcpy(t, f, sizeof(SGIHeader));
        endflip2(t, f, 0); // magic
        // storage, bpc are single bytes — no flip
        endflip2(t, f, 4);  // dim
        endflip2(t, f, 6);  // xSize
        endflip2(t, f, 8);  // ySize
        endflip2(t, f, 10); // zSize
        endflip4(t, f, 12); // pixmin
        endflip4(t, f, 16); // pixmax
        // imageName, pad — no flip
        endflip4(t, f, 100); // colorMap (offset 4+80+4+4+4+4 = see struct)
}

static inline uint32_t rev4(uint32_t n) {
        return ((n & 0xFF000000) >> 24) | ((n & 0x00FF0000) >> 8) | ((n & 0x0000FF00) << 8) | ((n & 0x000000FF) << 24);
}

static inline uint16_t rev2(uint16_t n) {
        return static_cast<uint16_t>(((n & 0xFF00) >> 8) | ((n & 0x00FF) << 8));
}

static bool isLittleEndian() {
        uint16_t val = 1;
        return *reinterpret_cast<uint8_t *>(&val) == 1;
}

// ===========================================================================
// Pixel format mapping
// ===========================================================================

static PixelFormat::ID sgiPixelFormat(uint8_t bpc, uint16_t zSize) {
        if (bpc == 1) {
                switch (zSize) {
                        case 1: return PixelFormat::Mono8_sRGB;
                        case 3: return PixelFormat::RGB8_sRGB;
                        case 4: return PixelFormat::RGBA8_sRGB;
                }
        } else if (bpc == 2) {
                switch (zSize) {
                        case 1: return PixelFormat::Mono16_BE_sRGB;
                        case 3: return PixelFormat::RGB16_BE_sRGB;
                        case 4: return PixelFormat::RGBA16_BE_sRGB;
                }
        }
        return PixelFormat::Invalid;
}

static bool sgiFormatParams(PixelFormat::ID id, uint8_t &bpc, uint16_t &zSize) {
        switch (id) {
                case PixelFormat::Mono8_sRGB:
                        bpc = 1;
                        zSize = 1;
                        return true;
                case PixelFormat::RGB8_sRGB:
                        bpc = 1;
                        zSize = 3;
                        return true;
                case PixelFormat::RGBA8_sRGB:
                        bpc = 1;
                        zSize = 4;
                        return true;
                case PixelFormat::Mono16_BE_sRGB:
                        bpc = 2;
                        zSize = 1;
                        return true;
                case PixelFormat::RGB16_BE_sRGB:
                        bpc = 2;
                        zSize = 3;
                        return true;
                case PixelFormat::RGBA16_BE_sRGB:
                        bpc = 2;
                        zSize = 4;
                        return true;
                default: return false;
        }
}

// ===========================================================================
// RLE codec (8-bit)
// ===========================================================================

static void expandRow8(uint8_t *dst, const uint8_t *src, size_t maxDst) {
        uint8_t *dstEnd = dst + maxDst;
        while (dst < dstEnd) {
                uint8_t pixel = *src++;
                int     count = pixel & 0x7F;
                if (count == 0) return;
                if (pixel & 0x80) {
                        // Literal run
                        while (count-- > 0 && dst < dstEnd) *dst++ = *src++;
                } else {
                        // Repeat run
                        uint8_t val = *src++;
                        while (count-- > 0 && dst < dstEnd) *dst++ = val;
                }
        }
}

// ===========================================================================
// RLE codec (16-bit)
// ===========================================================================

static void expandRow16(uint16_t *dst, const uint16_t *src, size_t maxDst) {
        uint16_t *dstEnd = dst + maxDst;
        bool      le = isLittleEndian();
        while (dst < dstEnd) {
                uint16_t pixel = *src++;
                int      count = le ? ((pixel & 0x7F00) >> 8) : (pixel & 0x7F);
                if (count == 0) return;
                bool isLiteral = le ? (pixel & 0x8000) : (pixel & 0x80);
                if (isLiteral) {
                        while (count-- > 0 && dst < dstEnd) *dst++ = *src++;
                } else {
                        uint16_t val = *src++;
                        while (count-- > 0 && dst < dstEnd) *dst++ = val;
                }
        }
}

// ===========================================================================
// Planar ↔ interleaved conversion
// ===========================================================================

// Deplanarize: SGI stores channels separately (RRRR...GGGG...BBBB...)
// with scanlines bottom-to-top. Convert to interleaved top-to-bottom.
static void deplanarize8(uint8_t *dst, const uint8_t *planar, size_t w, size_t h, uint16_t zSize) {
        size_t planeSize = w * h;
        for (size_t y = 0; y < h; ++y) {
                size_t sgiRow = h - 1 - y; // SGI row 0 = bottom
                for (size_t x = 0; x < w; ++x) {
                        size_t srcIdx = sgiRow * w + x;
                        size_t dstIdx = (y * w + x) * zSize;
                        for (uint16_t c = 0; c < zSize; ++c) {
                                dst[dstIdx + c] = planar[c * planeSize + srcIdx];
                        }
                }
        }
}

static void deplanarize16(uint16_t *dst, const uint16_t *planar, size_t w, size_t h, uint16_t zSize) {
        size_t planeSize = w * h;
        for (size_t y = 0; y < h; ++y) {
                size_t sgiRow = h - 1 - y;
                for (size_t x = 0; x < w; ++x) {
                        size_t srcIdx = sgiRow * w + x;
                        size_t dstIdx = (y * w + x) * zSize;
                        for (uint16_t c = 0; c < zSize; ++c) {
                                dst[dstIdx + c] = planar[c * planeSize + srcIdx];
                        }
                }
        }
}

// Planarize: Convert interleaved top-to-bottom to planar bottom-to-top.
static void planarize8(uint8_t *planar, const uint8_t *src, size_t w, size_t h, uint16_t zSize) {
        size_t planeSize = w * h;
        for (size_t y = 0; y < h; ++y) {
                size_t sgiRow = h - 1 - y;
                for (size_t x = 0; x < w; ++x) {
                        size_t srcIdx = (y * w + x) * zSize;
                        size_t dstIdx = sgiRow * w + x;
                        for (uint16_t c = 0; c < zSize; ++c) {
                                planar[c * planeSize + dstIdx] = src[srcIdx + c];
                        }
                }
        }
}

static void planarize16(uint16_t *planar, const uint16_t *src, size_t w, size_t h, uint16_t zSize) {
        size_t planeSize = w * h;
        for (size_t y = 0; y < h; ++y) {
                size_t sgiRow = h - 1 - y;
                for (size_t x = 0; x < w; ++x) {
                        size_t srcIdx = (y * w + x) * zSize;
                        size_t dstIdx = sgiRow * w + x;
                        for (uint16_t c = 0; c < zSize; ++c) {
                                planar[c * planeSize + dstIdx] = src[srcIdx + c];
                        }
                }
        }
}

// ===========================================================================
// ImageFileIO_SGI
// ===========================================================================

class ImageFileIO_SGI : public ImageFileIO {
        public:
                ImageFileIO_SGI() {
                        _id = ImageFile::SGI;
                        _canLoad = true;
                        _canSave = true;
                        _name = "SGI";
                        _description = "Silicon Graphics SGI image sequence";
                        _extensions = {"sgi", "rgb"};
                }
                Error load(ImageFile &imageFile, const MediaConfig &config) const override;
                Error save(ImageFile &imageFile, const MediaConfig &config) const override;
};
PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_SGI);

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

Error ImageFileIO_SGI::load(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        File  file(filename);
        Error err = file.open(File::ReadOnly);
        if (err.isError()) {
                promekiErr("SGI load '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        // Read header
        SGIHeader hdr;
        int64_t   n = file.read(&hdr, sizeof(SGIHeader));
        if (n != sizeof(SGIHeader)) {
                promekiErr("SGI load '%s': short header read", filename.cstr());
                file.close();
                return Error::IOError;
        }

        // Check magic and flip endianness if needed
        if (hdr.magic != SGI_MAGIC) {
                SGIHeader flipped;
                sgiFlipHeader(&flipped, &hdr);
                hdr = flipped;
        }
        if (hdr.magic != SGI_MAGIC) {
                promekiErr("SGI load '%s': invalid magic %d", filename.cstr(), hdr.magic);
                file.close();
                return Error::Invalid;
        }

        // Determine pixel format
        PixelFormat::ID pdId = sgiPixelFormat(hdr.bpc, hdr.zSize);
        if (pdId == PixelFormat::Invalid) {
                promekiErr("SGI load '%s': unsupported format (bpc=%d, zSize=%d)", filename.cstr(), hdr.bpc, hdr.zSize);
                file.close();
                return Error::PixelFormatNotSupported;
        }

        size_t w = hdr.xSize;
        size_t h = hdr.ySize;
        size_t z = hdr.zSize;
        size_t planarBytes = w * h * z * hdr.bpc;

        // Allocate planar buffer
        Buffer planarBuf(planarBytes);

        if (hdr.storage == 1) {
                // RLE compressed
                // Read the rest of the file
                auto sizeResult = file.size();
                if (!isOk(sizeResult)) {
                        file.close();
                        return error(sizeResult);
                }
                int64_t fileSize = value(sizeResult);
                int64_t dataSize = fileSize - sizeof(SGIHeader);
                Buffer  dataBuf(static_cast<size_t>(dataSize));
                n = file.read(dataBuf.data(), dataSize);
                file.close();
                if (n != dataSize) {
                        return Error::IOError;
                }

                // Parse offset table
                const uint32_t *offsets = static_cast<const uint32_t *>(dataBuf.data());
                bool            le = isLittleEndian();

                if (hdr.bpc == 1) {
                        for (size_t chan = 0; chan < z; ++chan) {
                                for (size_t row = 0; row < h; ++row) {
                                        size_t   tabIdx = row + chan * h;
                                        uint32_t off = offsets[tabIdx];
                                        if (le) off = rev4(off);
                                        off -= sizeof(SGIHeader); // Offset is from file start
                                        uint8_t *dst =
                                                static_cast<uint8_t *>(planarBuf.data()) + (chan * h * w) + (row * w);
                                        const uint8_t *src = static_cast<const uint8_t *>(dataBuf.data()) + off;
                                        expandRow8(dst, src, w);
                                }
                        }
                } else {
                        for (size_t chan = 0; chan < z; ++chan) {
                                for (size_t row = 0; row < h; ++row) {
                                        size_t   tabIdx = row + chan * h;
                                        uint32_t off = offsets[tabIdx];
                                        if (le) off = rev4(off);
                                        off -= sizeof(SGIHeader);
                                        uint16_t *dst =
                                                reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(planarBuf.data()) +
                                                                             (chan * h * w + row * w) * 2);
                                        const uint16_t *src = reinterpret_cast<const uint16_t *>(
                                                static_cast<const uint8_t *>(dataBuf.data()) + off);
                                        expandRow16(dst, src, w);
                                }
                        }
                }
        } else {
                // Uncompressed: read planar data directly
                n = file.read(planarBuf.data(), static_cast<int64_t>(planarBytes));
                file.close();
                if (n != static_cast<int64_t>(planarBytes)) {
                        promekiErr("SGI load '%s': short data read", filename.cstr());
                        return Error::IOError;
                }
        }

        // Allocate payload and convert planar → interleaved
        ImageDesc idesc(w, h, pdId);
        auto      payload = UncompressedVideoPayload::allocate(idesc);
        if (!payload.isValid()) {
                promekiErr("SGI load '%s': failed to allocate payload", filename.cstr());
                return Error::NoMem;
        }
        uint8_t *dst = payload.modify()->data()[0].data();

        if (hdr.bpc == 1) {
                deplanarize8(dst, static_cast<const uint8_t *>(planarBuf.data()), w, h, z);
        } else {
                deplanarize16(reinterpret_cast<uint16_t *>(dst), static_cast<const uint16_t *>(planarBuf.data()), w, h,
                              z);
        }

        Frame frame;
        frame.addPayload(payload);
        imageFile.setFrame(frame);
        promekiDebug("SGI load '%s': %zux%zu %s (%s)", filename.cstr(), w, h, PixelFormat(pdId).name().cstr(),
                     hdr.storage ? "RLE" : "raw");
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

Error ImageFileIO_SGI::save(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        VideoPayload::PtrList           vps = imageFile.frame().videoPayloads();
        const UncompressedVideoPayload *uvp = nullptr;
        if (!vps.isEmpty() && vps[0].isValid()) uvp = vps[0]->as<UncompressedVideoPayload>();
        if (uvp == nullptr || !uvp->desc().isValid() || uvp->planeCount() == 0) {
                promekiErr("SGI save '%s': no uncompressed video payload", filename.cstr());
                return Error::Invalid;
        }

        const ImageDesc &idesc = uvp->desc();
        uint8_t          bpc;
        uint16_t         zSize;
        if (!sgiFormatParams(idesc.pixelFormat().id(), bpc, zSize)) {
                promekiErr("SGI save '%s': unsupported pixel format '%s'", filename.cstr(),
                           idesc.pixelFormat().name().cstr());
                return Error::PixelFormatNotSupported;
        }

        size_t w = idesc.size().width();
        size_t h = idesc.size().height();
        auto   plane0 = uvp->plane(0);

        // Build header (always big-endian)
        SGIHeader hdr;
        std::memset(&hdr, 0, sizeof(hdr));
        hdr.magic = SGI_MAGIC;
        hdr.storage = 0; // Uncompressed (RLE write omitted for simplicity)
        hdr.bpc = bpc;
        hdr.dim = (zSize == 1) ? 2 : 3;
        hdr.xSize = static_cast<uint16_t>(w);
        hdr.ySize = static_cast<uint16_t>(h);
        hdr.zSize = zSize;
        hdr.pixmin = 0;
        hdr.pixmax = (bpc == 1) ? 255 : 65535;
        hdr.colorMap = 0;

        // Flip to big-endian if on little-endian host
        SGIHeader writeHdr;
        if (isLittleEndian()) {
                sgiFlipHeader(&writeHdr, &hdr);
        } else {
                writeHdr = hdr;
        }

        // Planarize image data
        size_t planarBytes = w * h * zSize * bpc;
        Buffer planarBuf(planarBytes);

        if (bpc == 1) {
                planarize8(static_cast<uint8_t *>(planarBuf.data()), plane0.data(), w, h, zSize);
        } else {
                planarize16(static_cast<uint16_t *>(planarBuf.data()),
                            reinterpret_cast<const uint16_t *>(plane0.data()), w, h, zSize);
        }

        File  file(filename);
        Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
        if (err.isError()) {
                promekiErr("SGI save '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        int64_t written = file.write(&writeHdr, sizeof(writeHdr));
        if (written != sizeof(writeHdr)) {
                file.close();
                return Error::IOError;
        }

        written = file.write(planarBuf.data(), static_cast<int64_t>(planarBytes));
        if (written != static_cast<int64_t>(planarBytes)) {
                file.close();
                return Error::IOError;
        }

        file.close();
        promekiDebug("SGI save '%s': %zux%zu %s", filename.cstr(), w, h, idesc.pixelFormat().name().cstr());
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
