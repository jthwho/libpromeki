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
#include <cstring>
#include <thread>
#include <vector>

#include <promeki/audiobuffer.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/audiomarker.h>
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
#include <promeki/ndimediaio.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/pixelformat.h>
#include <promeki/result.h>
#include <promeki/system.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/url.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Test-only friend of @ref NdiMediaIO.
 *
 * Exposes the audio-ingest path so unit tests can drive synthetic
 * NDI audio frames into the ring without spinning up the NDI SDK
 * (the live SDK requires network discovery + a sender).  Only used
 * by @c tests/unit/ndimediaio.cpp.
 */
struct NdiMediaIOTestAccess {
                static void ingest(NdiMediaIO &io, int64_t timestampTicks, size_t samples, size_t channels,
                                   float rate, const uint8_t *planar, size_t channelStrideBytes) {
                        io.ingestNdiAudio(timestampTicks, samples, channels, rate, planar, channelStrideBytes);
                }
                static int64_t firstSampleTicks(const NdiMediaIO &io) { return io._audioFirstSampleTicks; }
                static int64_t nextSampleTicks(const NdiMediaIO &io) { return io._audioNextSampleTicks; }
                static const AudioMarkerList &markers(const NdiMediaIO &io) {
                        return io._audioMarkersSinceDrain;
                }
                static int64_t silenceSamples(const NdiMediaIO &io) {
                        return io._audioSilenceSamples.load(std::memory_order_relaxed);
                }
                static int64_t gapEvents(const NdiMediaIO &io) {
                        return io._audioGapEvents.load(std::memory_order_relaxed);
                }
                static size_t  available(NdiMediaIO &io) { return io._audioRing.available(); }
};

PROMEKI_NAMESPACE_END

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

TEST_CASE("NdiFactory: urlToConfig populates NdiSourceName and NdiSendName from ndi:// URLs") {
        const MediaIOFactory *f = MediaIOFactory::findByName(String("Ndi"));
        REQUIRE(f != nullptr);

        // ndi://Machine/Source → source canonical "Machine (Source)",
        // sender name "Source" (the bare path component).
        {
                Result<Url> parsed = Url::fromString(String("ndi://MyHost/Channel%201"));
                REQUIRE(parsed.second().isOk());
                MediaIO::Config cfg;
                Error           err = f->urlToConfig(parsed.first(), &cfg);
                CHECK(err.isOk());
                CHECK(cfg.getAs<String>(MediaConfig::NdiSourceName, String())
                      == String("MyHost (Channel 1)"));
                CHECK(cfg.getAs<String>(MediaConfig::NdiSendName, String())
                      == String("Channel 1"));
        }
        // ndi:///Source → source canonical "<this machine> (Source)",
        // sender name "Source".  The host is filled in from the
        // local hostname so the source canonical round-trips through
        // the discovery registry on this machine.
        {
                Result<Url> parsed = Url::fromString(String("ndi:///GlobalSource"));
                REQUIRE(parsed.second().isOk());
                MediaIO::Config cfg;
                Error           err = f->urlToConfig(parsed.first(), &cfg);
                CHECK(err.isOk());
                const String me = System::hostname();
                const String expectedSrc = me.isEmpty()
                        ? String("GlobalSource")
                        : (me + String(" (GlobalSource)"));
                CHECK(cfg.getAs<String>(MediaConfig::NdiSourceName, String()) == expectedSrc);
                CHECK(cfg.getAs<String>(MediaConfig::NdiSendName, String()) == String("GlobalSource"));
        }
        // urlToConfig must not stamp OpenMode itself — the precedence
        // chain (defaults < createFromUrl hint < query) lives one
        // level up.  Verifies the regression of the previous behavior
        // (which forced Read and made sender URLs impossible).
        {
                Result<Url> parsed = Url::fromString(String("ndi:///AnyName"));
                REQUIRE(parsed.second().isOk());
                MediaIO::Config cfg;
                cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
                Error err = f->urlToConfig(parsed.first(), &cfg);
                CHECK(err.isOk());
                Enum mode = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
                CHECK(mode.value() == MediaIOOpenMode::Write.value());
        }
}

TEST_CASE("MediaIO::createForFileWrite stamps OpenMode = Write on ndi:// URLs") {
        // Driving createForFileWrite with an ndi:/// URL must produce
        // a sink-mode MediaIO (NdiSendName populated, OpenMode = Write
        // on the live config).  This is the user-visible end of the
        // urlToConfig + createFromUrlInternal change — without the
        // OpenMode hint the URL takeover would silently default to
        // Read and try to open a receiver instead.
        MediaIO *io = MediaIO::createForFileWrite(String("ndi:///PromekiUrlSinkProbe"));
        REQUIRE(io != nullptr);
        const MediaIO::Config &cfg = io->config();
        Enum mode = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        CHECK(mode.value() == MediaIOOpenMode::Write.value());
        CHECK(cfg.getAs<String>(MediaConfig::NdiSendName, String())
              == String("PromekiUrlSinkProbe"));
        delete io;
}

TEST_CASE("MediaIO::createForFileRead stamps OpenMode = Read on ndi:// URLs") {
        MediaIO *io = MediaIO::createForFileRead(String("ndi:///PromekiUrlSourceProbe"));
        REQUIRE(io != nullptr);
        const MediaIO::Config &cfg = io->config();
        Enum mode = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        CHECK(mode.value() == MediaIOOpenMode::Read.value());
        // Source canonical includes the local hostname when the URL
        // host is empty.
        const String me = System::hostname();
        const String expectedSrc = me.isEmpty()
                ? String("PromekiUrlSourceProbe")
                : (me + String(" (PromekiUrlSourceProbe)"));
        CHECK(cfg.getAs<String>(MediaConfig::NdiSourceName, String()) == expectedSrc);
        delete io;
}

TEST_CASE("NdiMediaIO: sink open rejects URL whose host is not this machine") {
        if (!NdiLib::instance().isLoaded()) {
                MESSAGE("NDI runtime not available; skipping non-local sink rejection test");
                return;
        }
        // Build an ndi://<otherhost>/<name> URL so urlToConfig stashes
        // a non-empty, non-local host on the live Url config.  open()
        // must reject this with InvalidArgument before touching the
        // SDK — NDI senders can only run on the local machine.
        MediaIO *io = MediaIO::createForFileWrite(
                String("ndi://not-this-machine.invalid/PromekiNonLocal"));
        REQUIRE(io != nullptr);

        ImageDesc imgDesc(Size2Du32(160, 120), PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
        MediaDesc desc;
        desc.setFrameRate(FrameRate(FrameRate::FPS_30));
        desc.imageList().pushToBack(imgDesc);
        REQUIRE(io->setPendingMediaDesc(desc).isOk());

        Error err = io->open().wait();
        CHECK(err == Error::InvalidArgument);
        io->close().wait();
        delete io;
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
                // The receiver stamps @c Metadata::FrameRate on every
                // emitted frame so downstream stages (e.g. the
                // inspector's marker-based A/V sync check) see the
                // real sender rate rather than the open-time placeholder.
                // Without this, a 29.97 source would be predicted at
                // a 30/1 cadence and accumulate ~1.6 sample/frame
                // drift in the A/V sync offset.
                REQUIRE(received->metadata().contains(Metadata::FrameRate));
                FrameRate rxFr = received->metadata()
                                         .get(Metadata::FrameRate)
                                         .get<FrameRate>();
                CHECK(rxFr.isValid());
                CHECK(rxFr == FrameRate(FrameRate::FPS_30));
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

// ============================================================================
// Audio ingest / gap-fill / PTS tracking
//
// These tests exercise NdiMediaIO::ingestNdiAudio directly via the
// NdiMediaIOTestAccess friend struct, bypassing the live SDK.  They
// verify the timeline tracking that drives the receiver's audio PTS
// and the @ref AudioMarkerList that flags synthesized silence to
// downstream stages.  Each test pushes synthetic planar-float
// "audio" with a known sender-anchored timestamp and inspects the
// resulting state.
// ============================================================================

namespace {

        // NDI ticks are 100 ns; one second is 1e7 ticks.
        constexpr int64_t kTicksPerSecond = 10'000'000;

        // Build a tightly-packed planar float buffer with @p samples
        // samples and @p channels channels.  Every sample is a
        // distinct value for content checks downstream of the ring.
        std::vector<float> makePlanarRamp(size_t samples, size_t channels, float seed) {
                std::vector<float> buf(samples * channels, 0.0f);
                for (size_t c = 0; c < channels; ++c) {
                        for (size_t s = 0; s < samples; ++s) {
                                buf[c * samples + s] = seed + static_cast<float>(c) * 100.0f +
                                                       static_cast<float>(s);
                        }
                }
                return buf;
        }

        const uint8_t *bytes(const std::vector<float> &v) {
                return reinterpret_cast<const uint8_t *>(v.data());
        }

} // namespace

TEST_CASE("NdiMediaIO: ingest first audio frame anchors the timeline") {
        NdiMediaIO  io;
        const float rate     = 48000.0f;
        const size_t channels = 2;
        const size_t samples  = 480;
        auto         buf      = makePlanarRamp(samples, channels, 0.0f);
        const int64_t ts      = 100 * kTicksPerSecond; // arbitrary anchor

        NdiMediaIOTestAccess::ingest(io, ts, samples, channels, rate, bytes(buf),
                                     samples * sizeof(float));

        CHECK(NdiMediaIOTestAccess::firstSampleTicks(io) == ts);
        // Next sample = ts + samples * (1e7 / rate) = ts + 100000 ticks.
        CHECK(NdiMediaIOTestAccess::nextSampleTicks(io) == ts + 100000);
        CHECK(NdiMediaIOTestAccess::markers(io).isEmpty());
        CHECK(NdiMediaIOTestAccess::silenceSamples(io) == 0);
        CHECK(NdiMediaIOTestAccess::available(io) == samples);
}

TEST_CASE("NdiMediaIO: contiguous frames coalesce without inserting silence") {
        NdiMediaIO    io;
        const float   rate     = 48000.0f;
        const size_t  channels = 2;
        const size_t  samples  = 240;
        const int64_t durTicks = 50000; // 240 samples at 48 kHz = 50000 ticks
        const int64_t ts0      = 5 * kTicksPerSecond;

        auto buf0 = makePlanarRamp(samples, channels, 0.0f);
        auto buf1 = makePlanarRamp(samples, channels, 1000.0f);

        NdiMediaIOTestAccess::ingest(io, ts0, samples, channels, rate, bytes(buf0),
                                     samples * sizeof(float));
        // Second frame's timestamp is exactly the expected next-sample
        // anchor — no gap, no silence.
        NdiMediaIOTestAccess::ingest(io, ts0 + durTicks, samples, channels, rate, bytes(buf1),
                                     samples * sizeof(float));

        CHECK(NdiMediaIOTestAccess::firstSampleTicks(io) == ts0);
        CHECK(NdiMediaIOTestAccess::nextSampleTicks(io) == ts0 + 2 * durTicks);
        CHECK(NdiMediaIOTestAccess::markers(io).isEmpty());
        CHECK(NdiMediaIOTestAccess::silenceSamples(io) == 0);
        CHECK(NdiMediaIOTestAccess::gapEvents(io) == 0);
        CHECK(NdiMediaIOTestAccess::available(io) == 2 * samples);
}

TEST_CASE("NdiMediaIO: gap beyond jitter window inserts silence with marker") {
        NdiMediaIO    io;
        const float   rate     = 48000.0f;
        const size_t  channels = 2;
        const size_t  samples  = 240;
        const int64_t durTicks = 50000; // 240 samples
        const int64_t ts0      = 5 * kTicksPerSecond;

        auto buf0 = makePlanarRamp(samples, channels, 0.0f);
        auto buf1 = makePlanarRamp(samples, channels, 1000.0f);

        NdiMediaIOTestAccess::ingest(io, ts0, samples, channels, rate, bytes(buf0),
                                     samples * sizeof(float));
        // Skip 500 samples worth of timeline (~10.42 ms — well above
        // the 5 ms jitter tolerance, so it counts as a real gap).
        const int64_t gapSamples = 500;
        const int64_t gapTicks   = static_cast<int64_t>(static_cast<double>(gapSamples) *
                                                        kTicksPerSecond / rate);
        const int64_t ts1        = ts0 + durTicks + gapTicks;
        NdiMediaIOTestAccess::ingest(io, ts1, samples, channels, rate, bytes(buf1),
                                     samples * sizeof(float));

        // Ring now holds: [frame0 (240) + silence (~500) + frame1 (240)].
        CHECK(NdiMediaIOTestAccess::available(io) >= 2 * samples + 499);
        CHECK(NdiMediaIOTestAccess::available(io) <= 2 * samples + 501);

        // Marker list has exactly one SilenceFill at offset = 240.
        const AudioMarkerList &mk = NdiMediaIOTestAccess::markers(io);
        REQUIRE(mk.size() == 1);
        CHECK(mk.entries()[0].offset() == static_cast<int64_t>(samples));
        CHECK(mk.entries()[0].length() >= 499);
        CHECK(mk.entries()[0].length() <= 501);
        CHECK(mk.entries()[0].type() == AudioMarkerType::SilenceFill);

        // First-sample anchor unchanged (still pointing at frame 0).
        CHECK(NdiMediaIOTestAccess::firstSampleTicks(io) == ts0);
        CHECK(NdiMediaIOTestAccess::silenceSamples(io) >= 499);
        CHECK(NdiMediaIOTestAccess::gapEvents(io) == 1);
}

TEST_CASE("NdiMediaIO: gap larger than 1 second triggers re-anchor") {
        NdiMediaIO    io;
        const float   rate     = 48000.0f;
        const size_t  channels = 2;
        const size_t  samples  = 240;
        const int64_t durTicks = 50000;
        const int64_t ts0      = 5 * kTicksPerSecond;

        auto buf0 = makePlanarRamp(samples, channels, 0.0f);
        auto buf1 = makePlanarRamp(samples, channels, 1000.0f);

        NdiMediaIOTestAccess::ingest(io, ts0, samples, channels, rate, bytes(buf0),
                                     samples * sizeof(float));
        // Jump >1 s into the future — this should clear the buffered
        // samples and re-anchor on the new frame.
        const int64_t ts1 = ts0 + durTicks + 2 * kTicksPerSecond;
        NdiMediaIOTestAccess::ingest(io, ts1, samples, channels, rate, bytes(buf1),
                                     samples * sizeof(float));

        // Re-anchored: only the new frame's samples are in the ring,
        // and PTS is the new frame's timestamp.
        CHECK(NdiMediaIOTestAccess::available(io) == samples);
        CHECK(NdiMediaIOTestAccess::firstSampleTicks(io) == ts1);
        CHECK(NdiMediaIOTestAccess::nextSampleTicks(io) == ts1 + durTicks);
        CHECK(NdiMediaIOTestAccess::markers(io).isEmpty());
        // Silence counter does not advance on a re-anchor — we
        // discard rather than bridge.
        CHECK(NdiMediaIOTestAccess::silenceSamples(io) == 0);
}

TEST_CASE("NdiMediaIO: sub-millisecond timestamp jitter does not bridge or warn") {
        // Sender-side timestamp jitter on a sample-locked audio
        // clock should be absorbed silently — no silence injection,
        // no marker, no warning, and the next-sample anchor keeps
        // tracking the prior prediction (not the noisy timestamp).
        NdiMediaIO    io;
        const float   rate     = 48000.0f;
        const size_t  channels = 2;
        const size_t  samples  = 240;
        const int64_t durTicks = 50000;
        const int64_t ts0      = 5 * kTicksPerSecond;

        auto buf0 = makePlanarRamp(samples, channels, 0.0f);
        auto buf1 = makePlanarRamp(samples, channels, 1000.0f);
        auto buf2 = makePlanarRamp(samples, channels, 2000.0f);

        NdiMediaIOTestAccess::ingest(io, ts0, samples, channels, rate, bytes(buf0),
                                     samples * sizeof(float));
        // Frame 1: 2 ms positive jitter (well inside the 5 ms tolerance).
        const int64_t ts1 = ts0 + durTicks + 20'000;
        NdiMediaIOTestAccess::ingest(io, ts1, samples, channels, rate, bytes(buf1),
                                     samples * sizeof(float));
        // Frame 2: 2 ms negative jitter (still inside tolerance).
        const int64_t ts2 = ts1 + durTicks - 20'000;
        NdiMediaIOTestAccess::ingest(io, ts2, samples, channels, rate, bytes(buf2),
                                     samples * sizeof(float));

        CHECK(NdiMediaIOTestAccess::available(io) == 3 * samples);
        CHECK(NdiMediaIOTestAccess::markers(io).isEmpty());
        CHECK(NdiMediaIOTestAccess::silenceSamples(io) == 0);
        CHECK(NdiMediaIOTestAccess::gapEvents(io) == 0);
        // Anchor advances by exactly samples each frame, ignoring
        // the noisy ts1 / ts2 values.
        CHECK(NdiMediaIOTestAccess::nextSampleTicks(io) == ts0 + 3 * durTicks);
}

TEST_CASE("NdiMediaIO: timestamp regression beyond jitter window is treated as continuous") {
        NdiMediaIO    io;
        const float   rate     = 48000.0f;
        const size_t  channels = 2;
        const size_t  samples  = 240;
        const int64_t durTicks = 50000;
        const int64_t ts0      = 5 * kTicksPerSecond;

        auto buf0 = makePlanarRamp(samples, channels, 0.0f);
        auto buf1 = makePlanarRamp(samples, channels, 1000.0f);

        NdiMediaIOTestAccess::ingest(io, ts0, samples, channels, rate, bytes(buf0),
                                     samples * sizeof(float));
        // 10 ms backward jump — well outside the 5 ms jitter window.
        const int64_t ts1 = ts0 + durTicks - 100'000;
        NdiMediaIOTestAccess::ingest(io, ts1, samples, channels, rate, bytes(buf1),
                                     samples * sizeof(float));

        CHECK(NdiMediaIOTestAccess::available(io) == 2 * samples);
        CHECK(NdiMediaIOTestAccess::markers(io).isEmpty());
        CHECK(NdiMediaIOTestAccess::silenceSamples(io) == 0);
        CHECK(NdiMediaIOTestAccess::gapEvents(io) == 0);
        // Real regression re-anchors on the regressed timestamp.
        CHECK(NdiMediaIOTestAccess::nextSampleTicks(io) == ts1 + durTicks);
}

TEST_CASE("NdiMediaIO: undefined timestamp skips gap detection") {
        NdiMediaIO    io;
        const float   rate     = 48000.0f;
        const size_t  channels = 2;
        const size_t  samples  = 240;
        const int64_t durTicks = 50000;
        const int64_t ts0      = 5 * kTicksPerSecond;

        auto buf0 = makePlanarRamp(samples, channels, 0.0f);
        auto buf1 = makePlanarRamp(samples, channels, 1000.0f);

        NdiMediaIOTestAccess::ingest(io, ts0, samples, channels, rate, bytes(buf0),
                                     samples * sizeof(float));
        // SDK sentinel for "no timestamp" — the public NDI header
        // defines NDIlib_recv_timestamp_undefined as -1.  Pass the
        // raw value here so the test does not depend on the SDK
        // header.
        NdiMediaIOTestAccess::ingest(io, -1, samples, channels, rate, bytes(buf1),
                                     samples * sizeof(float));

        CHECK(NdiMediaIOTestAccess::available(io) == 2 * samples);
        CHECK(NdiMediaIOTestAccess::markers(io).isEmpty());
        CHECK(NdiMediaIOTestAccess::silenceSamples(io) == 0);
        // Next-sample anchor unchanged from frame 0 — undefined
        // timestamps don't move it.
        CHECK(NdiMediaIOTestAccess::nextSampleTicks(io) == ts0 + durTicks);
}

#endif // PROMEKI_ENABLE_NDI
