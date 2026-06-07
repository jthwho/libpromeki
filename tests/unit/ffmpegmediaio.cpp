/**
 * @file      tests/unit/ffmpegmediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <doctest/doctest.h>
#include <promeki/config.h>
#include <promeki/ffmpegmediaio.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>

using namespace promeki;

TEST_CASE("FfmpegMediaIO: factory identity + fallback flag") {
        const MediaIOFactory *f = MediaIOFactory::findByName("FFmpeg");
        REQUIRE(f != nullptr);
        CHECK(f->name() == "FFmpeg");
        CHECK(f->canBeSource());
        CHECK(f->canBeSink());
        // The whole point of this backend: it is a last-resort fallback so it
        // never out-races a native backend during auto-dispatch.
        CHECK(f->isFallback());
        // It must advertise the container formats no native backend owns…
        bool hasMkv = false;
        for (const String &e : f->extensions()) {
                if (e == "mkv") hasMkv = true;
                // …and it must NOT claim the natively-owned ones.
                CHECK(e != "mov");
                CHECK(e != "mp4");
                CHECK(e != "wav");
        }
        CHECK(hasMkv);
}

TEST_CASE("FfmpegMediaIO: native backends win the auto-dispatch") {
        // .mov / .mp4 are owned by QuickTime — even though FFmpeg can also
        // handle them, the extension dispatcher must prefer the native backend.
        const MediaIOFactory *mov = MediaIOFactory::findByExtension("mov");
        REQUIRE(mov != nullptr);
        CHECK(mov->name() == "QuickTime");

        const MediaIOFactory *mp4 = MediaIOFactory::findByExtension("mp4");
        REQUIRE(mp4 != nullptr);
        CHECK(mp4->name() == "QuickTime");
}

TEST_CASE("FfmpegMediaIO: fallback wins only formats nothing native claims") {
        // .mkv has no native backend, so the fallback FFmpeg backend is chosen.
        const MediaIOFactory *mkv = MediaIOFactory::findByExtension("mkv");
        REQUIRE(mkv != nullptr);
        CHECK(mkv->name() == "FFmpeg");

        const MediaIOFactory *webm = MediaIOFactory::findByExtension("webm");
        REQUIRE(webm != nullptr);
        CHECK(webm->name() == "FFmpeg");
}

TEST_CASE("FfmpegMediaIO: explicit Type=FFmpeg always selects the backend") {
        // Even for a .mov, naming the backend explicitly forces FFmpeg.
        MediaConfig cfg = MediaIOFactory::defaultConfig("FFmpeg");
        cfg.set(MediaConfig::Type, String("FFmpeg"));
        cfg.set(MediaConfig::Filename, String("/nonexistent/clip.mov"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        // We constructed an FFmpeg backend, not QuickTime.
        CHECK(dynamic_cast<FfmpegMediaIO *>(io) != nullptr);
        delete io;
}

TEST_CASE("FfmpegMediaIO: writer config keys carry sane defaults") {
        MediaConfig cfg = MediaIOFactory::defaultConfig("FFmpeg");
        // PCM is the default writer audio codec (uncompressed mux).
        CHECK(cfg.getAs<AudioCodec>(MediaConfig::FfmpegAudioCodec, AudioCodec()).id() == AudioCodec::PCM);
        // Invalid video codec = passthrough.
        CHECK_FALSE(cfg.getAs<VideoCodec>(MediaConfig::FfmpegVideoCodec, VideoCodec(VideoCodec::H264)).isValid());
        // Empty format = derive from filename.
        CHECK(cfg.getAs<String>(MediaConfig::FfmpegFormat, String("x")).isEmpty());
}
