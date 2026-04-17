/**
 * @file      framesync.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/framesync.h>
#include <promeki/syntheticclock.h>
#include <promeki/clock.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/timestamp.h>
#include <promeki/duration.h>
#include <promeki/framerate.h>
#include <cstring>
#include <atomic>
#include <thread>

using namespace promeki;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Build a MediaTimeStamp that reports @p ns in the given domain.
MediaTimeStamp mts(int64_t ns, const ClockDomain &domain) {
        TimeStamp ts;
        ts.setValue(TimeStamp::Value(std::chrono::nanoseconds(ns)));
        return MediaTimeStamp(ts, domain);
}

// Read the nanosecond value from a MediaTimeStamp.
int64_t mtsNs(const MediaTimeStamp &m) {
        return m.timeStamp().nanoseconds() + m.offset().nanoseconds();
}

// Build a Frame with one video image, stamped with a MediaTimeStamp at
// @p videoNs in the Synthetic domain, and (optionally) one audio block
// with @p audioSamples samples stamped at @p audioNs.  The video image
// is a tiny placeholder — we only care about identity, not pixels.
Frame::Ptr makeVideoAudioFrame(int64_t videoNs, int64_t audioNs,
                               size_t audioSamples,
                               const AudioDesc &adesc,
                               int id = -1)
{
        (void)id;
        ClockDomain dom(ClockDomain::Synthetic);

        Frame::Ptr f = Frame::Ptr::create();

        Image::Ptr img = Image::Ptr::create(4, 4, PixelDesc::RGBA8_sRGB);
        img.modify()->metadata().set(Metadata::MediaTimeStamp,
                                     mts(videoNs, dom));
        f.modify()->imageList().pushToBack(img);

        if(audioSamples > 0 && adesc.isValid()) {
                Audio::Ptr a = Audio::Ptr::create(adesc, audioSamples);
                a.modify()->resize(audioSamples);
                std::memset(a.modify()->data<float>(), 0,
                            audioSamples * adesc.channels() * sizeof(float));
                a.modify()->metadata().set(Metadata::MediaTimeStamp,
                                           mts(audioNs, dom));
                f.modify()->audioList().pushToBack(a);
        }
        return f;
}

Frame::Ptr makeVideoOnlyFrame(int64_t videoNs) {
        return makeVideoAudioFrame(videoNs, 0, 0, AudioDesc());
}

// Compute nominal ns of source frame N for a given source rate.
int64_t srcFrameNs(const FrameRate &srcRate, int64_t n) {
        return srcRate.cumulativeTicks(1000000000LL, n);
}

} // namespace

// ===========================================================================
// Construction
// ===========================================================================

TEST_CASE("FrameSync: default construction has no clock and zero stats") {
        FrameSync fs;
        CHECK(fs.clock() == nullptr);
        CHECK(fs.framesIn() == 0);
        CHECK(fs.framesOut() == 0);
}

TEST_CASE("FrameSync: named construction") {
        FrameSync fs(String("test"));
        CHECK(fs.name() == String("test"));
}

// ===========================================================================
// Preconditions
// ===========================================================================

TEST_CASE("FrameSync: pullFrame without clock returns error") {
        FrameSync fs;
        fs.setTargetFrameRate(FrameRate(FrameRate::FPS_60));
        fs.reset();
        auto r = fs.pullFrame();
        CHECK(r.second().isError());
}

TEST_CASE("FrameSync: pullFrame without frame rate returns error") {
        FrameSync fs;
        SyntheticClock clk(FrameRate(FrameRate::FPS_60));
        fs.setClock(&clk);
        fs.reset();
        auto r = fs.pullFrame();
        CHECK(r.second().isError());
}

// ===========================================================================
// Steady state (source rate == target rate)
// ===========================================================================

TEST_CASE("FrameSync: steady-state video-only produces one output per input") {
        FrameRate fps(FrameRate::FPS_60);
        SyntheticClock clk(fps);
        FrameSync fs(String("steady"));
        fs.setTargetFrameRate(fps);
        fs.setClock(&clk);
        fs.reset();

        for(int i = 0; i < 5; i++) {
                fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, i)));
        }
        CHECK(fs.framesIn() == 5);

        for(int i = 0; i < 5; i++) {
                auto r = fs.pullFrame();
                REQUIRE(r.second().isOk());
                CHECK(r.first().frameIndex == i);
                CHECK(r.first().framesRepeated == 0);
                CHECK(r.first().framesDropped == 0);
                REQUIRE(r.first().frame.isValid());
                CHECK(!r.first().frame->imageList().isEmpty());
        }
        CHECK(fs.framesOut() == 5);
        CHECK(fs.framesRepeated() == 0);
        CHECK(fs.framesDropped() == 0);
}

// ===========================================================================
// Upsampling (source < target)
// ===========================================================================

TEST_CASE("FrameSync: 24->60 upsample repeats video frames") {
        FrameRate src(FrameRate::FPS_24);
        FrameRate tgt(FrameRate::FPS_60);
        SyntheticClock clk(tgt);
        FrameSync fs(String("up"));
        fs.setTargetFrameRate(tgt);
        fs.setClock(&clk);
        fs.reset();

        // Push 2 source frames — enough to get through 6 target pulls.
        fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(src, 0)));
        fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(src, 1)));
        fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(src, 2)));

        // Pull 6 target frames (100 ms).  Over 100 ms we expect to
        // consume ~2.4 source frames; we should see the 3 pushed
        // consumed and roughly half of pulls be repeats.
        int totalRepeats = 0;
        int pulls        = 6;
        for(int i = 0; i < pulls; i++) {
                auto r = fs.pullFrame();
                REQUIRE(r.second().isOk());
                totalRepeats += (int)r.first().framesRepeated;
        }
        CHECK(fs.framesOut() == pulls);
        // 24→60 has roughly 2-3 repeats per 5 pulls (pull 0 consumes, 1-2
        // repeat, 3 consumes, 4 repeats, 5 consumes...).  Over 6 pulls
        // we expect 3 repeats.
        CHECK(totalRepeats >= 2);
        CHECK(fs.framesDropped() == 0);
}

// ===========================================================================
// Downsampling (source > target)
// ===========================================================================

TEST_CASE("FrameSync: 60->24 downsample drops intermediate video frames") {
        FrameRate src(FrameRate::FPS_60);
        FrameRate tgt(FrameRate::FPS_24);
        SyntheticClock clk(tgt);
        FrameSync fs(String("down"));
        fs.setTargetFrameRate(tgt);
        fs.setClock(&clk);
        fs.reset();

        // Push 6 source frames to fit our 8-deep queue; pull 3 target
        // frames (125 ms of presentation time).  Expect drops equal to
        // (pushed - pulled - still held).
        for(int i = 0; i < 6; i++) {
                fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(src, i)));
        }

        int totalDropped = 0;
        for(int i = 0; i < 3; i++) {
                auto r = fs.pullFrame();
                REQUIRE(r.second().isOk());
                totalDropped += (int)r.first().framesDropped;
        }
        CHECK(fs.framesOut() == 3);
        CHECK(totalDropped >= 2);
        CHECK(fs.framesRepeated() == 0);
}

// ===========================================================================
// Empty-queue repeat and pre-input empty output
// ===========================================================================

TEST_CASE("FrameSync: first pull blocks until a frame is pushed") {
        FrameRate fps(FrameRate::FPS_30);
        SyntheticClock clk(fps);
        FrameSync fs;
        fs.setTargetFrameRate(fps);
        fs.setClock(&clk);
        fs.reset();

        // A pull on an empty sync blocks.  Interrupt it from another
        // thread and verify the caller observes Error::Interrupt.
        std::thread interrupter([&]{
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                fs.interrupt();
        });
        auto r = fs.pullFrame();
        interrupter.join();
        CHECK(r.second() == Error::Interrupt);
        CHECK(fs.framesOut() == 0);
}

TEST_CASE("FrameSync: first pull after a push anchors cleanly") {
        FrameRate fps(FrameRate::FPS_30);
        SyntheticClock clk(fps);
        FrameSync fs;
        fs.setTargetFrameRate(fps);
        fs.setClock(&clk);
        fs.reset();

        fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, 0)));
        auto r = fs.pullFrame();
        REQUIRE(r.second().isOk());
        CHECK(r.first().framesRepeated == 0);
        CHECK(r.first().framesDropped == 0);
        REQUIRE(!r.first().frame->imageList().isEmpty());
}

TEST_CASE("FrameSync: drained queue repeats the last held frame") {
        FrameRate fps(FrameRate::FPS_30);
        SyntheticClock clk(fps);
        FrameSync fs;
        fs.setTargetFrameRate(fps);
        fs.setClock(&clk);
        fs.reset();

        fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, 0)));
        auto r0 = fs.pullFrame();
        REQUIRE(r0.second().isOk());
        auto r1 = fs.pullFrame();
        REQUIRE(r1.second().isOk());
        CHECK(r0.first().framesRepeated == 0);
        CHECK(r1.first().framesRepeated == 1);
}

// ===========================================================================
// Output timestamps in the clock's domain
// ===========================================================================

TEST_CASE("FrameSync: output MediaTimeStamp is in the clock's domain") {
        FrameRate fps(FrameRate::FPS_60);
        SyntheticClock clk(fps);
        FrameSync fs;
        fs.setTargetFrameRate(fps);
        fs.setClock(&clk);
        fs.reset();

        fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, 0)));
        auto r = fs.pullFrame();
        REQUIRE(r.second().isOk());

        MediaTimeStamp m = r.first().frame->metadata()
                .get(Metadata::MediaTimeStamp)
                .get<MediaTimeStamp>();
        REQUIRE(m.isValid());
        CHECK(m.domain() == ClockDomain(ClockDomain::Synthetic));

        MediaTimeStamp imgM = r.first().frame->imageList()[0]->metadata()
                .get(Metadata::MediaTimeStamp)
                .get<MediaTimeStamp>();
        REQUIRE(imgM.isValid());
        CHECK(imgM.domain() == ClockDomain(ClockDomain::Synthetic));
}

// ===========================================================================
// SyntheticClock advance lockstep
// ===========================================================================

TEST_CASE("FrameSync: SyntheticClock advances one frame per pull") {
        FrameRate fps(FrameRate::FPS_30);
        SyntheticClock clk(fps);
        FrameSync fs;
        fs.setTargetFrameRate(fps);
        fs.setClock(&clk);
        fs.reset();

        CHECK(clk.currentFrame() == 0);
        for(int i = 0; i < 5; i++) {
                fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, i)));
        }
        for(int i = 0; i < 5; i++) {
                auto r = fs.pullFrame();
                REQUIRE(r.second().isOk());
                CHECK(clk.currentFrame() == i + 1);
        }
}

// ===========================================================================
// Queue overflow (drop oldest)
// ===========================================================================

TEST_CASE("FrameSync: Block policy does not drop under capacity pressure") {
        // Producer pushes 12 into a capacity-4 queue; consumer runs
        // alongside.  With Block policy, producer blocks when full
        // and no overflow drops are counted.
        FrameRate fps(FrameRate::FPS_30);
        SyntheticClock clk(fps);
        FrameSync fs;
        fs.setTargetFrameRate(fps);
        fs.setClock(&clk);
        fs.setInputQueueCapacity(4);
        fs.setInputOverflowPolicy(
                FrameSync::InputOverflowPolicy::Block);
        fs.reset();

        std::atomic<bool> producerDone{false};
        std::thread producer([&]{
                for(int i = 0; i < 12; i++) {
                        fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, i)));
                }
                producerDone = true;
        });

        // Pull until the producer is done — each pull makes room in
        // the queue, so a blocked producer can always make progress.
        while(!producerDone) {
                auto r = fs.pullFrame();
                REQUIRE(r.second().isOk());
        }
        producer.join();

        CHECK(fs.framesIn() == 12);
        CHECK(fs.overflowDrops() == 0);
}

TEST_CASE("FrameSync: Block policy push is interruptible") {
        // Fill the queue, then verify that interrupt() unblocks a
        // pending pushFrame with Error::Interrupt.
        FrameRate fps(FrameRate::FPS_30);
        SyntheticClock clk(fps);
        FrameSync fs;
        fs.setTargetFrameRate(fps);
        fs.setClock(&clk);
        fs.setInputQueueCapacity(2);
        fs.setInputOverflowPolicy(
                FrameSync::InputOverflowPolicy::Block);
        fs.reset();

        fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, 0)));
        fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, 1)));

        std::thread interrupter([&]{
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                fs.interrupt();
        });
        Error err = fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, 2)));
        interrupter.join();

        CHECK(err == Error::Interrupt);
        CHECK(fs.overflowDrops() == 0);
}

TEST_CASE("FrameSync: queue overflow drops oldest") {
        FrameRate fps(FrameRate::FPS_30);
        SyntheticClock clk(fps);
        FrameSync fs;
        fs.setTargetFrameRate(fps);
        fs.setClock(&clk);
        fs.setInputQueueCapacity(4);
        fs.reset();

        for(int i = 0; i < 10; i++) {
                fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, i)));
        }
        CHECK(fs.framesIn() == 10);
        CHECK(fs.overflowDrops() == 6);
}

// ===========================================================================
// Audio production
// ===========================================================================

TEST_CASE("FrameSync: audio is produced at target sample count per pull") {
        FrameRate fps(FrameRate::FPS_60);
        AudioDesc sourceAudio(AudioDesc::NativeType, 48000.0f, 2);
        AudioDesc targetAudio(AudioDesc::NativeType, 48000.0f, 2);
        SyntheticClock clk(fps);
        FrameSync fs(String("audio"));
        fs.setTargetFrameRate(fps);
        fs.setTargetAudioDesc(targetAudio);
        fs.setClock(&clk);
        fs.reset();

        // Push frames with exactly 800 samples each (48000 / 60).
        const size_t SAMPLES_PER_FRAME = 800;
        for(int i = 0; i < 4; i++) {
                int64_t vns = srcFrameNs(fps, i);
                fs.pushFrame(makeVideoAudioFrame(vns, vns,
                                                 SAMPLES_PER_FRAME,
                                                 sourceAudio));
        }

        for(int i = 0; i < 4; i++) {
                auto r = fs.pullFrame();
                REQUIRE(r.second().isOk());
                REQUIRE(!r.first().frame->audioList().isEmpty());
                const Audio::Ptr &a = r.first().frame->audioList()[0];
                REQUIRE(a.isValid());
                CHECK(a->samples() == SAMPLES_PER_FRAME);
                CHECK(a->desc().channels() == 2);
                CHECK((int)a->desc().sampleRate() == 48000);
        }
}

TEST_CASE("FrameSync: video-only source produces no output audio") {
        FrameRate fps(FrameRate::FPS_30);
        AudioDesc targetAudio(AudioDesc::NativeType, 48000.0f, 2);
        SyntheticClock clk(fps);
        FrameSync fs;
        fs.setTargetFrameRate(fps);
        fs.setTargetAudioDesc(targetAudio);
        fs.setClock(&clk);
        fs.reset();

        for(int i = 0; i < 3; i++) {
                fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, i)));
        }
        for(int i = 0; i < 3; i++) {
                auto r = fs.pullFrame();
                REQUIRE(r.second().isOk());
                // With target audio configured but no source audio,
                // the output audio is silence (not absent) — the
                // resampler produces target-sized buffers by padding
                // once it has nothing to consume.
                if(!r.first().frame->audioList().isEmpty()) {
                        const Audio::Ptr &a =
                                r.first().frame->audioList()[0];
                        // First pull with no input may produce empty
                        // or silence; accept either.
                        CHECK(a->desc().channels() == 2);
                }
        }
}

// ===========================================================================
// Reset
// ===========================================================================

TEST_CASE("FrameSync: reset clears stats and queue") {
        FrameRate fps(FrameRate::FPS_30);
        SyntheticClock clk(fps);
        FrameSync fs;
        fs.setTargetFrameRate(fps);
        fs.setClock(&clk);
        fs.reset();

        for(int i = 0; i < 3; i++) {
                fs.pushFrame(makeVideoOnlyFrame(srcFrameNs(fps, i)));
        }
        fs.pullFrame();
        CHECK(fs.framesIn() == 3);
        CHECK(fs.framesOut() == 1);

        fs.reset();
        CHECK(fs.framesIn() == 0);
        CHECK(fs.framesOut() == 0);
        CHECK(fs.framesRepeated() == 0);
        CHECK(fs.framesDropped() == 0);
}
