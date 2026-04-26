/**
 * @file      imagefileio_dpx.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <ctime>
#include <cmath>
#include <promeki/logger.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/file.h>
#include <promeki/fileinfo.h>
#include <promeki/buffer.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/videopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/timecode.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(DPX)

// ===========================================================================
// Constants
// ===========================================================================

static constexpr uint32_t DPX_MAGIC = 0x53445058; // "SDPX"
static constexpr uint32_t AUDIO_MAGIC_V1 = 0x57445541;
static constexpr uint32_t AUDIO_MAGIC_V2 = 0x57444442;
static constexpr size_t   DPX_ALIGN = 4096;

static const char *dpxVersion = "V2.0";

#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

// ===========================================================================
// DPX header structures (SMPTE 268M)
// ===========================================================================

#pragma pack(push, 1)

struct DPXHeader {
                struct FileInfo {
                                uint32_t magic;
                                uint32_t offset;
                                char     version[8];
                                uint32_t size;
                                uint32_t ditto;
                                uint32_t generic;
                                uint32_t industry;
                                uint32_t user;
                                char     fname[100];
                                char     date[24];
                                char     creator[100];
                                char     name[200];
                                char     copyright[200];
                                uint32_t enckey;
                                uint8_t  uuid[16];
                                char     _pad[88];
                } finfo;

                struct ImageInfo {
                                uint16_t orient;
                                uint16_t nelem;
                                uint32_t width;
                                uint32_t height;
                } imginfo;

                struct ImageElement {
                                uint32_t sign;
                                uint32_t reflowdata;
                                float    reflowquant;
                                uint32_t refhighdata;
                                float    refhighquant;
                                uint8_t  desc;
                                uint8_t  trans;
                                uint8_t  color;
                                uint8_t  bitdepth;
                                uint16_t packing;
                                uint16_t encoding;
                                uint32_t offset;
                                uint32_t endoflinepadding;
                                uint32_t endofimagepadding;
                                char     elementdesc[32];
                } imgelm[8];

                char _pad1[52];

                struct ImageSource {
                                uint32_t xoff;
                                uint32_t yoff;
                                float    xcent;
                                float    ycent;
                                uint32_t xorigsize;
                                uint32_t yorigsize;
                                char     fname[100];
                                char     date[24];
                                char     device[32];
                                char     serial[32];
                                uint16_t xlbord;
                                uint16_t xrbord;
                                uint16_t ytbord;
                                uint16_t ybbord;
                                uint32_t horizaspect;
                                uint32_t vertaspect;
                                float    xscansize;
                                float    yscansize;
                                char     _pad[20];
                } imgsrc;

                struct FilmInfo {
                                char     mfgid[2];
                                char     type[2];
                                char     offset[2];
                                char     prefix[6];
                                char     count[4];
                                char     format[32];
                                uint32_t seqpos;
                                uint32_t seqlen;
                                uint32_t heldcount;
                                float    fps;
                                float    shutter;
                                char     id[32];
                                char     slate[100];
                                char     _pad[56];
                } filminfo;

                struct TVInfo {
                                uint32_t timecode;
                                uint32_t userbits;
                                uint8_t  interlace;
                                uint8_t  field;
                                uint8_t  standard;
                                uint8_t  _pad1;
                                float    horizrate;
                                float    vertrate;
                                float    fps;
                                float    syncoffset;
                                float    gamma;
                                float    blackcode;
                                float    blackgain;
                                float    breakpoint;
                                float    whitecode;
                                float    integration;
                                char     _pad2[76];
                } tvinfo;
};

static_assert(sizeof(DPXHeader) == 2048, "DPX header must be 2048 bytes");

struct DPXUserData {
                int8_t   user[32];
                uint32_t type;
                int32_t  size;
};

struct DPXAudioHeaderV1 {
                DPXUserData user;
                int32_t     samps;
                int16_t     chans;
                int16_t     bps;
                float       rate;
                uint32_t    timecode;
                uint32_t    userbits;
};

struct DPXAudioHeaderV2 {
                DPXUserData user;
                uint32_t    model;
                int32_t     samps;
                int32_t     chans;
                int32_t     rate;
                uint32_t    timecode;
                uint32_t    userbits;
};

static_assert(sizeof(DPXAudioHeaderV1) == 60, "V1 audio header must be 60 bytes");
static_assert(sizeof(DPXAudioHeaderV2) == 64, "V2 audio header must be 64 bytes");

#pragma pack(pop)

// ===========================================================================
// Endian helpers
// ===========================================================================

static inline void endflip4(char *to, const char *from, int off) {
        char       *t = to + off;
        const char *f = from + off;
        t[0] = f[3];
        t[1] = f[2];
        t[2] = f[1];
        t[3] = f[0];
}

static inline void endflip2(char *to, const char *from, int off) {
        char       *t = to + off;
        const char *f = from + off;
        t[0] = f[1];
        t[1] = f[0];
}

static void dpxFlipHeader(DPXHeader *ndpx, const DPXHeader *dpx) {
        char       *t = reinterpret_cast<char *>(ndpx);
        const char *f = reinterpret_cast<const char *>(dpx);
        std::memset(t, 0, sizeof(DPXHeader));

        // File Info
        endflip4(t, f, 0);                // Magic
        endflip4(t, f, 4);                // Offset
        std::memcpy(t + 8, f + 8, 8);     // Version
        endflip4(t, f, 16);               // Size
        endflip4(t, f, 20);               // Ditto
        endflip4(t, f, 24);               // Generic
        endflip4(t, f, 28);               // Industry
        endflip4(t, f, 32);               // User
        std::memcpy(t + 36, f + 36, 624); // fname, date, creator, name, copyright
        endflip4(t, f, 660);              // Enc

        // Image information
        endflip2(t, f, 768); // Orientation
        endflip2(t, f, 770); // N. Elm
        endflip4(t, f, 772); // width
        endflip4(t, f, 776); // height

        // Image elements
        for (int i = 0; i < 8; i++) {
                int off = i * 72;
                endflip4(t, f, 780 + off);                     // Sign
                endflip4(t, f, 784 + off);                     // low data
                endflip4(t, f, 788 + off);                     // low value
                endflip4(t, f, 792 + off);                     // high data
                endflip4(t, f, 796 + off);                     // high value
                std::memcpy(t + 800 + off, f + 800 + off, 4);  // desc/trans/color/bitdepth
                endflip2(t, f, 804 + off);                     // Packing
                endflip2(t, f, 806 + off);                     // Encoding
                endflip4(t, f, 808 + off);                     // Offset
                endflip4(t, f, 812 + off);                     // line padding
                endflip4(t, f, 816 + off);                     // image padding
                std::memcpy(t + 820 + off, f + 820 + off, 32); // elementdesc
        }

        // Image source
        endflip4(t, f, 1408);                 // x off
        endflip4(t, f, 1412);                 // y off
        endflip4(t, f, 1416);                 // x cent
        endflip4(t, f, 1420);                 // y cent
        endflip4(t, f, 1424);                 // x osize
        endflip4(t, f, 1428);                 // y osize
        std::memcpy(t + 1432, f + 1432, 188); // fname, date, device, serial
        endflip2(t, f, 1620);
        endflip2(t, f, 1622);
        endflip2(t, f, 1624);
        endflip2(t, f, 1626);
        endflip4(t, f, 1628); // horiz aspect
        endflip4(t, f, 1632); // vert aspect
        endflip4(t, f, 1636); // x scan size
        endflip4(t, f, 1640); // y scan size

        // Film info
        std::memcpy(t + 1664, f + 1664, 48);
        endflip4(t, f, 1712); // Seq Pos
        endflip4(t, f, 1716); // Seq Len
        endflip4(t, f, 1720); // Held Count
        endflip4(t, f, 1724); // FPS
        endflip4(t, f, 1728); // Shutter
        std::memcpy(t + 1732, f + 1732, 132);

        // TV info
        endflip4(t, f, 1920); // Timecode
        endflip4(t, f, 1924); // Userbits
        std::memcpy(t + 1928, f + 1928, 4);
        endflip4(t, f, 1932); // Horiz rate
        endflip4(t, f, 1936); // Vert Rate
        endflip4(t, f, 1940); // fps
        endflip4(t, f, 1944); // sync offset
        endflip4(t, f, 1948); // gamma
        endflip4(t, f, 1952); // black level
        endflip4(t, f, 1956); // black gain
        endflip4(t, f, 1960); // breakpoint
        endflip4(t, f, 1964); // white level
        endflip4(t, f, 1968); // integration
}

// ===========================================================================
// Header initialization
// ===========================================================================

static void dpxInit(DPXHeader *hdr) {
        std::memset(hdr, 0xFF, sizeof(DPXHeader));

        hdr->finfo.magic = DPX_MAGIC;
        hdr->finfo.offset = sizeof(DPXHeader);
        std::strncpy(hdr->finfo.version, dpxVersion, sizeof(hdr->finfo.version));
        std::memset(hdr->finfo.fname, 0, sizeof(hdr->finfo.fname));
        std::memset(hdr->finfo.date, 0, sizeof(hdr->finfo.date));
        std::memset(hdr->finfo.creator, 0, sizeof(hdr->finfo.creator));
        std::memset(hdr->finfo.name, 0, sizeof(hdr->finfo.name));
        std::memset(hdr->finfo.copyright, 0, sizeof(hdr->finfo.copyright));
        std::memset(hdr->finfo.uuid, 0, sizeof(hdr->finfo.uuid));

        for (int i = 0; i < 8; i++) {
                std::memset(hdr->imgelm[i].elementdesc, 0, sizeof(hdr->imgelm[i].elementdesc));
        }

        std::memset(hdr->imgsrc.fname, 0, sizeof(hdr->imgsrc.fname));
        std::memset(hdr->imgsrc.date, 0, sizeof(hdr->imgsrc.date));
        std::memset(hdr->imgsrc.device, 0, sizeof(hdr->imgsrc.device));
        std::memset(hdr->imgsrc.serial, 0, sizeof(hdr->imgsrc.serial));

        std::memset(hdr->filminfo.mfgid, 0, sizeof(hdr->filminfo.mfgid));
        std::memset(hdr->filminfo.type, 0, sizeof(hdr->filminfo.type));
        std::memset(hdr->filminfo.offset, 0, sizeof(hdr->filminfo.offset));
        std::memset(hdr->filminfo.prefix, 0, sizeof(hdr->filminfo.prefix));
        std::memset(hdr->filminfo.count, 0, sizeof(hdr->filminfo.count));
        std::memset(hdr->filminfo.format, 0, sizeof(hdr->filminfo.format));
        std::memset(hdr->filminfo.id, 0, sizeof(hdr->filminfo.id));
        std::memset(hdr->filminfo.slate, 0, sizeof(hdr->filminfo.slate));

        hdr->tvinfo._pad1 = 0;
}

// ===========================================================================
// Pixel format mapping
// ===========================================================================

// Byte-swap helpers for 8-bit 4-component pixel data.
// DPX descriptor 51 = RGBA byte order, 52 = ABGR byte order.
// We always load into RGBA8_sRGB (R,G,B,A bytes).

static void swapABGRtoRGBA(uint8_t *data, size_t pixelCount) {
        for (size_t i = 0; i < pixelCount; ++i) {
                uint8_t *p = data + i * 4;
                uint8_t  a = p[0], b = p[1], g = p[2], r = p[3];
                p[0] = r;
                p[1] = g;
                p[2] = b;
                p[3] = a;
        }
}

static void swapRGBAtoABGR(uint8_t *data, size_t pixelCount) {
        swapABGRtoRGBA(data, pixelCount); // symmetric swap
}

static void swapARGBtoRGBA(uint8_t *data, size_t pixelCount) {
        for (size_t i = 0; i < pixelCount; ++i) {
                uint8_t *p = data + i * 4;
                uint8_t  a = p[0], r = p[1], g = p[2], b = p[3];
                p[0] = r;
                p[1] = g;
                p[2] = b;
                p[3] = a;
        }
}

static void swapRGBAtoARGB(uint8_t *data, size_t pixelCount) {
        for (size_t i = 0; i < pixelCount; ++i) {
                uint8_t *p = data + i * 4;
                uint8_t  r = p[0], g = p[1], b = p[2], a = p[3];
                p[0] = a;
                p[1] = r;
                p[2] = g;
                p[3] = b;
        }
}

static PixelFormat::ID dpxPixelFormat(const DPXHeader *hdr, bool bigEndian) {
        const auto &elm = hdr->imgelm[0];
        switch (elm.bitdepth) {
                case 8:
                        switch (elm.desc) {
                                case 50: return PixelFormat::RGB8_sRGB;  // RGB byte order (3 components)
                                case 51: return PixelFormat::RGBA8_sRGB; // RGBA byte order
                                case 52: return PixelFormat::RGBA8_sRGB; // ABGR byte order (swapped after read)
                                case 100: return PixelFormat::YUV8_Rec709;
                        }
                        break;
                case 10:
                        switch (elm.desc) {
                                case 50:
                                        return bigEndian ? PixelFormat::RGB10_DPX_sRGB : PixelFormat::RGB10_DPX_LE_sRGB;
                                case 100:
                                        switch (elm.packing) {
                                                case 1: return PixelFormat::YUV10_DPX_Rec709;
                                                case 2: return PixelFormat::YUV10_DPX_B_Rec709;
                                        }
                                        break;
                        }
                        break;
                case 16:
                        if (elm.desc == 50) return PixelFormat::RGB16_BE_sRGB;
                        break;
        }
        return PixelFormat::Invalid;
}

// Returns true when @p pdId was recognized and the element was
// populated.  Unknown formats leave the element untouched (header
// memset'd to 0xFF by dpxInit) and the caller must refuse the save —
// otherwise we'd emit a file whose header claims desc=255/bitdepth=255
// and no reader would be able to decode it.
static bool setDPXImageElement(DPXHeader *hdr, PixelFormat::ID pdId) {
        auto &elm = hdr->imgelm[0];
        elm.endoflinepadding = 0;
        elm.endofimagepadding = 0;
        switch (pdId) {
                case PixelFormat::RGB8_sRGB:
                        elm.desc = 50;
                        elm.bitdepth = 8;
                        elm.packing = 1;
                        hdr->tvinfo.blackcode = 0.0f;
                        hdr->tvinfo.whitecode = 255.0f;
                        return true;
                case PixelFormat::RGBA8_sRGB:
                        elm.desc = 51;
                        elm.bitdepth = 8;
                        elm.packing = 1;
                        hdr->tvinfo.blackcode = 0.0f;
                        hdr->tvinfo.whitecode = 255.0f;
                        return true;
                case PixelFormat::ARGB8_sRGB:
                        elm.desc = 51;
                        elm.bitdepth = 8;
                        elm.packing = 1;
                        hdr->tvinfo.blackcode = 0.0f;
                        hdr->tvinfo.whitecode = 255.0f;
                        return true;
                case PixelFormat::YUV8_Rec709:
                        elm.desc = 100;
                        elm.bitdepth = 8;
                        elm.packing = 1;
                        hdr->tvinfo.blackcode = 16.0f;
                        hdr->tvinfo.whitecode = 235.0f;
                        return true;
                case PixelFormat::RGB10_DPX_sRGB:
                case PixelFormat::RGB10_DPX_LE_sRGB:
                        elm.desc = 50;
                        elm.bitdepth = 10;
                        elm.packing = 1;
                        hdr->tvinfo.blackcode = 0.0f;
                        hdr->tvinfo.whitecode = 1023.0f;
                        return true;
                case PixelFormat::YUV10_DPX_Rec709:
                        elm.desc = 100;
                        elm.bitdepth = 10;
                        elm.packing = 1;
                        hdr->tvinfo.blackcode = 64.0f;
                        hdr->tvinfo.whitecode = 940.0f;
                        return true;
                case PixelFormat::YUV10_DPX_B_Rec709:
                        elm.desc = 100;
                        elm.bitdepth = 10;
                        elm.packing = 2;
                        hdr->tvinfo.blackcode = 64.0f;
                        hdr->tvinfo.whitecode = 940.0f;
                        return true;
                case PixelFormat::RGB16_BE_sRGB:
                        elm.desc = 50;
                        elm.bitdepth = 16;
                        elm.packing = 1;
                        hdr->tvinfo.blackcode = 0.0f;
                        hdr->tvinfo.whitecode = 65535.0f;
                        return true;
                default: break;
        }
        return false;
}

static void setDPXTransferCharacteristic(DPXHeader *hdr) {
        uint32_t w = hdr->imginfo.width;
        uint32_t h = hdr->imginfo.height;
        if (w == 720 && (h == 480 || h == 486)) {
                hdr->imgelm[0].trans = 8;
                hdr->imgelm[0].color = 8;
        } else if (w == 720 && h == 576) {
                hdr->imgelm[0].trans = 7;
                hdr->imgelm[0].color = 7;
        } else if ((w == 1280 && h == 720) || (w == 1920 && h == 1080)) {
                hdr->imgelm[0].trans = 6;
                hdr->imgelm[0].color = 6;
        } else {
                hdr->imgelm[0].trans = 4;
                hdr->imgelm[0].color = 4;
        }
}

// ===========================================================================
// BCD timecode helpers (SMPTE 12M)
// ===========================================================================

static uint8_t bcdDecode(uint8_t val) {
        return ((val >> 4) & 0x0F) * 10 + (val & 0x0F);
}

static uint8_t bcdEncode(uint8_t val) {
        return ((val / 10) & 0x0F) << 4 | (val % 10);
}

static Timecode dpxTimecodeToTimecode(uint32_t bcd, float fps) {
        if (bcd == 0xFFFFFFFF) return Timecode();

        uint8_t hh = bcdDecode((bcd >> 24) & 0xFF);
        uint8_t mm = bcdDecode((bcd >> 16) & 0xFF);
        uint8_t ss = bcdDecode((bcd >> 8) & 0xFF);
        uint8_t ff = bcdDecode((bcd >> 0) & 0xFF);

        Timecode::Mode mode;
        int            ifps = static_cast<int>(fps + 0.5f);
        if (ifps >= 23 && ifps <= 24)
                mode = Timecode::NDF24;
        else if (ifps == 25)
                mode = Timecode::NDF25;
        else if (ifps >= 29 && ifps <= 30)
                mode = Timecode::NDF30;
        else
                mode = Timecode::NDF30;

        return Timecode(mode, hh, mm, ss, ff);
}

static uint32_t timecodeToDPXBCD(const Timecode &tc) {
        uint32_t val = 0;
        val |= static_cast<uint32_t>(bcdEncode(tc.hour())) << 24;
        val |= static_cast<uint32_t>(bcdEncode(tc.min())) << 16;
        val |= static_cast<uint32_t>(bcdEncode(tc.sec())) << 8;
        val |= static_cast<uint32_t>(bcdEncode(tc.frame()));
        return val;
}

// ===========================================================================
// Metadata helpers
// ===========================================================================

static void setMetaString(Metadata &meta, const Metadata::ID &id, const char *str, size_t maxLen) {
        if (str[0] == 0 || static_cast<uint8_t>(str[0]) == 0xFF) return;
        size_t len = ::strnlen(str, maxLen);
        meta.set(id, String(str, len));
}

static void setMetaChars(Metadata &meta, const Metadata::ID &id, const char *str, size_t len) {
        if (str[0] == 0 || static_cast<uint8_t>(str[0]) == 0xFF) return;
        meta.set(id, String(str, len));
}

static void setMetaFloat(Metadata &meta, const Metadata::ID &id, float val) {
        if (std::isinf(val)) return;
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        if (bits == 0xFFFFFFFF) return;
        meta.set(id, static_cast<double>(val));
}

static void setMetaUInt(Metadata &meta, const Metadata::ID &id, uint32_t val) {
        if (val == 0xFFFFFFFF) return;
        meta.set(id, static_cast<int>(val));
}

static void setMetaUChar(Metadata &meta, const Metadata::ID &id, uint8_t val) {
        if (val == 0xFF) return;
        meta.set(id, static_cast<int>(val));
}

static void extractMetadata(Metadata &meta, const DPXHeader *hdr) {
        // File info
        setMetaString(meta, Metadata::FileOrigName, hdr->finfo.fname, sizeof(hdr->finfo.fname));
        setMetaString(meta, Metadata::Date, hdr->finfo.date, sizeof(hdr->finfo.date));
        setMetaString(meta, Metadata::Software, hdr->finfo.creator, sizeof(hdr->finfo.creator));
        setMetaString(meta, Metadata::Project, hdr->finfo.name, sizeof(hdr->finfo.name));
        setMetaString(meta, Metadata::Copyright, hdr->finfo.copyright, sizeof(hdr->finfo.copyright));
        setMetaString(meta, Metadata::Reel, hdr->imgsrc.device, sizeof(hdr->imgsrc.device));

        // Film info
        setMetaChars(meta, Metadata::FilmMfgID, hdr->filminfo.mfgid, sizeof(hdr->filminfo.mfgid));
        setMetaChars(meta, Metadata::FilmType, hdr->filminfo.type, sizeof(hdr->filminfo.type));
        setMetaChars(meta, Metadata::FilmOffset, hdr->filminfo.offset, sizeof(hdr->filminfo.offset));
        setMetaChars(meta, Metadata::FilmPrefix, hdr->filminfo.prefix, sizeof(hdr->filminfo.prefix));
        setMetaChars(meta, Metadata::FilmCount, hdr->filminfo.count, sizeof(hdr->filminfo.count));
        setMetaString(meta, Metadata::FilmFormat, hdr->filminfo.format, sizeof(hdr->filminfo.format));
        setMetaUInt(meta, Metadata::FilmSeqPos, hdr->filminfo.seqpos);
        setMetaUInt(meta, Metadata::FilmSeqLen, hdr->filminfo.seqlen);
        setMetaUInt(meta, Metadata::FilmHoldCount, hdr->filminfo.heldcount);
        setMetaFloat(meta, Metadata::FrameRate, hdr->filminfo.fps);
        setMetaFloat(meta, Metadata::FilmShutter, hdr->filminfo.shutter);
        setMetaString(meta, Metadata::FilmFrameID, hdr->filminfo.id, sizeof(hdr->filminfo.id));
        setMetaString(meta, Metadata::FilmSlate, hdr->filminfo.slate, sizeof(hdr->filminfo.slate));

        // TV info
        float    fps = hdr->tvinfo.fps;
        uint32_t bits;
        std::memcpy(&bits, &fps, sizeof(bits));
        if (bits == 0xFFFFFFFF || std::isinf(fps)) fps = hdr->filminfo.fps;
        Timecode tc = dpxTimecodeToTimecode(hdr->tvinfo.timecode, fps);
        if (tc.isValid()) meta.set(Metadata::Timecode, tc);

        setMetaUChar(meta, Metadata::FieldID, hdr->tvinfo.field);
        // Use TV fps if film fps was not set
        if (!meta.contains(Metadata::FrameRate)) {
                setMetaFloat(meta, Metadata::FrameRate, hdr->tvinfo.fps);
        }
        setMetaFloat(meta, Metadata::Gamma, hdr->tvinfo.gamma);

        // Round-trip fields
        setMetaUChar(meta, Metadata::TransferCharacteristic, hdr->imgelm[0].trans);
        setMetaUChar(meta, Metadata::Colorimetric, hdr->imgelm[0].color);
        uint16_t orient = hdr->imginfo.orient;
        if (orient != 0xFFFF) meta.set(Metadata::Orientation, static_cast<int>(orient));
}

static void setHdrStr(char *hstr, const Variant &v, size_t maxLen) {
        if (!v.isValid()) return;
        String s = v.get<String>();
        std::strncpy(hstr, s.cstr(), maxLen);
        hstr[maxLen - 1] = 0;
}

static void setHdrChars(char *hstr, const Variant &v, size_t maxLen) {
        if (!v.isValid()) return;
        String s = v.get<String>();
        std::strncpy(hstr, s.cstr(), maxLen);
}

static void fillHeaderFromMetadata(DPXHeader *hdr, const Metadata &meta, const String &filename) {
        // Filename and date are always set
        std::strncpy(hdr->finfo.fname, filename.cstr(), sizeof(hdr->finfo.fname) - 1);
        hdr->finfo.fname[sizeof(hdr->finfo.fname) - 1] = 0;

        char         timestr[24];
        const time_t caltime = std::time(nullptr);
        std::strftime(timestr, sizeof(timestr), "%Y:%m:%d:%H:%M:%SZ", std::gmtime(&caltime));
        std::strcpy(hdr->finfo.date, timestr);

        // Iterate metadata and fill header fields
        meta.forEach([&](const Metadata::ID &id, const Variant &val) {
                if (id == Metadata::Software) {
                        setHdrStr(hdr->finfo.creator, val, sizeof(hdr->finfo.creator));
                } else if (id == Metadata::Project) {
                        setHdrStr(hdr->finfo.name, val, sizeof(hdr->finfo.name));
                } else if (id == Metadata::Copyright) {
                        setHdrStr(hdr->finfo.copyright, val, sizeof(hdr->finfo.copyright));
                } else if (id == Metadata::FileOrigName) {
                        setHdrStr(hdr->imgsrc.fname, val, sizeof(hdr->imgsrc.fname));
                } else if (id == Metadata::Date) {
                        setHdrStr(hdr->imgsrc.date, val, sizeof(hdr->imgsrc.date));
                } else if (id == Metadata::Reel) {
                        setHdrStr(hdr->imgsrc.device, val, sizeof(hdr->imgsrc.device));
                } else if (id == Metadata::FilmMfgID) {
                        setHdrChars(hdr->filminfo.mfgid, val, sizeof(hdr->filminfo.mfgid));
                } else if (id == Metadata::FilmType) {
                        setHdrChars(hdr->filminfo.type, val, sizeof(hdr->filminfo.type));
                } else if (id == Metadata::FilmOffset) {
                        setHdrChars(hdr->filminfo.offset, val, sizeof(hdr->filminfo.offset));
                } else if (id == Metadata::FilmPrefix) {
                        setHdrChars(hdr->filminfo.prefix, val, sizeof(hdr->filminfo.prefix));
                } else if (id == Metadata::FilmCount) {
                        setHdrChars(hdr->filminfo.count, val, sizeof(hdr->filminfo.count));
                } else if (id == Metadata::FilmFormat) {
                        setHdrStr(hdr->filminfo.format, val, sizeof(hdr->filminfo.format));
                } else if (id == Metadata::FilmSeqPos) {
                        hdr->filminfo.seqpos = static_cast<uint32_t>(val.get<int>());
                } else if (id == Metadata::FilmSeqLen) {
                        hdr->filminfo.seqlen = static_cast<uint32_t>(val.get<int>());
                } else if (id == Metadata::FilmHoldCount) {
                        hdr->filminfo.heldcount = static_cast<uint32_t>(val.get<int>());
                } else if (id == Metadata::FrameRate) {
                        float f = static_cast<float>(val.get<double>());
                        hdr->filminfo.fps = f;
                        hdr->tvinfo.fps = f;
                } else if (id == Metadata::FilmShutter) {
                        hdr->filminfo.shutter = static_cast<float>(val.get<double>());
                } else if (id == Metadata::FilmFrameID) {
                        setHdrStr(hdr->filminfo.id, val, sizeof(hdr->filminfo.id));
                } else if (id == Metadata::FilmSlate) {
                        setHdrStr(hdr->filminfo.slate, val, sizeof(hdr->filminfo.slate));
                } else if (id == Metadata::Timecode) {
                        Timecode tc = val.get<Timecode>();
                        hdr->tvinfo.timecode = timecodeToDPXBCD(tc);
                        hdr->tvinfo.userbits = 0;
                        hdr->tvinfo.fps = static_cast<float>(tc.fps());
                } else if (id == Metadata::FieldID) {
                        hdr->tvinfo.field = static_cast<uint8_t>(val.get<int>());
                } else if (id == Metadata::Gamma) {
                        hdr->tvinfo.gamma = static_cast<float>(val.get<double>());
                }
        });
}

// ===========================================================================
// Embedded audio
// ===========================================================================

static PcmAudioPayload::Ptr readEmbeddedAudio(const Buffer &buf) {
        if (buf.size() < sizeof(DPXUserData)) return PcmAudioPayload::Ptr();

        const auto *user = reinterpret_cast<const DPXUserData *>(buf.data());
        if (std::memcmp(user->user, "AUDIO", 5) != 0) return PcmAudioPayload::Ptr();

        auto buildPayload = [&](AudioFormat::ID dt, float rate, unsigned int chans, size_t samples,
                                size_t headerBytes) -> PcmAudioPayload::Ptr {
                AudioDesc desc(dt, rate, chans);
                if (!desc.isValid()) return PcmAudioPayload::Ptr();
                size_t dataBytes = desc.bufferSize(samples);
                if (dataBytes == 0 || buf.size() < headerBytes + dataBytes) {
                        return PcmAudioPayload::Ptr();
                }
                Buffer::Ptr pcm = Buffer::Ptr::create(dataBytes);
                std::memcpy(pcm.modify()->data(), static_cast<const uint8_t *>(buf.data()) + headerBytes, dataBytes);
                pcm.modify()->setSize(dataBytes);
                BufferView view(pcm, 0, dataBytes);
                return PcmAudioPayload::Ptr::create(desc, samples, view);
        };

        if (user->type == AUDIO_MAGIC_V1 && user->size >= static_cast<int32_t>(sizeof(DPXAudioHeaderV1))) {
                const auto     *ahdr = reinterpret_cast<const DPXAudioHeaderV1 *>(buf.data());
                AudioFormat::ID dt = AudioFormat::Invalid;
                switch (ahdr->bps) {
                        case 1: dt = AudioFormat::PCMI_S8; break;
                        case 2: dt = AudioFormat::PCMI_S16LE; break;
                        case 3: dt = AudioFormat::PCMI_S24LE; break;
                        case 4: dt = AudioFormat::PCMI_S32LE; break;
                        default: return PcmAudioPayload::Ptr();
                }
                return buildPayload(dt, static_cast<float>(ahdr->rate), static_cast<unsigned int>(ahdr->chans),
                                    static_cast<size_t>(ahdr->samps), sizeof(DPXAudioHeaderV1));
        }

        if (user->type == AUDIO_MAGIC_V2 && user->size >= static_cast<int32_t>(sizeof(DPXAudioHeaderV2))) {
                const auto *ahdr = reinterpret_cast<const DPXAudioHeaderV2 *>(buf.data());
                // V2 stores the AudioFormat::ID directly as the model field.
                // Map the legacy Meki model IDs to our DataType.
                // Legacy IDs: Meki_S8=600, Meki_S16LE=601, Meki_S24LE=602, Meki_S32LE=603
                AudioFormat::ID dt = AudioFormat::Invalid;
                switch (ahdr->model) {
                        case 600: dt = AudioFormat::PCMI_S8; break;
                        case 601: dt = AudioFormat::PCMI_S16LE; break;
                        case 602: dt = AudioFormat::PCMI_S24LE; break;
                        case 603: dt = AudioFormat::PCMI_S32LE; break;
                        default:
                                // Try interpreting as native DataType enum
                                dt = static_cast<AudioFormat::ID>(ahdr->model);
                                break;
                }
                return buildPayload(dt, static_cast<float>(ahdr->rate), static_cast<unsigned int>(ahdr->chans),
                                    static_cast<size_t>(ahdr->samps), sizeof(DPXAudioHeaderV2));
        }

        return PcmAudioPayload::Ptr();
}

static size_t writeEmbeddedAudio(uint8_t *dest, const PcmAudioPayload &payload) {
        DPXAudioHeaderV2 ahdr;
        std::memset(&ahdr, 0, sizeof(ahdr));
        std::memcpy(ahdr.user.user, "AUDIO", 6);
        ahdr.user.type = AUDIO_MAGIC_V2;
        const AudioDesc &desc = payload.desc();
        const size_t     samples = payload.sampleCount();
        size_t           dataBytes = desc.bufferSize(samples);
        ahdr.user.size = static_cast<int32_t>(sizeof(DPXAudioHeaderV2) + dataBytes);
        ahdr.model = static_cast<uint32_t>(desc.format().id());
        ahdr.samps = static_cast<int32_t>(samples);
        ahdr.chans = static_cast<int32_t>(desc.channels());
        ahdr.rate = static_cast<int32_t>(desc.sampleRate());
        ahdr.timecode = 0xFFFFFFFF;
        ahdr.userbits = 0xFFFFFFFF;

        std::memcpy(dest, &ahdr, sizeof(ahdr));
        if (payload.planeCount() > 0) {
                auto view = payload.plane(0);
                std::memcpy(dest + sizeof(ahdr), view.data(), dataBytes < view.size() ? dataBytes : view.size());
        }
        return sizeof(ahdr) + dataBytes;
}

// ===========================================================================
// ImageFileIO_DPX
// ===========================================================================

class ImageFileIO_DPX : public ImageFileIO {
        public:
                ImageFileIO_DPX() {
                        _id = ImageFile::DPX;
                        _canLoad = true;
                        _canSave = true;
                        _name = "DPX";
                        _description = "DPX (SMPTE 268M) image sequence";
                        _extensions = {"dpx"};
                }
                Error load(ImageFile &imageFile, const MediaConfig &config) const override;
                Error save(ImageFile &imageFile, const MediaConfig &config) const override;
};
PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_DPX);

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

Error ImageFileIO_DPX::load(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const String &filename = imageFile.filename();

        File  file(filename);
        Error err = file.open(File::ReadOnly);
        if (err.isError()) {
                promekiErr("DPX load '%s': %s", filename.cstr(), err.name().cstr());
                return err;
        }

        // 1. Read the 2048-byte header
        DPXHeader hdr;
        int64_t   n = file.read(&hdr, sizeof(DPXHeader));
        if (n != static_cast<int64_t>(sizeof(DPXHeader))) {
                promekiErr("DPX load '%s': short header read (%lld of %zu)", filename.cstr(), (long long)n,
                           sizeof(DPXHeader));
                file.close();
                return Error::IOError;
        }

        // 2. Detect endianness
        bool bigEndian = false;
        if (hdr.finfo.magic != DPX_MAGIC) {
                DPXHeader flipped;
                dpxFlipHeader(&flipped, &hdr);
                hdr = flipped;
                bigEndian = true;
        }
        if (hdr.finfo.magic != DPX_MAGIC) {
                promekiErr("DPX load '%s': invalid magic 0x%08X", filename.cstr(), hdr.finfo.magic);
                file.close();
                return Error::Invalid;
        }

        // 3. Only single-element DPX is supported
        if (hdr.imginfo.nelem != 1) {
                promekiErr("DPX load '%s': multi-element DPX not supported (nelem=%d)", filename.cstr(),
                           hdr.imginfo.nelem);
                file.close();
                return Error::NotSupported;
        }

        // 4. Determine pixel format
        PixelFormat::ID pdId = dpxPixelFormat(&hdr, bigEndian);
        if (pdId == PixelFormat::Invalid) {
                promekiErr("DPX load '%s': unsupported format (desc=%d, depth=%d, packing=%d)", filename.cstr(),
                           hdr.imgelm[0].desc, hdr.imgelm[0].bitdepth, hdr.imgelm[0].packing);
                file.close();
                return Error::PixelFormatNotSupported;
        }

        size_t      w = hdr.imginfo.width;
        size_t      h = hdr.imginfo.height;
        PixelFormat pd(pdId);

        // 5. Read user data section (may contain embedded audio)
        PcmAudioPayload::Ptr audioPayload;
        int64_t userSize = static_cast<int64_t>(hdr.finfo.offset) - static_cast<int64_t>(sizeof(DPXHeader));
        if (userSize > 0) {
                Buffer userBuf(static_cast<size_t>(userSize));
                n = file.read(userBuf.data(), userSize);
                if (n == userSize) {
                        userBuf.setSize(static_cast<size_t>(userSize));
                        audioPayload = readEmbeddedAudio(userBuf);
                }
        }

        // 6. Read image data using readBulk for automatic direct I/O
        size_t imageBytes = pd.memLayout().planeSize(0, w, h);

        err = file.seek(hdr.finfo.offset);
        if (err.isError()) {
                promekiErr("DPX load '%s': seek to image data failed", filename.cstr());
                file.close();
                return err;
        }

        auto   alignResult = file.directIOAlignment();
        size_t bufAlign = isOk(alignResult) ? value(alignResult) : DPX_ALIGN;
        Buffer imgBuf(imageBytes + bufAlign, bufAlign);

        err = file.readBulk(imgBuf, static_cast<int64_t>(imageBytes));
        file.close();
        if (err.isError()) {
                promekiErr("DPX load '%s': read image data failed: %s", filename.cstr(), err.name().cstr());
                return err;
        }

        // 7. Allocate payload and copy data
        ImageDesc idesc(w, h, pd);
        auto      payload = UncompressedVideoPayload::allocate(idesc);
        if (!payload.isValid()) {
                promekiErr("DPX load '%s': failed to allocate %zux%zu payload", filename.cstr(), w, h);
                return Error::NoMem;
        }
        uint8_t *dst = payload.modify()->data()[0].data();
        std::memcpy(dst, imgBuf.data(), imageBytes);

        // 7b. Byte-swap for descriptor 52 (ABGR) → RGBA
        if (hdr.imgelm[0].desc == 52 && hdr.imgelm[0].bitdepth == 8) {
                swapABGRtoRGBA(dst, w * h);
        }

        // 8. Build Frame
        Frame frame;
        frame.addPayload(payload);
        if (audioPayload.isValid()) {
                frame.addPayload(audioPayload);
        }

        // 9. Extract metadata
        extractMetadata(frame.metadata(), &hdr);

        // 10. Set interlace flag on image descriptor if present
        if (hdr.tvinfo.interlace == 1) {
                // Image is already constructed; interlace is in ImageDesc.
                // For now, store as metadata for round-trip.
        }

        imageFile.setFrame(frame);
        promekiDebug("DPX load '%s': %zux%zu %s%s", filename.cstr(), w, h, pd.name().cstr(),
                     bigEndian ? " (big-endian)" : "");
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

Error ImageFileIO_DPX::save(ImageFile &imageFile, const MediaConfig &config) const {
        (void)config;
        const Frame &frame = imageFile.frame();
        auto         vids = frame.videoPayloads();
        if (vids.isEmpty()) {
                promekiErr("DPX save: no image in frame");
                return Error::Invalid;
        }

        const auto *uvp = vids[0]->as<UncompressedVideoPayload>();
        if (uvp == nullptr || !uvp->desc().isValid() || uvp->planeCount() == 0) {
                promekiErr("DPX save: DPX is a raster format — "
                           "compressed video payloads are not supported");
                return Error::Invalid;
        }

        const String    &filename = imageFile.filename();
        const Metadata  &meta = frame.metadata();
        const ImageDesc &idesc = uvp->desc();
        PixelFormat::ID  pdId = idesc.pixelFormat().id();
        const size_t     width = idesc.size().width();
        const size_t     height = idesc.size().height();
        auto             plane0 = uvp->plane(0);

        // 1. Calculate sizes
        size_t                 headerSize = sizeof(DPXHeader);
        size_t                 audioSize = 0;
        const PcmAudioPayload *audioPayload = nullptr;
        auto                   auds = frame.audioPayloads();
        if (!auds.isEmpty() && auds[0].isValid()) {
                audioPayload = auds[0]->as<PcmAudioPayload>();
                if (audioPayload != nullptr) {
                        audioSize =
                                sizeof(DPXAudioHeaderV2) + audioPayload->desc().bufferSize(audioPayload->sampleCount());
                        headerSize += audioSize;
                }
        }
        headerSize = ALIGN_UP(headerSize, DPX_ALIGN);

        size_t imageBytes = plane0.size();
        size_t imagePadded = ALIGN_UP(imageBytes, DPX_ALIGN);
        size_t totalSize = headerSize + imagePadded;

        // 2. Allocate aligned header buffer
        Buffer hdrBuf(headerSize, DPX_ALIGN);
        hdrBuf.fill(0);
        DPXHeader *hdr = static_cast<DPXHeader *>(hdrBuf.data());
        dpxInit(hdr);

        // 3. Embed audio if present
        if (audioPayload != nullptr) {
                uint8_t *audioStart = static_cast<uint8_t *>(hdrBuf.data()) + sizeof(DPXHeader);
                writeEmbeddedAudio(audioStart, *audioPayload);
        }

        // 4. Fill header fields
        hdr->finfo.offset = static_cast<uint32_t>(headerSize);
        hdr->finfo.size = static_cast<uint32_t>(headerSize + imageBytes);

        hdr->imginfo.orient = 0;
        hdr->imginfo.nelem = 1;
        hdr->imginfo.width = static_cast<uint32_t>(width);
        hdr->imginfo.height = static_cast<uint32_t>(height);

        hdr->imgelm[0].sign = 0;
        hdr->imgelm[0].encoding = 0;
        hdr->imgelm[0].offset = static_cast<uint32_t>(headerSize);

        if (!setDPXImageElement(hdr, pdId)) {
                promekiErr("DPX save '%s': pixel format '%s' is not supported "
                           "by this writer",
                           filename.cstr(), idesc.pixelFormat().name().cstr());
                return Error::PixelFormatNotSupported;
        }
        setDPXTransferCharacteristic(hdr);
        fillHeaderFromMetadata(hdr, meta, filename);

        hdr->tvinfo.syncoffset = 0;

        // 5. Byte-swap header for big-endian formats
        bool needFlip = (pdId == PixelFormat::RGB10_DPX_sRGB);
        if (needFlip) {
                DPXHeader flipped;
                dpxFlipHeader(&flipped, hdr);
                std::memcpy(hdr, &flipped, sizeof(DPXHeader));
        }

        // 6. Open file with direct I/O
        File file(filename);
        file.setDirectIO(true);
        Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
        if (err.isError()) {
                // Fall back to non-DIO if direct I/O open fails
                promekiDebug("DPX save '%s': DIO open failed, falling back to normal I/O", filename.cstr());
                file.setDirectIO(false);
                err = file.open(File::WriteOnly, File::Create | File::Truncate);
                if (err.isError()) {
                        promekiErr("DPX save '%s': %s", filename.cstr(), err.name().cstr());
                        return err;
                }
        }

        // 7. Preallocate (best-effort)
        file.preallocate(0, static_cast<int64_t>(totalSize));

        // 8. Prepare image write buffer.
        //    - ARGB8 needs byte-swap to RGBA for DPX descriptor 51.
        //    - DIO padding needs a separate aligned buffer when imagePadded > imageBytes.
        bool        needPixelSwap = (pdId == PixelFormat::ARGB8_sRGB);
        Buffer      imgWriteBuf;
        const void *imgWritePtr = plane0.data();
        if (needPixelSwap || imagePadded > imageBytes) {
                imgWriteBuf = Buffer(imagePadded, DPX_ALIGN);
                imgWriteBuf.fill(0);
                std::memcpy(imgWriteBuf.data(), plane0.data(), imageBytes);
                if (needPixelSwap) {
                        swapARGBtoRGBA(static_cast<uint8_t *>(imgWriteBuf.data()), width * height);
                }
                imgWritePtr = imgWriteBuf.data();
        }

        File::IOVec iov[2];
        iov[0] = {hdrBuf.data(), headerSize};
        iov[1] = {imgWritePtr, imagePadded};

        int64_t written = file.writev(iov, 2);
        if (written != static_cast<int64_t>(totalSize)) {
                promekiErr("DPX save '%s': short write (%lld of %zu)", filename.cstr(), (long long)written, totalSize);
                file.close();
                return Error::IOError;
        }

        // 9. Truncate excess padding
        if (imagePadded > imageBytes) {
                file.truncate(static_cast<int64_t>(headerSize + imageBytes));
        }

        file.close();
        promekiDebug("DPX save '%s': %zux%zu %s", filename.cstr(), width, height, idesc.pixelFormat().name().cstr());
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
