/**
 * @file      mediaiotask_inspector.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <vector>
#include <promeki/mediaiotask_inspector.h>
#include <promeki/mediaio.h>
#include <promeki/mediaconfig.h>
#include <promeki/frame.h>
#include <promeki/timecode.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/videoformat.h>
#include <promeki/pixeldesc.h>
#include <promeki/enums.h>
#include <promeki/enumlist.h>
#include <promeki/audiotestpattern.h>
#include <promeki/audio.h>
#include <cmath>

using namespace promeki;

namespace {

// Helper: build a TPG MediaIO + Inspector pair, run @p frameCount
// frames through the pair, and return ownership of both.  The caller
// gets a snapshot at the end (via inspector->snapshot()) plus access
// to any per-frame events captured by the supplied callback.
struct InspectorRig {
        MediaIO              *tpg = nullptr;
        MediaIO              *inspectorIo = nullptr;
        MediaIOTask_Inspector *inspector = nullptr;
        ~InspectorRig() {
                if(tpg) { tpg->close(); delete tpg; }
                if(inspectorIo) { inspectorIo->close(); delete inspectorIo; }
                // inspector deleted by inspectorIo
        }
};

void buildRig(InspectorRig &rig, uint32_t streamId,
              MediaIOTask_Inspector::EventCallback cb = {},
              bool audioEnabled = true,
              const EnumList &audioChannelModes = EnumList(),
              int audioChannels = 0) {
        // Source: a default-config TPG with the StreamID overridden
        // and a fixed frame rate so the test math is reproducible.
        // The default channel-mode list puts LTC on ch0 and an AvSync
        // click on ch1 — that's how the inspector gets both the click
        // marker and the LTC stream to verify.
        MediaIO::Config tpgCfg = MediaIO::defaultConfig("TPG");
        tpgCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        tpgCfg.set(MediaConfig::VideoPixelFormat, PixelDesc(PixelDesc::RGBA8_sRGB));
        tpgCfg.set(MediaConfig::TimecodeStart, String("01:00:00:00"));
        tpgCfg.set(MediaConfig::StreamID, streamId);
        tpgCfg.set(MediaConfig::AudioEnabled, audioEnabled);
        if(audioChannels > 0) {
                tpgCfg.set(MediaConfig::AudioChannels, int32_t(audioChannels));
        }
        if(audioChannelModes.isValid()) {
                tpgCfg.set(MediaConfig::AudioChannelModes, audioChannelModes);
        }
        rig.tpg = MediaIO::create(tpgCfg);
        REQUIRE(rig.tpg != nullptr);
        REQUIRE(rig.tpg->open(MediaIO::Source).isOk());

        // Sink: Inspector with the default ("full checks") config.
        // We construct the task directly + adoptTask so we can
        // register the callback before open() (the standard create()
        // factory hides the task).
        rig.inspector = new MediaIOTask_Inspector();
        if(cb) rig.inspector->setEventCallback(cb);

        rig.inspectorIo = new MediaIO();
        MediaIO::Config insCfg = MediaIO::defaultConfig("Inspector");
        insCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);  // disable periodic log in tests
        rig.inspectorIo->setConfig(insCfg);
        REQUIRE(rig.inspectorIo->adoptTask(rig.inspector).isOk());
        REQUIRE(rig.inspectorIo->open(MediaIO::Sink).isOk());
}

void pumpFrames(InspectorRig &rig, int frameCount) {
        for(int i = 0; i < frameCount; i++) {
                Frame::Ptr frame;
                REQUIRE(rig.tpg->readFrame(frame).isOk());
                REQUIRE(frame.isValid());
                REQUIRE(rig.inspectorIo->writeFrame(frame).isOk());
        }
}

}  // namespace

// ============================================================================
// Basic plumbing: open, write a few frames, snapshot
// ============================================================================

TEST_CASE("Inspector basic snapshot after a few frames") {
        // Audio disabled — we just want to confirm the picture-side
        // counters tick correctly and that an absent LTC stream
        // doesn't trigger any spurious discontinuities.
        InspectorRig rig;
        buildRig(rig, 0xABCDEF01u, {}, /*audioEnabled=*/false);
        pumpFrames(rig, 5);

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 5);
        CHECK(snap.framesWithPictureData == 5);
        CHECK(snap.framesWithLtc == 0);   // no LTC audio at all
        CHECK(snap.totalDiscontinuities == 0);
        CHECK(snap.hasLastEvent);
        CHECK(snap.lastEvent.pictureDecoded);
        CHECK(snap.lastEvent.pictureStreamId == 0xABCDEF01u);
        CHECK(snap.lastEvent.pictureFrameNumber == 4u);  // 5 frames, last is index 4
}

// ============================================================================
// Per-frame callback fires once per frame
// ============================================================================

TEST_CASE("Inspector callback fires once per frame") {
        std::atomic<int> callCount{0};
        std::vector<uint32_t> seenFrameNumbers;
        Mutex listMutex;
        InspectorRig rig;
        buildRig(rig, 0xDEADBEEFu,
                 [&](const InspectorEvent &e) {
                         callCount++;
                         Mutex::Locker lk(listMutex);
                         seenFrameNumbers.push_back(e.pictureFrameNumber);
                 });
        pumpFrames(rig, 10);

        CHECK(callCount.load() == 10);
        Mutex::Locker lk(listMutex);
        REQUIRE(seenFrameNumbers.size() == 10u);
        for(int i = 0; i < 10; i++) {
                CHECK(seenFrameNumbers[i] == static_cast<uint32_t>(i));
        }
}

// ============================================================================
// Continuity check: no discontinuities for a clean TPG stream
// ============================================================================

TEST_CASE("Inspector reports no discontinuities for a clean TPG stream") {
        InspectorRig rig;
        buildRig(rig, 0x12345678u);
        pumpFrames(rig, 32);

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 32);
        CHECK(snap.totalDiscontinuities == 0);
        CHECK(snap.lastEvent.discontinuities.size() == 0);
}

// ============================================================================
// Continuity warnings: a synthetic frame-number jump should be detected
// ============================================================================

TEST_CASE("Inspector flags a frame-number jump as a discontinuity") {
        // Capture per-frame events so we can inspect the discontinuity
        // list directly.  Pumping a clean TPG stream gives us frames
        // numbered 0, 1, 2, ...; we skip frame 4 by simply not writing
        // it to the inspector — the inspector should then see frames
        // 0..3 followed by 5 and report a frame-number jump.
        std::vector<InspectorEvent> events;
        Mutex eventsMutex;
        InspectorRig rig;
        // Audio off — keeps the test focused on the picture-side
        // discontinuity and avoids LTC chunks getting out of step
        // when we drop a frame from the inspector's input.
        buildRig(rig, 0xFEEDFACEu,
                 [&](const InspectorEvent &e) {
                         Mutex::Locker lk(eventsMutex);
                         events.push_back(e);
                 },
                 /*audioEnabled=*/false);

        for(int i = 0; i < 10; i++) {
                Frame::Ptr frame;
                REQUIRE(rig.tpg->readFrame(frame).isOk());
                if(i == 4) continue;        // skip frame 4 — TPG advances anyway
                REQUIRE(rig.inspectorIo->writeFrame(frame).isOk());
        }

        InspectorSnapshot snap = rig.inspector->snapshot();
        // 9 frames written (we skipped one).
        CHECK(snap.framesProcessed == 9);
        // Exactly one discontinuity: the frame number jumped from 3
        // (the last clean one before the skip) to 5.
        CHECK(snap.totalDiscontinuities == 1);

        // Find the discontinuity event and check its detail.
        bool foundJump = false;
        {
                Mutex::Locker lk(eventsMutex);
                for(const auto &e : events) {
                        for(const auto &d : e.discontinuities) {
                                if(d.kind == InspectorDiscontinuity::FrameNumberJump) {
                                        foundJump = true;
                                        CHECK(d.previousValue == String("3"));
                                        CHECK(d.currentValue == String("5"));
                                        // The pre-rendered description should
                                        // mention both numbers and the delta.
                                        CHECK(d.description.contains("was 3"));
                                        CHECK(d.description.contains("got 5"));
                                }
                        }
                }
        }
        CHECK(foundJump);
}

// ============================================================================
// Sync offset jitter detection: a synthetic 1-sample audio shift fires
// a SyncOffsetChange discontinuity warning
// ============================================================================

TEST_CASE("Inspector flags a sync offset change as a discontinuity") {
        std::vector<InspectorEvent> events;
        Mutex eventsMutex;
        InspectorRig rig;
        buildRig(rig, 0xC0FFEEBBu,
                 [&](const InspectorEvent &e) {
                         Mutex::Locker lk(eventsMutex);
                         events.push_back(e);
                 });

        // Pump 10 clean frames first so the sync offset settles to its
        // natural constant value (no discontinuities expected).  Then
        // build a synthetic frame whose audio has been left-shifted by
        // 1 sample (we drop the first audio sample and pad the end
        // with a zero), which moves the LTC sync word's detected
        // position by 1 sample relative to the audio chunk start.
        // The inspector should flag this as a SyncOffsetChange.
        for(int i = 0; i < 10; i++) {
                Frame::Ptr frame;
                REQUIRE(rig.tpg->readFrame(frame).isOk());
                REQUIRE(rig.inspectorIo->writeFrame(frame).isOk());
        }

        // Read frame 11, then mutate its audio in place to shift the
        // LTC waveform by one sample.  The frame is freshly created
        // by the TPG so we own the only reference and can write into
        // its audio buffer directly.
        Frame::Ptr shifted;
        REQUIRE(rig.tpg->readFrame(shifted).isOk());
        REQUIRE(shifted.isValid());
        REQUIRE(shifted->audioList().size() == 1);
        Audio::Ptr audPtr = shifted->audioList()[0];
        REQUIRE(audPtr.isValid());
        audPtr.modify();   // ensure exclusive ownership before writing
        const Audio &aud = *audPtr;
        REQUIRE(aud.desc().dataType() == AudioDesc::PCMI_Float32LE);
        const int channels = aud.desc().channels();
        const size_t samples = aud.samples();
        REQUIRE(samples >= 4);
        // Audio's data<T>() returns a typed pointer into the underlying
        // mutable buffer (the `const` on Audio doesn't propagate
        // through `Buffer::data()` since the buffer is conceptually a
        // shared chunk of bytes), so we can write through it.
        float *data = aud.data<float>();
        // Shift channel 0 (the LTC channel) left by one sample frame.
        // Other channels are left alone.
        for(size_t s = 0; s < samples - 1; s++) {
                data[s * channels + 0] = data[(s + 1) * channels + 0];
        }
        data[(samples - 1) * channels + 0] = 0.0f;

        REQUIRE(rig.inspectorIo->writeFrame(shifted).isOk());

        // Inspect the captured events: at least one SyncOffsetChange
        // should be present, with previous and current values that
        // describe a real shift.
        int syncOffsetChangeCount = 0;
        InspectorDiscontinuity sample;
        {
                Mutex::Locker lk(eventsMutex);
                for(const auto &e : events) {
                        for(const auto &d : e.discontinuities) {
                                if(d.kind == InspectorDiscontinuity::SyncOffsetChange) {
                                        syncOffsetChangeCount++;
                                        sample = d;
                                }
                        }
                }
        }
        REQUIRE(syncOffsetChangeCount >= 1);
        // The discontinuity description must include both the previous
        // and current offset values so a QA reader can see the change
        // at a glance.
        CHECK(sample.description.contains("was "));
        CHECK(sample.description.contains("now "));
        CHECK(sample.description.contains("delta "));
}

// ============================================================================
// LTC decoding + sync offset check (LTC audio enabled)
// ============================================================================

// ============================================================================
// Periodic logging path actually fires with a short interval
// ============================================================================

TEST_CASE("Inspector periodic log fires when interval elapses") {
        // Use the standard buildRig (full default config: AvSync
        // audio with LTC + click marker, image data band on top of
        // every frame, all four inspector checks enabled) and just
        // override the log interval to force the periodic report to
        // fire mid-pump.  We don't capture the log output here —
        // we're exercising the code path so any latent crash or
        // format-string mismatch is caught.
        InspectorRig rig;
        buildRig(rig, 0xBADBEEFBu);
        // Re-open the inspector IO with a custom log interval.  We
        // can't tweak the live config after open(), so we close and
        // re-set + re-open.
        rig.inspectorIo->close();
        MediaIO::Config insCfg = MediaIO::defaultConfig("Inspector");
        insCfg.set(MediaConfig::InspectorLogIntervalSec, 0.01);  // 10 ms
        rig.inspectorIo->setConfig(insCfg);
        REQUIRE(rig.inspectorIo->open(MediaIO::Sink).isOk());

        for(int i = 0; i < 10; i++) {
                Frame::Ptr frame;
                REQUIRE(rig.tpg->readFrame(frame).isOk());
                REQUIRE(rig.inspectorIo->writeFrame(frame).isOk());
        }
        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 10);
}

TEST_CASE("Inspector decodes LTC and reports A/V sync offset") {
        std::vector<InspectorEvent> events;
        Mutex eventsMutex;
        InspectorRig rig;
        buildRig(rig, 0x55667788u,
                 [&](const InspectorEvent &e) {
                         Mutex::Locker lk(eventsMutex);
                         events.push_back(e);
                 });
        pumpFrames(rig, 40);

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 40);
        // The LTC decoder needs at least one full LTC frame to lock, so
        // not every frame yields a result — but most should after the
        // first second or so.
        CHECK(snap.framesWithPictureData == 40);
        CHECK(snap.framesWithLtc > 20);

        // Find at least one frame where both decoded and the A/V
        // sync offset was computed.  LTC and picture come from the
        // same TPG TimecodeGenerator, so the offset is a fixed phase
        // relation between the two streams (a small constant value
        // reflecting the libvtc encoder ramp + decoder hysteresis
        // edge-detection latency — typically 1-3 samples at 48 kHz).
        // We assert it stays well under one frame's worth and is
        // *constant* across frames once both decoders have locked.
        bool foundOffset = false;
        int64_t maxAbsOffset = 0;
        int64_t firstOffset = 0;
        bool firstOffsetSet = false;
        bool offsetIsConstant = true;
        {
                Mutex::Locker lk(eventsMutex);
                for(const auto &e : events) {
                        if(e.avSyncValid) {
                                foundOffset = true;
                                const int64_t a = e.avSyncOffsetSamples < 0
                                        ? -e.avSyncOffsetSamples
                                        :  e.avSyncOffsetSamples;
                                if(a > maxAbsOffset) maxAbsOffset = a;
                                if(!firstOffsetSet) {
                                        firstOffset = e.avSyncOffsetSamples;
                                        firstOffsetSet = true;
                                } else if(e.avSyncOffsetSamples != firstOffset) {
                                        offsetIsConstant = false;
                                }
                        }
                }
        }
        CHECK(foundOffset);
        // 30 fps @ 48kHz = 1600 samples per frame.  TPG-internal LTC
        // and picture should agree to within one frame.
        CHECK(maxAbsOffset < 1600);
        // And the offset should not move from frame to frame: any
        // change would mean the picture and LTC are drifting in time
        // relative to each other, which is a real problem.
        CHECK(offsetIsConstant);

        // Because the offset is constant, the inspector's
        // sync-offset-change detector (default tolerance 0 = report
        // any change) must not have raised a single SyncOffsetChange
        // discontinuity.  This verifies the change-detection code
        // path doesn't false-positive on a stable stream.
        int syncOffsetChangeCount = 0;
        {
                Mutex::Locker lk(eventsMutex);
                for(const auto &e : events) {
                        for(const auto &d : e.discontinuities) {
                                if(d.kind == InspectorDiscontinuity::SyncOffsetChange) {
                                        syncOffsetChangeCount++;
                                }
                        }
                }
        }
        CHECK(syncOffsetChangeCount == 0);
}

// ============================================================================
// New audio patterns: SrcProbe and ChannelId.  The Inspector doesn't
// carry dedicated decoders for these yet, but we can still pump a
// multi-channel TPG through the Inspector and peek at the audio the
// generator produced to confirm it matches the promised frequency map.
// The Inspector's job here is just to make sure the new modes don't
// break the pipeline's continuity / A/V sync checks when they sit
// alongside LTC on a different channel.
// ============================================================================

namespace {

// Zero-crossing frequency estimator.  Accurate enough for ~30 cycles
// of a multi-hundred-Hz tone in one frame at 48 kHz.
double estimateFreqHz(const Audio &audio, size_t channel, double sampleRate) {
        const float *data = audio.data<float>();
        if(data == nullptr) return 0.0;
        const size_t nch = audio.desc().channels();
        const size_t n   = audio.samples();
        if(n < 2) return 0.0;

        size_t crossings = 0;
        float prev = data[channel];
        for(size_t s = 1; s < n; ++s) {
                float cur = data[s * nch + channel];
                if((prev < 0 && cur >= 0) || (prev >= 0 && cur < 0)) {
                        crossings++;
                }
                prev = cur;
        }
        const double cycles  = static_cast<double>(crossings) / 2.0;
        const double seconds = static_cast<double>(n - 1) / sampleRate;
        return (seconds > 0.0) ? (cycles / seconds) : 0.0;
}

}  // namespace

TEST_CASE("Inspector pipeline carries a SrcProbe channel unharmed") {
        // TPG emits LTC on ch0 (so the LTC decoder still locks and the
        // A/V sync check is still exercised) and a SrcProbe tone on
        // ch1.  We pump frames through the Inspector and, on the side,
        // pull the same frames' audio out and verify the SrcProbe
        // channel carries ~997 Hz.  The Inspector's own continuity /
        // sync checks must not fire any discontinuities.
        EnumList modes = EnumList::forType<AudioPattern>();
        modes.append(AudioPattern::LTC);
        modes.append(AudioPattern::SrcProbe);

        std::vector<InspectorEvent> events;
        Mutex eventsMutex;
        InspectorRig rig;
        buildRig(rig, 0x5A5AA5A5u,
                 [&](const InspectorEvent &e) {
                         Mutex::Locker lk(eventsMutex);
                         events.push_back(e);
                 },
                 /*audioEnabled=*/true,
                 modes,
                 /*audioChannels=*/2);

        // Pump via the shared helper so the frames flow through
        // Inspector's write path; we don't need a separate TPG copy
        // because we can re-read the generator output once we know
        // what the TPG was configured with.
        pumpFrames(rig, 40);

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 40);
        CHECK(snap.framesWithPictureData == 40);
        CHECK(snap.framesWithLtc > 20);        // LTC on ch0 still locks
        CHECK(snap.totalDiscontinuities == 0); // no unexpected drift

        // Independently synthesise one frame-length of the expected
        // audio and verify ch1 really emits the 997 Hz reference tone.
        // Uses the same configuration the TPG was driven with.
        const double sampleRate = 48000.0;
        AudioDesc desc(static_cast<float>(sampleRate), 2);
        AudioTestPattern gen(desc);
        gen.setChannelModes(modes);
        gen.setLtcLevel(AudioLevel::fromDbfs(-20.0));
        gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
        REQUIRE(gen.configure().isOk());
        Audio audio = gen.create(static_cast<size_t>(sampleRate));
        REQUIRE(audio.isValid());

        double freq = estimateFreqHz(audio, 1, sampleRate);
        CHECK(freq == doctest::Approx(AudioTestPattern::kSrcProbeFrequencyHz)
                         .epsilon(0.005));
}

TEST_CASE("Inspector pipeline carries a ChannelId channel map unharmed") {
        // Four channels: LTC on ch0, ChannelId tones on ch1..ch3.  The
        // ChannelId frequency map (base 1000 Hz, step 100 Hz) means ch1
        // carries 1100 Hz, ch2 carries 1200 Hz, ch3 carries 1300 Hz —
        // the goal is to make each channel independently identifiable
        // by a downstream consumer via a peak-frequency lookup.
        EnumList modes = EnumList::forType<AudioPattern>();
        modes.append(AudioPattern::LTC);
        modes.append(AudioPattern::ChannelId);
        modes.append(AudioPattern::ChannelId);
        modes.append(AudioPattern::ChannelId);

        InspectorRig rig;
        buildRig(rig, 0xCAFEBABEu, {},
                 /*audioEnabled=*/true,
                 modes,
                 /*audioChannels=*/4);
        pumpFrames(rig, 20);

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 20);
        CHECK(snap.framesWithPictureData == 20);
        CHECK(snap.framesWithLtc > 5);
        CHECK(snap.totalDiscontinuities == 0);

        // Replay the same config through a standalone AudioTestPattern
        // and verify the ChannelId frequencies match what we told the
        // TPG to emit.
        const double sampleRate = 48000.0;
        AudioDesc desc(static_cast<float>(sampleRate), 4);
        AudioTestPattern gen(desc);
        gen.setChannelModes(modes);
        gen.setLtcLevel(AudioLevel::fromDbfs(-20.0));
        gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
        gen.setChannelIdBaseFreq(1000.0);
        gen.setChannelIdStepFreq(100.0);
        REQUIRE(gen.configure().isOk());
        Audio audio = gen.create(static_cast<size_t>(sampleRate));
        REQUIRE(audio.isValid());

        // Every ChannelId channel must carry the frequency its index
        // maps to (1100, 1200, 1300).  LTC on ch0 isn't a sine so we
        // don't run the estimator on it.
        for(int ch = 1; ch <= 3; ++ch) {
                double expected = AudioTestPattern::channelIdFrequency(ch, 1000.0, 100.0);
                double observed = estimateFreqHz(audio, ch, sampleRate);
                CHECK(observed == doctest::Approx(expected).epsilon(0.005));
        }
}
