/**
 * @file      tests/mediaiotask_mpegtsfile.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * End-to-end smoke test for @ref MpegTsFileMediaIO: builds a sink,
 * writes a handful of synthetic compressed video frames, closes,
 * reopens as a source, and verifies the same access-unit bytes come
 * back out via the demuxer.
 */

#include <cstdio>
#include <doctest/doctest.h>

#include <promeki/audiocodec.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/dir.h>
#include <promeki/enums_mediaio.h>
#include <promeki/filepath.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/mpegtsfilemediaio.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/videopayload.h>
#include <cstring>
#include <vector>

using namespace promeki;

namespace {

        String scratchPath() {
                return (Dir::temp().path() / "promeki-mpegtsfile-test.ts").toString();
        }

        Buffer makeBuffer(const std::vector<uint8_t> &v) {
                Buffer b(v.size() ? v.size() : 1);
                b.setSize(v.size());
                if (!v.empty()) std::memcpy(b.data(), v.data(), v.size());
                return b;
        }

        std::vector<uint8_t> fakeAu(size_t size, uint8_t seed) {
                std::vector<uint8_t> v(size);
                for (size_t i = 0; i < size; ++i) v[i] = static_cast<uint8_t>((seed + i) & 0xFF);
                return v;
        }

} // namespace

TEST_CASE("MpegTsFileMediaIO: factory registration") {
        const MediaIOFactory *factory = MediaIOFactory::findByName(String("MpegTsFile"));
        REQUIRE(factory != nullptr);
        CHECK(factory->canBeSource());
        CHECK(factory->canBeSink());
        const StringList exts = factory->extensions();
        CHECK(exts.contains(String("ts")));
        CHECK(exts.contains(String("m2ts")));
        CHECK(exts.contains(String("mts")));
}

TEST_CASE("MpegTsFileMediaIO: end-to-end write-then-read") {
        const String path = scratchPath();
        std::remove(path.cstr());

        // -- Writer phase ----------------------------------------
        struct WrittenAu {
                        std::vector<uint8_t> bytes;
                        bool                 key;
        };
        std::vector<WrittenAu> written;

        {
                MediaIO::Config cfg = MediaIOFactory::defaultConfig("MpegTsFile");
                cfg.set(MediaConfig::Type, "MpegTsFile");
                cfg.set(MediaConfig::Filename, path);
                cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
                cfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open().wait().isOk());

                const int kCount = 8;
                for (int i = 0; i < kCount; ++i) {
                        Frame frame;
                        const std::vector<uint8_t> au = fakeAu(400 + i * 91, static_cast<uint8_t>(0x60 + i));
                        Buffer                     buf = makeBuffer(au);
                        ImageDesc                  imgDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::H264));
                        auto                       p = CompressedVideoPayload::Ptr::create(imgDesc, std::move(buf));
                        const bool                 isKey = (i == 0);
                        if (isKey) p.modify()->addFlag(MediaPayload::Keyframe);
                        frame.addPayload(p);
                        MediaIOSink *sk = io->sink(0);
                        REQUIRE(sk != nullptr);
                        REQUIRE(sk->writeFrame(frame).wait().isOk());
                        WrittenAu w;
                        w.bytes = au;
                        w.key = isKey;
                        written.push_back(std::move(w));
                }
                REQUIRE(io->close().wait().isOk());
                delete io;
        }

        // -- Reader phase ----------------------------------------
        {
                MediaIO::Config cfg = MediaIOFactory::defaultConfig("MpegTsFile");
                cfg.set(MediaConfig::Type, "MpegTsFile");
                cfg.set(MediaConfig::Filename, path);
                cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Read));
                cfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open().wait().isOk());

                std::vector<std::vector<uint8_t>> readBytes;
                std::vector<bool>                 readKeyflags;
                MediaIOSource *src = io->source(0);
                REQUIRE(src != nullptr);
                while (true) {
                        MediaIORequest req = src->readFrame();
                        Error          err = req.wait();
                        if (err == Error::EndOfFile) break;
                        REQUIRE(err.isOk());
                        const auto *cmd = req.commandAs<MediaIOCommandRead>();
                        REQUIRE(cmd != nullptr);
                        Frame f = cmd->frame;
                        REQUIRE(f.isValid());
                        const auto vps = f.videoPayloads();
                        REQUIRE_FALSE(vps.isEmpty());
                        const auto *cvp = vps[0]->as<CompressedVideoPayload>();
                        REQUIRE(cvp != nullptr);
                        auto plane0 = cvp->plane(0);
                        std::vector<uint8_t> bytes(plane0.data(), plane0.data() + plane0.size());
                        readBytes.push_back(std::move(bytes));
                        readKeyflags.push_back(cvp->isKeyframe());
                }
                REQUIRE(io->close().wait().isOk());
                delete io;

                REQUIRE(readBytes.size() == written.size());
                for (size_t i = 0; i < written.size(); ++i) {
                        CHECK(readBytes[i] == written[i].bytes);
                        CHECK(readKeyflags[i] == written[i].key);
                }
        }

        std::remove(path.cstr());
}

// proposeInput PCM rewrite: when MpegTsAudioCodec=PCM and the source
// offers an uncompressed audio shape that SMPTE 302M can't pack as-is
// (Float32, planar, wrong rate), proposeInput must rewrite the offered
// AudioFormat to PCMI_S16LE @ 48 kHz so the planner splices an SRC.
TEST_CASE("MpegTsFileMediaIO::proposeInput: PCM rewrites incompatible offered shape") {
        const String                  path = scratchPath();
        std::remove(path.cstr());

        MediaIO::Config cfg = MediaIOFactory::defaultConfig(String("MpegTsFile"));
        cfg.set(MediaConfig::Type, String("MpegTsFile"));
        cfg.set(MediaConfig::Filename, path);
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        cfg.set(MediaConfig::MpegTsAudioCodec, AudioCodec(AudioCodec::PCM));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        SUBCASE("Float32LE planar → PCMI_S16LE @ 48 kHz") {
                MediaDesc offered;
                offered.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::H264)));
                // Float32LE planar at 44.1 kHz, 2 channels — 302M can
                // accept neither the format (Float) nor the rate
                // (44.1 kHz).
                offered.audioList().pushToBack(
                        AudioDesc(AudioFormat(AudioFormat::PCMP_Float32LE), 44100.0f, 2u));
                MediaDesc preferred;
                REQUIRE(io->proposeInput(offered, &preferred).isOk());
                REQUIRE_FALSE(preferred.audioList().isEmpty());
                CHECK(preferred.audioList()[0].format().id() == AudioFormat::PCMI_S16LE);
                CHECK(preferred.audioList()[0].sampleRate() == 48000.0f);
        }

        SUBCASE("PCMI_S16LE @ 48 kHz already supported → left alone") {
                MediaDesc offered;
                offered.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::H264)));
                offered.audioList().pushToBack(
                        AudioDesc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2u));
                MediaDesc preferred;
                REQUIRE(io->proposeInput(offered, &preferred).isOk());
                REQUIRE_FALSE(preferred.audioList().isEmpty());
                // Already 302M-eligible — no rewrite.
                CHECK(preferred.audioList()[0].format().id() == AudioFormat::PCMI_S16LE);
                CHECK(preferred.audioList()[0].sampleRate() == 48000.0f);
        }

        SUBCASE("AAC requested + uncompressed offered → rewritten to AAC") {
                // Switch to AAC by rewriting and reapplying the IO's config.
                MediaIO::Config c2 = io->config();
                c2.set(MediaConfig::MpegTsAudioCodec, AudioCodec(AudioCodec::AAC));
                io->setConfig(c2);

                MediaDesc offered;
                offered.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::H264)));
                offered.audioList().pushToBack(
                        AudioDesc(AudioFormat(AudioFormat::PCMP_Float32LE), 48000.0f, 2u));
                MediaDesc preferred;
                REQUIRE(io->proposeInput(offered, &preferred).isOk());
                REQUIRE_FALSE(preferred.audioList().isEmpty());
                CHECK(preferred.audioList()[0].format().id() == AudioFormat::AAC);
        }

        delete io;
}
