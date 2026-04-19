/**
 * @file      imagefileio_pnm.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <promeki/logger.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/file.h>
#include <promeki/buffer.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(PNM)

// ===========================================================================
// PNM format types
// ===========================================================================

enum PNMType {
        PNM_Invalid = 0,
        PBM_ASCII,      // P1
        PGM_ASCII,      // P2
        PPM_ASCII,      // P3
        PGM_BINARY,     // P5
        PPM_BINARY      // P6
};

static PNMType pnmTypeFromMagic(const char *magic) {
        if(magic[0] != 'P') return PNM_Invalid;
        switch(magic[1]) {
                case '1': return PBM_ASCII;
                case '2': return PGM_ASCII;
                case '3': return PPM_ASCII;
                case '5': return PGM_BINARY;
                case '6': return PPM_BINARY;
                default:  return PNM_Invalid;
        }
}

static bool pnmIsBinary(PNMType type) {
        return type == PGM_BINARY || type == PPM_BINARY;
}

static bool pnmIsColor(PNMType type) {
        return type == PPM_ASCII || type == PPM_BINARY;
}

// ===========================================================================
// ASCII parsing helpers
// ===========================================================================

// Skip whitespace and comments in PNM header.
// Returns pointer past the skipped region, or nullptr on end of data.
static const char *skipWhitespaceAndComments(const char *p, const char *end) {
        while(p < end) {
                if(*p == '#') {
                        // Skip to end of line
                        while(p < end && *p != '\n') ++p;
                        if(p < end) ++p; // skip the newline
                } else if(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
                        ++p;
                } else {
                        return p;
                }
        }
        return p;
}

// Parse an unsigned integer from ASCII text.
// Returns pointer past the parsed number, or nullptr on failure.
static const char *parseUInt(const char *p, const char *end, unsigned int &out) {
        p = skipWhitespaceAndComments(p, end);
        if(p >= end || *p < '0' || *p > '9') return nullptr;
        out = 0;
        while(p < end && *p >= '0' && *p <= '9') {
                out = out * 10 + (*p - '0');
                ++p;
        }
        return p;
}

// ===========================================================================
// ImageFileIO_PNM
// ===========================================================================

class ImageFileIO_PNM : public ImageFileIO {
        public:
                ImageFileIO_PNM() {
                        _id = ImageFile::PNM;
                        _canLoad = true;
                        _canSave = true;
                        _name = "PNM";
                        _description = "Netpbm PNM/PPM/PGM image sequence";
                        _extensions = { "pnm", "ppm", "pgm" };
                }
                Error load(ImageFile &imageFile, const MediaConfig &config) const override;
                Error save(ImageFile &imageFile, const MediaConfig &config) const override;
};
PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_PNM);

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

Error ImageFileIO_PNM::load(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        // Read entire file into memory for easy parsing
        File file(filename);
        Error err = file.open(File::ReadOnly);
        if(err.isError()) {
                promekiErr("PNM load '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }
        auto sizeResult = file.size();
        if(!isOk(sizeResult)) { file.close(); return error(sizeResult); }
        int64_t fileSize = value(sizeResult);

        Buffer fileBuf(static_cast<size_t>(fileSize));
        int64_t n = file.read(fileBuf.data(), fileSize);
        file.close();
        if(n != fileSize) {
                promekiErr("PNM load '%s': short read", filename.cstr());
                return Error::IOError;
        }

        const char *p = static_cast<const char *>(fileBuf.data());
        const char *end = p + fileSize;

        // Parse magic
        if(fileSize < 3 || p[0] != 'P') {
                promekiErr("PNM load '%s': invalid magic", filename.cstr());
                return Error::Invalid;
        }
        char magic[3] = { p[0], p[1], 0 };
        PNMType type = pnmTypeFromMagic(magic);
        if(type == PNM_Invalid) {
                promekiErr("PNM load '%s': unsupported PNM type '%s'", filename.cstr(), magic);
                return Error::NotSupported;
        }
        p += 2;

        // Parse width, height
        unsigned int width = 0, height = 0, maxval = 255;
        p = parseUInt(p, end, width);
        if(!p) { promekiErr("PNM load '%s': missing width", filename.cstr()); return Error::Invalid; }
        p = parseUInt(p, end, height);
        if(!p) { promekiErr("PNM load '%s': missing height", filename.cstr()); return Error::Invalid; }

        // Parse maxval (absent for PBM)
        if(type != PBM_ASCII) {
                p = parseUInt(p, end, maxval);
                if(!p) { promekiErr("PNM load '%s': missing maxval", filename.cstr()); return Error::Invalid; }
        }

        // Skip exactly one whitespace character after the header
        if(p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;

        // Determine pixel format
        PixelDesc::ID pdId;
        if(pnmIsColor(type)) {
                pdId = (maxval >= 256) ? PixelDesc::RGB16_BE_sRGB : PixelDesc::RGB8_sRGB;
        } else {
                pdId = (maxval >= 256) ? PixelDesc::Mono16_BE_sRGB : PixelDesc::Mono8_sRGB;
        }

        Image image(width, height, PixelDesc(pdId));
        if(!image.isValid()) {
                promekiErr("PNM load '%s': failed to allocate image", filename.cstr());
                return Error::NoMem;
        }

        size_t imageBytes = image.pixelDesc().pixelFormat().planeSize(0, width, height);

        if(pnmIsBinary(type)) {
                // Binary: direct copy
                size_t avail = static_cast<size_t>(end - p);
                size_t toCopy = (avail < imageBytes) ? avail : imageBytes;
                std::memcpy(image.data(), p, toCopy);
        } else {
                // ASCII: parse integers
                uint8_t *dst8 = static_cast<uint8_t *>(image.data());
                uint16_t *dst16 = static_cast<uint16_t *>(image.data());
                size_t pixels = imageBytes;
                if(maxval >= 256) pixels = imageBytes / 2;

                for(size_t i = 0; i < pixels; ++i) {
                        unsigned int val = 0;
                        p = parseUInt(p, end, val);
                        if(!p) break;
                        if(type == PBM_ASCII) {
                                dst8[i] = (val == 0) ? 255 : 0; // PBM: 0=white, 1=black
                        } else if(maxval >= 256) {
                                dst16[i] = static_cast<uint16_t>(val);
                        } else {
                                dst8[i] = static_cast<uint8_t>(val);
                        }
                }
        }

        Frame frame;
        frame.imageList().pushToBack(Image::Ptr::create(image));
        imageFile.setFrame(frame);
        promekiDebug("PNM load '%s': %ux%u %s (%s)", filename.cstr(), width, height,
                     PixelDesc(pdId).name().cstr(), magic);
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

Error ImageFileIO_PNM::save(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const Image image = imageFile.image();
        const String &filename = imageFile.filename();

        if(!image.isValid()) {
                promekiErr("PNM save '%s': image is not valid", filename.cstr());
                return Error::Invalid;
        }

        // Determine PNM type from pixel format
        const char *magic;
        unsigned int maxval;
        PixelDesc::ID pdId = image.pixelDesc().id();

        switch(pdId) {
                case PixelDesc::RGB8_sRGB:
                        magic = "P6"; maxval = 255; break;
                case PixelDesc::RGB16_BE_sRGB:
                        magic = "P6"; maxval = 65535; break;
                case PixelDesc::Mono8_sRGB:
                        magic = "P5"; maxval = 255; break;
                case PixelDesc::Mono16_BE_sRGB:
                        magic = "P5"; maxval = 65535; break;
                default:
                        promekiErr("PNM save '%s': unsupported pixel format '%s'",
                                   filename.cstr(), image.pixelDesc().name().cstr());
                        return Error::PixelFormatNotSupported;
        }

        // Build header
        char header[64];
        int headerLen = std::snprintf(header, sizeof(header), "%s\n%zu %zu\n%u\n",
                                      magic, image.width(), image.height(), maxval);

        size_t imageBytes = image.pixelDesc().pixelFormat().planeSize(0, image.width(), image.height());

        File file(filename);
        Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
        if(err.isError()) {
                promekiErr("PNM save '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        // Write header
        int64_t written = file.write(header, headerLen);
        if(written != headerLen) { file.close(); return Error::IOError; }

        // Write binary pixel data
        written = file.write(image.data(), static_cast<int64_t>(imageBytes));
        if(written != static_cast<int64_t>(imageBytes)) { file.close(); return Error::IOError; }

        file.close();
        promekiDebug("PNM save '%s': %zux%zu %s", filename.cstr(),
                     image.width(), image.height(), image.pixelDesc().name().cstr());
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
