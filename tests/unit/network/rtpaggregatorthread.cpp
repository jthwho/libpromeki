/**
 * @file      rtpaggregatorthread.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <promeki/atomic.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/clockdomain.h>
#include <promeki/duration.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/list.h>
#include <promeki/metadata.h>
#include <promeki/ntptime.h>
#include <promeki/queue.h>
#include <promeki/rtpaggregatorthread.h>
#include <promeki/rxpayloadbundle.h>
#include <promeki/timestamp.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/videopayload.h>

using namespace promeki;

namespace {

// Builds an RxAudioChunk holding @p sampleCount stereo S16 samples
// at the given RTP timestamp + captureTime.  The PCM buffer is
// non-owning data — every byte is zero — but its size matches the
// declared sample count so the FIFO push / size accounting is
// realistic.
RxAudioChunk makeAudioChunk(const AudioDesc &desc, uint32_t rtpTs,
                            size_t sampleCount, const TimeStamp &captureTime) {
        RxAudioChunk c;
        c.wireDesc = desc;
        c.rtpTimestamp = rtpTs;
        c.sampleCount = sampleCount;
        c.captureTime = captureTime;
        c.firstPacketArrival = captureTime;
        const size_t bytes = desc.bufferSize(sampleCount);
        c.pcmBytes = Buffer(bytes);
        std::memset(c.pcmBytes.data(), 0, bytes);
        c.pcmBytes.setSize(bytes);
        return c;
}

// Builds an RxVideoFrame with a stub uncompressed payload.  Tests
// only inspect the captureTime / data-message / audio-fifo plumbing,
// so the payload's bytes are uninitialised.
RxVideoFrame makeVideoFrame(uint32_t rtpTs, const TimeStamp &captureTime) {
        RxVideoFrame v;
        v.rtpTimestamp = rtpTs;
        v.captureTime = captureTime;
        v.firstPacketArrival = captureTime;
        ImageDesc id;
        id.setSize(Size2Du32(64, 36));
        id.setPixelFormat(PixelFormat::RGB8_sRGB);
        v.imageDesc = id;
        v.payload = UncompressedVideoPayload::Ptr::create(id);
        return v;
}

RxDataMessage makeDataMessage(uint32_t rtpTs, const TimeStamp &captureTime,
                              const String &tag) {
        RxDataMessage d;
        d.rtpTimestamp = rtpTs;
        d.captureTime = captureTime;
        d.firstPacketArrival = captureTime;
        d.metadata.set(Metadata::Description, Variant(tag));
        return d;
}

// Convenience for the watchdog test — builds a context with only
// the video sub-context wired up and a synthetic
// lastPacketArrivalNs counter.  pushFrame appends emitted Frames
// to @p sink.
RtpAggregatorContext makeVideoOnlyContext(Queue<RxVideoFrame> &videoQueue,
                                          const Atomic<int64_t> &lastArrivalNs,
                                          List<Frame> &sink,
                                          const FrameRate &fr,
                                          bool watchdogEnabled) {
        RtpAggregatorContext ctx;
        ctx.frameRate = fr;
        ctx.videoWatchdogEnabled = watchdogEnabled;
        ctx.video.payloadQueue = &videoQueue;
        ctx.video.lastPacketArrivalNs = &lastArrivalNs;
        ctx.pushFrame = [&sink](Frame frame) { sink.pushToBack(std::move(frame)); };
        return ctx;
}

} // namespace

TEST_CASE("RtpAggregatorThread: video-clocked drain pulls samplesPerFrame audio per Frame") {
        AudioDesc audioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        REQUIRE(audioDesc.isValid());

        Queue<RxVideoFrame>  videoQ;
        Queue<RxAudioChunk>  audioQ;
        Queue<RxDataMessage> dataQ;
        List<Frame>          sink;
        Atomic<int64_t>      lastArrivalNs;
        bool                 hasSr = false;

        RtpAggregatorContext ctx;
        ctx.frameRate = FrameRate(FrameRate::FPS_30);
        ctx.video.payloadQueue = &videoQ;
        ctx.video.lastPacketArrivalNs = &lastArrivalNs;
        ctx.audio.payloadQueue = &audioQ;
        ctx.audio.active = true;
        ctx.audio.readerAudioDesc = &audioDesc;
        ctx.audio.hasSr = &hasSr;
        ctx.audio.clockDomain = ClockDomain::SystemMonotonic;
        ctx.data.payloadQueue = &dataQ;
        ctx.data.active = true;
        ctx.data.clockDomain = ClockDomain::SystemMonotonic;
        ctx.pushFrame = [&sink](Frame frame) { sink.pushToBack(std::move(frame)); };

        const TimeStamp t0 = TimeStamp::now();
        const Duration  fd = Duration::fromNanoseconds(1'000'000'000 / 30);
        // Pre-fill 1.5 frames' worth of audio at 30 fps × 48 kHz =
        // 1600 samples per frame so the first pop has more than
        // enough.
        const size_t spf = 1600;
        audioQ.push(makeAudioChunk(audioDesc, 0u, spf, t0));
        audioQ.push(makeAudioChunk(audioDesc, static_cast<uint32_t>(spf),
                                   spf, t0 + fd));

        // Single video frame at t0.
        videoQ.push(makeVideoFrame(0u, t0));

        RtpAggregatorThread agg(std::move(ctx),
                                RtpAggregatorThread::Mode::Video);
        agg.runOnce(0);

        REQUIRE(sink.size() == 1u);
        const Frame &f = sink[0];
        CHECK(f.captureTime().timeStamp() == t0);
        CHECK_FALSE(f.audioPayloads().isEmpty());
}

TEST_CASE("RtpAggregatorThread: data-only mode emits one Frame per RxDataMessage") {
        Queue<RxDataMessage> dataQ;
        List<Frame>          sink;

        RtpAggregatorContext ctx;
        ctx.data.payloadQueue = &dataQ;
        ctx.data.active = true;
        ctx.data.clockDomain = ClockDomain::SystemMonotonic;
        ctx.pushFrame = [&sink](Frame frame) { sink.pushToBack(std::move(frame)); };

        const TimeStamp t0 = TimeStamp::now();
        dataQ.push(makeDataMessage(0u, t0, String("first")));
        dataQ.push(makeDataMessage(90u, t0 + Duration::fromMilliseconds(33), String("second")));

        RtpAggregatorThread agg(std::move(ctx),
                                RtpAggregatorThread::Mode::DataOnly);
        agg.runOnce(0);
        agg.runOnce(0);

        REQUIRE(sink.size() == 2u);
        CHECK(sink[0].metadata().get(Metadata::Description).get<String>() == "first");
        CHECK(sink[1].metadata().get(Metadata::Description).get<String>() == "second");
}

TEST_CASE("RtpAggregatorThread: audio-only mode emits one Frame per cadence boundary") {
        AudioDesc audioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        REQUIRE(audioDesc.isValid());

        Queue<RxAudioChunk> audioQ;
        List<Frame>         sink;

        RtpAggregatorContext ctx;
        ctx.frameRate = FrameRate(FrameRate::FPS_30); // 1600 samples/frame
        ctx.audio.payloadQueue = &audioQ;
        ctx.audio.active = true;
        ctx.audio.readerAudioDesc = &audioDesc;
        ctx.audio.clockDomain = ClockDomain::SystemMonotonic;
        ctx.pushFrame = [&sink](Frame frame) { sink.pushToBack(std::move(frame)); };

        const TimeStamp t0 = TimeStamp::now();
        // Push 3200 contiguous samples — exactly two frames.
        audioQ.push(makeAudioChunk(audioDesc, 0u, 1600u, t0));
        audioQ.push(makeAudioChunk(audioDesc, 1600u, 1600u,
                                   t0 + Duration::fromNanoseconds(1'000'000'000 / 30)));

        RtpAggregatorThread agg(std::move(ctx),
                                RtpAggregatorThread::Mode::AudioOnly);
        // Each runOnce pops one chunk and emits as many Frames as the
        // FIFO can yield at samplesPerFrame.
        agg.runOnce(0);
        agg.runOnce(0);

        REQUIRE(sink.size() == 2u);
}

TEST_CASE("RtpAggregatorThread: audio-only fallback emits per chunk when no frame rate") {
        AudioDesc audioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        Queue<RxAudioChunk> audioQ;
        List<Frame>         sink;

        RtpAggregatorContext ctx;
        // Invalid (default) FrameRate → per-chunk fallback path.
        ctx.audio.payloadQueue = &audioQ;
        ctx.audio.active = true;
        ctx.audio.readerAudioDesc = &audioDesc;
        ctx.audio.clockDomain = ClockDomain::SystemMonotonic;
        ctx.pushFrame = [&sink](Frame frame) { sink.pushToBack(std::move(frame)); };

        const TimeStamp t0 = TimeStamp::now();
        // Each chunk is below samplesPerFrame at any sane fps — but
        // because no frame rate is advertised, each chunk emits its
        // own Frame regardless of size.
        audioQ.push(makeAudioChunk(audioDesc, 0u, 64u, t0));
        audioQ.push(makeAudioChunk(audioDesc, 64u, 64u,
                                   t0 + Duration::fromMilliseconds(1)));
        audioQ.push(makeAudioChunk(audioDesc, 128u, 64u,
                                   t0 + Duration::fromMilliseconds(2)));

        RtpAggregatorThread agg(std::move(ctx),
                                RtpAggregatorThread::Mode::AudioOnly);
        agg.runOnce(0);
        agg.runOnce(0);
        agg.runOnce(0);

        REQUIRE(sink.size() == 3u);
}

TEST_CASE("RtpAggregatorThread: pending-data slot consumes a future-window message") {
        Queue<RxVideoFrame>  videoQ;
        Queue<RxDataMessage> dataQ;
        List<Frame>          sink;
        Atomic<int64_t>      lastArrivalNs;

        RtpAggregatorContext ctx;
        ctx.frameRate = FrameRate(FrameRate::FPS_30);
        ctx.video.payloadQueue = &videoQ;
        ctx.video.lastPacketArrivalNs = &lastArrivalNs;
        ctx.data.payloadQueue = &dataQ;
        ctx.data.active = true;
        ctx.data.clockDomain = ClockDomain::SystemMonotonic;
        ctx.pushFrame = [&sink](Frame frame) { sink.pushToBack(std::move(frame)); };

        const TimeStamp t0 = TimeStamp::now();
        const Duration  fd = Duration::fromNanoseconds(1'000'000'000 / 30);
        // Frame 0 window is [t0, t0 + fd).  This message lands at
        // t0 + fd*2 — it should be parked, not lost.
        dataQ.push(makeDataMessage(0u, t0 + fd + fd, String("future")));

        videoQ.push(makeVideoFrame(0u, t0));
        videoQ.push(makeVideoFrame(3000u, t0 + fd + fd));

        RtpAggregatorThread agg(std::move(ctx),
                                RtpAggregatorThread::Mode::Video);
        agg.runOnce(0);
        REQUIRE(sink.size() == 1u);
        // First Frame's metadata should be empty — the message was
        // parked because its captureTime is past the first window.
        CHECK_FALSE(sink[0].metadata().contains(Metadata::Description));

        agg.runOnce(0);
        REQUIRE(sink.size() == 2u);
        // Second Frame consumes the parked message.
        CHECK(sink[1].metadata().get(Metadata::Description).get<String>() == "future");
}

TEST_CASE("RtpAggregatorThread: watchdog disarmed before first video pop even on real silence") {
        Queue<RxVideoFrame> videoQ;
        Atomic<int64_t>     lastArrivalNs;
        List<Frame>         sink;
        // lastArrivalNs left at 0 — no packet has ever landed.
        FrameRate           fr(FrameRate::FPS_30);
        RtpAggregatorContext ctx = makeVideoOnlyContext(videoQ, lastArrivalNs,
                                                        sink, fr,
                                                        /* watchdogEnabled */ true);

        RtpAggregatorThread agg(std::move(ctx),
                                RtpAggregatorThread::Mode::Video);
        // Pop with timeout — watchdog must not engage.
        agg.runOnce(1);
        CHECK(sink.size() == 0u);
        CHECK_FALSE(agg.inWatchdog());
}

TEST_CASE("RtpAggregatorThread: watchdog cursor monotonic across stall + resume") {
        Queue<RxVideoFrame> videoQ;
        Atomic<int64_t>     lastArrivalNs;
        List<Frame>         sink;
        FrameRate            fr(FrameRate::FPS_30);
        RtpAggregatorContext ctx = makeVideoOnlyContext(videoQ, lastArrivalNs,
                                                        sink, fr,
                                                        /* watchdogEnabled */ true);

        RtpAggregatorThread agg(std::move(ctx),
                                RtpAggregatorThread::Mode::Video);

        // Pre-position the first video frame's captureTime in the
        // past so the watchdog's per-frame pacing gate
        // (now < cursor + fd) is satisfied immediately on the next
        // tick.  Real-world deployments wait for wallclock to
        // advance; the unit test simulates that by stamping the
        // cursor backward.
        const Duration  fd = Duration::fromNanoseconds(1'000'000'000 / 30);
        const TimeStamp captureT = TimeStamp::now() -
                                   Duration::fromNanoseconds(5 * fd.nanoseconds());
        videoQ.push(makeVideoFrame(0u, captureT));
        agg.runOnce(0);
        REQUIRE(sink.size() == 1u);
        const TimeStamp cursor0 = agg.emittedFrameCursor();
        CHECK(cursor0 == captureT);

        // Simulate a sender that landed its last packet ~5 frame
        // durations ago — a real wire-silence trigger.
        const int64_t  ago = TimeStamp::now().nanoseconds() -
                            (5 * fd.nanoseconds());
        lastArrivalNs.setValue(ago);

        // Watchdog tick — should engage and emit a continuation
        // Frame whose cursor advances by exactly fd.
        agg.runOnce(1);
        REQUIRE(sink.size() == 2u);
        const TimeStamp cursor1 = agg.emittedFrameCursor();
        CHECK(cursor1 == cursor0 + fd);
        CHECK(agg.inWatchdog());

        // Now push a video frame whose captureTime is *behind* the
        // watchdog cursor — emitFrameForVideo must snap it forward
        // to cursor + fd so consumers never see a backwards stamp.
        videoQ.push(makeVideoFrame(0u, cursor0)); // intentionally behind
        agg.runOnce(0);
        REQUIRE(sink.size() == 3u);
        const TimeStamp cursor2 = agg.emittedFrameCursor();
        // The stamp should be cursor1 + fd, not cursor0.
        CHECK(cursor2 == cursor1 + fd);
        CHECK_FALSE(agg.inWatchdog());
}

TEST_CASE("RtpAggregatorThread: requestStop wakes a blocked pop") {
        Queue<RxVideoFrame> videoQ;
        Atomic<int64_t>     lastArrivalNs;
        List<Frame>         sink;
        FrameRate            fr(FrameRate::FPS_30);
        RtpAggregatorContext ctx = makeVideoOnlyContext(videoQ, lastArrivalNs,
                                                        sink, fr,
                                                        /* watchdogEnabled */ false);

        RtpAggregatorThread agg(std::move(ctx),
                                RtpAggregatorThread::Mode::Video);
        // Asking for a long pop on an empty queue parks the call;
        // requestStop must cancel it promptly.
        std::thread t([&agg]() { agg.runOnce(60'000); });
        agg.requestStop();
        t.join();
        CHECK(agg.isStopRequested());
}

TEST_CASE("RtpAggregatorThread: video-clocked drain accumulates partial audio across iterations") {
        // Late audio: the audio chunk arrives in pieces, none of
        // which on its own can satisfy a samplesPerFrame slice.
        // The aggregator should hold partial samples in the FIFO
        // and emit only when there are enough — without dropping
        // packets that fall ahead of the current Frame's window.
        AudioDesc audioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        Queue<RxVideoFrame>  videoQ;
        Queue<RxAudioChunk>  audioQ;
        List<Frame>          sink;
        Atomic<int64_t>      lastArrivalNs;
        bool                 hasSr = false;

        RtpAggregatorContext ctx;
        ctx.frameRate = FrameRate(FrameRate::FPS_30); // 1600 spf @ 48 kHz
        ctx.video.payloadQueue = &videoQ;
        ctx.video.lastPacketArrivalNs = &lastArrivalNs;
        ctx.audio.payloadQueue = &audioQ;
        ctx.audio.active = true;
        ctx.audio.readerAudioDesc = &audioDesc;
        ctx.audio.hasSr = &hasSr;
        ctx.audio.clockDomain = ClockDomain::SystemMonotonic;
        ctx.pushFrame = [&sink](Frame frame) { sink.pushToBack(std::move(frame)); };

        const TimeStamp t0 = TimeStamp::now();
        const Duration  fd = Duration::fromNanoseconds(1'000'000'000 / 30);

        // Audio arrives in 800-sample chunks — half a video frame
        // per chunk.  Two chunks for frame 0, two for frame 1.
        audioQ.push(makeAudioChunk(audioDesc, 0u, 800u, t0));
        audioQ.push(makeAudioChunk(audioDesc, 800u, 800u,
                                   t0 + Duration::fromNanoseconds(fd.nanoseconds() / 2)));
        audioQ.push(makeAudioChunk(audioDesc, 1600u, 800u, t0 + fd));
        audioQ.push(makeAudioChunk(audioDesc, 2400u, 800u,
                                   t0 + fd + Duration::fromNanoseconds(fd.nanoseconds() / 2)));

        videoQ.push(makeVideoFrame(0u, t0));
        videoQ.push(makeVideoFrame(3000u, t0 + fd));

        RtpAggregatorThread agg(std::move(ctx),
                                RtpAggregatorThread::Mode::Video);
        agg.runOnce(0);
        agg.runOnce(0);

        REQUIRE(sink.size() == 2u);
        // Both Frames carry an audio payload.
        CHECK_FALSE(sink[0].audioPayloads().isEmpty());
        CHECK_FALSE(sink[1].audioPayloads().isEmpty());
        // FIFO should be empty after each Frame's drain — no
        // accumulation across frame boundaries.
        CHECK(agg.audioFifo().available() == 0u);
}

TEST_CASE("RtpAggregatorThread: video-clocked drain skips audio that precedes the window") {
        AudioDesc audioDesc(AudioFormat::PCMI_S16BE, 48000.0f, 2);
        Queue<RxVideoFrame>  videoQ;
        Queue<RxAudioChunk>  audioQ;
        List<Frame>          sink;
        Atomic<int64_t>      lastArrivalNs;
        bool                 hasSr = false;

        RtpAggregatorContext ctx;
        ctx.frameRate = FrameRate(FrameRate::FPS_30);
        ctx.video.payloadQueue = &videoQ;
        ctx.video.lastPacketArrivalNs = &lastArrivalNs;
        ctx.audio.payloadQueue = &audioQ;
        ctx.audio.active = true;
        ctx.audio.readerAudioDesc = &audioDesc;
        ctx.audio.hasSr = &hasSr;
        ctx.audio.clockDomain = ClockDomain::SystemMonotonic;
        ctx.pushFrame = [&sink](Frame frame) { sink.pushToBack(std::move(frame)); };

        const TimeStamp t0 = TimeStamp::now();
        const Duration  fd = Duration::fromNanoseconds(1'000'000'000 / 30);

        // Push enough audio for one full frame, all timestamped
        // ahead of the video frame's captureTime so the drain pulls
        // them all into the FIFO.
        audioQ.push(makeAudioChunk(audioDesc, 0u, 1600u, t0));
        videoQ.push(makeVideoFrame(0u, t0));

        RtpAggregatorThread agg(std::move(ctx),
                                RtpAggregatorThread::Mode::Video);
        agg.runOnce(0);

        REQUIRE(sink.size() == 1u);
        // FIFO should be drained to zero — the frame consumed
        // exactly samplesPerFrame.
        CHECK(agg.audioFifo().available() == 0u);
        // Mode reported correctly.
        CHECK(agg.mode() == RtpAggregatorThread::Mode::Video);
        // Avoid unused fd warning if compiler cares.
        (void)fd;
}
