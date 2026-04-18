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
#include <promeki/pixeldesc.h>
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
                          PixelDesc(PixelDesc::RGB8_sRGB)));
        return md;
}

AudioDesc makeAudioDesc() {
        return AudioDesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
}

Frame::Ptr makeFrame(uint8_t fill) {
        Frame::Ptr f = Frame::Ptr::create();
        Image::Ptr img = Image::Ptr::create(
                ImageDesc(Size2Du32(64, 48),
                          PixelDesc(PixelDesc::RGB8_sRGB)));
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
                        CHECK(d.canInput);
                        CHECK(d.canOutput);
                        CHECK_FALSE(d.canInputAndOutput);
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
        io->setMediaDesc(makeMediaDesc());
        io->setAudioDesc(makeAudioDesc());
        REQUIRE(io->open(MediaIO::Input).isOk());
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
        pub->setMediaDesc(makeMediaDesc());
        pub->setAudioDesc(makeAudioDesc());
        REQUIRE(pub->open(MediaIO::Input).isOk());

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
                subOpenErr = sub->open(MediaIO::Output);
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
        Error err = io->open(MediaIO::Input);
        CHECK(err == Error::InvalidArgument);
        delete io;
}
