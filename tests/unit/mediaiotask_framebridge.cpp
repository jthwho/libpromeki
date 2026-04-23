/**
 * @file      mediaiotask_framebridge.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <thread>
#include <chrono>
#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_framebridge.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/audiodesc.h>
#include <promeki/imagedesc.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>
#include <promeki/uuid.h>
#include <promeki/dir.h>
#include <promeki/sharedmemory.h>
#include <promeki/localserver.h>
#include <promeki/mediatimestamp.h>
#include <promeki/clockdomain.h>

using namespace promeki;

namespace {

String uniqueName(const char *tag) {
        return String("mi-fb-test-") + String(tag) + String("-") +
               UUID::generateV4().toString();
}

MediaDesc makeMediaDesc() {
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(
                ImageDesc(Size2Du32(64, 48),
                          PixelFormat(PixelFormat::RGB8_sRGB)));
        return md;
}

AudioDesc makeAudioDesc() {
        return AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
}

Frame::Ptr makeFrame(uint8_t fill) {
        Frame::Ptr f = Frame::Ptr::create();
        Image::Ptr img = Image::Ptr::create(
                ImageDesc(Size2Du32(64, 48),
                          PixelFormat(PixelFormat::RGB8_sRGB)));
        for(int p = 0; p < img->desc().planeCount(); ++p) {
                std::memset(img->plane(p)->data(), fill,
                            img->plane(p)->size());
        }
        f.modify()->imageList().pushToBack(img);
        Audio::Ptr aud = Audio::Ptr::create(makeAudioDesc(), 1600);
        std::memset(aud->buffer()->data(), fill, aud->buffer()->size());
        f.modify()->audioList().pushToBack(aud);
        f.modify()->metadata().set(Metadata::Title, String("unit-test"));
        return f;
}

} // namespace

TEST_CASE("MediaIOTask_FrameBridge: registered in MediaIO factory") {
        bool found = false;
        for(const auto &d : MediaIO::registeredFormats()) {
                if(d.name == "FrameBridge") {
                        CHECK(d.canBeSink);
                        CHECK(d.canBeSource);
                        CHECK_FALSE(d.canBeTransform);
                        CHECK(d.extensions.isEmpty());
                        found = true;
                        break;
                }
        }
        CHECK(found);
}

TEST_CASE("MediaIOTask_FrameBridge: input-mode open creates output bridge") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        Dir::ipc().mkpath();

        String name = uniqueName("in-mode");
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameBridge");
        cfg.set(MediaConfig::FrameBridgeName, name);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        io->setExpectedDesc(makeMediaDesc());
        io->setExpectedAudioDesc(makeAudioDesc());
        REQUIRE(io->open(MediaIO::Sink).isOk());
        CHECK(io->mediaDesc().isValid());
        CHECK(io->audioDesc().channels() == 2);
        CHECK(io->frameCount() == MediaIO::FrameCountInfinite);
        CHECK_FALSE(io->canSeek());
        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_FrameBridge: round-trip frame through MediaIO") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        Dir::ipc().mkpath();

        String name = uniqueName("rt");

        // Producer side: MediaIO opened in Input mode — caller writes, we
        // publish to the bridge.
        MediaIO::Config pubCfg = MediaIO::defaultConfig("FrameBridge");
        pubCfg.set(MediaConfig::FrameBridgeName, name);
        MediaIO *pub = MediaIO::create(pubCfg);
        REQUIRE(pub != nullptr);
        pub->setExpectedDesc(makeMediaDesc());
        pub->setExpectedAudioDesc(makeAudioDesc());
        REQUIRE(pub->open(MediaIO::Sink).isOk());

        // Consumer side: MediaIO opened in Output mode — caller reads,
        // we pull from the bridge.  Use no-sync so the pumping-driver
        // pattern this test relies on doesn't deadlock on ACK waits.
        MediaIO::Config subCfg = MediaIO::defaultConfig("FrameBridge");
        subCfg.set(MediaConfig::FrameBridgeName, name);
        subCfg.set(MediaConfig::FrameBridgeSyncMode, false);
        MediaIO *sub = MediaIO::create(subCfg);
        REQUIRE(sub != nullptr);

        // The consumer's open() needs the producer to service its socket.
        // Run sub->open(Output) on a helper thread while we drive the
        // publisher's write loop.
        std::atomic<bool> subDone{false};
        Error subOpenErr;
        std::thread subOpener([&]() {
                subOpenErr = sub->open(MediaIO::Source);
                subDone.store(true);
        });

        // Pump empty writes to drive service() until the subscriber
        // finishes its handshake.
        for(int i = 0; i < 200 && !subDone.load(); ++i) {
                Frame::Ptr drop = makeFrame(0x00);
                pub->writeFrame(drop);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        subOpener.join();
        REQUIRE(subOpenErr.isOk());

        // Write a recognisable frame and read it on the other side.
        Frame::Ptr tx = makeFrame(0x5A);
        REQUIRE(pub->writeFrame(tx).isOk());

        Frame::Ptr rx;
        for(int i = 0; i < 200 && !rx; ++i) {
                Error rerr = sub->readFrame(rx);
                if(rerr == Error::TryAgain || !rx) {
                        rx.clear();
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        continue;
                }
                REQUIRE(rerr.isOk());
        }
        REQUIRE(rx);
        REQUIRE(rx->imageList().size() == 1);
        const Image::Ptr &img = rx->imageList()[0];
        CHECK(static_cast<const uint8_t *>(img->plane(0)->data())[0] == 0x5A);

        // SourceUUID is stamped on the metadata.
        String srcUuid = rx->metadata().getAs<String>(
                Metadata::stringToID(String("SourceUUID")), String());
        CHECK_FALSE(srcUuid.isEmpty());

        // FrameBridgeTimeStamp is stamped on the metadata and carries
        // a valid SystemMonotonic timestamp.
        MediaTimeStamp mts = rx->metadata().getAs<MediaTimeStamp>(
                Metadata::FrameBridgeTimeStamp, MediaTimeStamp());
        CHECK(mts.isValid());
        CHECK(mts.domain() == ClockDomain::SystemMonotonic);
        CHECK(mts.timeStamp().nanoseconds() > 0);

        sub->close();
        pub->close();
        delete sub;
        delete pub;
}

TEST_CASE("MediaIOTask_FrameBridge: open without FrameBridgeName fails") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameBridge");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        Error err = io->open(MediaIO::Sink);
        CHECK(err == Error::InvalidArgument);
        delete io;
}

TEST_CASE("MediaIOTask_FrameBridge: registers pmfb URL scheme") {
        const MediaIO::FormatDesc *desc = MediaIO::findFormatByScheme("pmfb");
        REQUIRE(desc != nullptr);
        CHECK(desc->name == "FrameBridge");
        CHECK(desc->schemes.contains(String("pmfb")));
        CHECK(static_cast<bool>(desc->urlToConfig));
}

TEST_CASE("MediaIOTask_FrameBridge: createFromUrl name-only") {
        MediaIO *io = MediaIO::createFromUrl(String("pmfb://studio-a"));
        REQUIRE(io != nullptr);
        CHECK(io->config().getAs<String>(MediaConfig::Type) == "FrameBridge");
        CHECK(io->config().getAs<String>(MediaConfig::FrameBridgeName) ==
              "studio-a");
        // Defaults preserved for knobs the URL did not override.
        CHECK(io->config().getAs<int32_t>(MediaConfig::FrameBridgeRingDepth) ==
              int32_t(2));
        CHECK(io->config().getAs<bool>(MediaConfig::FrameBridgeSyncMode) == true);
        // MediaConfig::Url carries the URL verbatim so downstream
        // introspection can show the caller what was passed in.
        Url seeded = io->config().getAs<Url>(MediaConfig::Url);
        CHECK(seeded.isValid());
        CHECK(seeded.scheme() == "pmfb");
        CHECK(seeded.host() == "studio-a");
        // Filename stays empty — URL and path are mutually exclusive
        // entry points; one gets populated at a time.
        CHECK(io->config().getAs<String>(MediaConfig::Filename).isEmpty());
        delete io;
}

TEST_CASE("MediaIOTask_FrameBridge: createFromUrl with canonical query keys") {
        MediaIO *io = MediaIO::createFromUrl(String(
                "pmfb://bridge"
                "?FrameBridgeRingDepth=4"
                "&FrameBridgeSyncMode=false"
                "&FrameBridgeWaitForConsumer=no"
                "&FrameBridgeAudioHeadroomFraction=0.25"
                "&FrameBridgeMetadataReserveBytes=32768"
                "&FrameBridgeGroupName=video"
                "&FrameBridgeAccessMode=416")); // decimal 416 == 0640 octal
        REQUIRE(io != nullptr);
        const MediaIO::Config &cfg = io->config();
        CHECK(cfg.getAs<String>(MediaConfig::FrameBridgeName) == "bridge");
        CHECK(cfg.getAs<int32_t>(MediaConfig::FrameBridgeRingDepth) == 4);
        CHECK(cfg.getAs<bool>(MediaConfig::FrameBridgeSyncMode) == false);
        CHECK(cfg.getAs<bool>(MediaConfig::FrameBridgeWaitForConsumer) == false);
        CHECK(cfg.getAs<double>(MediaConfig::FrameBridgeAudioHeadroomFraction) ==
              doctest::Approx(0.25));
        CHECK(cfg.getAs<int32_t>(MediaConfig::FrameBridgeMetadataReserveBytes) ==
              32768);
        CHECK(cfg.getAs<String>(MediaConfig::FrameBridgeGroupName) == "video");
        CHECK(cfg.getAs<int32_t>(MediaConfig::FrameBridgeAccessMode) == 0640);
        delete io;
}

TEST_CASE("MediaIOTask_FrameBridge: createFromUrl rejects empty name") {
        MediaIO *io = MediaIO::createFromUrl(String("pmfb://"));
        CHECK(io == nullptr);
}

TEST_CASE("MediaIOTask_FrameBridge: createFromUrl rejects malformed bool value") {
        MediaIO *io = MediaIO::createFromUrl(
                String("pmfb://x?FrameBridgeSyncMode=maybe"));
        CHECK(io == nullptr);
}

TEST_CASE("MediaIOTask_FrameBridge: createFromUrl rejects unknown query key") {
        // Fictional key — exists in neither the global spec registry
        // nor the backend's spec map.  Should fail the open rather
        // than silently storing it and letting the backend ignore it.
        MediaIO *io = MediaIO::createFromUrl(
                String("pmfb://x?NotARealKey=42"));
        CHECK(io == nullptr);
}

TEST_CASE("MediaIOTask_FrameBridge: createFromUrl rejects non-backend spec key") {
        // VideoFormat IS a declared MediaConfig key (from some other
        // backend) but FrameBridge's spec map doesn't include it.
        // applyQueryToConfig should reject it at the "not part of
        // this backend's spec map" check.
        MediaIO *io = MediaIO::createFromUrl(
                String("pmfb://x?VideoFormat=Smpte1080p29_97"));
        CHECK(io == nullptr);
}

TEST_CASE("MediaIOTask_FrameBridge: createFromUrl rejects uncoercible int") {
        MediaIO *io = MediaIO::createFromUrl(
                String("pmfb://x?FrameBridgeRingDepth=not-a-number"));
        CHECK(io == nullptr);
}

TEST_CASE("MediaIOTask_FrameBridge: createForFileRead transparently opens pmfb URL") {
        MediaIO *io = MediaIO::createForFileRead(String("pmfb://consumer"));
        REQUIRE(io != nullptr);
        CHECK(io->config().getAs<String>(MediaConfig::Type) == "FrameBridge");
        CHECK(io->config().getAs<String>(MediaConfig::FrameBridgeName) ==
              "consumer");
        delete io;
}

TEST_CASE("MediaIOTask_FrameBridge: createForFileWrite transparently opens pmfb URL") {
        MediaIO *io = MediaIO::createForFileWrite(String("pmfb://producer"));
        REQUIRE(io != nullptr);
        CHECK(io->config().getAs<String>(MediaConfig::Type) == "FrameBridge");
        CHECK(io->config().getAs<String>(MediaConfig::FrameBridgeName) ==
              "producer");
        delete io;
}

TEST_CASE("MediaIOTask_FrameBridge: unknown scheme falls through to file path") {
        // A string that parses as a URL but whose scheme no backend
        // claims must NOT be mistaken for a URL open — if it happened
        // to be a file path it would still need to go through the
        // normal file-probe logic.  Here we just check that the URL
        // takeover doesn't fire: createForFileRead returns nullptr
        // because "nope://x" is neither a known scheme nor a readable
        // file.
        MediaIO *io = MediaIO::createForFileRead(String("nope://unknown"));
        CHECK(io == nullptr);
}

TEST_CASE("MediaIOTask_FrameBridge: open via pmfb URL succeeds") {
        if(!SharedMemory::isSupported() || !LocalServer::isSupported()) return;
        Dir::ipc().mkpath();

        String name = uniqueName("url-open");
        String url = String("pmfb://") + name;

        MediaIO *io = MediaIO::createFromUrl(url);
        REQUIRE(io != nullptr);
        io->setExpectedDesc(makeMediaDesc());
        io->setExpectedAudioDesc(makeAudioDesc());
        REQUIRE(io->open(MediaIO::Sink).isOk());
        CHECK(io->mediaDesc().isValid());
        io->close();
        delete io;
}
