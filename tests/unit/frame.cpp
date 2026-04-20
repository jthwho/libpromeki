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

TEST_CASE("Frame: makeString resolves metadata, frame scalars, and Image[N]/Audio[N] subscripts") {
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

        // Frame metadata (under "Meta.") + frame scalar keys.
        CHECK(VariantLookup<Frame>::format(f, "[{Meta.Timecode:smpte}] {Meta.Title}")
              == String("[01:00:00:00] clip"));
        CHECK(VariantLookup<Frame>::format(f, "imgs={ImageCount} auds={AudioCount}")
              == String("imgs=1 auds=1"));

        // Subscripted descent into image: scalars and metadata both work.
        CHECK(VariantLookup<Frame>::format(f, "{Image[0].Size} {Image[0].PixelDesc}")
              == String("1920x1080 RGBA8_sRGB"));
        CHECK(VariantLookup<Frame>::format(f, "frame#{Image[0].Meta.FrameNumber}")
              == String("frame#42"));

        // Subscripted descent into audio.
        CHECK(VariantLookup<Frame>::format(f, "{Audio[0].Channels}ch x {Audio[0].Samples}")
              == String("2ch x 1024"));
        CHECK(VariantLookup<Frame>::format(f, "album={Audio[0].Meta.Album}")
              == String("album=studio"));

        // VideoFormat / VideoFormat[N] reuse the frame's FrameRate.
        f.metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_29_97));
        ImageDesc imgDesc2(Size2Du32(3840, 2160), PixelDesc::RGBA8_sRGB);
        imgDesc2.setVideoScanMode(VideoScanMode::Progressive);
        f.imageList().pushToBack(Image::Ptr::create(imgDesc2));

        CHECK(VariantLookup<Frame>::format(f, "{VideoFormat:smpte}") == String("1080p29.97"));
        CHECK(VariantLookup<Frame>::format(f, "{VideoFormat[0]:smpte}") == String("1080p29.97"));
        CHECK(VariantLookup<Frame>::format(f, "{VideoFormat[1]:smpte}") == String("2160p29.97"));

        // Out-of-range VideoFormat[N] falls through.
        Error errVf;
        String sVf = VariantLookup<Frame>::format(f, "{VideoFormat[9]}", &errVf);
        CHECK(errVf == Error::IdNotFound);
        CHECK(sVf == String("[UNKNOWN KEY: VideoFormat[9]]"));

        // Out-of-range subscript falls through to UNKNOWN.
        Error err;
        String s = VariantLookup<Frame>::format(f, "{Image[5].Width}", &err);
        CHECK(err == Error::IdNotFound);
        CHECK(s == String("[UNKNOWN KEY: Image[5].Width]"));

        // User-supplied resolver fires for keys nothing else can resolve.
        Error err2;
        String s2 = VariantLookup<Frame>::format(f, "hello {Custom}",
                [](const String &key, const String &) -> std::optional<String> {
                        if(key == String("Custom")) return String("world");
                        return std::nullopt;
                }, &err2);
        CHECK(err2.isOk());
        CHECK(s2 == String("hello world"));
}

TEST_CASE("Image: makeString resolves metadata and scalar keys") {
        ImageDesc desc(Size2Du32(640, 480), PixelDesc::RGBA8_sRGB);
        desc.setLinePad(8);
        desc.setLineAlign(16);
        desc.setVideoScanMode(VideoScanMode::Progressive);
        Image img(desc);
        img.metadata().set(Metadata::Title, String("frame0"));

        CHECK(VariantLookup<Image>::format(img, "{Width}x{Height}") == String("640x480"));
        CHECK(VariantLookup<Image>::format(img, "{Size}") == String("640x480"));
        CHECK(VariantLookup<Image>::format(img, "{PixelDesc} planes={PlaneCount}")
              == String("RGBA8_sRGB planes=1"));
        CHECK(VariantLookup<Image>::format(img, "pad={LinePad} align={LineAlign}")
              == String("pad=8 align=16"));
        CHECK(VariantLookup<Image>::format(img, "{Meta.Title} - {ScanMode}")
              == String("frame0 - Progressive"));

        // Format specs flow through Variant::format.
        CHECK(VariantLookup<Image>::format(img, "{Width:05}") == String("00640"));
}

TEST_CASE("Audio: makeString resolves metadata and scalar keys") {
        AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        Audio aud(desc, 256);
        aud.metadata().set(Metadata::Album, String("LiveSet"));

        CHECK(VariantLookup<Audio>::format(aud, "{SampleRate:.1f}Hz {Channels}ch")
              == String("48000.0Hz 2ch"));
        CHECK(VariantLookup<Audio>::format(aud, "{Samples}/{MaxSamples} ({Frames})")
              == String("256/256 (512)"));
        CHECK(VariantLookup<Audio>::format(aud, "type={DataType} compressed={IsCompressed}")
              == String("type=PCMI_S16LE compressed=false"));
        CHECK(VariantLookup<Audio>::format(aud, "{Meta.Album}") == String("LiveSet"));
}

TEST_CASE("Image: resolveKey returns typed Variant values") {
        ImageDesc desc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB);
        desc.setLinePad(8);
        desc.setLineAlign(16);
        desc.setVideoScanMode(VideoScanMode::Progressive);
        Image img(desc);
        img.metadata().set(Metadata::FrameNumber, int64_t(42));

        auto width = VariantLookup<Image>::resolve(img, "Width");
        REQUIRE(width.has_value());
        CHECK(width->get<uint32_t>() == 1920);

        auto height = VariantLookup<Image>::resolve(img, "Height");
        REQUIRE(height.has_value());
        CHECK(height->get<uint32_t>() == 1080);

        auto pd = VariantLookup<Image>::resolve(img, "PixelDesc");
        REQUIRE(pd.has_value());
        CHECK(pd->get<PixelDesc>() == PixelDesc(PixelDesc::RGBA8_sRGB));

        auto planes = VariantLookup<Image>::resolve(img, "PlaneCount");
        REQUIRE(planes.has_value());
        CHECK(planes->get<int32_t>() == 1);

        auto fn = VariantLookup<Image>::resolve(img, "Meta.FrameNumber");
        REQUIRE(fn.has_value());
        CHECK(fn->get<int64_t>() == 42);

        CHECK_FALSE(VariantLookup<Image>::resolve(img, "BogusScalar").has_value());
        CHECK_FALSE(VariantLookup<Image>::resolve(img, "NotAKey").has_value());
        // Unprefixed metadata keys no longer resolve — Meta. required.
        CHECK_FALSE(VariantLookup<Image>::resolve(img, "FrameNumber").has_value());
}

TEST_CASE("Audio: resolveKey returns typed Variant values") {
        AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        Audio aud(desc, 256);
        aud.metadata().set(Metadata::Album, String("LiveSet"));

        auto sr = VariantLookup<Audio>::resolve(aud, "SampleRate");
        REQUIRE(sr.has_value());
        CHECK(sr->get<float>() == 48000.0f);

        auto ch = VariantLookup<Audio>::resolve(aud, "Channels");
        REQUIRE(ch.has_value());
        CHECK(ch->get<uint32_t>() == 2);

        auto samples = VariantLookup<Audio>::resolve(aud, "Samples");
        REQUIRE(samples.has_value());
        CHECK(samples->get<uint64_t>() == 256);

        auto frames = VariantLookup<Audio>::resolve(aud, "Frames");
        REQUIRE(frames.has_value());
        CHECK(frames->get<uint64_t>() == 512);

        auto album = VariantLookup<Audio>::resolve(aud, "Meta.Album");
        REQUIRE(album.has_value());
        CHECK(album->get<String>() == "LiveSet");

        CHECK_FALSE(VariantLookup<Audio>::resolve(aud, "BogusScalar").has_value());
}

TEST_CASE("Frame: resolveKey returns typed Variant values and dispatches subscripts") {
        Frame f;
        f.metadata().set(Metadata::Title, String("clip"));
        f.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 0, 0, 0));
        f.metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_29_97));

        ImageDesc imgDesc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB);
        imgDesc.setVideoScanMode(VideoScanMode::Progressive);
        Image::Ptr img = Image::Ptr::create(imgDesc);
        img.modify()->metadata().set(Metadata::FrameNumber, int64_t(42));
        f.imageList().pushToBack(img);

        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        Audio::Ptr aud = Audio::Ptr::create(adesc, 1024);
        aud.modify()->metadata().set(Metadata::Album, String("studio"));
        f.audioList().pushToBack(aud);

        // Frame scalar keys.
        auto ic = VariantLookup<Frame>::resolve(f, "ImageCount");
        REQUIRE(ic.has_value());
        CHECK(ic->get<uint64_t>() == 1);

        auto ac = VariantLookup<Frame>::resolve(f, "AudioCount");
        REQUIRE(ac.has_value());
        CHECK(ac->get<uint64_t>() == 1);

        auto hb = VariantLookup<Frame>::resolve(f, "HasBenchmark");
        REQUIRE(hb.has_value());
        CHECK(hb->get<bool>() == false);

        auto vf = VariantLookup<Frame>::resolve(f, "VideoFormat");
        REQUIRE(vf.has_value());
        CHECK(vf->get<VideoFormat>().raster() == Size2Du32(1920, 1080));

        // Frame metadata lookup.
        auto title = VariantLookup<Frame>::resolve(f, "Meta.Title");
        REQUIRE(title.has_value());
        CHECK(title->get<String>() == "clip");

        // Subscripted descent into image.
        auto w = VariantLookup<Frame>::resolve(f, "Image[0].Width");
        REQUIRE(w.has_value());
        CHECK(w->get<uint32_t>() == 1920);

        auto fn = VariantLookup<Frame>::resolve(f, "Image[0].Meta.FrameNumber");
        REQUIRE(fn.has_value());
        CHECK(fn->get<int64_t>() == 42);

        // Subscripted descent into audio.
        auto ch = VariantLookup<Frame>::resolve(f, "Audio[0].Channels");
        REQUIRE(ch.has_value());
        CHECK(ch->get<uint32_t>() == 2);

        auto album = VariantLookup<Frame>::resolve(f, "Audio[0].Meta.Album");
        REQUIRE(album.has_value());
        CHECK(album->get<String>() == "studio");

        // Out-of-range / bogus keys all fail cleanly.
        CHECK_FALSE(VariantLookup<Frame>::resolve(f, "Image[5].Width").has_value());
        CHECK_FALSE(VariantLookup<Frame>::resolve(f, "Audio[5].Channels").has_value());
        CHECK_FALSE(VariantLookup<Frame>::resolve(f, "VideoFormat[9]").has_value());
        CHECK_FALSE(VariantLookup<Frame>::resolve(f, "BogusScalar").has_value());
        CHECK_FALSE(VariantLookup<Frame>::resolve(f, "NotAKey").has_value());
}

TEST_CASE("Frame: assign writes through Meta database and child Image metadata") {
        Frame f;
        ImageDesc imgDesc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB);
        f.imageList().pushToBack(Image::Ptr::create(imgDesc));

        // Write into frame-level metadata via Meta. prefix.
        Error err;
        CHECK(VariantLookup<Frame>::assign(f, "Meta.Title",
                                          Variant(String("new title")), &err));
        CHECK(err.isOk());
        CHECK(f.metadata().getAs<String>(Metadata::Title) == "new title");

        // Write into child image's metadata via Image[N].Meta.Key —
        // exercises the indexedChild mutable accessor recursion.
        CHECK(VariantLookup<Frame>::assign(f, "Image[0].Meta.FrameNumber",
                                          Variant(int64_t(99)), &err));
        CHECK(err.isOk());
        CHECK(f.imageList()[0]->metadata().getAs<int64_t>(Metadata::FrameNumber) == 99);

        // Read-only scalar assign fails with ReadOnly.
        CHECK_FALSE(VariantLookup<Frame>::assign(f, "ImageCount",
                                                Variant(uint64_t(5)), &err));
        CHECK(err == Error::ReadOnly);
}

TEST_CASE("Image: dump emits scalar keys, planes, and metadata") {
        ImageDesc desc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB);
        desc.setVideoScanMode(VideoScanMode::Progressive);
        Image img(desc);
        img.metadata().set(Metadata::Title, String("frame0"));

        StringList lines = img.dump();
        REQUIRE_FALSE(lines.isEmpty());

        // Flatten to a single string for substring checks — exact
        // order/layout is intentionally not asserted so the dump can
        // grow without brittle test churn.
        String joined;
        for(const String &ln : lines) { joined += ln; joined += '\n'; }
        CHECK(joined.contains("Size: 1920x1080"));
        CHECK(joined.contains("PixelDesc: RGBA8_sRGB"));
        CHECK(joined.contains("PlaneCount: 1"));
        CHECK(joined.contains("Plane[0]:"));
        CHECK(joined.contains("Meta:"));
        CHECK(joined.contains("Title"));
        CHECK(joined.contains("frame0"));
}

TEST_CASE("Image: dump respects indent") {
        Image img(ImageDesc(Size2Du32(320, 240), PixelDesc::RGBA8_sRGB));
        StringList lines = img.dump(String("    "));
        REQUIRE_FALSE(lines.isEmpty());
        for(const String &ln : lines) CHECK(ln.startsWith("    "));
}

TEST_CASE("Audio: dump emits scalar keys, buffer, and metadata") {
        AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        Audio aud(desc, 256);
        aud.metadata().set(Metadata::Album, String("LiveSet"));

        StringList lines = aud.dump();
        REQUIRE_FALSE(lines.isEmpty());
        String joined;
        for(const String &ln : lines) { joined += ln; joined += '\n'; }
        CHECK(joined.contains("SampleRate"));
        CHECK(joined.contains("48000"));
        CHECK(joined.contains("Channels: 2"));
        CHECK(joined.contains("Samples: 256"));
        CHECK(joined.contains("DataType: PCMI_S16LE"));
        CHECK(joined.contains("Buffer:"));
        CHECK(joined.contains("Meta:"));
        CHECK(joined.contains("Album"));
        CHECK(joined.contains("LiveSet"));
}

TEST_CASE("Frame: dump includes scalar keys, metadata, images, audio") {
        Frame f;
        f.metadata().set(Metadata::Title, String("clip"));
        f.metadata().set(Metadata::FrameRate, FrameRate(FrameRate::FPS_24));

        ImageDesc idesc(Size2Du32(1920, 1080), PixelDesc::RGBA8_sRGB);
        idesc.setVideoScanMode(VideoScanMode::Progressive);
        Image::Ptr img = Image::Ptr::create(idesc);
        img.modify()->metadata().set(Metadata::FrameNumber, int64_t(42));
        f.imageList().pushToBack(img);

        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        Audio::Ptr aud = Audio::Ptr::create(adesc, 1024);
        f.audioList().pushToBack(aud);

        StringList lines = f.dump();
        REQUIRE_FALSE(lines.isEmpty());
        // First line is the first scalar-key, not a "Frame:" header.
        // The caller is responsible for printing a header if needed —
        // this keeps multi-frame and single-frame layouts consistent.
        CHECK(lines[0].contains(":"));

        String joined;
        for(const String &ln : lines) { joined += ln; joined += '\n'; }
        CHECK(joined.contains("ImageCount: 1"));
        CHECK(joined.contains("AudioCount: 1"));
        CHECK(joined.contains("Meta:"));
        CHECK(joined.contains("Title"));
        CHECK(joined.contains("clip"));
        CHECK(joined.contains("Image[0]:"));
        CHECK(joined.contains("1920x1080"));
        CHECK(joined.contains("Audio[0]:"));
        CHECK(joined.contains("Channels: 2"));
}

TEST_CASE("Frame: dump surfaces configUpdate when non-empty") {
        Frame f;
        f.configUpdate().set(MediaConfig::OutputFrameRate, FrameRate(FrameRate::FPS_30));

        StringList lines = f.dump();
        String joined;
        for(const String &ln : lines) { joined += ln; joined += '\n'; }
        CHECK(joined.contains("ConfigUpdate:"));
        CHECK(joined.contains("OutputFrameRate"));
}

TEST_CASE("Frame: dump on empty frame omits optional sections") {
        Frame f;
        StringList lines = f.dump();
        String joined;
        for(const String &ln : lines) { joined += ln; joined += '\n'; }
        CHECK(joined.contains("ImageCount: 0"));
        CHECK(joined.contains("AudioCount: 0"));
        // No metadata, no config update, no images, no audio.
        CHECK_FALSE(joined.contains("Meta:"));
        CHECK_FALSE(joined.contains("ConfigUpdate:"));
        CHECK_FALSE(joined.contains("Image["));
        CHECK_FALSE(joined.contains("Audio["));
}

TEST_CASE("Image: carries its compressed MediaPacket") {
        auto buf = Buffer::Ptr::create(32);
        buf.modify()->setSize(16);
        Image img = Image::fromBuffer(buf, 16, 1, PixelDesc(PixelDesc::H264));
        REQUIRE(img.isValid());
        CHECK(img.isCompressed());
        CHECK_FALSE(img.packet().isValid());

        auto pkt = MediaPacket::Ptr::create(buf, PixelDesc(PixelDesc::H264));
        pkt.modify()->addFlag(MediaPacket::Keyframe);
        img.setPacket(pkt);

        REQUIRE(img.packet().isValid());
        CHECK(img.packet()->isKeyframe());
        CHECK(img.packet()->pixelDesc().id() == PixelDesc::H264);
        CHECK(img.packet()->buffer().ptr() == buf.ptr());

        img.setPacket(MediaPacket::Ptr());
        CHECK_FALSE(img.packet().isValid());
}
