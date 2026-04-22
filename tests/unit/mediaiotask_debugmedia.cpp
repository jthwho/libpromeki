/**
 * @file      mediaiotask_debugmedia.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_debugmedia.h>
#include <promeki/debugmediafile.h>
#include <promeki/mediaconfig.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/pixeldesc.h>
#include <promeki/dir.h>
#include <promeki/enums.h>

using namespace promeki;

namespace {

Frame::Ptr sampleFrame(int64_t idx) {
        Frame::Ptr f = Frame::Ptr::create();
        Frame *raw = f.modify();
        // NB: Metadata::FrameNumber is overwritten by MediaIO from its
        // own internal counter on read (see MediaIO::readFrame), so we
        // stash a test-only tag (Comment) that survives the round-trip.
        raw->metadata().set(Metadata::Comment,
                            String::sprintf("frame-%lld", (long long)idx));
        raw->metadata().set(Metadata::Title, String("pmdf-mediaio-test"));

        ImageDesc idesc(Size2Du32(16, 8), PixelDesc::RGB8_sRGB);
        idesc.setVideoScanMode(VideoScanMode::Progressive);
        Image::Ptr img = Image::Ptr::create(idesc);
        uint8_t *d = static_cast<uint8_t *>(img->data(0));
        for(size_t b = 0; b < img->plane(0)->size(); ++b) {
                d[b] = static_cast<uint8_t>((b + idx * 17) & 0xFF);
        }
        raw->imageList().pushToBack(img);
        return f;
}

String frameTag(const Frame::Ptr &f) {
        return f->metadata().getAs<String>(Metadata::Comment);
}

FilePath scratchDir() {
        FilePath dir = Dir::temp().path() / "promeki_test_pmdf_mediaio";
        Dir d(dir);
        if(!d.exists()) d.mkdir();
        return dir;
}

} // namespace

TEST_CASE("MediaIOTask_DebugMedia: registered as 'PMDF' with .pmdf extension") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "PMDF") {
                        CHECK(desc.canBeSource);
                        CHECK(desc.canBeSink);
                        CHECK_FALSE(desc.canBeTransform);
                        REQUIRE(desc.extensions.size() == 1);
                        CHECK(desc.extensions[0] == "pmdf");
                        found = true;
                }
        }
        CHECK(found);
}

TEST_CASE("MediaIOTask_DebugMedia: findFormatForPath routes .pmdf here") {
        String path = (scratchDir() / "probe.pmdf").toString();
        const MediaIO::FormatDesc *desc = MediaIO::findFormatForPath(path);
        REQUIRE(desc != nullptr);
        CHECK(desc->name == "PMDF");
}

TEST_CASE("MediaIOTask_DebugMedia: write then read round-trips the frame") {
        String fn = (scratchDir() / "roundtrip.pmdf").toString();

        // Write side.
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "PMDF");
                cfg.set(MediaConfig::Filename, fn);
                MediaIO *sink = MediaIO::create(cfg);
                REQUIRE(sink != nullptr);
                REQUIRE(sink->open(MediaIO::Sink).isOk());
                for(int i = 0; i < 3; ++i) {
                        REQUIRE(sink->writeFrame(sampleFrame(i)).isOk());
                }
                sink->close();
                delete sink;
        }

        // Read side.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "PMDF");
        cfg.set(MediaConfig::Filename, fn);
        MediaIO *src = MediaIO::create(cfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open(MediaIO::Source).isOk());
        CHECK(src->canSeek());
        CHECK(src->frameCount() == 3);

        for(int i = 0; i < 3; ++i) {
                Frame::Ptr f;
                REQUIRE(src->readFrame(f).isOk());
                REQUIRE(f.isValid());
                CHECK(frameTag(f) == String::sprintf("frame-%d", i));
                REQUIRE(f->imageList().size() == 1);
                CHECK(f->imageList()[0]->desc().size()
                          == Size2Du32(16, 8));
        }

        Frame::Ptr eof;
        CHECK(src->readFrame(eof) == Error::EndOfFile);
        src->close();
        delete src;
}

TEST_CASE("MediaIOTask_DebugMedia: seekToFrame jumps to a specific frame") {
        String fn = (scratchDir() / "seek.pmdf").toString();
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "PMDF");
                cfg.set(MediaConfig::Filename, fn);
                MediaIO *sink = MediaIO::create(cfg);
                REQUIRE(sink != nullptr);
                REQUIRE(sink->open(MediaIO::Sink).isOk());
                for(int i = 0; i < 5; ++i) REQUIRE(sink->writeFrame(sampleFrame(i)).isOk());
                sink->close();
                delete sink;
        }
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "PMDF");
        cfg.set(MediaConfig::Filename, fn);
        MediaIO *src = MediaIO::create(cfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open(MediaIO::Source).isOk());

        REQUIRE(src->seekToFrame(3).isOk());
        Frame::Ptr f;
        REQUIRE(src->readFrame(f).isOk());
        CHECK(frameTag(f) == "frame-3");

        src->close();
        delete src;
}

TEST_CASE("MediaIOTask_DebugMedia: copyFrames through MediaIO") {
        // Round-trip via MediaIO::copyFrames: this is the real use-case
        // for pmdf-inspect's --extract path.
        String fn = (scratchDir() / "copy_src.pmdf").toString();
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "PMDF");
                cfg.set(MediaConfig::Filename, fn);
                MediaIO *sink = MediaIO::create(cfg);
                REQUIRE(sink != nullptr);
                REQUIRE(sink->open(MediaIO::Sink).isOk());
                for(int i = 0; i < 4; ++i) REQUIRE(sink->writeFrame(sampleFrame(i)).isOk());
                sink->close();
                delete sink;
        }
        String outFn = (scratchDir() / "copy_dst.pmdf").toString();
        MediaIO::Config srcCfg;
        srcCfg.set(MediaConfig::Type, "PMDF");
        srcCfg.set(MediaConfig::Filename, fn);
        MediaIO::Config dstCfg;
        dstCfg.set(MediaConfig::Type, "PMDF");
        dstCfg.set(MediaConfig::Filename, outFn);

        MediaIO *src = MediaIO::create(srcCfg);
        MediaIO *dst = MediaIO::create(dstCfg);
        REQUIRE(src != nullptr);
        REQUIRE(dst != nullptr);
        REQUIRE(src->open(MediaIO::Source).isOk());
        REQUIRE(dst->open(MediaIO::Sink).isOk());

        auto [copied, err] = MediaIO::copyFrames(src, dst, 1, 2);
        CHECK(err.isOk());
        CHECK(copied == FrameCount(2));

        src->close(); dst->close();
        delete src; delete dst;

        // Verify the destination file has two frames indexed as 0, 1.
        DebugMediaFile r;
        REQUIRE(r.open(outFn, DebugMediaFile::Read).isOk());
        CHECK(r.frameCount() == 2);
}

TEST_CASE("MediaIOTask_DebugMedia: registers pmdf URL scheme") {
        const MediaIO::FormatDesc *desc = MediaIO::findFormatByScheme("pmdf");
        REQUIRE(desc != nullptr);
        CHECK(desc->name == "PMDF");
        CHECK(desc->schemes.contains(String("pmdf")));
}

TEST_CASE("MediaIOTask_DebugMedia: createFromUrl opaque absolute path") {
        // pmdf:/abs/path.pmdf — opaque form, url.path() carries the
        // leading slash so the filename comes out exactly as written.
        MediaIO *io = MediaIO::createFromUrl(String("pmdf:/var/tmp/capture.pmdf"));
        REQUIRE(io != nullptr);
        CHECK(io->config().getAs<String>(MediaConfig::Type) == "PMDF");
        CHECK(io->config().getAs<String>(MediaConfig::Filename) ==
              "/var/tmp/capture.pmdf");
        CHECK(io->config().getAs<Url>(MediaConfig::Url).scheme() == "pmdf");
        delete io;
}

TEST_CASE("MediaIOTask_DebugMedia: createFromUrl authority-form with empty host") {
        MediaIO *io = MediaIO::createFromUrl(
                String("pmdf:///var/tmp/capture.pmdf"));
        REQUIRE(io != nullptr);
        CHECK(io->config().getAs<String>(MediaConfig::Filename) ==
              "/var/tmp/capture.pmdf");
        delete io;
}

TEST_CASE("MediaIOTask_DebugMedia: createFromUrl rejects non-empty host") {
        // pmdf://host/... makes no sense for a local file capture —
        // surface the nonsense as a hard error rather than silently
        // dropping the host.
        MediaIO *io = MediaIO::createFromUrl(
                String("pmdf://somehost/tmp/capture.pmdf"));
        CHECK(io == nullptr);
}

TEST_CASE("MediaIOTask_DebugMedia: createFromUrl rejects empty path") {
        MediaIO *io = MediaIO::createFromUrl(String("pmdf:"));
        CHECK(io == nullptr);
}

TEST_CASE("MediaIOTask_DebugMedia: createForFileRead takes pmdf URL") {
        // Write a tiny real PMDF to a scratch path, then open it via
        // URL — confirms the end-to-end URL path actually talks to the
        // backend, not just populates a Config.
        String outFn = (scratchDir() / "url-read.pmdf").toString();
        {
                DebugMediaFile w;
                REQUIRE(w.open(outFn, DebugMediaFile::Write).isOk());
                REQUIRE(w.writeFrame(sampleFrame(0)).isOk());
                w.close();
        }
        String url = String("pmdf:") + outFn;
        MediaIO *io = MediaIO::createForFileRead(url);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());
        Frame::Ptr f;
        CHECK(io->readFrame(f).isOk());
        CHECK(f.isValid());
        io->close();
        delete io;
}
