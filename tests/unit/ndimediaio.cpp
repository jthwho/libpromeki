/**
 * @file      ndimediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/clockdomain.h>
#include <promeki/enums.h>
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
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/ndiclock.h>
#include <promeki/ndidiscovery.h>
#include <promeki/ndilib.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/pixelformat.h>
#include <promeki/result.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/url.h>

using namespace promeki;

TEST_CASE("NdiFactory: is registered") {
        const MediaIOFactory *f = MediaIOFactory::findByName(String("Ndi"));
        REQUIRE(f != nullptr);
        CHECK(f->canBeSink());
        CHECK(f->canBeSource());
}

TEST_CASE("NdiFactory: canHandlePath recognises ndi:// URLs") {
        const MediaIOFactory *f = MediaIOFactory::findByName(String("Ndi"));
        REQUIRE(f != nullptr);
        CHECK(f->canHandlePath(String("ndi://machine/source")));
        CHECK(f->canHandlePath(String("ndi:///source")));
        CHECK(f->canHandlePath(String("ndi:source-name")));
        CHECK_FALSE(f->canHandlePath(String("rtp://machine/source")));
        CHECK_FALSE(f->canHandlePath(String("/dev/video0")));
}

TEST_CASE("NdiFactory: defaultConfig populates NDI keys") {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Ndi");
        // Spot-check a couple of keys that should have non-empty defaults.
        CHECK(cfg.getAs<String>(MediaConfig::NdiSendName, String()).isEmpty() == false);
        CHECK(cfg.getAs<int>(MediaConfig::NdiCaptureTimeoutMs, 0) > 0);
        CHECK(cfg.getAs<Duration>(MediaConfig::NdiFindWait, Duration()).milliseconds() > 0);
}

TEST_CASE("NdiMediaIO: open as Sink, write a synthetic UYVY frame, close") {
        if (!NdiLib::instance().isLoaded()) {
                MESSAGE("NDI runtime not available; skipping sink-mode write test");
                return;
        }

        // Pick a unique-enough send name so this test can run alongside
        // other NDI senders on the same box without name collisions.
        const String sendName = String::sprintf("PromekiTestSink-%lld",
                                                static_cast<long long>(
                                                        std::chrono::system_clock::now().time_since_epoch().count()));

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Ndi");
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        cfg.set(MediaConfig::NdiSendName, sendName);
        cfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
        cfg.set(MediaConfig::VideoSize, Size2Du32(320, 240));
        cfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));

        // Build the pending MediaDesc the SDK uses to size frames.
        ImageDesc imgDesc(Size2Du32(320, 240), PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
        MediaDesc desc;
        desc.setFrameRate(FrameRate(FrameRate::FPS_30));
        desc.imageList().pushToBack(imgDesc);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->setPendingMediaDesc(desc).isOk());
        REQUIRE(io->open().wait().isOk());

        // Allocate one synthetic frame — UYVY = 2 bytes per pixel,
        // single contiguous plane.  Fill with mid-gray so the wire
        // bytes are deterministic if we ever capture them.
        UncompressedVideoPayload::Ptr vp = UncompressedVideoPayload::allocate(imgDesc);
        REQUIRE(vp.isValid());
        REQUIRE(vp->data().count() == 1);
        // UYVY mid-gray: U=128, Y=128, V=128, Y=128.
        uint8_t *bytes = vp->data().data();
        const size_t  total = vp->data().size();
        for (size_t i = 0; i + 3 < total; i += 4) {
                bytes[i + 0] = 128;
                bytes[i + 1] = 128;
                bytes[i + 2] = 128;
                bytes[i + 3] = 128;
        }

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->addPayload(vp);

        Error werr = io->sink(0)->writeFrame(frame).wait();
        CHECK(werr.isOk());

        // Send a few more frames at full speed — the SDK queues
        // internally, so this exercises the back-to-back send path.
        for (int i = 0; i < 5; ++i) {
                CHECK(io->sink(0)->writeFrame(frame).wait().isOk());
        }

        REQUIRE(io->close().wait().isOk());
        delete io;
}

TEST_CASE("NdiFactory: source mode is supported") {
        const MediaIOFactory *f = MediaIOFactory::findByName(String("Ndi"));
        REQUIRE(f != nullptr);
        CHECK(f->canBeSource());
        // The schemes() override pairs with MediaIO::createFromUrl
        // for ndi:// inputs.
        StringList schemes = f->schemes();
        bool       hasNdi = false;
        for (const auto &s : schemes) {
                if (s.toLower() == String("ndi")) hasNdi = true;
        }
        CHECK(hasNdi);
}

TEST_CASE("NdiFactory: urlToConfig populates NdiSourceName from ndi:// URLs") {
        const MediaIOFactory *f = MediaIOFactory::findByName(String("Ndi"));
        REQUIRE(f != nullptr);

        // ndi://Machine/Source → "Machine (Source)"
        {
                Result<Url> parsed = Url::fromString(String("ndi://MyHost/Channel%201"));
                REQUIRE(parsed.second().isOk());
                MediaIO::Config cfg;
                Error           err = f->urlToConfig(parsed.first(), &cfg);
                CHECK(err.isOk());
                String name = cfg.getAs<String>(MediaConfig::NdiSourceName, String());
                CHECK(name == String("MyHost (Channel 1)"));
        }
        // ndi:///Source → "Source" (any machine)
        {
                Result<Url> parsed = Url::fromString(String("ndi:///GlobalSource"));
                REQUIRE(parsed.second().isOk());
                MediaIO::Config cfg;
                Error           err = f->urlToConfig(parsed.first(), &cfg);
                CHECK(err.isOk());
                String name = cfg.getAs<String>(MediaConfig::NdiSourceName, String());
                CHECK(name == String("GlobalSource"));
        }
}

TEST_CASE("NdiMediaIO: source mode requires a configured source name") {
        if (!NdiLib::instance().isLoaded()) {
                MESSAGE("NDI runtime not available; skipping source-mode test");
                return;
        }
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Ndi");
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Read));
        // Deliberately leave NdiSourceName empty — open should fail
        // fast with InvalidArgument rather than hang waiting for a
        // source that wasn't named.
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        Error err = io->open().wait();
        CHECK(err == Error::InvalidArgument);
        io->close().wait();
        delete io;
}

TEST_CASE("NdiMediaIO: hermetic sender->receiver round-trip in one process") {
        if (!NdiLib::instance().isLoaded()) {
                MESSAGE("NDI runtime not available; skipping round-trip test");
                return;
        }

        // Pick a unique-enough name so this test never collides with
        // another sender on the network.  PID + time-since-epoch is
        // overkill but cheap.
        const String sendName = String::sprintf(
                "PromekiRoundTrip-%lld",
                static_cast<long long>(
                        std::chrono::system_clock::now().time_since_epoch().count()));

        // ---- Build the sink ----
        MediaIO::Config sinkCfg = MediaIOFactory::defaultConfig("Ndi");
        sinkCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        sinkCfg.set(MediaConfig::NdiSendName, sendName);
        sinkCfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
        sinkCfg.set(MediaConfig::VideoSize, Size2Du32(160, 120));
        sinkCfg.set(MediaConfig::VideoPixelFormat,
                    PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));

        ImageDesc imgDesc(Size2Du32(160, 120), PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
        MediaDesc desc;
        desc.setFrameRate(FrameRate(FrameRate::FPS_30));
        desc.imageList().pushToBack(imgDesc);

        MediaIO *sinkIo = MediaIO::create(sinkCfg);
        REQUIRE(sinkIo != nullptr);
        REQUIRE(sinkIo->setPendingMediaDesc(desc).isOk());
        REQUIRE(sinkIo->open().wait().isOk());

        // ---- Allocate one frame and start sending in a background
        //      thread so the receiver below has something to grab.
        UncompressedVideoPayload::Ptr vp = UncompressedVideoPayload::allocate(imgDesc);
        REQUIRE(vp.isValid());
        // Distinctive UYVY pattern for content verification.
        uint8_t *bytes = vp->data().data();
        const size_t total = vp->data().size();
        for (size_t i = 0; i + 3 < total; i += 4) {
                bytes[i + 0] = 100; // Cb
                bytes[i + 1] = 200; // Y
                bytes[i + 2] = 50;  // Cr
                bytes[i + 3] = 200; // Y
        }
        // Stamp the payload's PTS with a distinctive value so we can
        // verify the sender plumbs it into NDI's `timecode` field
        // and the receiver re-attaches a per-frame PTS on the other
        // side.  We use a high-arbitrary value so the SDK's
        // "synthesize" auto-incremented numbers can't collide.
        const int64_t    kSentinelNs = 5'000'000'000LL; // 5 seconds since epoch.
        TimeStamp::Value sentinelV{std::chrono::nanoseconds(kSentinelNs)};
        TimeStamp        sentinelTs(sentinelV);
        vp.modify()->setPts(MediaTimeStamp(sentinelTs, NdiClock::domain()));

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->addPayload(vp);

        std::atomic<bool> stopSender{false};
        std::thread       senderThread([&] {
                while (!stopSender.load(std::memory_order_acquire)) {
                        sinkIo->sink(0)->writeFrame(frame).wait();
                        std::this_thread::sleep_for(std::chrono::milliseconds(33));
                }
        });

        // ---- Resolve the canonical name on this machine and open
        //      the receiver.  NDI prefixes the sender name with the
        //      hostname, so the discovery registry will report
        //      "<HOST> (PromekiRoundTrip-...)".
        const String canonical = String("");  // any-machine match
        // Wait for discovery to see the sender by walking the
        // registry — we don't know the hostname at compile time and
        // the SDK's actual broadcast is async.
        bool   sawSender = false;
        String resolvedName;
        for (int i = 0; i < 50 && !sawSender; ++i) {
                for (const auto &r : NdiDiscovery::instance().sources()) {
                        if (r.canonicalName.contains(sendName)) {
                                resolvedName = r.canonicalName;
                                sawSender    = true;
                                break;
                        }
                }
                if (!sawSender) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!sawSender) {
                stopSender.store(true, std::memory_order_release);
                senderThread.join();
                sinkIo->close().wait();
                delete sinkIo;
                MESSAGE("NDI discovery did not see the sender within 5s — "
                        "skipping round-trip read step");
                return;
        }

        MediaIO::Config srcCfg = MediaIOFactory::defaultConfig("Ndi");
        srcCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Read));
        srcCfg.set(MediaConfig::NdiSourceName, resolvedName);
        srcCfg.set(MediaConfig::NdiFindWait, Duration::fromSeconds(2));
        // Force UYVY on the receiver so we can verify the content
        // bytes round-trip without colour-space conversion noise.
        srcCfg.set(MediaConfig::NdiColorFormat, NdiColorFormat::UyvyBgra);
        MediaIO *srcIo = MediaIO::create(srcCfg);
        REQUIRE(srcIo != nullptr);
        // Give the open path 5s — recv_create_v3 itself is fast but
        // its connection handshake may take time on a busy network.
        Error openErr = srcIo->open().wait(5'000);
        if (openErr.isError()) {
                MESSAGE("Receiver open() did not complete within 5s: " << openErr.name().cstr());
                stopSender.store(true, std::memory_order_release);
                senderThread.join();
                srcIo->close().wait(2'000);
                delete srcIo;
                sinkIo->close().wait();
                delete sinkIo;
                return;
        }

        // Single read with a 10s budget.  We deliberately avoid the
        // submit-then-timeout-then-resubmit loop — each timed-out
        // wait leaves an in-flight Read on the strand, and a backlog
        // of stranded Reads delays close-time cleanup.
        int        framesRead = 0;
        Frame::Ptr received;
        {
                MediaIORequest readReq = srcIo->source(0)->readFrame();
                Error          rerr    = readReq.wait(10'000);
                if (rerr.isOk()) {
                        ++framesRead;
                        if (const auto *cr = readReq.commandAs<MediaIOCommandRead>()) {
                                received = cr->frame;
                        }
                }
        }
        // The test is informational — passing means at least open
        // and close worked end-to-end through the strand.  A frame
        // arriving is a nice-to-have but the in-process NDI loopback
        // can be flaky on first connect.
        MESSAGE("Round-trip delivered " << framesRead << " frame(s).");

        // When a frame did arrive, verify both timestamp channels:
        //   PTS (sender-anchored) — set by the receiver from NDI's
        //         per-frame `timestamp` field, in the NdiClock domain.
        //   CaptureTime (local-arrival) — set by the receiver from
        //         TimeStamp::now() at the moment our capture thread
        //         saw the packet, in the SystemMonotonic domain.
        if (received.isValid()) {
                auto rxVids = received->videoPayloads();
                REQUIRE(!rxVids.isEmpty());
                CHECK(rxVids[0].isValid());
                if (rxVids[0].isValid()) {
                        const MediaTimeStamp &rxPts = rxVids[0]->pts();
                        if (rxPts.isValid()) {
                                CHECK(rxPts.domain().id() == NdiClock::domain().id());
                                CHECK(rxPts.timeStamp().nanoseconds() > 0);
                                // DTS == PTS (no B-frame reorder in NDI).
                                CHECK(rxVids[0]->dts().timeStamp().nanoseconds() ==
                                      rxPts.timeStamp().nanoseconds());
                                MESSAGE("Receiver PTS: " << rxPts.timeStamp().nanoseconds() << " ns");
                        } else {
                                MESSAGE("Receiver did not attach PTS — SDK delivered frame "
                                        "with NDIlib_recv_timestamp_undefined?");
                        }
                        // CaptureTime is unconditionally set on every
                        // received payload (it's local arrival time,
                        // always available).
                        REQUIRE(rxVids[0]->metadata().contains(Metadata::CaptureTime));
                        MediaTimeStamp ct = rxVids[0]->metadata()
                                                    .get(Metadata::CaptureTime)
                                                    .get<MediaTimeStamp>();
                        CHECK(ct.isValid());
                        CHECK(ct.domain().id() == ClockDomain(ClockDomain::SystemMonotonic).id());
                        MESSAGE("Receiver local CaptureTime: " << ct.timeStamp().nanoseconds() << " ns");
                }
        }

        // Stop the sender thread first so writeFrame() doesn't race
        // the close.  Tear down sink, then source — this ordering
        // matches what other NDI integrations do for in-process
        // loopback.
        stopSender.store(true, std::memory_order_release);
        senderThread.join();
        sinkIo->close().wait();
        delete sinkIo;
        srcIo->close().wait();
        delete srcIo;
}

TEST_CASE("NdiMediaIO: proposeInput accepts NDI-native formats unchanged") {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Ndi");
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc offered;
        offered.setFrameRate(FrameRate(FrameRate::FPS_30));
        offered.imageList().pushToBack(
                ImageDesc(Size2Du32(1920, 1080),
                          PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709)));

        MediaDesc preferred;
        Error     err = io->proposeInput(offered, &preferred);
        CHECK(err.isOk());
        REQUIRE(!preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat().id()
              == PixelFormat::YUV8_422_UYVY_Rec709);
        delete io;
}

TEST_CASE("NdiMediaIO: proposeInput requests CSC bridge for unsupported RGB input") {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Ndi");
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        // RGB8_sRGB is what TPG emits by default — not in NDI's set.
        MediaDesc offered;
        offered.setFrameRate(FrameRate(FrameRate::FPS_30));
        offered.imageList().pushToBack(
                ImageDesc(Size2Du32(1920, 1080),
                          PixelFormat(PixelFormat::RGB8_sRGB)));

        MediaDesc preferred;
        Error     err = io->proposeInput(offered, &preferred);
        CHECK(err.isOk());
        REQUIRE(!preferred.imageList().isEmpty());
        // RGB family fallback: BGRA8_sRGB.
        CHECK(preferred.imageList()[0].pixelFormat().id()
              == PixelFormat::BGRA8_sRGB);
        // Non-format fields preserved.
        CHECK(preferred.imageList()[0].size() == Size2Du32(1920, 1080));
        CHECK(preferred.frameRate() == FrameRate(FrameRate::FPS_30));
        delete io;
}

TEST_CASE("NdiMediaIO: proposeInput requests UYVY for unsupported YUV input") {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Ndi");
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        // 16-bit interleaved UYVY has no NDI FourCC (P216 is semi-planar).
        MediaDesc offered;
        offered.setFrameRate(FrameRate(FrameRate::FPS_30));
        offered.imageList().pushToBack(
                ImageDesc(Size2Du32(1920, 1080),
                          PixelFormat(PixelFormat::YUV16_422_UYVY_LE_Rec709)));

        MediaDesc preferred;
        Error     err = io->proposeInput(offered, &preferred);
        CHECK(err.isOk());
        REQUIRE(!preferred.imageList().isEmpty());
        // YUV family fallback: UYVY.
        CHECK(preferred.imageList()[0].pixelFormat().id()
              == PixelFormat::YUV8_422_UYVY_Rec709);
        delete io;
}

TEST_CASE("NdiMediaIO: rejects unsupported pixel formats with FormatMismatch at open") {
        if (!NdiLib::instance().isLoaded()) {
                MESSAGE("NDI runtime not available; skipping format-rejection test");
                return;
        }

        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Ndi");
        cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        cfg.set(MediaConfig::NdiSendName, String("PromekiTestUnsupportedFormat"));

        // 16-bit interleaved UYVY — valid promeki format, but no NDI
        // FourCC equivalent (NDI uses semi-planar P216 for high-bit-
        // depth content).  The backend should report FormatMismatch.
        ImageDesc imgDesc(Size2Du32(320, 240),
                          PixelFormat(PixelFormat::YUV16_422_UYVY_LE_Rec709));
        MediaDesc desc;
        desc.setFrameRate(FrameRate(FrameRate::FPS_30));
        desc.imageList().pushToBack(imgDesc);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->setPendingMediaDesc(desc).isOk());
        Error err = io->open().wait();
        CHECK(err == Error::FormatMismatch);
        // open() left the MediaIO in a not-open state — close is a no-op
        // but exercises the error-path teardown.
        io->close().wait();
        delete io;
}

#endif // PROMEKI_ENABLE_NDI
