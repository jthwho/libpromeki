/**
 * @file      frame.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/frame.h>
#include <promeki/mediadesc.h>
#include <promeki/mediapacket.h>
#include <promeki/buffer.h>
#include <promeki/pixeldesc.h>
#include <promeki/enums.h>

using namespace promeki;

TEST_CASE("Frame: default construction") {
        Frame f;
        CHECK(f.imageList().isEmpty());
        CHECK(f.audioList().isEmpty());
        CHECK(f.packetList().isEmpty());
}

TEST_CASE("Frame: metadata access") {
        Frame f;
        const auto &md = f.metadata();
        CHECK(md.isEmpty());
}

TEST_CASE("Frame: videoFormat uses FrameRate metadata and per-image scan") {
        Frame f;
        f.metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_29_97));

        ImageDesc hdDesc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB);
        hdDesc.setVideoScanMode(VideoScanMode::InterlacedEvenFirst);
        f.imageList().pushToBack(Image::Ptr::create(hdDesc));

        ImageDesc uhdDesc(Size2Du32(3840, 2160), PixelDesc::RGBA8_sRGB);
        uhdDesc.setVideoScanMode(VideoScanMode::Progressive);
        f.imageList().pushToBack(Image::Ptr::create(uhdDesc));

        VideoFormat v0 = f.videoFormat(0);
        CHECK(v0.isValid());
        CHECK(v0.raster() == Size2Du32(1920, 1080));
        CHECK(v0.frameRate() == FrameRate(FrameRate::FPS_29_97));
        CHECK(v0.videoScanMode() == VideoScanMode::InterlacedEvenFirst);
        CHECK(v0.toString() == "1080i59.94");

        VideoFormat v1 = f.videoFormat(1);
        CHECK(v1.raster() == Size2Du32(3840, 2160));
        CHECK(v1.toString() == "2160p29.97");
}

TEST_CASE("Frame: videoFormat returns invalid for out-of-range or missing rate") {
        Frame f;
        CHECK_FALSE(f.videoFormat(0).isValid());

        auto img = Image::Ptr::create(
                ImageDesc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB));
        f.imageList().pushToBack(img);

        // FrameRate metadata missing — videoFormat cannot be built.
        CHECK_FALSE(f.videoFormat(0).isValid());

        // Out-of-range.
        f.metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_24));
        CHECK(f.videoFormat(0).isValid());
        CHECK_FALSE(f.videoFormat(1).isValid());
        CHECK_FALSE(f.videoFormat(99).isValid());
}

TEST_CASE("Frame: mediaDesc assembles from state") {
        Frame f;
        f.metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_24));
        f.metadata().set(Metadata::Title, String("clip"));

        ImageDesc imgDesc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB);
        imgDesc.setVideoScanMode(VideoScanMode::Progressive);
        f.imageList().pushToBack(Image::Ptr::create(imgDesc));

        auto aud = Audio::Ptr::create(
                AudioDesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2), 1024);
        f.audioList().pushToBack(aud);

        MediaDesc md = f.mediaDesc();
        CHECK(md.isValid());
        CHECK(md.frameRate() == FrameRate(FrameRate::FPS_24));
        REQUIRE(md.imageList().size() == 1);
        CHECK(md.imageList()[0].size() == Size2Du32(1920, 1080));
        CHECK(md.imageList()[0].videoScanMode() == VideoScanMode::Progressive);
        REQUIRE(md.audioList().size() == 1);
        CHECK(md.audioList()[0].sampleRate() == 48000.0f);
        CHECK(md.metadata().get(Metadata::Title).get<String>() == "clip");

        // Round-trip through MediaDesc::videoFormat matches Frame::videoFormat.
        CHECK(md.videoFormat(0) == f.videoFormat(0));
}

TEST_CASE("Frame: mediaDesc on empty frame is invalid") {
        Frame f;
        MediaDesc md = f.mediaDesc();
        CHECK_FALSE(md.isValid());
        CHECK(md.imageList().isEmpty());
        CHECK(md.audioList().isEmpty());
}

TEST_CASE("Frame: makeString resolves metadata, frame pseudo, and Image[N]/Audio[N] subscripts") {
        Frame f;
        f.metadata().set(Metadata::Title, String("clip"));
        f.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));

        ImageDesc imgDesc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB);
        imgDesc.setVideoScanMode(VideoScanMode::Progressive);
        Image::Ptr img = Image::Ptr::create(imgDesc);
        img.modify()->metadata().set(Metadata::FrameNumber, int64_t(42));
        f.imageList().pushToBack(img);

        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        Audio::Ptr aud = Audio::Ptr::create(adesc, 1024);
        aud.modify()->metadata().set(Metadata::Album, String("studio"));
        f.audioList().pushToBack(aud);

        // Direct frame metadata + frame pseudo keys.
        CHECK(f.makeString("[{Timecode:smpte}] {Title}")
              == String("[01:00:00:00] clip"));
        CHECK(f.makeString("imgs={@ImageCount} auds={@AudioCount} pkts={@PacketCount}")
              == String("imgs=1 auds=1 pkts=0"));

        // Subscripted descent into image: pseudo and metadata keys both work.
        CHECK(f.makeString("{Image[0].@Size} {Image[0].@PixelDesc}")
              == String("1920x1080 RGBA8_sRGB"));
        CHECK(f.makeString("frame#{Image[0].FrameNumber}")
              == String("frame#42"));

        // Subscripted descent into audio.
        CHECK(f.makeString("{Audio[0].@Channels}ch x {Audio[0].@Samples}")
              == String("2ch x 1024"));
        CHECK(f.makeString("album={Audio[0].Album}")
              == String("album=studio"));

        // @VideoFormat / @VideoFormat[N] reuse the frame's FrameRate.
        f.metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_29_97));
        ImageDesc imgDesc2(Size2Du32(3840, 2160), PixelDesc::RGBA8_sRGB);
        imgDesc2.setVideoScanMode(VideoScanMode::Progressive);
        f.imageList().pushToBack(Image::Ptr::create(imgDesc2));

        CHECK(f.makeString("{@VideoFormat:smpte}") == String("1080p29.97"));
        CHECK(f.makeString("{@VideoFormat[0]:smpte}") == String("1080p29.97"));
        CHECK(f.makeString("{@VideoFormat[1]:smpte}") == String("2160p29.97"));

        // Out-of-range @VideoFormat[N] falls through.
        Error errVf;
        String sVf = f.makeString("{@VideoFormat[9]}", &errVf);
        CHECK(errVf == Error::IdNotFound);
        CHECK(sVf == String("[UNKNOWN KEY: @VideoFormat[9]]"));

        // Out-of-range subscript falls through to UNKNOWN.
        Error err;
        String s = f.makeString("{Image[5].@Width}", &err);
        CHECK(err == Error::IdNotFound);
        CHECK(s == String("[UNKNOWN KEY: Image[5].@Width]"));

        // User-supplied resolver fires for keys nothing else can resolve.
        Error err2;
        String s2 = f.makeString("hello {Custom}",
                [](const String &key, const String &) -> std::optional<String> {
                        if(key == String("Custom")) return String("world");
                        return std::nullopt;
                }, &err2);
        CHECK(err2.isOk());
        CHECK(s2 == String("hello world"));
}

TEST_CASE("Image: makeString resolves metadata and pseudo keys") {
        ImageDesc desc(Size2Du32(640, 480), PixelDesc::RGBA8_sRGB);
        desc.setLinePad(8);
        desc.setLineAlign(16);
        desc.setVideoScanMode(VideoScanMode::Progressive);
        Image img(desc);
        img.metadata().set(Metadata::Title, String("frame0"));

        CHECK(img.makeString("{@Width}x{@Height}") == String("640x480"));
        CHECK(img.makeString("{@Size}") == String("640x480"));
        CHECK(img.makeString("{@PixelDesc} planes={@PlaneCount}")
              == String("RGBA8_sRGB planes=1"));
        CHECK(img.makeString("pad={@LinePad} align={@LineAlign}")
              == String("pad=8 align=16"));
        CHECK(img.makeString("{Title} - {@ScanMode}")
              == String("frame0 - Progressive"));

        // Format specs flow through Variant::format.
        CHECK(img.makeString("{@Width:05}") == String("00640"));
}

TEST_CASE("Audio: makeString resolves metadata and pseudo keys") {
        AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        Audio aud(desc, 256);
        aud.metadata().set(Metadata::Album, String("LiveSet"));

        CHECK(aud.makeString("{@SampleRate:.1f}Hz {@Channels}ch")
              == String("48000.0Hz 2ch"));
        CHECK(aud.makeString("{@Samples}/{@MaxSamples} ({@Frames})")
              == String("256/256 (512)"));
        CHECK(aud.makeString("type={@DataType} compressed={@IsCompressed}")
              == String("type=PCMI_S16LE compressed=false"));
        CHECK(aud.makeString("{Album}") == String("LiveSet"));
}

TEST_CASE("Frame: packetList carries compressed access units") {
        Frame f;
        auto buf = Buffer::Ptr::create(32);
        buf.modify()->setSize(16);
        auto pkt = MediaPacket::Ptr::create(buf, PixelDesc(PixelDesc::H264));
        pkt.modify()->addFlag(MediaPacket::Keyframe);

        f.packetList().pushToBack(pkt);

        // Const accessor returns the same element.
        const Frame &cf = f;
        REQUIRE(cf.packetList().size() == 1);
        CHECK(cf.packetList().at(0)->isKeyframe());
        CHECK(cf.packetList().at(0)->pixelDesc().id() == PixelDesc::H264);
}
