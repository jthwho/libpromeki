/**
 * @file      tests/mediaiotask_videoencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Integration tests for the generic video-encoder MediaIO backend.
 * Uses the in-process "Passthrough" VideoEncoder registered by
 * tests/videocodec.cpp so the checks run everywhere — no GPU, NVENC
 * runtime, or Video Codec SDK required.  A separate device-gated
 * NVENC integration exercise belongs with the CUDA + NVENC tests.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_videoencoder.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/imagedesc.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/pixeldesc.h>
#include <promeki/buffer.h>
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

TEST_CASE("MediaIOTask_VideoEncoder: backend is registered under \"VideoEncoder\"") {
        const MediaIO::FormatDesc *fd = nullptr;
        for(const auto &f : MediaIO::registeredFormats()) {
                if(f.name == "VideoEncoder") { fd = &f; break; }
        }
        REQUIRE(fd != nullptr);
        CHECK(fd->canBeTransform);
        CHECK_FALSE(fd->canBeSink);
        CHECK_FALSE(fd->canBeSource);
}

TEST_CASE("MediaIOTask_VideoEncoder: open requires VideoCodec") {
        MediaIO::Config cfg = MediaIO::defaultConfig("VideoEncoder");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc srcDesc;
        srcDesc.imageList().pushToBack(
                ImageDesc(Size2Du32(8, 4), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setExpectedDesc(srcDesc);

        // VideoCodec empty in cfg => open must fail.
        Error err = io->open(MediaIO::Transform);
        CHECK(err.isError());
        delete io;
}

TEST_CASE("MediaIOTask_VideoEncoder: open rejects a codec without an encoder factory") {
        MediaIO::Config cfg = MediaIO::defaultConfig("VideoEncoder");
        cfg.set(MediaConfig::VideoCodec, VideoCodec(VideoCodec::VP9));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc srcDesc;
        srcDesc.imageList().pushToBack(
                ImageDesc(Size2Du32(8, 4), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setExpectedDesc(srcDesc);

        Error err = io->open(MediaIO::Transform);
        CHECK(err == Error::NotSupported);
        delete io;
}

TEST_CASE("MediaIOTask_VideoEncoder: write -> read emits a packet via passthrough codec") {
        MediaIO::Config cfg = MediaIO::defaultConfig("VideoEncoder");
        cfg.set(MediaConfig::VideoCodec, VideoCodec::lookup("Passthrough"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        constexpr int kW = 16;
        constexpr int kH = 8;
        MediaDesc srcDesc;
        srcDesc.imageList().pushToBack(
                ImageDesc(Size2Du32(kW, kH), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setExpectedDesc(srcDesc);

        REQUIRE(io->open(MediaIO::Transform) == Error::Ok);

        // Output MediaDesc should have substituted the encoder's
        // output PixelDesc (the passthrough codec pins it to H264).
        MediaDesc outDesc = io->mediaDesc();
        REQUIRE(!outDesc.imageList().isEmpty());
        CHECK(outDesc.imageList()[0].pixelDesc().id() == PixelDesc::H264);

        // Build a source Frame with a single RGB image and push it
        // through writeFrame → readFrame.
        Image src = makeRgb8Frame(kW, kH, 0x42);
        Frame::Ptr inFrame = Frame::Ptr::create();
        inFrame.modify()->imageList().pushToBack(Image::Ptr::create(src));

        Error werr = io->writeFrame(inFrame, true);
        REQUIRE(werr == Error::Ok);

        Frame::Ptr outFrame;
        Error rerr = io->readFrame(outFrame, true);
        REQUIRE(rerr == Error::Ok);
        REQUIRE(outFrame);
        REQUIRE(outFrame->imageList().size() == 1);

        const Image &outImg = *outFrame->imageList()[0];
        REQUIRE(outImg.isCompressed());
        REQUIRE(outImg.packet().isValid());
        const MediaPacket &pkt = *outImg.packet();
        CHECK(pkt.isKeyframe());
        CHECK(pkt.size() == src.plane(0)->size());

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_VideoEncoder: empty output queue returns TryAgain before close") {
        MediaIO::Config cfg = MediaIO::defaultConfig("VideoEncoder");
        cfg.set(MediaConfig::VideoCodec, VideoCodec::lookup("Passthrough"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc srcDesc;
        srcDesc.imageList().pushToBack(
                ImageDesc(Size2Du32(8, 4), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setExpectedDesc(srcDesc);
        REQUIRE(io->open(MediaIO::Transform) == Error::Ok);

        // Non-blocking read before any write — queue is empty, should
        // report TryAgain so the pipeline can wait for more input.
        Frame::Ptr outFrame;
        Error err = io->readFrame(outFrame, false);
        CHECK(err == Error::TryAgain);
        CHECK_FALSE(outFrame);

        io->close();
        delete io;
}
