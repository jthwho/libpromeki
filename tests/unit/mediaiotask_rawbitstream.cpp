/**
 * @file      mediaiotask_rawbitstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Tests for RawBitstreamMediaIO's introspection / negotiation
 * path (@c describe and @c proposeInput).  RawBitstream is a "dumb"
 * sink that appends @ref CompressedVideoPayload bytes to a file
 * verbatim — it must advertise the compressed accept set via @c describe
 * and reject uncompressed input via @c proposeInput so the planner
 * splices in a VideoEncoder ahead of us instead of routing raw frames
 * that would hit the "no CompressedVideoPayload" warning at runtime.
 */

#include <cstdio>
#include <doctest/doctest.h>

#include <promeki/dir.h>
#include <promeki/enums.h>
#include <promeki/filepath.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaiosink.h>
#include <promeki/rawbitstreammediaio.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>

using namespace promeki;

namespace {

        // Returns a scratch path under Dir::temp() so RawBitstream's
        // open() succeeds and the backend installs its sink port —
        // proposeInput is now per-port (MediaIOSink::proposeInput) so
        // we need a live sink(0) to query.  The file is removed by
        // deleteRawBitstream() so each test cleans up after itself.
        String scratchPath() {
                return (Dir::temp().path() / "promeki-rawbitstream-test.h264").toString();
        }

        // Builds a RawBitstream MediaIO and opens it as a sink so the
        // sink(0) port exists for proposeInput probes.  The simpler
        // pre-open create() is no longer enough because the sink port
        // is populated inside executeCmd(Open).
        MediaIO *makeRawBitstream() {
                MediaIO::Config cfg = MediaIOFactory::defaultConfig("RawBitstream");
                cfg.set(MediaConfig::Type, "RawBitstream");
                cfg.set(MediaConfig::Filename, scratchPath());
                cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
                MediaIO *io = MediaIO::create(cfg);
                if (io == nullptr) return nullptr;
                if (io->open().wait().isError()) {
                        delete io;
                        return nullptr;
                }
                return io;
        }

        void deleteRawBitstream(MediaIO *io) {
                if (io == nullptr) return;
                if (io->isOpen()) (void)io->close().wait();
                delete io;
                std::remove(scratchPath().cstr());
        }

        MediaDesc makeVideoDesc(uint32_t w, uint32_t h, PixelFormat::ID id) {
                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::FPS_30));
                md.imageList().pushToBack(ImageDesc(Size2Du32(w, h), PixelFormat(id)));
                return md;
        }

} // namespace

TEST_CASE("RawBitstreamMediaIO: Registry") {
        // Sink-only, with the elementary-stream extensions the
        // mediaplay file-path auto-detection uses.
        const MediaIOFactory *factory = MediaIOFactory::findByName(String("RawBitstream"));
        REQUIRE(factory != nullptr);
        CHECK_FALSE(factory->canBeSource());
        CHECK(factory->canBeSink());
        CHECK_FALSE(factory->canBeTransform());
        const StringList exts = factory->extensions();
        CHECK(exts.contains(String("h264")));
        CHECK(exts.contains(String("h265")));
        CHECK(exts.contains(String("hevc")));
        CHECK(exts.contains(String("bit")));
}

TEST_CASE("RawBitstreamMediaIO: describe advertises compressed accept set") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        MediaIODescription d;
        REQUIRE(io->describe(&d) == Error::Ok);

        // Identity from MediaIOFactory.
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
        for (const MediaDesc &md : accepted) {
                REQUIRE_FALSE(md.imageList().isEmpty());
                const PixelFormat &pd = md.imageList()[0].pixelFormat();
                CHECK(pd.isValid());
                CHECK(pd.isCompressed());
        }

        // Producible formats are empty — this is a sink, it doesn't
        // produce anything.
        CHECK(d.producibleFormats().isEmpty());

        deleteRawBitstream(io);
}

TEST_CASE("RawBitstreamMediaIO: describe covers every registered compressed PixelFormat") {
        // Every compressed PixelFormat the library knows about — via
        // VideoCodec::compressedPixelFormats — must appear in the
        // acceptable set so the planner can pick any of them when
        // bridging an uncompressed source into this sink.
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        MediaIODescription d;
        REQUIRE(io->describe(&d) == Error::Ok);

        const auto &accepted = d.acceptableFormats();
        for (VideoCodec::ID cid : VideoCodec::registeredIDs()) {
                VideoCodec codec(cid);
                if (!codec.isValid()) continue;
                for (const PixelFormat &pd : codec.compressedPixelFormats()) {
                        if (!pd.isValid()) continue;
                        bool found = false;
                        for (const MediaDesc &md : accepted) {
                                if (md.imageList().isEmpty()) continue;
                                if (md.imageList()[0].pixelFormat().id() == pd.id()) {
                                        found = true;
                                        break;
                                }
                        }
                        INFO("codec=", codec.name().cstr(), " pixelFormat=", pd.name().cstr());
                        CHECK(found);
                }
        }

        deleteRawBitstream(io);
}

TEST_CASE("RawBitstreamMediaIO: describe rejects null out") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);
        CHECK(io->describe(nullptr) == Error::Invalid);
        deleteRawBitstream(io);
}

TEST_CASE("RawBitstreamMediaIO: proposeInput accepts compressed") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(1920, 1080, PixelFormat::H264);
        MediaDesc       preferred;
        CHECK(io->sink(0)->proposeInput(offered, &preferred) == Error::Ok);
        // Compressed in, compressed out — no rewrite.
        CHECK(preferred == offered);

        deleteRawBitstream(io);
}

TEST_CASE("RawBitstreamMediaIO: proposeInput accepts HEVC") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(3840, 2160, PixelFormat::HEVC);
        MediaDesc       preferred;
        CHECK(io->sink(0)->proposeInput(offered, &preferred) == Error::Ok);
        CHECK(preferred == offered);

        deleteRawBitstream(io);
}

TEST_CASE("RawBitstreamMediaIO: proposeInput rejects uncompressed RGBA") {
        // The planner must see "not acceptable" so it inserts a
        // VideoEncoder ahead of us — not silently route raw pixels
        // through that the writer would then discard with the
        // "no compressed Image" warning.
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        MediaDesc       preferred;
        CHECK(io->sink(0)->proposeInput(offered, &preferred) == Error::NotSupported);

        deleteRawBitstream(io);
}

TEST_CASE("RawBitstreamMediaIO: proposeInput rejects uncompressed YUV") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(1920, 1080, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        MediaDesc       preferred;
        CHECK(io->sink(0)->proposeInput(offered, &preferred) == Error::NotSupported);

        deleteRawBitstream(io);
}

TEST_CASE("RawBitstreamMediaIO: proposeInput rejects empty image list") {
        // An audio-only MediaDesc has nothing for this sink to write.
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        MediaDesc offered;
        offered.setFrameRate(FrameRate(FrameRate::FPS_30));
        MediaDesc preferred;
        CHECK(io->sink(0)->proposeInput(offered, &preferred) == Error::NotSupported);

        deleteRawBitstream(io);
}

TEST_CASE("RawBitstreamMediaIO: proposeInput rejects null preferred") {
        MediaIO *io = makeRawBitstream();
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(1920, 1080, PixelFormat::H264);
        CHECK(io->sink(0)->proposeInput(offered, nullptr) == Error::Invalid);

        deleteRawBitstream(io);
}
