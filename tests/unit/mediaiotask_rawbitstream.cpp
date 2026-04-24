/**
 * @file      mediaiotask_rawbitstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Tests for MediaIOTask_RawBitstream's introspection / negotiation
 * path (@c describe and @c proposeInput).  RawBitstream is a "dumb"
 * sink that appends @ref CompressedVideoPayload bytes to a file
 * verbatim — it must advertise the compressed accept set via @c describe
 * and reject uncompressed input via @c proposeInput so the planner
 * splices in a VideoEncoder ahead of us instead of routing raw frames
 * that would hit the "no CompressedVideoPayload" warning at runtime.
 */

#include <doctest/doctest.h>

#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaiotask_rawbitstream.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>

using namespace promeki;

namespace {

MediaIO *makeRawBitstream() {
        MediaIO::Config cfg = MediaIO::defaultConfig("RawBitstream");
        return MediaIO::create(cfg);
}

MediaDesc makeVideoDesc(uint32_t w, uint32_t h, PixelFormat::ID id) {
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(
                ImageDesc(Size2Du32(w, h), PixelFormat(id)));
        return md;
}

} // namespace

TEST_CASE("MediaIOTask_RawBitstream: Registry") {
        // Sink-only, with the elementary-stream extensions the
        // mediaplay file-path auto-detection uses.
        bool found = false;
        for(const auto &d : MediaIO::registeredFormats()) {
                if(d.name == "RawBitstream") {
                        CHECK_FALSE(d.canBeSource);
                        CHECK(d.canBeSink);
                        CHECK_FALSE(d.canBeTransform);
                        CHECK(d.extensions.contains(String("h264")));
                        CHECK(d.extensions.contains(String("h265")));
                        CHECK(d.extensions.contains(String("hevc")));
                        CHECK(d.extensions.contains(String("bit")));
                        found = true;
                        break;
                }
        }
        CHECK(found);
}

TEST_CASE("MediaIOTask_RawBitstream: describe advertises compressed accept set") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        MediaIODescription d;
        REQUIRE(io->describe(&d) == Error::Ok);

        // Identity from FormatDesc.
        CHECK(d.backendName() == "RawBitstream");
        CHECK(d.canBeSink());
        CHECK_FALSE(d.canBeSource());
        CHECK_FALSE(d.canBeTransform());

        // Acceptable formats: every registered compressed PixelFormat
        // advertised by a registered VideoCodec.  Must be non-empty
        // (the library registers H.264 + HEVC unconditionally) and
        // every entry must carry a compressed PixelFormat — otherwise
        // we'd be telling the planner the sink accepts raw frames,
        // which is exactly the runtime-fail path we're trying to
        // avoid.
        const auto &accepted = d.acceptableFormats();
        REQUIRE_FALSE(accepted.isEmpty());
        for(const MediaDesc &md : accepted) {
                REQUIRE_FALSE(md.imageList().isEmpty());
                const PixelFormat &pd = md.imageList()[0].pixelFormat();
                CHECK(pd.isValid());
                CHECK(pd.isCompressed());
        }

        // Producible formats are empty — this is a sink, it doesn't
        // produce anything.
        CHECK(d.producibleFormats().isEmpty());

        delete io;
}

TEST_CASE("MediaIOTask_RawBitstream: describe covers every registered compressed PixelFormat") {
        // Every compressed PixelFormat the library knows about — via
        // VideoCodec::compressedPixelFormats — must appear in the
        // acceptable set so the planner can pick any of them when
        // bridging an uncompressed source into this sink.
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        MediaIODescription d;
        REQUIRE(io->describe(&d) == Error::Ok);

        const auto &accepted = d.acceptableFormats();
        for(VideoCodec::ID cid : VideoCodec::registeredIDs()) {
                VideoCodec codec(cid);
                if(!codec.isValid()) continue;
                for(const PixelFormat &pd : codec.compressedPixelFormats()) {
                        if(!pd.isValid()) continue;
                        bool found = false;
                        for(const MediaDesc &md : accepted) {
                                if(md.imageList().isEmpty()) continue;
                                if(md.imageList()[0].pixelFormat().id() == pd.id()) {
                                        found = true;
                                        break;
                                }
                        }
                        INFO("codec=", codec.name().cstr(),
                             " pixelFormat=", pd.name().cstr());
                        CHECK(found);
                }
        }

        delete io;
}

TEST_CASE("MediaIOTask_RawBitstream: describe rejects null out") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);
        CHECK(io->describe(nullptr) == Error::Invalid);
        delete io;
}

TEST_CASE("MediaIOTask_RawBitstream: proposeInput accepts compressed") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(1920, 1080, PixelFormat::H264);
        MediaDesc preferred;
        CHECK(io->proposeInput(offered, &preferred) == Error::Ok);
        // Compressed in, compressed out — no rewrite.
        CHECK(preferred == offered);

        delete io;
}

TEST_CASE("MediaIOTask_RawBitstream: proposeInput accepts HEVC") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(3840, 2160, PixelFormat::HEVC);
        MediaDesc preferred;
        CHECK(io->proposeInput(offered, &preferred) == Error::Ok);
        CHECK(preferred == offered);

        delete io;
}

TEST_CASE("MediaIOTask_RawBitstream: proposeInput rejects uncompressed RGBA") {
        // The planner must see "not acceptable" so it inserts a
        // VideoEncoder ahead of us — not silently route raw pixels
        // through that the writer would then discard with the
        // "no compressed Image" warning.
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        MediaDesc preferred;
        CHECK(io->proposeInput(offered, &preferred) == Error::NotSupported);

        delete io;
}

TEST_CASE("MediaIOTask_RawBitstream: proposeInput rejects uncompressed YUV") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        const MediaDesc offered =
                makeVideoDesc(1920, 1080, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        MediaDesc preferred;
        CHECK(io->proposeInput(offered, &preferred) == Error::NotSupported);

        delete io;
}

TEST_CASE("MediaIOTask_RawBitstream: proposeInput rejects empty image list") {
        // An audio-only MediaDesc has nothing for this sink to write.
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        MediaDesc offered;
        offered.setFrameRate(FrameRate(FrameRate::FPS_30));
        MediaDesc preferred;
        CHECK(io->proposeInput(offered, &preferred) == Error::NotSupported);

        delete io;
}

TEST_CASE("MediaIOTask_RawBitstream: proposeInput rejects null preferred") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(1920, 1080, PixelFormat::H264);
        CHECK(io->proposeInput(offered, nullptr) == Error::Invalid);

        delete io;
}
