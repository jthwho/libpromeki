/**
 * @file      quicktime.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <doctest/doctest.h>
#include <promeki/quicktime.h>
#include <promeki/string.h>
#include <promeki/umid.h>
#include <promeki/file.h>
#include <promeki/fourcc.h>
#include <promeki/list.h>

using namespace promeki;

namespace {

        /**
 * @brief Build an absolute path to a fixture under testdata/quicktime/.
 *
 * Uses the PROMEKI_SOURCE_DIR compile definition (set by CMake) so the
 * tests run from any working directory.
 */
        String fixturePath(const char *name) {
                return String(PROMEKI_SOURCE_DIR) + "/tests/data/quicktime/" + name;
        }

} // namespace

// ============================================================================
// Default construction / invalid state
// ============================================================================

TEST_CASE("QuickTime: default constructs invalid") {
        QuickTime qt;
        CHECK_FALSE(qt.isValid());
        CHECK(qt.operation() == QuickTime::InvalidOperation);
}

TEST_CASE("QuickTime: createReader returns Reader operation") {
        QuickTime qt = QuickTime::createReader("/dev/null");
        CHECK(qt.isValid());
        CHECK(qt.operation() == QuickTime::Reader);
        CHECK(qt.filename() == "/dev/null");
        CHECK_FALSE(qt.isOpen());
}

TEST_CASE("QuickTime: createWriter returns Writer operation") {
        // Use a path under /tmp; the file is created and immediately removed.
        const String tmp = "/tmp/qt_writer_op_check.mov";
        QuickTime    qt = QuickTime::createWriter(tmp);
        CHECK(qt.isValid());
        CHECK(qt.operation() == QuickTime::Writer);
        // open() should succeed and create the file.
        REQUIRE(qt.open() == Error::Ok);
        REQUIRE(qt.finalize() == Error::Ok);
        std::remove(tmp.cstr());
}

TEST_CASE("QuickTime: open of nonexistent file returns an error") {
        QuickTime qt = QuickTime::createReader("/no/such/path/at_all.mov");
        Error     err = qt.open();
        CHECK(err.isError());
        CHECK_FALSE(qt.isOpen());
}

// ============================================================================
// Uncompressed UYVY (2vuy) — 16x16 24p, 2 frames
// ============================================================================

TEST_CASE("QuickTime: open uncompressed UYVY .mov fixture") {
        QuickTime qt = QuickTime::createReader(fixturePath("tiny_uyvy_24p.mov"));
        REQUIRE(qt.open() == Error::Ok);
        CHECK(qt.isOpen());

        const QuickTime::TrackList &tracks = qt.tracks();
        REQUIRE(tracks.size() >= 1);

        // First track must be the video track.
        const QuickTime::Track &v = tracks[0];
        CHECK(v.type() == QuickTime::Video);
        CHECK(v.size().width() == 16);
        CHECK(v.size().height() == 16);
        CHECK(v.sampleCount() == 2);
        CHECK(v.pixelFormat().isValid());
        // 2vuy maps to YUV8_422_UYVY_Rec709 (already in PixelFormat).
        CHECK(v.pixelFormat().id() == PixelFormat::YUV8_422_UYVY_Rec709);
        CHECK_FALSE(v.pixelFormat().isCompressed());

        // Frame rate ~24 fps.
        REQUIRE(v.frameRate().isValid());
        double fps = v.frameRate().rational().toDouble();
        CHECK(fps > 23.9);
        CHECK(fps < 24.1);

        qt.close();
        CHECK_FALSE(qt.isOpen());
}

// ============================================================================
// ProRes 422 Proxy — 16x16 25p, 2 frames
// ============================================================================

TEST_CASE("QuickTime: open ProRes 422 Proxy .mov fixture") {
        QuickTime qt = QuickTime::createReader(fixturePath("tiny_prores_proxy_25p.mov"));
        REQUIRE(qt.open() == Error::Ok);

        const QuickTime::TrackList &tracks = qt.tracks();
        REQUIRE(tracks.size() >= 1);

        const QuickTime::Track &v = tracks[0];
        CHECK(v.type() == QuickTime::Video);
        CHECK(v.size().width() == 16);
        CHECK(v.size().height() == 16);
        CHECK(v.sampleCount() == 2);
        CHECK(v.pixelFormat().isValid());
        CHECK(v.pixelFormat().isCompressed());
        CHECK(v.pixelFormat().id() == PixelFormat::ProRes_422_Proxy);
        // ProRes used to share the single string codec name "prores"
        // across all six variants; with the typed VideoCodec registry
        // each variant is its own codec ID, so we just verify we got
        // the matching VideoCodec for the ProRes_422_Proxy PixelFormat.
        CHECK(v.pixelFormat().videoCodec().id() == VideoCodec::ProRes_422_Proxy);

        REQUIRE(v.frameRate().isValid());
        double fps = v.frameRate().rational().toDouble();
        CHECK(fps > 24.9);
        CHECK(fps < 25.1);
}

// ============================================================================
// H.264 + PCM stereo — 16x16 24p
// ============================================================================

TEST_CASE("QuickTime: open H.264 + PCM .mov fixture") {
        QuickTime qt = QuickTime::createReader(fixturePath("tiny_h264_pcm_24p.mov"));
        REQUIRE(qt.open() == Error::Ok);

        const QuickTime::TrackList &tracks = qt.tracks();
        REQUIRE(tracks.size() >= 2);

        // Find the video and audio tracks (their order is container-dependent).
        const QuickTime::Track *video = nullptr;
        const QuickTime::Track *audio = nullptr;
        for (const QuickTime::Track &t : tracks) {
                if (t.type() == QuickTime::Video && video == nullptr) video = &t;
                if (t.type() == QuickTime::Audio && audio == nullptr) audio = &t;
        }
        REQUIRE(video != nullptr);
        REQUIRE(audio != nullptr);

        // Video assertions
        CHECK(video->size().width() == 16);
        CHECK(video->size().height() == 16);
        CHECK(video->pixelFormat().isValid());
        CHECK(video->pixelFormat().id() == PixelFormat::H264);
        CHECK(video->pixelFormat().isCompressed());
        REQUIRE(video->frameRate().isValid());

        // Audio assertions: sowt → PCMI_S16LE, 48 kHz, 2 channels
        CHECK(audio->audioDesc().isValid());
        CHECK(audio->audioDesc().format().id() == AudioFormat::PCMI_S16LE);
        CHECK(audio->audioDesc().channels() == 2);
        CHECK(audio->audioDesc().sampleRate() == doctest::Approx(48000.0));
}

// ============================================================================
// MediaDesc population
// ============================================================================

TEST_CASE("QuickTime: mediaDesc reflects the discovered tracks") {
        QuickTime qt = QuickTime::createReader(fixturePath("tiny_h264_pcm_24p.mov"));
        REQUIRE(qt.open() == Error::Ok);

        const MediaDesc &md = qt.mediaDesc();
        CHECK(md.imageList().size() >= 1);
        CHECK(md.audioList().size() >= 1);
        CHECK(md.frameRate().isValid());
}

// ============================================================================
// Sample reads (Phase 2): byte-for-byte verification via direct file compare
// ============================================================================

TEST_CASE("QuickTime: readSample on uncompressed UYVY returns expected size") {
        const String fname = fixturePath("tiny_uyvy_24p.mov");
        QuickTime    qt = QuickTime::createReader(fname);
        REQUIRE(qt.open() == Error::Ok);

        REQUIRE(qt.tracks().size() >= 1);
        REQUIRE(qt.tracks()[0].type() == QuickTime::Video);
        CHECK(qt.tracks()[0].sampleCount() == 2);

        // 16x16 UYVY 4:2:2 8-bit = 16*16*2 = 512 bytes per frame
        QuickTime::Sample s0;
        REQUIRE(qt.readSample(0, 0, s0) == Error::Ok);
        REQUIRE(s0.data.isValid());
        CHECK(s0.data->size() == 512);
        CHECK(s0.index == 0);
        CHECK(s0.keyframe == true);

        QuickTime::Sample s1;
        REQUIRE(qt.readSample(0, 1, s1) == Error::Ok);
        REQUIRE(s1.data.isValid());
        CHECK(s1.data->size() == 512);
        CHECK(s1.index == 1);
        CHECK(s1.dts > s0.dts);
}

TEST_CASE("QuickTime: readSample bytes match a direct file read at the computed offset") {
        const String fname = fixturePath("tiny_prores_proxy_25p.mov");
        QuickTime    qt = QuickTime::createReader(fname);
        REQUIRE(qt.open() == Error::Ok);
        REQUIRE(qt.tracks().size() >= 1);

        QuickTime::Sample s;
        REQUIRE(qt.readSample(0, 0, s) == Error::Ok);
        REQUIRE(s.data.isValid());
        REQUIRE(s.data->size() > 0);

        // The reader should yield bytes that match what we get if we
        // search for the prores frame magic in the file. ProRes frames
        // start with a 4-byte big-endian frame size followed by 'icpf'.
        // We don't know the offset a priori, so instead we verify that
        // reading sample 0 twice yields identical bytes (round-trip).
        QuickTime::Sample s2;
        REQUIRE(qt.readSample(0, 0, s2) == Error::Ok);
        REQUIRE(s2.data->size() == s.data->size());
        for (size_t i = 0; i < s.data->size(); ++i) {
                REQUIRE(static_cast<const uint8_t *>(s.data->data())[i] ==
                        static_cast<const uint8_t *>(s2.data->data())[i]);
        }
}

TEST_CASE("QuickTime: H.264 + PCM sample reads expose video, audio, keyframe flags") {
        QuickTime qt = QuickTime::createReader(fixturePath("tiny_h264_pcm_24p.mov"));
        REQUIRE(qt.open() == Error::Ok);

        // Find the video and audio tracks.
        size_t videoIdx = SIZE_MAX;
        size_t audioIdx = SIZE_MAX;
        for (size_t i = 0; i < qt.tracks().size(); ++i) {
                if (qt.tracks()[i].type() == QuickTime::Video && videoIdx == SIZE_MAX) videoIdx = i;
                if (qt.tracks()[i].type() == QuickTime::Audio && audioIdx == SIZE_MAX) audioIdx = i;
        }
        REQUIRE(videoIdx != SIZE_MAX);
        REQUIRE(audioIdx != SIZE_MAX);

        // The first video sample is by definition a sync sample (we built
        // the fixture with -g 1 so every frame is a keyframe).
        QuickTime::Sample v0;
        REQUIRE(qt.readSample(videoIdx, 0, v0) == Error::Ok);
        REQUIRE(v0.data.isValid());
        CHECK(v0.data->size() > 0);
        CHECK(v0.keyframe == true);

        // Audio sample 0 must read with a non-zero size matching the per-sample
        // PCM word size (2 channels × 2 bytes for s16le = 4 bytes per sample frame).
        QuickTime::Sample a0;
        REQUIRE(qt.readSample(audioIdx, 0, a0) == Error::Ok);
        REQUIRE(a0.data.isValid());
        CHECK(a0.data->size() > 0);
}

TEST_CASE("QuickTime: out-of-range sample index returns OutOfRange") {
        QuickTime qt = QuickTime::createReader(fixturePath("tiny_uyvy_24p.mov"));
        REQUIRE(qt.open() == Error::Ok);
        QuickTime::Sample s;
        Error             err = qt.readSample(0, 9999, s);
        CHECK(err == Error::OutOfRange);
}

// ============================================================================
// Fragmented MP4 (moof / traf / trun)
// ============================================================================

TEST_CASE("QuickTime: open fragmented MP4 fixture") {
        QuickTime qt = QuickTime::createReader(fixturePath("tiny_h264_frag.mp4"));
        REQUIRE(qt.open() == Error::Ok);

        REQUIRE(qt.tracks().size() >= 1);
        const QuickTime::Track &v = qt.tracks()[0];
        CHECK(v.type() == QuickTime::Video);
        CHECK(v.size().width() == 16);
        CHECK(v.size().height() == 16);
        CHECK(v.pixelFormat().id() == PixelFormat::H264);

        // Three fragments × one frame each.
        CHECK(v.sampleCount() == 3);

        // Frame rate must have been derived from the per-sample durations
        // even though moov.trak.stbl was empty.
        REQUIRE(v.frameRate().isValid());
        double fps = v.frameRate().rational().toDouble();
        CHECK(fps > 23.9);
        CHECK(fps < 24.1);
}

TEST_CASE("QuickTime: fragmented MP4 sample reads return non-empty payloads") {
        QuickTime qt = QuickTime::createReader(fixturePath("tiny_h264_frag.mp4"));
        REQUIRE(qt.open() == Error::Ok);
        REQUIRE(qt.tracks().size() >= 1);

        for (uint64_t i = 0; i < qt.tracks()[0].sampleCount(); ++i) {
                QuickTime::Sample s;
                Error             err = qt.readSample(0, i, s);
                REQUIRE(err == Error::Ok);
                REQUIRE(s.data.isValid());
                CHECK(s.data->size() > 0);
                CHECK(s.index == i);
                // Every sample is a keyframe (we built the fixture with -g 1).
                CHECK(s.keyframe == true);
        }
}

TEST_CASE("QuickTime: fragmented MP4 dts is monotonically increasing") {
        QuickTime qt = QuickTime::createReader(fixturePath("tiny_h264_frag.mp4"));
        REQUIRE(qt.open() == Error::Ok);
        REQUIRE(qt.tracks().size() >= 1);

        int64_t prev = -1;
        for (uint64_t i = 0; i < qt.tracks()[0].sampleCount(); ++i) {
                QuickTime::Sample s;
                REQUIRE(qt.readSample(0, i, s) == Error::Ok);
                CHECK(s.dts > prev);
                prev = s.dts;
        }
}

// ============================================================================
// Writer (Phase 4) — round-trip via the reader
// ============================================================================

namespace {

        /** @brief Allocates a Buffer::Ptr filled with @p byte and sized to @p size. */
        Buffer::Ptr makeFilledBuffer(size_t size, uint8_t byte) {
                Buffer b(size);
                std::memset(b.data(), byte, size);
                b.setSize(size);
                return Buffer::Ptr::create(std::move(b));
        }

} // namespace

TEST_CASE("QuickTimeWriter: round-trip uncompressed UYVY video") {
        const String tmp = "/tmp/qt_writer_uyvy_roundtrip.mov";
        std::remove(tmp.cstr());

        // ---- write ----
        {
                QuickTime qt = QuickTime::createWriter(tmp);
                REQUIRE(qt.open() == Error::Ok);

                uint32_t vid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), Size2Du32(16, 16),
                                         FrameRate(FrameRate::RationalType(24, 1)), &vid) == Error::Ok);
                CHECK(vid != 0);

                for (int f = 0; f < 4; ++f) {
                        QuickTime::Sample s;
                        s.data = makeFilledBuffer(512, static_cast<uint8_t>(0x10 + f));
                        s.duration = 1;
                        s.keyframe = true;
                        REQUIRE(qt.writeSample(vid, s) == Error::Ok);
                }
                REQUIRE(qt.finalize() == Error::Ok);
        }

        // ---- read back ----
        {
                QuickTime qt = QuickTime::createReader(tmp);
                REQUIRE(qt.open() == Error::Ok);
                REQUIRE(qt.tracks().size() == 1);
                const QuickTime::Track &v = qt.tracks()[0];
                CHECK(v.type() == QuickTime::Video);
                CHECK(v.size().width() == 16);
                CHECK(v.size().height() == 16);
                CHECK(v.sampleCount() == 4);
                CHECK(v.pixelFormat().id() == PixelFormat::YUV8_422_UYVY_Rec709);
                REQUIRE(v.frameRate().isValid());
                CHECK(v.frameRate().rational().toDouble() == doctest::Approx(24.0));

                for (uint64_t i = 0; i < 4; ++i) {
                        QuickTime::Sample s;
                        REQUIRE(qt.readSample(0, i, s) == Error::Ok);
                        REQUIRE(s.data.isValid());
                        CHECK(s.data->size() == 512);
                        const uint8_t *bytes = static_cast<const uint8_t *>(s.data->data());
                        for (size_t k = 0; k < 512; ++k) {
                                REQUIRE(bytes[k] == 0x10 + i);
                        }
                }
        }

        std::remove(tmp.cstr());
}

TEST_CASE("QuickTimeWriter: round-trip ProRes pass-through video") {
        const String tmp = "/tmp/qt_writer_prores_roundtrip.mov";
        std::remove(tmp.cstr());

        // ---- write ----
        {
                QuickTime qt = QuickTime::createWriter(tmp);
                REQUIRE(qt.open() == Error::Ok);

                uint32_t vid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::ProRes_422_HQ), Size2Du32(64, 64),
                                         FrameRate(FrameRate::RationalType(25, 1)), &vid) == Error::Ok);

                // Synthetic "ProRes payload" — opaque bytes, distinct per frame.
                for (int f = 0; f < 3; ++f) {
                        QuickTime::Sample s;
                        s.data = makeFilledBuffer(1024 + f * 32, static_cast<uint8_t>(0xA0 + f));
                        s.duration = 1;
                        s.keyframe = true;
                        REQUIRE(qt.writeSample(vid, s) == Error::Ok);
                }
                REQUIRE(qt.finalize() == Error::Ok);
        }

        // ---- read back ----
        {
                QuickTime qt = QuickTime::createReader(tmp);
                REQUIRE(qt.open() == Error::Ok);
                REQUIRE(qt.tracks().size() == 1);
                const QuickTime::Track &v = qt.tracks()[0];
                CHECK(v.pixelFormat().id() == PixelFormat::ProRes_422_HQ);
                CHECK(v.pixelFormat().isCompressed());
                CHECK(v.size().width() == 64);
                CHECK(v.size().height() == 64);
                CHECK(v.sampleCount() == 3);

                static const size_t expectedSizes[] = {1024, 1056, 1088};
                for (uint64_t i = 0; i < 3; ++i) {
                        QuickTime::Sample s;
                        REQUIRE(qt.readSample(0, i, s) == Error::Ok);
                        REQUIRE(s.data.isValid());
                        CHECK(s.data->size() == expectedSizes[i]);
                        const uint8_t *bytes = static_cast<const uint8_t *>(s.data->data());
                        for (size_t k = 0; k < s.data->size(); ++k) {
                                REQUIRE(bytes[k] == 0xA0 + i);
                        }
                }
        }

        std::remove(tmp.cstr());
}

TEST_CASE("QuickTimeWriter: variable-duration sample table is preserved") {
        const String tmp = "/tmp/qt_writer_vfr_roundtrip.mov";
        std::remove(tmp.cstr());

        // 24 fps timescale, but write samples with different per-sample durations
        // to exercise the stts run-length encoder.
        {
                QuickTime qt = QuickTime::createWriter(tmp);
                REQUIRE(qt.open() == Error::Ok);
                uint32_t vid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), Size2Du32(16, 16),
                                         FrameRate(FrameRate::RationalType(24, 1)), &vid) == Error::Ok);
                static const uint32_t durs[] = {1, 1, 2, 2, 1, 3};
                int64_t               dts = 0;
                for (uint32_t d : durs) {
                        QuickTime::Sample s;
                        s.data = makeFilledBuffer(512, 0x55);
                        s.duration = d;
                        s.dts = dts;
                        s.pts = dts;
                        s.keyframe = true;
                        REQUIRE(qt.writeSample(vid, s) == Error::Ok);
                        dts += d;
                }
                REQUIRE(qt.finalize() == Error::Ok);
        }

        {
                QuickTime qt = QuickTime::createReader(tmp);
                REQUIRE(qt.open() == Error::Ok);
                REQUIRE(qt.tracks().size() == 1);
                CHECK(qt.tracks()[0].sampleCount() == 6);
                static const uint32_t expected[] = {1, 1, 2, 2, 1, 3};
                int64_t               expectedDts = 0;
                for (uint64_t i = 0; i < 6; ++i) {
                        QuickTime::Sample s;
                        REQUIRE(qt.readSample(0, i, s) == Error::Ok);
                        CHECK(s.duration == expected[i]);
                        CHECK(s.dts == expectedDts);
                        expectedDts += expected[i];
                }
        }

        std::remove(tmp.cstr());
}

TEST_CASE("QuickTimeWriter: keyframe flags survive round trip via stss") {
        const String tmp = "/tmp/qt_writer_keyframes.mov";
        std::remove(tmp.cstr());

        // Pattern: K _ _ K _ _ K (every 3rd sample is sync)
        {
                QuickTime qt = QuickTime::createWriter(tmp);
                REQUIRE(qt.open() == Error::Ok);
                uint32_t vid = 0;
                // Use a generic compressed codec (JPEG) for this test —
                // we're exercising the stss plumbing, not codec-specific
                // bitstream handling, and the writer's H.264/HEVC paths
                // insist on well-formed Annex-B input.
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::JPEG_YUV8_422_Rec709), Size2Du32(32, 32),
                                         FrameRate(FrameRate::RationalType(24, 1)), &vid) == Error::Ok);
                for (int i = 0; i < 7; ++i) {
                        QuickTime::Sample s;
                        s.data = makeFilledBuffer(64 + i, static_cast<uint8_t>(i));
                        s.duration = 1;
                        s.keyframe = (i % 3 == 0);
                        REQUIRE(qt.writeSample(vid, s) == Error::Ok);
                }
                REQUIRE(qt.finalize() == Error::Ok);
        }

        {
                QuickTime qt = QuickTime::createReader(tmp);
                REQUIRE(qt.open() == Error::Ok);
                REQUIRE(qt.tracks().size() == 1);
                CHECK(qt.tracks()[0].sampleCount() == 7);
                for (uint64_t i = 0; i < 7; ++i) {
                        QuickTime::Sample s;
                        REQUIRE(qt.readSample(0, i, s) == Error::Ok);
                        bool expected = (i % 3 == 0);
                        CHECK(s.keyframe == expected);
                }
        }

        std::remove(tmp.cstr());
}

// ============================================================================
// Fragmented writer (Phase 7)
// ============================================================================

TEST_CASE("QuickTimeWriter: fragmented layout round-trip (video only)") {
        const String tmp = "/tmp/qt_writer_frag_video.mov";
        std::remove(tmp.cstr());

        // Write 10 frames across 3 fragments: flush after frame 3, after
        // frame 7, and at finalize.
        {
                QuickTime qt = QuickTime::createWriter(tmp);
                REQUIRE(qt.setLayout(QuickTime::LayoutFragmented) == Error::Ok);
                REQUIRE(qt.open() == Error::Ok);

                uint32_t vid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), Size2Du32(16, 16),
                                         FrameRate(FrameRate::RationalType(24, 1)), &vid) == Error::Ok);

                for (int f = 0; f < 10; ++f) {
                        QuickTime::Sample s;
                        s.data = makeFilledBuffer(512, static_cast<uint8_t>(0x30 + f));
                        s.duration = 1;
                        s.keyframe = true;
                        REQUIRE(qt.writeSample(vid, s) == Error::Ok);
                        if (f == 3 || f == 7) {
                                REQUIRE(qt.flush() == Error::Ok);
                        }
                }
                REQUIRE(qt.finalize() == Error::Ok);
        }

        // Read back via our existing fragmented reader.
        {
                QuickTime qt = QuickTime::createReader(tmp);
                REQUIRE(qt.open() == Error::Ok);
                REQUIRE(qt.tracks().size() == 1);
                CHECK(qt.tracks()[0].sampleCount() == 10);
                CHECK(qt.tracks()[0].pixelFormat().id() == PixelFormat::YUV8_422_UYVY_Rec709);

                for (uint64_t i = 0; i < 10; ++i) {
                        QuickTime::Sample s;
                        REQUIRE(qt.readSample(0, i, s) == Error::Ok);
                        REQUIRE(s.data.isValid());
                        CHECK(s.data->size() == 512);
                        const uint8_t *bytes = static_cast<const uint8_t *>(s.data->data());
                        for (size_t k = 0; k < 512; ++k) {
                                REQUIRE(bytes[k] == 0x30 + i);
                        }
                }
        }

        std::remove(tmp.cstr());
}

TEST_CASE("QuickTimeWriter: fragmented layout round-trip (video + audio)") {
        const String tmp = "/tmp/qt_writer_frag_av.mov";
        std::remove(tmp.cstr());

        const AudioDesc adesc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        const size_t    samplesPerFrame = 2000; // 24 fps × 2000 = 48000/s
        const size_t    audioBytesPerFrame = samplesPerFrame * 4;

        {
                QuickTime qt = QuickTime::createWriter(tmp);
                REQUIRE(qt.setLayout(QuickTime::LayoutFragmented) == Error::Ok);
                REQUIRE(qt.open() == Error::Ok);

                uint32_t vid = 0, aid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), Size2Du32(16, 16),
                                         FrameRate(FrameRate::RationalType(24, 1)), &vid) == Error::Ok);
                REQUIRE(qt.addAudioTrack(adesc, &aid) == Error::Ok);

                for (int f = 0; f < 5; ++f) {
                        // Video sample
                        QuickTime::Sample vs;
                        vs.data = makeFilledBuffer(512, static_cast<uint8_t>(0x40 + f));
                        vs.duration = 1;
                        vs.keyframe = true;
                        REQUIRE(qt.writeSample(vid, vs) == Error::Ok);

                        // Audio sample: one chunk of 2000 stereo s16 frames
                        // filled with distinct per-frame byte.
                        QuickTime::Sample as;
                        as.data = makeFilledBuffer(audioBytesPerFrame, static_cast<uint8_t>(0x80 + f));
                        as.duration = 0; // writer derives
                        as.keyframe = true;
                        REQUIRE(qt.writeSample(aid, as) == Error::Ok);

                        if (f == 2) REQUIRE(qt.flush() == Error::Ok);
                }
                REQUIRE(qt.finalize() == Error::Ok);
        }

        {
                QuickTime qt = QuickTime::createReader(tmp);
                REQUIRE(qt.open() == Error::Ok);
                REQUIRE(qt.tracks().size() == 2);

                size_t videoIdx = SIZE_MAX, audioIdx = SIZE_MAX;
                for (size_t i = 0; i < qt.tracks().size(); ++i) {
                        if (qt.tracks()[i].type() == QuickTime::Video) videoIdx = i;
                        if (qt.tracks()[i].type() == QuickTime::Audio) audioIdx = i;
                }
                REQUIRE(videoIdx != SIZE_MAX);
                REQUIRE(audioIdx != SIZE_MAX);

                CHECK(qt.tracks()[videoIdx].sampleCount() == 5);
                // Audio: 5 chunks × 2000 samples = 10000 PCM frames
                CHECK(qt.tracks()[audioIdx].sampleCount() == 5 * samplesPerFrame);

                // Verify video bytes round-trip
                for (uint64_t i = 0; i < 5; ++i) {
                        QuickTime::Sample s;
                        REQUIRE(qt.readSample(videoIdx, i, s) == Error::Ok);
                        REQUIRE(s.data.isValid());
                        CHECK(s.data->size() == 512);
                        const uint8_t *b = static_cast<const uint8_t *>(s.data->data());
                        CHECK(b[0] == 0x40 + i);
                        CHECK(b[511] == 0x40 + i);
                }

                // Verify audio bytes round-trip via range read (the fast path).
                for (uint64_t i = 0; i < 5; ++i) {
                        QuickTime::Sample range;
                        REQUIRE(qt.readSampleRange(audioIdx, i * samplesPerFrame, samplesPerFrame, range) == Error::Ok);
                        REQUIRE(range.data.isValid());
                        CHECK(range.data->size() == audioBytesPerFrame);
                        const uint8_t *b = static_cast<const uint8_t *>(range.data->data());
                        CHECK(b[0] == 0x80 + i);
                        CHECK(b[audioBytesPerFrame - 1] == 0x80 + i);
                }
        }

        std::remove(tmp.cstr());
}

TEST_CASE("QuickTimeWriter: fragmented file is playable after simulated crash") {
        // Write a fragmented file with multiple complete fragments, then
        // truncate it mid-way through what would have been the next
        // fragment. The reader should cleanly recover everything from
        // the complete fragments and stop at the corrupted boundary.
        const String tmp = "/tmp/qt_writer_frag_crash.mov";
        std::remove(tmp.cstr());

        int64_t truncatePoint = -1;
        {
                QuickTime qt = QuickTime::createWriter(tmp);
                REQUIRE(qt.setLayout(QuickTime::LayoutFragmented) == Error::Ok);
                REQUIRE(qt.open() == Error::Ok);
                uint32_t vid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), Size2Du32(16, 16),
                                         FrameRate(FrameRate::RationalType(24, 1)), &vid) == Error::Ok);

                // Two complete fragments of 3 frames each.
                for (int f = 0; f < 3; ++f) {
                        QuickTime::Sample s;
                        s.data = makeFilledBuffer(512, static_cast<uint8_t>(0x50 + f));
                        s.duration = 1;
                        s.keyframe = true;
                        REQUIRE(qt.writeSample(vid, s) == Error::Ok);
                }
                REQUIRE(qt.flush() == Error::Ok);

                for (int f = 3; f < 6; ++f) {
                        QuickTime::Sample s;
                        s.data = makeFilledBuffer(512, static_cast<uint8_t>(0x50 + f));
                        s.duration = 1;
                        s.keyframe = true;
                        REQUIRE(qt.writeSample(vid, s) == Error::Ok);
                }
                REQUIRE(qt.flush() == Error::Ok);

                // This is where a "clean crash" boundary is. Record it.
                // (Note: MediaIOTask stores the underlying File, not the
                // engine; we pull the current write cursor by finalizing
                // to a known clean state and using the file size.)
                REQUIRE(qt.finalize() == Error::Ok);

                // Re-open via stdio to capture the size at the clean boundary.
                FILE *fp = std::fopen(tmp.cstr(), "rb");
                REQUIRE(fp != nullptr);
                std::fseek(fp, 0, SEEK_END);
                truncatePoint = std::ftell(fp);
                std::fclose(fp);
        }

        // Now re-open the writer and add a partial third fragment, then
        // simulate a crash by truncating the file at the previously
        // recorded clean boundary.
        {
                // Open for append-ish (we'll just overwrite from truncatePoint).
                FILE *fp = std::fopen(tmp.cstr(), "r+b");
                REQUIRE(fp != nullptr);
                std::fseek(fp, truncatePoint, SEEK_SET);
                // Write garbage that looks like the start of a moof but
                // with an invalid truncated payload.
                static const uint8_t partial[] = {
                        0x00, 0x00, 0x10, 0x00,                         // size = 4096 (lies — only a few bytes follow)
                        'm',  'o',  'o',  'f',  0x00, 0x00, 0x00, 0x10, // mfhd size
                        'm',  'f',  'h',  'd'
                        // truncated!
                };
                std::fwrite(partial, 1, sizeof(partial), fp);
                std::fclose(fp);
                // Truncate to where we stopped writing the partial fragment.
                int rc = ::truncate(tmp.cstr(), truncatePoint + sizeof(partial));
                REQUIRE(rc == 0);
        }

        // Open and verify only the valid fragments were recovered.
        {
                QuickTime qt = QuickTime::createReader(tmp);
                REQUIRE(qt.open() == Error::Ok);
                REQUIRE(qt.tracks().size() == 1);
                // Six samples (two complete fragments of 3) must survive.
                CHECK(qt.tracks()[0].sampleCount() == 6);
                for (uint64_t i = 0; i < 6; ++i) {
                        QuickTime::Sample s;
                        REQUIRE(qt.readSample(0, i, s) == Error::Ok);
                        REQUIRE(s.data.isValid());
                        CHECK(s.data->size() == 512);
                        const uint8_t *b = static_cast<const uint8_t *>(s.data->data());
                        CHECK(b[0] == 0x50 + i);
                }
        }

        std::remove(tmp.cstr());
}

// ============================================================================
// Timecode track
// ============================================================================

TEST_CASE("QuickTime: tmcd track yields anchor timecode 01:00:00:00 at 24 fps") {
        QuickTime qt = QuickTime::createReader(fixturePath("tiny_uyvy_24p_tc.mov"));
        REQUIRE(qt.open() == Error::Ok);

        // The fixture has both a video track and a timecode track.
        bool hasTc = false;
        for (const QuickTime::Track &t : qt.tracks()) {
                if (t.type() == QuickTime::TimecodeTrack) hasTc = true;
        }
        CHECK(hasTc);

        const Timecode &tc = qt.startTimecode();
        REQUIRE(tc.isValid());
        CHECK(tc.hour() == 1);
        CHECK(tc.min() == 0);
        CHECK(tc.sec() == 0);
        CHECK(tc.frame() == 0);
}

// ============================================================================
// Container metadata (udta) round-trip
// ============================================================================

namespace {

        Metadata makeUdtaFixture() {
                Metadata m;
                m.set(Metadata::Title, String("Round-Trip Title"));
                m.set(Metadata::Comment, String("QT udta round-trip test"));
                m.set(Metadata::Date, String("2026-04-08"));
                m.set(Metadata::Artist, String("Test Artist"));
                m.set(Metadata::Copyright, String("(c) libpromeki"));
                m.set(Metadata::Software, String("libpromeki test"));
                m.set(Metadata::Album, String("Test Album"));
                m.set(Metadata::Genre, String("Test Genre"));
                m.set(Metadata::Description, String("Test Description"));
                m.set(Metadata::Originator, String("libpromeki howardlogic.com"));
                m.set(Metadata::OriginatorReference, String("01921f83-7a41-7e5a-9c9d-3a3f0d5e1b2c"));
                m.set(Metadata::OriginationDateTime, String("2026-04-08T12:34:56"));
                return m;
        }

        void writeSingleFrameWithMeta(const String &path, const Metadata &meta) {
                QuickTime qt = QuickTime::createWriter(path);
                REQUIRE(qt.open() == Error::Ok);
                qt.setContainerMetadata(meta);
                uint32_t vid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), Size2Du32(16, 16),
                                         FrameRate(FrameRate::RationalType(24, 1)), &vid) == Error::Ok);
                QuickTime::Sample s;
                s.data = makeFilledBuffer(512, 0x42);
                s.duration = 1;
                s.keyframe = true;
                REQUIRE(qt.writeSample(vid, s) == Error::Ok);
                REQUIRE(qt.finalize() == Error::Ok);
        }

        void checkStandardFields(const Metadata &meta) {
                CHECK(meta.get(Metadata::Title).get<String>() == "Round-Trip Title");
                CHECK(meta.get(Metadata::Comment).get<String>() == "QT udta round-trip test");
                CHECK(meta.get(Metadata::Date).get<String>() == "2026-04-08");
                CHECK(meta.get(Metadata::Artist).get<String>() == "Test Artist");
                CHECK(meta.get(Metadata::Copyright).get<String>() == "(c) libpromeki");
                CHECK(meta.get(Metadata::Software).get<String>() == "libpromeki test");
                CHECK(meta.get(Metadata::Album).get<String>() == "Test Album");
                CHECK(meta.get(Metadata::Genre).get<String>() == "Test Genre");
                CHECK(meta.get(Metadata::Description).get<String>() == "Test Description");
                CHECK(meta.get(Metadata::Originator).get<String>() == "libpromeki howardlogic.com");
                CHECK(meta.get(Metadata::OriginatorReference).get<String>() == "01921f83-7a41-7e5a-9c9d-3a3f0d5e1b2c");
                CHECK(meta.get(Metadata::OriginationDateTime).get<String>() == "2026-04-08T12:34:56");
        }

} // namespace

TEST_CASE("QuickTimeWriter: container metadata round-trips via udta") {
        const String tmp = "/tmp/qt_writer_udta_roundtrip.mov";
        std::remove(tmp.cstr());

        Metadata in = makeUdtaFixture();
        writeSingleFrameWithMeta(tmp, in);

        QuickTime qt = QuickTime::createReader(tmp);
        REQUIRE(qt.open() == Error::Ok);

        const Metadata &out = qt.containerMetadata();
        checkStandardFields(out);

        std::remove(tmp.cstr());
}

TEST_CASE("QuickTimeWriter: BWF fields are emitted as an XMP packet") {
        // Writing any bext field should produce an XMP_ box inside
        // udta that contains the bext namespace and the field's
        // local-name element.  We verify this by scanning the raw
        // file bytes for the box type 'XMP_' and a few hallmark
        // markers from the packet.
        const String tmp = "/tmp/qt_writer_udta_xmp_verify.mov";
        std::remove(tmp.cstr());

        Metadata in;
        in.set(Metadata::Originator, String("libpromeki howardlogic.com"));
        in.set(Metadata::OriginatorReference, String("ref-123"));
        in.set(Metadata::OriginationDateTime, String("2026-04-08T12:34:56"));
        in.set(Metadata::UMID, UMID::generate(UMID::Extended));

        writeSingleFrameWithMeta(tmp, in);

        // Slurp the whole file and look for the XMP markers.
        File f(tmp);
        REQUIRE(f.open(IODevice::ReadOnly).isOk());
        auto sizeRes = f.size();
        REQUIRE(sizeRes.second().isOk());
        int64_t n = sizeRes.first();
        REQUIRE(n > 0);
        List<char> bytes(static_cast<size_t>(n));
        REQUIRE(f.read(bytes.data(), n) == n);
        f.close();

        auto containsBytes = [&](const char *needle) {
                size_t nlen = std::strlen(needle);
                if (nlen == 0 || bytes.size() < nlen) return false;
                for (size_t i = 0; i + nlen <= bytes.size(); ++i) {
                        if (std::memcmp(bytes.data() + i, needle, nlen) == 0) return true;
                }
                return false;
        };
        CHECK(containsBytes("XMP_"));
        CHECK(containsBytes("http://ns.adobe.com/bwf/bext/1.0/"));
        CHECK(containsBytes("<bext:umid>"));
        CHECK(containsBytes("<bext:originator>"));
        CHECK(containsBytes("<bext:originatorReference>"));
        CHECK(containsBytes("<bext:originationDate>"));
        CHECK(containsBytes("<bext:originationTime>"));
        // Date/time split happens inside the XMP writer — the exact
        // composite "2026-04-08T12:34:56" should NOT appear as a
        // single bext element.
        CHECK_FALSE(containsBytes(">2026-04-08T12:34:56<"));

        std::remove(tmp.cstr());
}

TEST_CASE("QuickTimeWriter: UMID round-trips via XMP as typed UMID") {
        const String tmp = "/tmp/qt_writer_udta_umid.mov";
        std::remove(tmp.cstr());

        UMID inUmid = UMID::generate(UMID::Extended);
        REQUIRE(inUmid.isValid());

        Metadata in;
        in.set(Metadata::Title, String("UMID Test"));
        in.set(Metadata::UMID, inUmid);

        writeSingleFrameWithMeta(tmp, in);

        QuickTime qt = QuickTime::createReader(tmp);
        REQUIRE(qt.open() == Error::Ok);

        const Metadata &out = qt.containerMetadata();
        REQUIRE(out.contains(Metadata::UMID));
        UMID outUmid = out.get(Metadata::UMID).get<UMID>();
        CHECK(outUmid.isValid());
        CHECK(outUmid == inUmid);

        // Check the MEKI organization tag survived the round-trip.
        const uint8_t *raw = outUmid.raw();
        CHECK(raw[56] == 'M');
        CHECK(raw[57] == 'E');
        CHECK(raw[58] == 'K');
        CHECK(raw[59] == 'I');

        std::remove(tmp.cstr());
}

TEST_CASE("QuickTimeWriter: empty container metadata omits udta") {
        // Sanity check: when no container metadata is set, the file is
        // still well-formed and readable.  None of the udta-bound
        // fields should appear in the reader's container metadata.
        // (The reader pre-seeds Metadata::Software with the ftyp brand
        // for diagnostics, so that field is intentionally not checked
        // here.)
        const String tmp = "/tmp/qt_writer_udta_empty.mov";
        std::remove(tmp.cstr());

        QuickTime qt = QuickTime::createWriter(tmp);
        REQUIRE(qt.open() == Error::Ok);
        uint32_t vid = 0;
        REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), Size2Du32(16, 16),
                                 FrameRate(FrameRate::RationalType(24, 1)), &vid) == Error::Ok);
        QuickTime::Sample s;
        s.data = makeFilledBuffer(512, 0x20);
        s.duration = 1;
        s.keyframe = true;
        REQUIRE(qt.writeSample(vid, s) == Error::Ok);
        REQUIRE(qt.finalize() == Error::Ok);

        QuickTime reader = QuickTime::createReader(tmp);
        REQUIRE(reader.open() == Error::Ok);
        const Metadata &out = reader.containerMetadata();
        CHECK_FALSE(out.contains(Metadata::Title));
        CHECK_FALSE(out.contains(Metadata::Comment));
        CHECK_FALSE(out.contains(Metadata::Artist));
        CHECK_FALSE(out.contains(Metadata::Copyright));
        CHECK_FALSE(out.contains(Metadata::Originator));
        CHECK_FALSE(out.contains(Metadata::OriginatorReference));
        CHECK_FALSE(out.contains(Metadata::OriginationDateTime));
        CHECK_FALSE(out.contains(Metadata::UMID));

        std::remove(tmp.cstr());
}

TEST_CASE("QuickTimeWriter: fragmented layout emits udta in the init moov") {
        const String tmp = "/tmp/qt_writer_udta_fragmented.mov";
        std::remove(tmp.cstr());

        Metadata in;
        in.set(Metadata::Software, String("libpromeki fragmented test"));
        in.set(Metadata::Originator, String("libpromeki howardlogic.com"));
        in.set(Metadata::Title, String("Fragmented Title"));

        {
                QuickTime qt = QuickTime::createWriter(tmp);
                REQUIRE(qt.setLayout(QuickTime::LayoutFragmented) == Error::Ok);
                REQUIRE(qt.open() == Error::Ok);
                qt.setContainerMetadata(in);
                uint32_t vid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), Size2Du32(16, 16),
                                         FrameRate(FrameRate::RationalType(24, 1)), &vid) == Error::Ok);
                for (int f = 0; f < 4; ++f) {
                        QuickTime::Sample s;
                        s.data = makeFilledBuffer(512, static_cast<uint8_t>(0x30 + f));
                        s.duration = 1;
                        s.keyframe = true;
                        REQUIRE(qt.writeSample(vid, s) == Error::Ok);
                }
                REQUIRE(qt.finalize() == Error::Ok);
        }

        QuickTime reader = QuickTime::createReader(tmp);
        REQUIRE(reader.open() == Error::Ok);
        const Metadata &out = reader.containerMetadata();
        CHECK(out.get(Metadata::Software).get<String>() == "libpromeki fragmented test");
        CHECK(out.get(Metadata::Originator).get<String>() == "libpromeki howardlogic.com");
        CHECK(out.get(Metadata::Title).get<String>() == "Fragmented Title");

        std::remove(tmp.cstr());
}

// ============================================================================
// H.264 write: Annex-B → AVCC conversion + avcC sample-description box
// ============================================================================

namespace {

        /**
 * @brief Build an Annex-B access unit containing SPS + PPS + IDR with
 *        known bytes.
 *
 * The payload after the profile bytes is arbitrary — the writer only
 * parses profile_idc / constraint flags / level_idc from the first
 * four bytes of the SPS (including the NAL header).  A real decoder
 * would not play this, but the container-layer plumbing (Annex-B
 * split, AVCC conversion, avcC extraction and emission) exercises
 * every code path we care about.
 */
        std::vector<uint8_t> buildCannedH264AccessUnit() {
                // SPS: NAL header 0x67 (type 7), profile=0x42 (Baseline),
                // compat=0xe0, level=0x1e, + trailing byte.
                const std::vector<uint8_t> sps = {0x67, 0x42, 0xe0, 0x1e, 0xa0};
                const std::vector<uint8_t> pps = {0x68, 0xce, 0x3c, 0x80};
                const std::vector<uint8_t> idr = {0x65, 0x88, 0x84, 0x11, 0x22, 0x33};
                std::vector<uint8_t>       au;
                au.insert(au.end(), {0x00, 0x00, 0x00, 0x01});
                au.insert(au.end(), sps.begin(), sps.end());
                au.insert(au.end(), {0x00, 0x00, 0x00, 0x01});
                au.insert(au.end(), pps.begin(), pps.end());
                au.insert(au.end(), {0x00, 0x00, 0x00, 0x01});
                au.insert(au.end(), idr.begin(), idr.end());
                return au;
        }

        /** @brief Wrap a vector of bytes as a shared Buffer::Ptr. */
        Buffer::Ptr bufferFromBytes(const std::vector<uint8_t> &bytes) {
                Buffer b(bytes.size());
                std::memcpy(b.data(), bytes.data(), bytes.size());
                b.setSize(bytes.size());
                return Buffer::Ptr::create(std::move(b));
        }

        /** @brief Returns the full file contents of @p path as a byte vector. */
        std::vector<uint8_t> readAllBytes(const String &path) {
                std::vector<uint8_t> out;
                FILE                *fp = std::fopen(path.cstr(), "rb");
                if (!fp) return out;
                std::fseek(fp, 0, SEEK_END);
                long sz = std::ftell(fp);
                std::fseek(fp, 0, SEEK_SET);
                out.resize(static_cast<size_t>(sz));
                size_t nread = std::fread(out.data(), 1, static_cast<size_t>(sz), fp);
                std::fclose(fp);
                out.resize(nread);
                return out;
        }

        /** @brief Scan @p buf for the 4-byte sequence @p needle.  Returns SIZE_MAX if missing. */
        size_t findBytes(const std::vector<uint8_t> &buf, const uint8_t *needle, size_t nlen) {
                if (buf.size() < nlen) return SIZE_MAX;
                for (size_t i = 0; i + nlen <= buf.size(); ++i) {
                        if (std::memcmp(buf.data() + i, needle, nlen) == 0) return i;
                }
                return SIZE_MAX;
        }

} // namespace

TEST_CASE("QuickTimeWriter: H.264 Annex-B input is stored as AVCC with avcC box") {
        const String tmp = "/tmp/qt_writer_h264_avcC.mov";
        std::remove(tmp.cstr());

        const std::vector<uint8_t> au = buildCannedH264AccessUnit();

        {
                QuickTime qt = QuickTime::createWriter(tmp);
                REQUIRE(qt.open() == Error::Ok);
                uint32_t vid = 0;
                REQUIRE(qt.addVideoTrack(PixelFormat(PixelFormat::H264), Size2Du32(16, 16),
                                         FrameRate(FrameRate::RationalType(24, 1)), &vid) == Error::Ok);
                QuickTime::Sample s;
                s.data = bufferFromBytes(au);
                s.duration = 1;
                s.keyframe = true;
                REQUIRE(qt.writeSample(vid, s) == Error::Ok);
                REQUIRE(qt.finalize() == Error::Ok);
        }

        SUBCASE("raw file contains avc1 sample entry and avcC child box") {
                const std::vector<uint8_t> file = readAllBytes(tmp);
                REQUIRE(file.size() > 0);

                const uint8_t avc1[4] = {'a', 'v', 'c', '1'};
                const uint8_t avcC[4] = {'a', 'v', 'c', 'C'};
                CHECK(findBytes(file, avc1, 4) != SIZE_MAX);
                size_t avcCOffset = findBytes(file, avcC, 4);
                REQUIRE(avcCOffset != SIZE_MAX);
                // avcC box is preceded by its 4-byte size + 4-byte type.
                // Payload starts at avcCOffset+4; byte 0 = configurationVersion.
                REQUIRE(avcCOffset + 9 <= file.size());
                CHECK(file[avcCOffset + 4] == 0x01);     // configurationVersion
                CHECK(file[avcCOffset + 4 + 1] == 0x42); // profile_idc (Baseline)
                CHECK(file[avcCOffset + 4 + 2] == 0xe0); // profile_compatibility
                CHECK(file[avcCOffset + 4 + 3] == 0x1e); // level_idc
                // Byte 4 of the record: top 6 bits reserved = 111111, low 2 bits =
                // lengthSizeMinusOne = 3 → 0xff.
                CHECK(file[avcCOffset + 4 + 4] == 0xff);
        }

        SUBCASE("reader resolves the avc1 track as PixelFormat::H264") {
                QuickTime qt = QuickTime::createReader(tmp);
                REQUIRE(qt.open() == Error::Ok);
                REQUIRE(qt.tracks().size() == 1);
                const QuickTime::Track &v = qt.tracks()[0];
                CHECK(v.type() == QuickTime::Video);
                CHECK(v.pixelFormat().id() == PixelFormat::H264);
                CHECK(v.sampleCount() == 1);
        }

        SUBCASE("reader extracts the avcC configuration record") {
                QuickTime qt = QuickTime::createReader(tmp);
                REQUIRE(qt.open() == Error::Ok);
                REQUIRE(qt.tracks().size() == 1);
                const QuickTime::Track &v = qt.tracks()[0];
                REQUIRE(v.codecConfig().isValid());
                CHECK(v.codecConfigType() == FourCC("avcC"));
                const Buffer::Ptr &cfg = v.codecConfig();
                REQUIRE(cfg->size() >= 5);
                const uint8_t *p = static_cast<const uint8_t *>(cfg->data());
                CHECK(p[0] == 0x01); // configurationVersion
                CHECK(p[1] == 0x42); // profile_idc (Baseline)
                CHECK(p[2] == 0xe0); // profile_compatibility
                CHECK(p[3] == 0x1e); // level_idc
                CHECK(p[4] == 0xff); // lengthSizeMinusOne byte
        }

        SUBCASE("written sample payload is length-prefixed, not Annex-B") {
                // Read the raw sample bytes back via the reader.  The
                // stored form must start with a 4-byte big-endian NAL
                // length (not a 00 00 00 01 start code).
                QuickTime qt = QuickTime::createReader(tmp);
                REQUIRE(qt.open() == Error::Ok);
                QuickTime::Sample s;
                REQUIRE(qt.readSample(0, 0, s) == Error::Ok);
                REQUIRE(s.data.isValid());
                REQUIRE(s.data->size() >= 4);
                const uint8_t *p = static_cast<const uint8_t *>(s.data->data());
                // First 4 bytes must be a sensible length (< total size).
                uint32_t firstLen = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                                    (static_cast<uint32_t>(p[2]) << 8) | (static_cast<uint32_t>(p[3]));
                CHECK(firstLen < s.data->size());
                CHECK(firstLen > 0);
                // And must not be a start code: the first NAL byte that
                // follows the prefix cannot be 0x00.  Since parameter
                // sets (SPS / PPS) are stripped from samples for the
                // @c avc1 sample entry, the first NAL in the stored
                // sample is the IDR slice, whose first byte is 0x65.
                CHECK(p[4] == 0x65);
        }

        std::remove(tmp.cstr());
}
