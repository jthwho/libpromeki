/**
 * @file      tests/mediaiotask_videodecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Integration tests for the generic video-decoder MediaIO backend,
 * driven via the in-process passthrough VideoDecoder defined in
 * tests/videocodec.cpp.  No GPU / NVDEC runtime involvement.
 */

#include <doctest/doctest.h>
#include <promeki/config.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_videoencoder.h>
#include <promeki/mediaiotask_videodecoder.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/imagedesc.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/imagefile.h>
#include <promeki/mediapacket.h>
#include <promeki/pixeldesc.h>
#include <promeki/buffer.h>
#include <promeki/videocodec.h>
#include <cstdio>
#include <cstring>

using namespace promeki;

namespace {

Image makeRgb8Frame(int width, int height, uint8_t fill) {
        PixelDesc pd(PixelDesc::RGB8_sRGB);
        const size_t bytes = pd.pixelFormat().planeSize(0, width, height);
        auto buf = Buffer::Ptr::create(bytes);
        buf.modify()->fill(static_cast<char>(fill));
        buf.modify()->setSize(bytes);
        return Image::fromBuffer(buf, width, height, pd);
}

} // namespace

TEST_CASE("MediaIOTask_VideoDecoder: backend is registered under \"VideoDecoder\"") {
        const MediaIO::FormatDesc *fd = nullptr;
        for(const auto &f : MediaIO::registeredFormats()) {
                if(f.name == "VideoDecoder") { fd = &f; break; }
        }
        REQUIRE(fd != nullptr);
        CHECK(fd->canInputAndOutput);
}

TEST_CASE("MediaIOTask_VideoDecoder: open without VideoCodec defers to auto-detect") {
        MediaIO::Config cfg = MediaIO::defaultConfig("VideoDecoder");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc srcDesc;
        srcDesc.imageList().pushToBack(
                ImageDesc(Size2Du32(8, 4), PixelDesc(PixelDesc::H264)));
        io->setMediaDesc(srcDesc);

        Error err = io->open(MediaIO::InputAndOutput);
        CHECK(!err.isError());
        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_VideoDecoder: encoder → decoder round-trip via passthrough") {
        constexpr int kW = 16;
        constexpr int kH = 8;

        // -- Encoder stage (passthrough codec) --
        MediaIO::Config encCfg = MediaIO::defaultConfig("VideoEncoder");
        encCfg.set(MediaConfig::VideoCodec, VideoCodec::lookup("Passthrough"));
        MediaIO *enc = MediaIO::create(encCfg);
        REQUIRE(enc != nullptr);
        MediaDesc srcDesc;
        srcDesc.imageList().pushToBack(
                ImageDesc(Size2Du32(kW, kH), PixelDesc(PixelDesc::RGB8_sRGB)));
        enc->setMediaDesc(srcDesc);
        REQUIRE(enc->open(MediaIO::InputAndOutput) == Error::Ok);

        // Push an input image through the encoder and retrieve the
        // compressed Frame on the other side.
        Image src = makeRgb8Frame(kW, kH, 0x37);
        Frame::Ptr inFrame = Frame::Ptr::create();
        inFrame.modify()->imageList().pushToBack(Image::Ptr::create(src));
        REQUIRE(enc->writeFrame(inFrame, true) == Error::Ok);

        Frame::Ptr encodedFrame;
        REQUIRE(enc->readFrame(encodedFrame, true) == Error::Ok);
        REQUIRE(encodedFrame);
        REQUIRE(encodedFrame->imageList().size() == 1);
        REQUIRE(encodedFrame->imageList()[0]->isCompressed());
        REQUIRE(encodedFrame->imageList()[0]->packet().isValid());

        // -- Decoder stage (passthrough codec) --
        MediaIO::Config decCfg = MediaIO::defaultConfig("VideoDecoder");
        decCfg.set(MediaConfig::VideoCodec, VideoCodec::lookup("Passthrough"));
        // PassthroughVideoDecoder reads VideoSize + OutputPixelDesc
        // out of the MediaConfig at configure() time to recreate the
        // uncompressed Image it rebuilds in receiveFrame().
        decCfg.set(MediaConfig::VideoSize, Size2Du32(kW, kH));
        decCfg.set(MediaConfig::OutputPixelDesc,
                   PixelDesc(PixelDesc::RGB8_sRGB));
        MediaIO *dec = MediaIO::create(decCfg);
        REQUIRE(dec != nullptr);
        MediaDesc encDesc = enc->mediaDesc();
        dec->setMediaDesc(encDesc);
        REQUIRE(dec->open(MediaIO::InputAndOutput) == Error::Ok);

        REQUIRE(dec->writeFrame(encodedFrame, true) == Error::Ok);

        Frame::Ptr decodedFrame;
        REQUIRE(dec->readFrame(decodedFrame, true) == Error::Ok);
        REQUIRE(decodedFrame);
        REQUIRE(decodedFrame->imageList().size() == 1);

        const Image &round = *decodedFrame->imageList()[0];
        REQUIRE(round.isValid());
        CHECK(round.width() == kW);
        CHECK(round.height() == kH);

        // Payload bytes must survive byte-for-byte through the
        // encoder → packet → decoder path.
        REQUIRE(src.plane(0));
        REQUIRE(round.plane(0));
        CHECK(src.plane(0)->size() == round.plane(0)->size());
        const auto *srcB = static_cast<const uint8_t *>(src.plane(0)->data());
        const auto *dstB = static_cast<const uint8_t *>(round.plane(0)->data());
        for(size_t i = 0; i < src.plane(0)->size(); ++i) {
                CHECK(srcB[i] == dstB[i]);
        }

        dec->close();
        enc->close();
        delete dec;
        delete enc;
}

#if PROMEKI_ENABLE_JPEGXS
// Full end-to-end path for an intraframe codec: a compressed JPEG XS
// file loaded via ImageFile carries its bitstream as an attached
// MediaPacket on the compressed Image, which is what
// MediaIOTask_VideoDecoder consumes.  Both the explicit VideoCodec
// route and the auto-detect route must produce an uncompressed
// Image from the loaded Frame.
TEST_CASE("MediaIOTask_VideoDecoder: decodes a JPEG XS file via Image::packet") {
        constexpr int kW = 128;
        constexpr int kH = 96;

        // Write a real JPEG XS bitstream to a scratch file.
        const char *fn = "/tmp/promeki_mediaio_jxs_decode.jxs";
        Image src(kW, kH, PixelDesc(PixelDesc::YUV8_422_Planar_Rec709));
        REQUIRE(src.isValid());
        {
                uint8_t *luma = static_cast<uint8_t *>(src.data(0));
                const size_t stride = src.lineStride(0);
                for(int y = 0; y < kH; ++y) {
                        uint8_t *row = luma + y * stride;
                        for(int x = 0; x < kW; ++x) {
                                row[x] = static_cast<uint8_t>(16 + (x * 219) / kW);
                        }
                }
        }
        ImageFile sf(ImageFile::JpegXS);
        sf.setFilename(fn);
        sf.setImage(src);
        REQUIRE(sf.save() == Error::Ok);

        // Load it back — the Frame must carry a compressed Image with
        // an attached MediaPacket (the upstream invariant the decoder
        // relies on).
        ImageFile lf(ImageFile::JpegXS);
        lf.setFilename(fn);
        REQUIRE(lf.load() == Error::Ok);
        Frame::Ptr inFrame = Frame::Ptr::create(lf.frame());
        REQUIRE(!inFrame->imageList().isEmpty());
        REQUIRE(inFrame->imageList()[0]->isCompressed());
        REQUIRE(inFrame->imageList()[0]->packet().isValid());

        auto runDecoder = [&](bool explicitCodec) -> Frame::Ptr {
                MediaIO::Config cfg = MediaIO::defaultConfig("VideoDecoder");
                if(explicitCodec) {
                        cfg.set(MediaConfig::VideoCodec,
                                VideoCodec(VideoCodec::JPEG_XS));
                }
                MediaIO *dec = MediaIO::create(cfg);
                REQUIRE(dec != nullptr);

                MediaDesc srcDesc;
                srcDesc.imageList().pushToBack(
                        ImageDesc(Size2Du32(kW, kH),
                                  PixelDesc(PixelDesc::JPEG_XS_YUV8_422_Rec709)));
                dec->setMediaDesc(srcDesc);
                REQUIRE(dec->open(MediaIO::InputAndOutput) == Error::Ok);

                REQUIRE(dec->writeFrame(inFrame, true) == Error::Ok);

                Frame::Ptr out;
                REQUIRE(dec->readFrame(out, true) == Error::Ok);
                dec->close();
                delete dec;
                return out;
        };

        {
                Frame::Ptr decoded = runDecoder(/*explicitCodec=*/true);
                REQUIRE(decoded);
                REQUIRE(decoded->imageList().size() == 1);
                const Image &img = *decoded->imageList()[0];
                CHECK(img.isValid());
                CHECK(!img.isCompressed());
                CHECK(img.width() == kW);
                CHECK(img.height() == kH);
        }

        {
                Frame::Ptr decoded = runDecoder(/*explicitCodec=*/false);
                REQUIRE(decoded);
                REQUIRE(decoded->imageList().size() == 1);
                const Image &img = *decoded->imageList()[0];
                CHECK(img.isValid());
                CHECK(!img.isCompressed());
                CHECK(img.width() == kW);
                CHECK(img.height() == kH);
        }

        std::remove(fn);
}
#endif // PROMEKI_ENABLE_JPEGXS
