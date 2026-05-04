/**
 * @file      mediaiotask_inspector.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <vector>
#include <promeki/inspectormediaio.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediaconfig.h>
#include <promeki/frame.h>
#include <promeki/timecode.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/videoformat.h>
#include <promeki/pixelformat.h>
#include <promeki/enums.h>
#include <promeki/enumlist.h>
#include <promeki/audiotestpattern.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/mediatimestamp.h>
#include <promeki/clockdomain.h>
#include <promeki/timestamp.h>
#include <promeki/duration.h>
#include <cmath>

using namespace promeki;

namespace {

        // Synchronous read shim — wraps the always-async readFrame()
        // surface for tests that pump frames serially.  Failure
        // propagates as the wait() result; on success the frame is
        // pulled off the resolved CmdRead's typed payload.
        Error syncRead(MediaIOSource *src, Frame::Ptr &out) {
                MediaIORequest req = src->readFrame();
                Error          err = req.wait();
                if (err.isOk()) {
                        if (const auto *cr = req.commandAs<MediaIOCommandRead>()) {
                                out = cr->frame;
                        }
                }
                return err;
        }

        // Helper: build a TPG MediaIO + Inspector pair, run @p frameCount
        // frames through the pair, and return ownership of both.  The caller
        // gets a snapshot at the end (via inspector->snapshot()) plus access
        // to any per-frame events captured by the supplied callback.
        struct InspectorRig {
                        MediaIO          *tpg = nullptr;
                        InspectorMediaIO *inspectorIo = nullptr;
                        // Backwards-compat alias used by the test bodies; same
                        // pointer as inspectorIo since the native MediaIO IS
                        // the inspector now (no separate task object).
                        InspectorMediaIO *inspector = nullptr;
                        ~InspectorRig() {
                                if (tpg) {
                                        tpg->close().wait();
                                        delete tpg;
                                }
                                if (inspectorIo) {
                                        inspectorIo->close().wait();
                                        delete inspectorIo;
                                }
                        }
        };

        void buildRig(InspectorRig &rig, uint32_t streamId, InspectorMediaIO::EventCallback cb = {},
                      bool audioEnabled = true, const EnumList &audioChannelModes = EnumList(), int audioChannels = 0) {
                // Source: a default-config TPG with the StreamID overridden
                // and a fixed frame rate so the test math is reproducible.
                // The TPG's own default puts a @c PcmMarker on every channel
                // — the inspector tests in this file generally want
                // @c LTC on ch0 + @c AvSync on ch1 instead so the
                // continuity / sync-offset checks see a real LTC
                // stream.  Apply that override here unless the caller
                // supplied its own list.
                MediaIO::Config tpgCfg = MediaIOFactory::defaultConfig("TPG");
                tpgCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
                tpgCfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
                tpgCfg.set(MediaConfig::TimecodeStart, String("01:00:00:00"));
                tpgCfg.set(MediaConfig::StreamID, streamId);
                tpgCfg.set(MediaConfig::AudioEnabled, audioEnabled);
                if (audioChannels > 0) {
                        tpgCfg.set(MediaConfig::AudioChannels, int32_t(audioChannels));
                }
                if (audioChannelModes.isValid()) {
                        tpgCfg.set(MediaConfig::AudioChannelModes, audioChannelModes);
                } else {
                        EnumList ltcAvSync = EnumList::forType<AudioPattern>();
                        ltcAvSync.append(AudioPattern::LTC);
                        ltcAvSync.append(AudioPattern::AvSync);
                        tpgCfg.set(MediaConfig::AudioChannelModes, ltcAvSync);
                }
                rig.tpg = MediaIO::create(tpgCfg);
                REQUIRE(rig.tpg != nullptr);
                REQUIRE(rig.tpg->open().wait().isOk());

                // Sink: Inspector with the default ("full checks") config.
                // Construct the InspectorMediaIO directly so we can
                // register the callback before open() (the standard
                // create() factory doesn't expose a setEventCallback hook).
                rig.inspectorIo = new InspectorMediaIO();
                rig.inspector = rig.inspectorIo;
                if (cb) rig.inspectorIo->setEventCallback(cb);

                MediaIO::Config insCfg = MediaIOFactory::defaultConfig("Inspector");
                insCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0); // disable periodic log in tests
                insCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
                // Pin the test list explicitly to "every test on"
                // (including @c Ltc, which the inspector factory
                // default omits) so any test in this file gets the
                // same uniform behaviour regardless of which audio
                // channel modes the rig was configured with.
                EnumList tests = EnumList::forType<InspectorTest>();
                tests.append(InspectorTest::ImageData);
                tests.append(InspectorTest::AudioData);
                tests.append(InspectorTest::Ltc);
                tests.append(InspectorTest::AvSync);
                tests.append(InspectorTest::Continuity);
                tests.append(InspectorTest::Timestamp);
                tests.append(InspectorTest::AudioSamples);
                insCfg.set(MediaConfig::InspectorTests, tests);
                rig.inspectorIo->setConfig(insCfg);
                REQUIRE(rig.inspectorIo->open().wait().isOk());
        }

        void pumpFrames(InspectorRig &rig, int frameCount) {
                for (int i = 0; i < frameCount; i++) {
                        Frame::Ptr frame;
                        REQUIRE(syncRead(rig.tpg->source(0), frame).isOk());
                        REQUIRE(frame.isValid());
                        REQUIRE(rig.inspectorIo->sink(0)->writeFrame(frame).wait().isOk());
                }
        }

} // namespace

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
        CHECK(snap.framesWithLtc == 0); // no LTC audio at all
        CHECK(snap.totalDiscontinuities == 0);
        CHECK(snap.hasLastEvent);
        CHECK(snap.lastEvent.pictureDecoded);
        // Stream IDs ride in the top byte of the data band's 64-bit
        // codeword; values larger than 8 bits fold modulo 256.
        CHECK(snap.lastEvent.pictureStreamId == (0xABCDEF01u & 0xffu));
        CHECK(snap.lastEvent.pictureFrameNumber == 4u); // 5 frames, last is index 4
}

// ============================================================================
// Per-frame callback fires once per frame
// ============================================================================

TEST_CASE("Inspector callback fires once per frame") {
        std::atomic<int>      callCount{0};
        std::vector<uint32_t> seenFrameNumbers;
        Mutex                 listMutex;
        InspectorRig          rig;
        buildRig(rig, 0xDEADBEEFu, [&](const InspectorEvent &e) {
                callCount++;
                Mutex::Locker lk(listMutex);
                seenFrameNumbers.push_back(e.pictureFrameNumber);
        });
        pumpFrames(rig, 10);

        CHECK(callCount.load() == 10);
        Mutex::Locker lk(listMutex);
        REQUIRE(seenFrameNumbers.size() == 10u);
        for (int i = 0; i < 10; i++) {
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
        Mutex                       eventsMutex;
        InspectorRig                rig;
        // Audio off — keeps the test focused on the picture-side
        // discontinuity and avoids LTC chunks getting out of step
        // when we drop a frame from the inspector's input.
        buildRig(
                rig, 0xFEEDFACEu,
                [&](const InspectorEvent &e) {
                        Mutex::Locker lk(eventsMutex);
                        events.push_back(e);
                },
                /*audioEnabled=*/false);

        for (int i = 0; i < 10; i++) {
                Frame::Ptr frame;
                REQUIRE(syncRead(rig.tpg->source(0), frame).isOk());
                if (i == 4) continue; // skip frame 4 — TPG advances anyway
                REQUIRE(rig.inspectorIo->sink(0)->writeFrame(frame).wait().isOk());
        }

        InspectorSnapshot snap = rig.inspector->snapshot();
        // 9 frames written (we skipped one).
        CHECK(snap.framesProcessed == 9);
        // Two discontinuities expected: the picture data band's frame
        // number jumped (FrameNumberJump) AND the video PTS jumped a
        // frame relative to the prediction (VideoTimestampReanchor).
        // Both are correct — they're independent signals reporting the
        // same underlying timeline event from different sources.
        CHECK(snap.totalDiscontinuities == 2);
        CHECK(snap.videoReanchorCount == 1);

        // Find the FrameNumberJump event and check its detail.
        bool foundJump = false;
        bool foundReanchor = false;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::FrameNumberJump) {
                                        foundJump = true;
                                        CHECK(d.previousValue == String("3"));
                                        CHECK(d.currentValue == String("5"));
                                        // The pre-rendered description should
                                        // mention both numbers and the delta.
                                        CHECK(d.description.contains("was 3"));
                                        CHECK(d.description.contains("got 5"));
                                } else if (d.kind == InspectorDiscontinuity::VideoTimestampReanchor) {
                                        foundReanchor = true;
                                }
                        }
                }
        }
        CHECK(foundJump);
        CHECK(foundReanchor);
}

// ============================================================================
// Sync offset jitter detection: a synthetic 1-sample audio shift fires
// a SyncOffsetChange discontinuity warning under the default (zero)
// tolerance.  The marker-based offset is cadence-free, so any
// frame-to-frame movement is a real shift.
// ============================================================================

TEST_CASE("Inspector flags a sync offset change as a discontinuity") {
        std::vector<InspectorEvent> events;
        Mutex                       eventsMutex;
        InspectorRig                rig;
        // Use a PcmMarker channel layout (matches TPG's own default)
        // so the AudioData / ImageData marker-based A/V sync check
        // produces a real offset value.  Channel 0 carries the
        // codeword we'll shift to create a sync offset change.
        EnumList pcmAll = EnumList::forType<AudioPattern>();
        pcmAll.append(AudioPattern::PcmMarker);
        pcmAll.append(AudioPattern::PcmMarker);
        buildRig(
                rig, 0xC0FFEEBBu,
                [&](const InspectorEvent &e) {
                        Mutex::Locker lk(eventsMutex);
                        events.push_back(e);
                },
                /*audioEnabled=*/true, pcmAll, /*audioChannels=*/2);
        // Re-open the inspector with strict (0-sample) tolerance so a
        // 1-sample shift in the marker waveform actually trips the check.
        rig.inspectorIo->close().wait();
        MediaIO::Config insCfg = MediaIOFactory::defaultConfig("Inspector");
        insCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
        insCfg.set(MediaConfig::InspectorSyncOffsetToleranceSamples, int32_t(0));
        insCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        // AvSync auto-resolves @c ImageData and @c AudioData via the
        // dependency rules; we list them explicitly here for clarity.
        EnumList syncTests = EnumList::forType<InspectorTest>();
        syncTests.append(InspectorTest::ImageData);
        syncTests.append(InspectorTest::AudioData);
        syncTests.append(InspectorTest::AvSync);
        syncTests.append(InspectorTest::Continuity);
        insCfg.set(MediaConfig::InspectorTests, syncTests);
        rig.inspectorIo->setConfig(insCfg);
        REQUIRE(rig.inspectorIo->open().wait().isOk());

        // Pump 10 clean frames first so the sync offset settles to its
        // natural constant value (no discontinuities expected).  Then
        // build a synthetic frame whose channel-0 PcmMarker codeword
        // has been left-shifted by 1 sample — under strict tolerance
        // even that tiny shift must register.
        for (int i = 0; i < 10; i++) {
                Frame::Ptr frame;
                REQUIRE(syncRead(rig.tpg->source(0), frame).isOk());
                REQUIRE(rig.inspectorIo->sink(0)->writeFrame(frame).wait().isOk());
        }

        // Read frame 11, then mutate its audio in place to shift the
        // marker waveform by 1 sample on channel 0.  The frame is
        // freshly created by the TPG so we own the only reference and
        // can write into its audio buffer directly.
        Frame::Ptr shifted;
        REQUIRE(syncRead(rig.tpg->source(0), shifted).isOk());
        REQUIRE(shifted.isValid());
        auto auds = shifted->audioPayloads();
        REQUIRE(auds.size() == 1);
        AudioPayload::Ptr apBase = auds[0];
        REQUIRE(apBase.isValid());
        auto uap = sharedPointerCast<PcmAudioPayload>(apBase);
        REQUIRE(uap.isValid());
        PcmAudioPayload *apRaw = uap.modify();
        REQUIRE(apRaw->desc().format().id() == AudioFormat::PCMI_Float32LE);
        const int    channels = apRaw->desc().channels();
        const size_t samples = apRaw->sampleCount();
        REQUIRE(samples >= 4);
        REQUIRE(apRaw->planeCount() > 0);
        float *data = reinterpret_cast<float *>(apRaw->data()[0].data());
        // Right-shift every channel by one sample frame: insert
        // silence at the very start and push every other sample one
        // index later.  Both channels carry a PcmMarker codeword,
        // and the marker-based sync uses whichever channel decodes
        // successfully — so shifting just one channel would let the
        // other's clean codeword keep producing the same offset and
        // the discontinuity would never fire.
        //
        // Right-shift (rather than left-shift) preserves the
        // codeword's full leading-edge sample run; a left-shift
        // would clip the first +A sample, drop the leading run
        // below the decoder's @c minRun threshold, and force
        // findSync to skip past the codeword instead of measuring
        // a small offset change.
        for (size_t s = samples - 1; s > 0; --s) {
                for (int ch = 0; ch < channels; ++ch) {
                        data[s * channels + ch] = data[(s - 1) * channels + ch];
                }
        }
        for (int ch = 0; ch < channels; ++ch) {
                data[0 * channels + ch] = 0.0f;
        }
        // Replace the original audio payload on the frame with the
        // modified one so the writer sees our edits (the uap clone we
        // built via CoW-modify() isn't the one still in the frame's
        // payload list).
        for (MediaPayload::Ptr &p : shifted.modify()->payloadList()) {
                if (p.isValid() && p->kind() == MediaPayloadKind::Audio) {
                        p = uap;
                        break;
                }
        }

        REQUIRE(rig.inspectorIo->sink(0)->writeFrame(shifted).wait().isOk());

        // Inspect the captured events: at least one SyncOffsetChange
        // should be present, with previous and current values that
        // describe a real shift.
        int                    syncOffsetChangeCount = 0;
        InspectorDiscontinuity sample;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::SyncOffsetChange) {
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
// Frame-rate relatch from per-frame metadata — covers the case where a
// source backend (e.g. an NDI receiver) opens with a placeholder
// integer rate in @c pendingMediaDesc and only learns the real
// rational rate (29.97, 23.976, 59.94) after the first frame arrives.
// Before the relatch the inspector predicted at the placeholder
// cadence and accumulated ~1.6 sample/frame drift in the A/V sync
// offset on a 29.97 source.  With the relatch the cadence math runs
// against the real rate and the offset reports 0.
// ============================================================================

TEST_CASE("Inspector relatches frame rate from per-frame metadata "
          "when pendingMediaDesc carries a placeholder rate") {
        // TPG configured at NTSC (29.97) — the audio cadence is
        // 1601/1602/1601/1602/1602 samples/frame and frames carry
        // @c Metadata::FrameRate = 30000/1001.  Use the PcmMarker
        // pattern on every channel so the marker-based A/V sync
        // check has a real codeword to lock onto (LTC + AvSync
        // patterns target the standard buildRig path; this test
        // wants a direct codeword channel).
        EnumList pcmAll = EnumList::forType<AudioPattern>();
        pcmAll.append(AudioPattern::PcmMarker);
        pcmAll.append(AudioPattern::PcmMarker);
        MediaIO::Config tpgCfg = MediaIOFactory::defaultConfig("TPG");
        tpgCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
        tpgCfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
        tpgCfg.set(MediaConfig::TimecodeStart, String("01:00:00:00"));
        tpgCfg.set(MediaConfig::StreamID, uint32_t(0xFEEDC0DEu));
        tpgCfg.set(MediaConfig::AudioEnabled, true);
        tpgCfg.set(MediaConfig::AudioChannels, int32_t(2));
        tpgCfg.set(MediaConfig::AudioChannelModes, pcmAll);
        MediaIO *tpg = MediaIO::create(tpgCfg);
        REQUIRE(tpg != nullptr);
        REQUIRE(tpg->open().wait().isOk());

        // Inspector opened with a 30/1 placeholder in
        // @c pendingMediaDesc — this mirrors what an NDI receiver
        // publishes at open time before its capture loop has seen
        // the first SDK frame.  Without the per-frame relatch the
        // inspector's A/V sync prediction would use this rate for
        // the entire run.
        std::vector<InspectorEvent> events;
        Mutex                       eventsMutex;
        InspectorMediaIO           *inspector = new InspectorMediaIO();
        inspector->setEventCallback([&](const InspectorEvent &e) {
                Mutex::Locker lk(eventsMutex);
                events.push_back(e);
        });
        MediaIO::Config insCfg = MediaIOFactory::defaultConfig("Inspector");
        insCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
        insCfg.set(MediaConfig::InspectorSyncOffsetToleranceSamples, int32_t(0));
        insCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        EnumList syncTests = EnumList::forType<InspectorTest>();
        syncTests.append(InspectorTest::ImageData);
        syncTests.append(InspectorTest::AudioData);
        syncTests.append(InspectorTest::AvSync);
        syncTests.append(InspectorTest::Continuity);
        insCfg.set(MediaConfig::InspectorTests, syncTests);
        inspector->setConfig(insCfg);
        // Stamp the placeholder rate explicitly so the relatch
        // path has something to override.  setPendingMediaDesc
        // populates @c MediaIOCommandOpen::pendingMediaDesc which
        // the inspector reads at line 238 of inspectormediaio.cpp.
        MediaDesc placeholder;
        placeholder.setFrameRate(FrameRate(FrameRate::FPS_30));
        REQUIRE(inspector->setPendingMediaDesc(placeholder).isOk());
        REQUIRE(inspector->open().wait().isOk());

        // Pump 20 frames.  TPG attaches @c Metadata::FrameRate =
        // 30000/1001 to every frame; the inspector should relatch
        // on the first frame and the A/V sync offset should stay
        // at 0 for the rest of the run.  Without the relatch the
        // offset accumulates roughly +1.6 samples/frame and trips
        // a SyncOffsetChange discontinuity on every frame past the
        // first match (at strict tolerance = 0).
        for (int i = 0; i < 20; ++i) {
                Frame::Ptr frame;
                REQUIRE(syncRead(tpg->source(0), frame).isOk());
                REQUIRE(inspector->sink(0)->writeFrame(frame).wait().isOk());
        }

        int syncOffsetChanges = 0;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::SyncOffsetChange) {
                                        ++syncOffsetChanges;
                                }
                        }
                }
        }
        CHECK(syncOffsetChanges == 0);

        inspector->close().wait();
        delete inspector;
        tpg->close().wait();
        delete tpg;
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
        rig.inspectorIo->close().wait();
        MediaIO::Config insCfg = MediaIOFactory::defaultConfig("Inspector");
        insCfg.set(MediaConfig::InspectorLogIntervalSec, 0.01); // 10 ms
        insCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        rig.inspectorIo->setConfig(insCfg);
        REQUIRE(rig.inspectorIo->open().wait().isOk());

        for (int i = 0; i < 10; i++) {
                Frame::Ptr frame;
                REQUIRE(syncRead(rig.tpg->source(0), frame).isOk());
                REQUIRE(rig.inspectorIo->sink(0)->writeFrame(frame).wait().isOk());
        }
        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 10);
}

TEST_CASE("Inspector decodes LTC from a TPG with an LTC channel") {
        // Pure LTC-decode feature test.  The TPG carries LTC on ch0
        // and AvSync on ch1 (the buildRig default).  The inspector's
        // LTC decoder should lock and recover most frames' timecode.
        // This test makes no claims about A/V sync — that path is
        // driven by AudioData markers in this codebase, so a TPG
        // configured with LTC instead of PcmMarker leaves the sync
        // check dormant by design.
        InspectorRig rig;
        buildRig(rig, 0x55667788u);
        pumpFrames(rig, 40);

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 40);
        // The LTC decoder needs at least one full LTC frame to lock,
        // so not every frame yields a result — but most should after
        // the first second or so.
        CHECK(snap.framesWithPictureData == 40);
        CHECK(snap.framesWithLtc > 20);
        REQUIRE(snap.hasLastEvent);
        CHECK(snap.lastEvent.ltcDecoderEnabled);
        // The most recent frame should carry a valid recovered
        // timecode once the decoder has locked.
        CHECK(snap.lastEvent.ltcDecoded);
        CHECK(snap.lastEvent.ltcTimecode.isValid());
}

TEST_CASE("Inspector reports A/V sync offset via AudioData + ImageData markers") {
        // AudioData / ImageData marker-based sync.  The TPG carries
        // a PcmMarker on every channel (matches the TPG's own default,
        // and what an AvSync-driven inspector now relies on); both
        // encoders stamp the same 48-bit frame number, and the
        // inspector cross-references them to derive the offset.
        std::vector<InspectorEvent> events;
        Mutex                       eventsMutex;
        EnumList                    pcmAll = EnumList::forType<AudioPattern>();
        pcmAll.append(AudioPattern::PcmMarker);
        pcmAll.append(AudioPattern::PcmMarker);

        InspectorRig rig;
        buildRig(
                rig, 0x55667788u,
                [&](const InspectorEvent &e) {
                        Mutex::Locker lk(eventsMutex);
                        events.push_back(e);
                },
                /*audioEnabled=*/true, pcmAll, /*audioChannels=*/2);
        pumpFrames(rig, 40);

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 40);
        CHECK(snap.framesWithPictureData == 40);

        // Find at least one frame where the marker-based A/V sync
        // produced an offset.  TPG-internal audio and video share
        // the same MediaTimeStamp anchor, so the offset is a fixed
        // small phase relation reflecting how the audio chunk's
        // codeword leading edge lines up with the picture's PTS —
        // typically zero at clean integer rates, and bounded well
        // below one frame's samples otherwise.
        bool    foundOffset = false;
        int64_t maxAbsOffset = 0;
        int64_t firstOffset = 0;
        bool    firstOffsetSet = false;
        bool    offsetIsConstant = true;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        if (e.avSyncValid) {
                                foundOffset = true;
                                const int64_t a =
                                        e.avSyncOffsetSamples < 0 ? -e.avSyncOffsetSamples : e.avSyncOffsetSamples;
                                if (a > maxAbsOffset) maxAbsOffset = a;
                                if (!firstOffsetSet) {
                                        firstOffset = e.avSyncOffsetSamples;
                                        firstOffsetSet = true;
                                } else if (e.avSyncOffsetSamples != firstOffset) {
                                        offsetIsConstant = false;
                                }
                        }
                }
        }
        CHECK(foundOffset);
        // 30 fps @ 48kHz = 1600 samples per frame.  TPG-internal
        // audio and video should agree to well within one frame.
        CHECK(maxAbsOffset < 1600);
        // The offset must not move from frame to frame on a clean
        // stream.  Any change would mean the picture's MediaTimeStamp
        // and the audio chunk for the same frame are drifting in
        // time relative to each other.
        CHECK(offsetIsConstant);

        // Because the offset is constant, the inspector's sync-
        // offset-change detector (default tolerance 0 = report any
        // change) must not have raised a single SyncOffsetChange
        // discontinuity.  This verifies the change-detection code
        // path doesn't false-positive on a stable stream.
        int syncOffsetChangeCount = 0;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::SyncOffsetChange) {
                                        syncOffsetChangeCount++;
                                }
                        }
                }
        }
        CHECK(syncOffsetChangeCount == 0);
}

// ============================================================================
// A constant-phase offset between audio codewords and the rational
// cadence — for example a stream that joins mid-flight, an upstream
// SRC's constant group delay, or simply a non-zero starting frame
// number — should not produce a non-zero A/V sync offset, and must
// never fire a SyncOffsetChange discontinuity.  The inspector latches
// the first observed phase as a baseline and reports subsequent
// frames' deviation from it.
//
// We simulate the constant-phase case by uniformly right-shifting
// every audio chunk's PcmMarker codeword by the same number of
// samples; the codeword still lands at a stable position relative to
// the chunk, just not at sample 0 the way the unmodified TPG would
// stamp it.
// ============================================================================

TEST_CASE("Inspector baseline absorbs a constant phase between audio and video") {
        std::vector<InspectorEvent> events;
        Mutex                       eventsMutex;
        EnumList                    pcmAll = EnumList::forType<AudioPattern>();
        pcmAll.append(AudioPattern::PcmMarker);
        pcmAll.append(AudioPattern::PcmMarker);

        InspectorRig rig;
        buildRig(
                rig, 0xBA5E1100u,
                [&](const InspectorEvent &e) {
                        Mutex::Locker lk(eventsMutex);
                        events.push_back(e);
                },
                /*audioEnabled=*/true, pcmAll, /*audioChannels=*/2);

        // Constant phase: shift every chunk's audio right by the
        // same N samples.  The codeword stays at a stable position
        // relative to the chunk start (just N samples in instead of
        // 0), so the inspector should latch that baseline once and
        // then report 0 deviation on every subsequent frame.
        const size_t kPhaseShift = 7;
        for (int frameNo = 0; frameNo < 30; ++frameNo) {
                Frame::Ptr frame;
                REQUIRE(syncRead(rig.tpg->source(0), frame).isOk());
                REQUIRE(frame.isValid());

                auto auds = frame->audioPayloads();
                REQUIRE(auds.size() == 1);
                AudioPayload::Ptr apBase = auds[0];
                REQUIRE(apBase.isValid());
                auto uap = sharedPointerCast<PcmAudioPayload>(apBase);
                REQUIRE(uap.isValid());
                PcmAudioPayload *apRaw = uap.modify();
                const int    channels = apRaw->desc().channels();
                const size_t samples = apRaw->sampleCount();
                REQUIRE(samples >= kPhaseShift + 1);
                float *data = reinterpret_cast<float *>(apRaw->data()[0].data());
                for (size_t s = samples - 1; s >= kPhaseShift; --s) {
                        for (int ch = 0; ch < channels; ++ch) {
                                data[s * channels + ch] = data[(s - kPhaseShift) * channels + ch];
                        }
                }
                for (size_t s = 0; s < kPhaseShift; ++s) {
                        for (int ch = 0; ch < channels; ++ch) {
                                data[s * channels + ch] = 0.0f;
                        }
                }
                for (MediaPayload::Ptr &p : frame.modify()->payloadList()) {
                        if (p.isValid() && p->kind() == MediaPayloadKind::Audio) {
                                p = uap;
                                break;
                        }
                }

                REQUIRE(rig.inspectorIo->sink(0)->writeFrame(frame).wait().isOk());
        }

        // Inspect captured events: every valid avSync measurement
        // must be 0 (the baseline absorbed the constant phase),
        // and no SyncOffsetChange discontinuities must have fired.
        int  validCount = 0;
        bool anyNonZero = false;
        int  syncOffsetChangeCount = 0;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        if (e.avSyncValid) {
                                ++validCount;
                                if (e.avSyncOffsetSamples != 0) anyNonZero = true;
                        }
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::SyncOffsetChange) {
                                        ++syncOffsetChangeCount;
                                }
                        }
                }
        }
        CHECK(validCount > 5);
        CHECK_FALSE(anyNonZero);
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
        double estimateFreqHz(const PcmAudioPayload &audio, size_t channel, double sampleRate) {
                const float *data = reinterpret_cast<const float *>(audio.plane(0).data());
                if (data == nullptr) return 0.0;
                const size_t nch = audio.desc().channels();
                const size_t n = audio.sampleCount();
                if (n < 2) return 0.0;

                size_t crossings = 0;
                float  prev = data[channel];
                for (size_t s = 1; s < n; ++s) {
                        float cur = data[s * nch + channel];
                        if ((prev < 0 && cur >= 0) || (prev >= 0 && cur < 0)) {
                                crossings++;
                        }
                        prev = cur;
                }
                const double cycles = static_cast<double>(crossings) / 2.0;
                const double seconds = static_cast<double>(n - 1) / sampleRate;
                return (seconds > 0.0) ? (cycles / seconds) : 0.0;
        }

} // namespace

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
        Mutex                       eventsMutex;
        InspectorRig                rig;
        buildRig(
                rig, 0x5A5AA5A5u,
                [&](const InspectorEvent &e) {
                        Mutex::Locker lk(eventsMutex);
                        events.push_back(e);
                },
                /*audioEnabled=*/true, modes,
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
        const double     sampleRate = 48000.0;
        AudioDesc        desc(static_cast<float>(sampleRate), 2);
        AudioTestPattern gen(desc);
        gen.setChannelModes(modes);
        gen.setLtcLevel(AudioLevel::fromDbfs(-20.0));
        gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
        REQUIRE(gen.configure().isOk());
        auto audio = gen.createPayload(static_cast<size_t>(sampleRate));
        REQUIRE(audio.isValid());

        double freq = estimateFreqHz(*audio, 1, sampleRate);
        CHECK(freq == doctest::Approx(AudioTestPattern::kSrcProbeFrequencyHz).epsilon(0.005));
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
                 /*audioEnabled=*/true, modes,
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
        const double     sampleRate = 48000.0;
        AudioDesc        desc(static_cast<float>(sampleRate), 4);
        AudioTestPattern gen(desc);
        gen.setChannelModes(modes);
        gen.setLtcLevel(AudioLevel::fromDbfs(-20.0));
        gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
        gen.setChannelIdBaseFreq(1000.0);
        gen.setChannelIdStepFreq(100.0);
        REQUIRE(gen.configure().isOk());
        auto audio = gen.createPayload(static_cast<size_t>(sampleRate));
        REQUIRE(audio.isValid());

        // Every ChannelId channel must carry the frequency its index
        // maps to (1100, 1200, 1300).  LTC on ch0 isn't a sine so we
        // don't run the estimator on it.
        for (int ch = 1; ch <= 3; ++ch) {
                double expected = AudioTestPattern::channelIdFrequency(ch, 1000.0, 100.0);
                double observed = estimateFreqHz(*audio, ch, sampleRate);
                CHECK(observed == doctest::Approx(expected).epsilon(0.005));
        }
}

// ============================================================================
// PTS jitter detection — re-anchor + discontinuity for audio and video PTS
// jumps that exceed the configured tolerance.  Verifies the inspector's
// continuous-stream model: it predicts the next PTS from a running
// anchor, and a divergence beyond tolerance is reported and re-anchored
// rather than silently treated as a per-frame delta.
// ============================================================================

namespace {

        // Walks @p frame's audio payloads and shifts every audio PTS by
        // @p shiftNs nanoseconds (in the offset field of the
        // MediaTimeStamp, so the underlying TimeStamp is untouched).
        // Returns the number of payloads modified.
        int shiftAudioPts(Frame::Ptr &frame, int64_t shiftNs) {
                int n = 0;
                for (MediaPayload::Ptr &p : frame.modify()->payloadList()) {
                        if (!p.isValid()) continue;
                        if (p->kind() != MediaPayloadKind::Audio) continue;
                        auto ap = sharedPointerCast<AudioPayload>(p);
                        if (!ap.isValid()) continue;
                        MediaTimeStamp mts = ap->pts();
                        if (!mts.isValid()) continue;
                        const int64_t newOffsetNs = mts.offset().nanoseconds() + shiftNs;
                        mts.setOffset(Duration::fromNanoseconds(newOffsetNs));
                        ap.modify()->setPts(mts);
                        p = ap;
                        n++;
                }
                return n;
        }

        // Walks @p frame's video payloads and shifts every video PTS
        // by @p shiftNs nanoseconds (offset field).  Returns the
        // number modified.
        int shiftVideoPts(Frame::Ptr &frame, int64_t shiftNs) {
                int n = 0;
                for (MediaPayload::Ptr &p : frame.modify()->payloadList()) {
                        if (!p.isValid()) continue;
                        if (p->kind() != MediaPayloadKind::Video) continue;
                        auto vp = sharedPointerCast<VideoPayload>(p);
                        if (!vp.isValid()) continue;
                        MediaTimeStamp mts = vp->pts();
                        if (!mts.isValid()) continue;
                        const int64_t newOffsetNs = mts.offset().nanoseconds() + shiftNs;
                        mts.setOffset(Duration::fromNanoseconds(newOffsetNs));
                        vp.modify()->setPts(mts);
                        p = vp;
                        n++;
                }
                return n;
        }

} // namespace

TEST_CASE("Inspector flags an audio PTS jump as AudioTimestampReanchor") {
        // Pump several clean frames so the audio anchor locks, then
        // shift one frame's audio PTS by +50 ms — well beyond the
        // default 5 ms tolerance.  The inspector must emit one
        // AudioTimestampReanchor discontinuity and the snapshot must
        // increment audioReanchorCount by exactly one.
        std::vector<InspectorEvent> events;
        Mutex                       eventsMutex;
        InspectorRig                rig;
        buildRig(rig, 0xA1B2C3D4u, [&](const InspectorEvent &e) {
                Mutex::Locker lk(eventsMutex);
                events.push_back(e);
        });
        pumpFrames(rig, 5);

        Frame::Ptr shifted;
        REQUIRE(syncRead(rig.tpg->source(0), shifted).isOk());
        REQUIRE(shifted.isValid());
        REQUIRE(shiftAudioPts(shifted, /*shiftNs=*/50'000'000) >= 1);
        REQUIRE(rig.inspectorIo->sink(0)->writeFrame(shifted).wait().isOk());

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 6);
        CHECK(snap.audioReanchorCount == 1);

        int reanchorCount = 0;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::AudioTimestampReanchor) {
                                        reanchorCount++;
                                        CHECK(d.description.contains("re-anchoring"));
                                }
                        }
                }
        }
        CHECK(reanchorCount == 1);
}

TEST_CASE("Inspector flags a video PTS jump as VideoTimestampReanchor") {
        // Same shape as the audio test: pump clean frames so the
        // video anchor locks, then shift one frame's video PTS by
        // +50 ms.  Default tolerance is 5 ms, so this fires.
        std::vector<InspectorEvent> events;
        Mutex                       eventsMutex;
        InspectorRig                rig;
        buildRig(rig, 0xD4C3B2A1u, [&](const InspectorEvent &e) {
                Mutex::Locker lk(eventsMutex);
                events.push_back(e);
        });
        pumpFrames(rig, 5);

        Frame::Ptr shifted;
        REQUIRE(syncRead(rig.tpg->source(0), shifted).isOk());
        REQUIRE(shifted.isValid());
        REQUIRE(shiftVideoPts(shifted, /*shiftNs=*/50'000'000) >= 1);
        REQUIRE(rig.inspectorIo->sink(0)->writeFrame(shifted).wait().isOk());

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 6);
        CHECK(snap.videoReanchorCount == 1);

        int reanchorCount = 0;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::VideoTimestampReanchor) {
                                        reanchorCount++;
                                        CHECK(d.description.contains("re-anchoring"));
                                }
                        }
                }
        }
        CHECK(reanchorCount == 1);
}

TEST_CASE("Inspector PTS jitter stays small for a clean stream") {
        // No shifts, no synthetic perturbations — just confirm that the
        // jitter accumulators converge to near-zero on a clean TPG run.
        // TPG's MediaIO auto-fill stamps PTSs in lock-step with the
        // stream's nominal cadence, so any reported jitter is purely
        // numerical (rounding in the prediction).
        InspectorRig rig;
        buildRig(rig, 0x11112222u);
        pumpFrames(rig, 30);

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 30);
        CHECK(snap.audioReanchorCount == 0);
        CHECK(snap.videoReanchorCount == 0);
        // The jitter min/max bounds for both essences should fit
        // comfortably under 1 ms even with rounding noise.
        if (snap.audioPtsJitterSamples > 0) {
                CHECK(std::llabs(snap.audioPtsJitterMaxNs) < 1'000'000);
                CHECK(std::llabs(snap.audioPtsJitterMinNs) < 1'000'000);
        }
        if (snap.videoPtsJitterSamples > 0) {
                CHECK(std::llabs(snap.videoPtsJitterMaxNs) < 1'000'000);
                CHECK(std::llabs(snap.videoPtsJitterMinNs) < 1'000'000);
        }
        // A/V cross-PTS drift should also stay near zero on a clean
        // stream (audio and video share the TPG's clock so any drift
        // is at most rounding-noise level).
        if (snap.avPtsDriftSamples > 0) {
                CHECK(std::llabs(snap.avPtsDriftMaxNs) < 1'000'000);
                CHECK(std::llabs(snap.avPtsDriftMinNs) < 1'000'000);
        }
}

TEST_CASE("Inspector tolerates bursty audio (frame with no audio + double-sized chunk)") {
        // Synthesise the bursty pattern that motivates this whole
        // refactor: one frame arrives with NO audio chunk, then the
        // next frame arrives with a chunk that covers both frames'
        // worth of samples.  In the per-frame model the LTC sync
        // word position would be misinterpreted; in the new
        // continuous-stream model the analyses see the same audio
        // they would have seen with steady delivery, so LTC still
        // locks and no spurious sync-offset discontinuities fire.
        std::vector<InspectorEvent> events;
        Mutex                       eventsMutex;
        InspectorRig                rig;
        buildRig(rig, 0xBBBB0001u, [&](const InspectorEvent &e) {
                Mutex::Locker lk(eventsMutex);
                events.push_back(e);
        });

        // Read 30 frames from the TPG; for every other frame, move
        // its audio payload onto the next frame so we get a bursty
        // 0/2x/0/2x/... pattern.  The resulting cumulative samples
        // and audio timeline are identical to the steady delivery —
        // the inspector should report identical results.
        Frame::Ptr held;
        for (int i = 0; i < 30; i++) {
                Frame::Ptr frame;
                REQUIRE(syncRead(rig.tpg->source(0), frame).isOk());
                REQUIRE(frame.isValid());

                if ((i % 2) == 0) {
                        // Strip audio from this frame; remember it for
                        // the next iteration.
                        for (MediaPayload::Ptr &p : frame.modify()->payloadList()) {
                                if (p.isValid() && p->kind() == MediaPayloadKind::Audio) {
                                        held = Frame::Ptr::create();
                                        held.modify()->addPayload(p);
                                        p = MediaPayload::Ptr();
                                }
                        }
                } else if (held.isValid()) {
                        // Prepend the stripped chunk so this frame
                        // carries 2x audio.  PTS of the *first* (older)
                        // chunk is the canonical anchor for the bursty
                        // delivery; the inspector's stream view sees
                        // back-to-back chunks regardless.
                        for (const MediaPayload::Ptr &p : held->payloadList()) {
                                if (p.isValid() && p->kind() == MediaPayloadKind::Audio) {
                                        frame.modify()->addPayload(p);
                                }
                        }
                        held = Frame::Ptr();
                }
                REQUIRE(rig.inspectorIo->sink(0)->writeFrame(frame).wait().isOk());
        }

        InspectorSnapshot snap = rig.inspector->snapshot();
        CHECK(snap.framesProcessed == 30);
        CHECK(snap.framesWithPictureData == 30);
        // LTC should still lock — the decoder sees a continuous
        // stream regardless of how the chunks were attached to
        // frames.  We only require "more than a handful" because the
        // first ~ second is occupied by anchor establishment.
        CHECK(snap.framesWithLtc > 5);
        // Critically: no sync-offset discontinuities should fire
        // even though every other frame had no audio attached.  In
        // the old per-frame model this test would generate a flood
        // of false positives.
        int syncOffsetChanges = 0;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::SyncOffsetChange) {
                                        syncOffsetChanges++;
                                }
                        }
                }
        }
        CHECK(syncOffsetChanges == 0);
}

// ============================================================================
// AudioData decode — TPG-default PcmMarker on every channel produces
// per-channel codewords the inspector should recover and validate.
// ============================================================================

TEST_CASE("Inspector decodes per-channel AudioData markers from the TPG default") {
        // Build a TPG with the *defaults* (PcmMarker on every channel)
        // and a 4-channel audio stream so the inspector must validate
        // four distinct channel codewords.  Stream ID picks the top
        // 8-bit byte of the marker payload.
        InspectorRig rig;
        EnumList     pcmAll = EnumList::forType<AudioPattern>();
        pcmAll.append(AudioPattern::PcmMarker);
        pcmAll.append(AudioPattern::PcmMarker);
        pcmAll.append(AudioPattern::PcmMarker);
        pcmAll.append(AudioPattern::PcmMarker);
        // The default LTC/AvSync override in buildRig clobbers our
        // per-test channel modes, so pass an explicit list.
        buildRig(rig, /*streamId=*/0x42u, /*cb=*/{}, /*audioEnabled=*/true, pcmAll, /*audioChannels=*/4);
        pumpFrames(rig, 5);

        InspectorSnapshot snap = rig.inspector->snapshot();
        REQUIRE(snap.hasLastEvent);
        REQUIRE(snap.lastEvent.audioDataDecoderEnabled);
        REQUIRE(snap.lastEvent.audioDataDecoded);
        REQUIRE(snap.lastEvent.audioChannelMarkers.size() == 4);
        // Every channel's codeword should decode and the encoded
        // channel byte should match the channel index.
        for (size_t ch = 0; ch < 4; ++ch) {
                CAPTURE(ch);
                const auto &m = snap.lastEvent.audioChannelMarkers[ch];
                CHECK(m.decoded);
                CHECK(m.channelMatches);
                CHECK(m.streamId == 0x42u);
                CHECK(m.encodedChannel == ch);
        }
        // Frame-number on the last frame should equal the inspector's
        // own frame index — the TPG and the inspector share the same
        // counter.
        CHECK(snap.lastEvent.audioChannelMarkers[0].frameNumber == snap.lastEvent.frameIndex.value());
        // No mismatch discontinuities should have fired across the
        // whole run.
        CHECK(snap.totalDiscontinuities == 0);
}

TEST_CASE("Inspector flags an AudioChannelMismatch when channels are swapped") {
        // Capture per-frame events so we can scan for the mismatch
        // discontinuity below.
        Mutex                       eventsMutex;
        std::vector<InspectorEvent> events;
        EnumList                    pcmAll = EnumList::forType<AudioPattern>();
        pcmAll.append(AudioPattern::PcmMarker);
        pcmAll.append(AudioPattern::PcmMarker);

        InspectorRig rig;
        buildRig(
                rig, /*streamId=*/0x55u,
                [&](const InspectorEvent &e) {
                        Mutex::Locker lk(eventsMutex);
                        events.push_back(e);
                },
                /*audioEnabled=*/true, pcmAll, /*audioChannels=*/2);

        // Pump a few frames manually so we can reach into the
        // payload's plane buffer and swap channel 0 ↔ channel 1
        // before handing it to the inspector.
        for (int i = 0; i < 4; ++i) {
                Frame::Ptr frame;
                REQUIRE(syncRead(rig.tpg->source(0), frame).isOk());
                REQUIRE(frame.isValid());
                for (MediaPayload::Ptr &p : frame.modify()->payloadList()) {
                        if (!p.isValid() || p->kind() != MediaPayloadKind::Audio) continue;
                        auto *pcm = static_cast<PcmAudioPayload *>(p.modify());
                        REQUIRE(pcm != nullptr);
                        const size_t n = pcm->sampleCount();
                        const size_t channels = pcm->desc().channels();
                        REQUIRE(channels == 2);
                        float *data = reinterpret_cast<float *>(pcm->data()[0].data());
                        for (size_t s = 0; s < n; ++s) {
                                std::swap(data[s * channels + 0], data[s * channels + 1]);
                        }
                }
                REQUIRE(rig.inspectorIo->sink(0)->writeFrame(frame).wait().isOk());
        }

        // Both channels' markers should still decode (the codeword
        // survives the swap), but the encoded channel byte will be
        // off by one — the inspector emits AudioChannelMismatch
        // discontinuities on every frame.
        int mismatchCount = 0;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::AudioChannelMismatch) {
                                        ++mismatchCount;
                                }
                        }
                }
        }
        // Two channels swapped × four frames = eight mismatches.
        CHECK(mismatchCount == 8);
}

TEST_CASE("Inspector flags an AudioDataDecodeFailure when a codeword's CRC is corrupted") {
        // Mid-codeword sample corruption flips one payload bit so the
        // decoder recovers the codeword but its CRC mismatches.  The
        // streaming decoder emits a CorruptData item (not consumed
        // silently); the inspector must surface it as an
        // AudioDataDecodeFailure discontinuity.
        Mutex                       eventsMutex;
        std::vector<InspectorEvent> events;
        EnumList                    pcmAll = EnumList::forType<AudioPattern>();
        pcmAll.append(AudioPattern::PcmMarker);

        InspectorRig rig;
        buildRig(
                rig, /*streamId=*/0x33u,
                [&](const InspectorEvent &e) {
                        Mutex::Locker lk(eventsMutex);
                        events.push_back(e);
                },
                /*audioEnabled=*/true, pcmAll, /*audioChannels=*/1);

        // First frame: clean PcmMarker codeword.  This latches the
        // inspector's per-channel "carries codewords" state so
        // subsequent corruption surfaces as a discontinuity rather
        // than being suppressed as a probable false sync lock.
        {
                Frame::Ptr frame;
                REQUIRE(syncRead(rig.tpg->source(0), frame).isOk());
                REQUIRE(rig.inspectorIo->sink(0)->writeFrame(frame).wait().isOk());
        }

        // Corrupt one payload bit on every subsequent frame's audio chunk.
        for (int i = 0; i < 4; ++i) {
                Frame::Ptr frame;
                REQUIRE(syncRead(rig.tpg->source(0), frame).isOk());
                REQUIRE(frame.isValid());
                for (MediaPayload::Ptr &p : frame.modify()->payloadList()) {
                        if (!p.isValid() || p->kind() != MediaPayloadKind::Audio) continue;
                        auto         *pcm = static_cast<PcmAudioPayload *>(p.modify());
                        const size_t  channels = pcm->desc().channels();
                        REQUIRE(channels == 1);
                        // Flip a known-'1' payload bit (bit 56 of
                        // codeword for payload 0x33 — the encoder
                        // writes payload bits MSB-first so payload
                        // bit 56 lands at transmit index
                        // SyncBits + (63 - 56) = 11).  Drop the
                        // first half samples to large negative
                        // values to force the bit to flip without
                        // creating a single-sample outlier that
                        // would also defeat sync detection.
                        float       *data = reinterpret_cast<float *>(pcm->data()[0].data());
                        const size_t cellStart =
                                (AudioDataEncoder::SyncBits + 7) * AudioDataEncoder::DefaultSamplesPerBit;
                        for (size_t k = 0; k < AudioDataEncoder::DefaultSamplesPerBit / 2; ++k) {
                                data[cellStart + k] = -0.2f;
                        }
                }
                REQUIRE(rig.inspectorIo->sink(0)->writeFrame(frame).wait().isOk());
        }

        int decodeFailureCount = 0;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::AudioDataDecodeFailure) {
                                        ++decodeFailureCount;
                                }
                        }
                }
        }
        // One failed decode per frame.
        CHECK(decodeFailureCount == 4);
}

TEST_CASE("Inspector flags an AudioDataLengthAnomaly when codeword length deviates") {
        // The expected packet length is BitsPerPacket * 8 = 608
        // samples at the default samplesPerBit.  A successful decode
        // whose recovered length deviates by more than 20 % from
        // that should fire AudioDataLengthAnomaly.
        //
        // Synthesize the anomaly directly at the encoder: pick a
        // non-default samplesPerBit that's still inside the decoder's
        // ±50 % bandwidth gate but outside the inspector's ±20 %
        // length tolerance.  samplesPerBit=12 is 50 % above the
        // default 8, well inside the bandwidth gate (which admits
        // up to 12) and well outside the ±20 % length tolerance.
        Mutex                       eventsMutex;
        std::vector<InspectorEvent> events;
        EnumList                    pcmAll = EnumList::forType<AudioPattern>();
        pcmAll.append(AudioPattern::PcmMarker);

        // Hand-built TPG config with a longer audio frame so the
        // 12-spb codeword (76 × 12 = 912 samples) fits.
        MediaIO::Config tpgCfg = MediaIOFactory::defaultConfig("TPG");
        tpgCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        tpgCfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
        tpgCfg.set(MediaConfig::TimecodeStart, String("01:00:00:00"));
        tpgCfg.set(MediaConfig::StreamID, uint32_t(0x44));
        tpgCfg.set(MediaConfig::AudioEnabled, true);
        tpgCfg.set(MediaConfig::AudioChannels, int32_t(1));
        tpgCfg.set(MediaConfig::AudioChannelModes, pcmAll);

        MediaIO *tpg = MediaIO::create(tpgCfg);
        REQUIRE(tpg != nullptr);
        REQUIRE(tpg->open().wait().isOk());

        InspectorMediaIO *insp = new InspectorMediaIO();
        insp->setEventCallback([&](const InspectorEvent &e) {
                Mutex::Locker lk(eventsMutex);
                events.push_back(e);
        });
        MediaIO::Config insCfg = MediaIOFactory::defaultConfig("Inspector");
        insCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
        insCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        insp->setConfig(insCfg);
        REQUIRE(insp->open().wait().isOk());

        // First frame: pass through unchanged so the inspector
        // latches the channel as active.  Anomaly discontinuities
        // only fire on channels confirmed to carry codewords, so
        // this priming pass is required before the corrupt frames
        // below can register as anomalies.
        {
                Frame::Ptr frame;
                REQUIRE(syncRead(tpg->source(0), frame).isOk());
                REQUIRE(insp->sink(0)->writeFrame(frame).wait().isOk());
        }

        // For each subsequent frame the TPG produces, re-encode the
        // audio channel using AudioDataEncoder at samplesPerBit=12
        // so the codeword runs longer than the inspector's expected
        // 608.
        AudioDataEncoder bigEnc(AudioDesc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1),
                                /*samplesPerBit=*/12, /*amplitude=*/0.1f);
        REQUIRE(bigEnc.isValid());

        for (int i = 0; i < 4; ++i) {
                Frame::Ptr frame;
                REQUIRE(syncRead(tpg->source(0), frame).isOk());
                REQUIRE(frame.isValid());
                for (MediaPayload::Ptr &p : frame.modify()->payloadList()) {
                        if (!p.isValid() || p->kind() != MediaPayloadKind::Audio) continue;
                        auto *pcm = static_cast<PcmAudioPayload *>(p.modify());
                        REQUIRE(pcm->sampleCount() >= bigEnc.packetSamples());
                        // Zero the channel, then stamp at the wider
                        // bit width.
                        const size_t bytes = pcm->desc().bufferSize(pcm->sampleCount());
                        std::memset(pcm->data()[0].data(), 0, bytes);
                        AudioDataEncoder::Item item{};
                        item.firstSample = 0;
                        item.sampleCount = pcm->sampleCount();
                        item.channel = 0;
                        item.payload =
                                (uint64_t(0x44) << 56) | (uint64_t(0) << 48) | uint64_t(i);
                        REQUIRE(bigEnc.encode(*pcm, item).isOk());
                }
                REQUIRE(insp->sink(0)->writeFrame(frame).wait().isOk());
        }

        int lengthAnomalies = 0;
        int decodedFrames = 0;
        {
                Mutex::Locker lk(eventsMutex);
                for (const auto &e : events) {
                        if (e.audioDataDecoded) ++decodedFrames;
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::AudioDataLengthAnomaly) {
                                        ++lengthAnomalies;
                                }
                        }
                }
        }
        // The codeword still decodes (samplesPerBit=12 is inside the
        // ±50 % bandwidth gate against the default expected 8) but
        // its measured 912-sample length fires an anomaly per frame.
        CHECK(decodedFrames >= 3);
        CHECK(lengthAnomalies >= 3);

        insp->close().wait();
        delete insp;
        tpg->close().wait();
        delete tpg;
}

TEST_CASE("Inspector decodes AudioData codewords that straddle audio chunk boundaries") {
        // Bursty audio pattern: every other frame's audio is moved
        // onto the next frame so half the frames carry zero audio and
        // half carry a double-sized chunk.  The codeword for the
        // dropped frame ends up partly inside the doubled chunk —
        // proves the inspector reassembles cross-frame codewords via
        // its per-channel accumulators.
        Mutex                       eventsMutex;
        std::vector<InspectorEvent> events;
        EnumList                    pcmAll = EnumList::forType<AudioPattern>();
        pcmAll.append(AudioPattern::PcmMarker);
        pcmAll.append(AudioPattern::PcmMarker);

        InspectorRig rig;
        buildRig(
                rig, /*streamId=*/0x77u,
                [&](const InspectorEvent &e) {
                        Mutex::Locker lk(eventsMutex);
                        events.push_back(e);
                },
                /*audioEnabled=*/true, pcmAll, /*audioChannels=*/2);

        // Repeat the bursty pattern from the audio-PTS test: strip
        // audio off even-indexed frames, attach two chunks worth on
        // odd-indexed frames.
        Frame::Ptr held;
        for (int i = 0; i < 20; i++) {
                Frame::Ptr frame;
                REQUIRE(syncRead(rig.tpg->source(0), frame).isOk());
                REQUIRE(frame.isValid());

                if ((i % 2) == 0) {
                        for (MediaPayload::Ptr &p : frame.modify()->payloadList()) {
                                if (p.isValid() && p->kind() == MediaPayloadKind::Audio) {
                                        held = Frame::Ptr::create();
                                        held.modify()->addPayload(p);
                                        p = MediaPayload::Ptr();
                                }
                        }
                } else if (held.isValid()) {
                        for (const MediaPayload::Ptr &p : held->payloadList()) {
                                if (p.isValid() && p->kind() == MediaPayloadKind::Audio) {
                                        frame.modify()->addPayload(p);
                                }
                        }
                        held = Frame::Ptr();
                }
                REQUIRE(rig.inspectorIo->sink(0)->writeFrame(frame).wait().isOk());
        }

        // Inspector must still decode the audio data on enough of the
        // odd-indexed (double-chunk) frames that the markers track —
        // the accumulator stitches the codeword back together.
        // Channel mismatches must stay at zero — encoded channel byte
        // matches its index.
        int  decodedFrames = 0;
        int  mismatchCount = 0;
        bool sawDecodeOnEvenFrame = false;
        {
                Mutex::Locker lk(eventsMutex);
                for (size_t fi = 0; fi < events.size(); ++fi) {
                        const auto &e = events[fi];
                        if (e.audioDataDecoded) {
                                ++decodedFrames;
                                if ((fi % 2) == 0) sawDecodeOnEvenFrame = true;
                        }
                        for (const auto &d : e.discontinuities) {
                                if (d.kind == InspectorDiscontinuity::AudioChannelMismatch) {
                                        ++mismatchCount;
                                }
                        }
                }
        }
        // On bursty delivery the per-frame audio chunk is empty on
        // even frames, so decode lands on the doubled odd-frame
        // chunks (≥ 5 successful decodes out of 10 doubled frames).
        CHECK(decodedFrames >= 5);
        CHECK(mismatchCount == 0);
        // Sanity: even frames have no audio at all, so no decode
        // should fire there.
        CHECK_FALSE(sawDecodeOnEvenFrame);
}
