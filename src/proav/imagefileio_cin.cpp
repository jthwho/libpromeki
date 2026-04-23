/**
 * @file      imagefileio_cin.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <cmath>
#include <promeki/logger.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/file.h>
#include <promeki/buffer.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(Cineon)

// ===========================================================================
// Cineon header structures (Kodak Cineon 4.5 specification)
// ===========================================================================

static constexpr uint32_t CINEON_MAGIC = 0x802A5FD7;

#pragma pack(push, 1)

struct CineonChanInfo {
        uint8_t  desc[2];       // Channel designator
        uint8_t  bpp;           // Bits per pixel
        uint8_t  _align;
        uint32_t width;         // Pixels per line
        uint32_t height;        // Lines per image
        float    datamin;       // Minimum data value
        float    quantmin;      // Minimum quantity represented
        float    datamax;       // Maximum data value
        float    quantmax;      // Maximum quantity represented
};

struct CineonHeader {
        // File info (168 bytes)
        struct {
                uint32_t magic;
                uint32_t offset;        // Offset to image data
                uint32_t generic;       // Generic header length
                uint32_t industry;      // Industry header length
                uint32_t user;          // User data length
                uint32_t size;          // Total file size
                char     version[8];
                char     name[100];
                char     date[12];      // yyyy:mm:dd
                char     time[12];      // hh:mm:ssxxx
                uint8_t  _reserved[36];
        } file;

        // Image info (652 bytes)
        struct {
                uint8_t         orient;
                uint8_t         nchan;
                uint8_t         _align[2];
                CineonChanInfo  chan[8];
                float           white[2];       // White point chromaticity
                float           red[2];         // Red primary
                float           green[2];       // Green primary
                float           blue[2];        // Blue primary
                char            label[200];
                uint8_t         _reserved[28];
        } img;

        // Image format (28 bytes)
        struct {
                uint8_t  interleave;    // 0=pixel, 1=line, 2=channel
                uint8_t  packing;       // 5 = 4-byte bounds, left justified
                uint8_t  issigned;
                uint8_t  isnegative;
                uint32_t eolpad;
                uint32_t eocpad;
                uint8_t  _reserved[20];
        } fmt;

        // Image orientation (376 bytes)
        struct {
                int32_t  xoff;
                int32_t  yoff;
                char     name[100];
                char     date[12];
                char     time[12];
                char     dev[64];
                char     model[32];
                char     serial[32];
                float    xpitch;
                float    ypitch;
                float    gamma;
                uint8_t  _reserved[40];
        } orient;

        // Film info (1024 bytes)
        struct {
                uint8_t  id;
                uint8_t  type;
                uint8_t  perf;
                uint8_t  _align;
                uint32_t prefix;
                uint32_t count;
                char     format[32];
                uint32_t position;
                float    fps;
                char     attr[32];
                char     slate[200];
                uint8_t  _reserved[740];
        } film;
};

#pragma pack(pop)

// ===========================================================================
// Endian helpers (Cineon is big-endian)
// ===========================================================================

static inline uint32_t flip32(uint32_t val) {
        return ((val >> 24) & 0xFF)
             | (((val >> 16) & 0xFF) <<  8)
             | (((val >>  8) & 0xFF) << 16)
             | ((val         & 0xFF) << 24);
}

static inline float flipFloat(float val) {
        uint32_t i;
        std::memcpy(&i, &val, sizeof(i));
        i = flip32(i);
        float result;
        std::memcpy(&result, &i, sizeof(result));
        return result;
}

static inline int32_t flipI32(int32_t val) {
        uint32_t u;
        std::memcpy(&u, &val, sizeof(u));
        u = flip32(u);
        int32_t result;
        std::memcpy(&result, &u, sizeof(result));
        return result;
}

static void cineonFlipHeader(CineonHeader *hdr) {
        // File info
        hdr->file.magic    = flip32(hdr->file.magic);
        hdr->file.offset   = flip32(hdr->file.offset);
        hdr->file.generic  = flip32(hdr->file.generic);
        hdr->file.industry = flip32(hdr->file.industry);
        hdr->file.user     = flip32(hdr->file.user);
        hdr->file.size     = flip32(hdr->file.size);

        // Channel info
        for(int i = 0; i < 8; ++i) {
                auto &ch = hdr->img.chan[i];
                ch.width    = flip32(ch.width);
                ch.height   = flip32(ch.height);
                ch.datamin  = flipFloat(ch.datamin);
                ch.quantmin = flipFloat(ch.quantmin);
                ch.datamax  = flipFloat(ch.datamax);
                ch.quantmax = flipFloat(ch.quantmax);
        }

        // Chromaticity
        for(int i = 0; i < 2; ++i) {
                hdr->img.white[i] = flipFloat(hdr->img.white[i]);
                hdr->img.red[i]   = flipFloat(hdr->img.red[i]);
                hdr->img.green[i] = flipFloat(hdr->img.green[i]);
                hdr->img.blue[i]  = flipFloat(hdr->img.blue[i]);
        }

        // Image orientation
        hdr->orient.xoff   = flipI32(hdr->orient.xoff);
        hdr->orient.yoff   = flipI32(hdr->orient.yoff);
        hdr->orient.xpitch = flipFloat(hdr->orient.xpitch);
        hdr->orient.ypitch = flipFloat(hdr->orient.ypitch);
        hdr->orient.gamma  = flipFloat(hdr->orient.gamma);

        // Film info
        hdr->film.prefix   = flip32(hdr->film.prefix);
        hdr->film.count    = flip32(hdr->film.count);
        hdr->film.position = flip32(hdr->film.position);
        hdr->film.fps      = flipFloat(hdr->film.fps);
}

// ===========================================================================
// Metadata extraction
// ===========================================================================

static void setMetaStr(Metadata &meta, const Metadata::ID &id, const char *str, size_t maxLen) {
        if(str[0] == 0 || static_cast<uint8_t>(str[0]) == 0xFF) return;
        size_t len = ::strnlen(str, maxLen);
        meta.set(id, String(str, len));
}

static void extractCineonMetadata(Metadata &meta, const CineonHeader *hdr) {
        setMetaStr(meta, Metadata::FileOrigName, hdr->file.name, sizeof(hdr->file.name));

        // Combine date + time
        if(hdr->file.date[0] != 0 && static_cast<uint8_t>(hdr->file.date[0]) != 0xFF) {
                char datetime[32];
                std::snprintf(datetime, sizeof(datetime), "%.12s %.12s",
                              hdr->file.date, hdr->file.time);
                meta.set(Metadata::Date, String(datetime));
        }

        // Film info
        if(hdr->film.id != 0xFF)
                meta.set(Metadata::FilmMfgID, String(1, static_cast<char>('0' + hdr->film.id)));
        if(hdr->film.type != 0xFF)
                meta.set(Metadata::FilmType, String(1, static_cast<char>('0' + hdr->film.type)));
        if(hdr->film.perf != 0xFF)
                meta.set(Metadata::FilmOffset, String(1, static_cast<char>('0' + hdr->film.perf)));
        if(hdr->film.prefix != 0xFFFFFFFF)
                meta.set(Metadata::FilmPrefix, String::number(static_cast<int>(hdr->film.prefix)));
        if(hdr->film.count != 0xFFFFFFFF)
                meta.set(Metadata::FilmCount, String::number(static_cast<int>(hdr->film.count)));
        setMetaStr(meta, Metadata::FilmFormat, hdr->film.format, sizeof(hdr->film.format));
        if(hdr->film.position != 0xFFFFFFFF)
                meta.set(Metadata::FilmSeqPos, static_cast<int>(hdr->film.position));

        float fps = hdr->film.fps;
        uint32_t fpsBits;
        std::memcpy(&fpsBits, &fps, sizeof(fpsBits));
        if(fpsBits != 0xFFFFFFFF && !std::isinf(fps) && fps > 0.0f)
                meta.set(Metadata::FrameRate, static_cast<double>(fps));

        setMetaStr(meta, Metadata::FilmSlate, hdr->film.slate, sizeof(hdr->film.slate));

        float gamma = hdr->orient.gamma;
        uint32_t gammaBits;
        std::memcpy(&gammaBits, &gamma, sizeof(gammaBits));
        if(gammaBits != 0xFFFFFFFF && !std::isinf(gamma))
                meta.set(Metadata::Gamma, static_cast<double>(gamma));
}

// ===========================================================================
// ImageFileIO_Cineon
// ===========================================================================

class ImageFileIO_Cineon : public ImageFileIO {
        public:
                ImageFileIO_Cineon() {
                        _id = ImageFile::Cineon;
                        _canLoad = true;
                        _canSave = false;
                        _name = "Cineon";
                        _description = "Kodak Cineon 4.5 image sequence (load only)";
                        _extensions = { "cin" };
                }
                Error load(ImageFile &imageFile, const MediaConfig &config) const override;
};
PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_Cineon);

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

Error ImageFileIO_Cineon::load(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        File file(filename);
        Error err = file.open(File::ReadOnly);
        if(err.isError()) {
                promekiErr("Cineon load '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        // Read header
        CineonHeader hdr;
        int64_t n = file.read(&hdr, sizeof(CineonHeader));
        if(n != static_cast<int64_t>(sizeof(CineonHeader))) {
                promekiErr("Cineon load '%s': short header read", filename.cstr());
                file.close();
                return Error::IOError;
        }

        // Check magic and endian-flip if needed
        if(hdr.file.magic != CINEON_MAGIC) {
                cineonFlipHeader(&hdr);
        }
        if(hdr.file.magic != CINEON_MAGIC) {
                promekiErr("Cineon load '%s': invalid magic 0x%08X", filename.cstr(), hdr.file.magic);
                file.close();
                return Error::Invalid;
        }

        // Validate format: only 10-bit RGB, packing mode 5
        if(hdr.img.nchan != 3 || hdr.img.chan[0].bpp != 10 || hdr.fmt.packing != 5) {
                promekiErr("Cineon load '%s': unsupported format (nchan=%d, bpp=%d, packing=%d)",
                           filename.cstr(), hdr.img.nchan, hdr.img.chan[0].bpp, hdr.fmt.packing);
                file.close();
                return Error::PixelFormatNotSupported;
        }

        size_t w = hdr.img.chan[0].width;
        size_t h = hdr.img.chan[0].height;
        PixelFormat pd(PixelFormat::RGB10_DPX_sRGB);

        // Read image data using readBulk for DIO
        size_t imageBytes = pd.memLayout().planeSize(0, w, h);

        err = file.seek(hdr.file.offset);
        if(err.isError()) { file.close(); return err; }

        auto alignResult = file.directIOAlignment();
        size_t bufAlign = isOk(alignResult) ? value(alignResult) : 4096;
        Buffer imgBuf(imageBytes + bufAlign, bufAlign);

        err = file.readBulk(imgBuf, static_cast<int64_t>(imageBytes));
        file.close();
        if(err.isError()) {
                promekiErr("Cineon load '%s': read image data failed: %s",
                           filename.cstr(), err.name().cstr());
                return err;
        }

        Image image(w, h, pd);
        if(!image.isValid()) {
                promekiErr("Cineon load '%s': failed to allocate image", filename.cstr());
                return Error::NoMem;
        }
        std::memcpy(image.data(), imgBuf.data(), imageBytes);

        Frame frame;
        frame.imageList().pushToBack(Image::Ptr::create(image));
        extractCineonMetadata(frame.metadata(), &hdr);

        imageFile.setFrame(frame);
        promekiDebug("Cineon load '%s': %zux%zu RGB10", filename.cstr(), w, h);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
