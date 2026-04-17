/**
 * @file      imagefileio_cin.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/file.h>
#include <promeki/buffer.h>

using namespace promeki;

// Reproduce the Cineon header struct for test construction
#pragma pack(push, 1)
struct TestCineonChanInfo {
        uint8_t  desc[2];
        uint8_t  bpp;
        uint8_t  _align;
        uint32_t width;
        uint32_t height;
        float    datamin;
        float    quantmin;
        float    datamax;
        float    quantmax;
};

struct TestCineonHeader {
        // File info
        struct {
                uint32_t magic;
                uint32_t offset;
                uint32_t generic;
                uint32_t industry;
                uint32_t user;
                uint32_t size;
                char     version[8];
                char     name[100];
                char     date[12];
                char     time[12];
                uint8_t  _reserved[36];
        } file;
        // Image info
        struct {
                uint8_t              orient;
                uint8_t              nchan;
                uint8_t              _align[2];
                TestCineonChanInfo   chan[8];
                float                white[2];
                float                red[2];
                float                green[2];
                float                blue[2];
                char                 label[200];
                uint8_t              _reserved[28];
        } img;
        // Image format
        struct {
                uint8_t  interleave;
                uint8_t  packing;
                uint8_t  issigned;
                uint8_t  isnegative;
                uint32_t eolpad;
                uint32_t eocpad;
                uint8_t  _reserved[20];
        } fmt;
        // Image orientation
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
        // Film info
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

// Big-endian encode helpers
static uint32_t toBE32(uint32_t v) {
        uint8_t b[4];
        b[0] = (v >> 24) & 0xFF;
        b[1] = (v >> 16) & 0xFF;
        b[2] = (v >>  8) & 0xFF;
        b[3] = v & 0xFF;
        uint32_t r;
        std::memcpy(&r, b, 4);
        return r;
}

TEST_CASE("ImageFileIO Cineon: handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::Cineon);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK_FALSE(io->canSave());
        CHECK(io->name() == "Cineon");
}

TEST_CASE("ImageFileIO Cineon: load synthetic file") {
        const char *fn = "/tmp/promeki_cin_synth.cin";
        const size_t w = 64, h = 48;

        PixelDesc pd(PixelDesc::RGB10_DPX_sRGB);
        size_t imageBytes = pd.pixelFormat().planeSize(0, w, h);
        size_t headerSize = sizeof(TestCineonHeader);
        size_t totalSize = headerSize + imageBytes;

        // Build big-endian Cineon header
        TestCineonHeader hdr;
        std::memset(&hdr, 0xFF, sizeof(hdr));

        hdr.file.magic   = toBE32(0x802A5FD7);
        hdr.file.offset  = toBE32(static_cast<uint32_t>(headerSize));
        hdr.file.generic = toBE32(static_cast<uint32_t>(headerSize));
        hdr.file.industry = toBE32(0);
        hdr.file.user    = toBE32(0);
        hdr.file.size    = toBE32(static_cast<uint32_t>(totalSize));
        std::memset(hdr.file.version, 0, sizeof(hdr.file.version));
        std::memcpy(hdr.file.version, "V4.5", 4);
        std::memset(hdr.file.name, 0, sizeof(hdr.file.name));
        std::memset(hdr.file.date, 0, sizeof(hdr.file.date));
        std::memset(hdr.file.time, 0, sizeof(hdr.file.time));

        hdr.img.orient = 0;
        hdr.img.nchan = 3;
        hdr.img._align[0] = 0;
        hdr.img._align[1] = 0;

        // Set channel 0 info (channels 1-7 stay 0xFF)
        std::memset(&hdr.img.chan[0], 0, sizeof(TestCineonChanInfo));
        hdr.img.chan[0].desc[0] = 0;
        hdr.img.chan[0].desc[1] = 1;
        hdr.img.chan[0].bpp = 10;
        hdr.img.chan[0].width  = toBE32(static_cast<uint32_t>(w));
        hdr.img.chan[0].height = toBE32(static_cast<uint32_t>(h));

        hdr.fmt.interleave = 0;
        hdr.fmt.packing = 5;
        hdr.fmt.issigned = 0;
        hdr.fmt.isnegative = 0;

        // Write file
        Buffer fileBuf(totalSize);
        std::memcpy(fileBuf.data(), &hdr, headerSize);
        uint8_t *imgData = static_cast<uint8_t *>(fileBuf.data()) + headerSize;
        for(size_t i = 0; i < imageBytes; ++i) {
                imgData[i] = static_cast<uint8_t>((i * 13) & 0xFF);
        }

        {
                File file(fn);
                Error err = file.open(File::WriteOnly, File::Create | File::Truncate);
                REQUIRE(err == Error::Ok);
                file.write(fileBuf.data(), static_cast<int64_t>(totalSize));
                file.close();
        }

        // Load
        ImageFile lf(ImageFile::Cineon);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        Image img = lf.image();
        REQUIRE(img.isValid());
        CHECK(img.width() == w);
        CHECK(img.height() == h);
        CHECK(img.pixelDesc().id() == PixelDesc::RGB10_DPX_sRGB);
        CHECK(std::memcmp(img.data(), imgData, imageBytes) == 0);

        std::remove(fn);
}

TEST_CASE("ImageFileIO Cineon: load nonexistent file returns error") {
        ImageFile lf(ImageFile::Cineon);
        lf.setFilename("/tmp/promeki_cin_nonexist.cin");
        CHECK(lf.load() != Error::Ok);
}

TEST_CASE("ImageFileIO Cineon: load invalid file returns error") {
        const char *fn = "/tmp/promeki_cin_bad.cin";
        FILE *fp = std::fopen(fn, "wb");
        REQUIRE(fp);
        const char garbage[] = "Not a Cineon file at all";
        std::fwrite(garbage, 1, sizeof(garbage), fp);
        std::fclose(fp);

        ImageFile lf(ImageFile::Cineon);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}
