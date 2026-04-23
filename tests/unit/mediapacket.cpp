/**
 * @file      tests/mediapacket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Exercises the abstract @ref MediaPacket base and its concrete
 * subclasses @ref VideoPacket and @ref AudioPacket.
 */

#include <doctest/doctest.h>
#include <promeki/mediapacket.h>
#include <promeki/videopacket.h>
#include <promeki/audiopacket.h>
#include <promeki/audiocodec.h>
#include <promeki/mediaconfig.h>
#include <promeki/bufferview.h>
#include <promeki/clockdomain.h>
#include <promeki/metadata.h>
#include <promeki/timestamp.h>

using namespace promeki;

TEST_CASE("VideoPacket: default-constructed packet is invalid") {
        VideoPacket pkt;
        CHECK_FALSE(pkt.isValid());
        CHECK(pkt.size() == 0);
        CHECK(pkt.flags() == 0);
        CHECK_FALSE(pkt.isKeyframe());
        CHECK_FALSE(pkt.isEndOfStream());
        CHECK(pkt.kind() == MediaPacket::Video);
}

TEST_CASE("VideoPacket: buffer + pixel desc round-trips") {
        auto buf = Buffer::Ptr::create(64);
        buf.modify()->setSize(32);
        VideoPacket pkt(buf, PixelFormat(PixelFormat::H264));

        CHECK(pkt.isValid());
        CHECK(pkt.size() == 32);
        CHECK(pkt.pixelFormat().id() == PixelFormat::H264);
        CHECK(pkt.pixelFormat().videoCodec().id() == VideoCodec::H264);
        CHECK(pkt.buffer() == buf);
}

TEST_CASE("VideoPacket: flag manipulation") {
        VideoPacket pkt;
        CHECK_FALSE(pkt.hasFlag(VideoPacket::Keyframe));

        pkt.addFlag(VideoPacket::Keyframe);
        CHECK(pkt.isKeyframe());
        CHECK(pkt.flags() == VideoPacket::Keyframe);

        pkt.markEndOfStream();
        CHECK(pkt.isKeyframe());
        CHECK(pkt.isEndOfStream());

        pkt.removeFlag(VideoPacket::Keyframe);
        CHECK_FALSE(pkt.isKeyframe());
        CHECK(pkt.isEndOfStream());

        pkt.setFlags(VideoPacket::ParameterSet | VideoPacket::Discardable);
        CHECK(pkt.isParameterSet());
        CHECK(pkt.hasFlag(VideoPacket::Discardable));
        CHECK_FALSE(pkt.isKeyframe());
}

TEST_CASE("MediaPacket: timestamps and duration") {
        VideoPacket pkt;
        ClockDomain dom(ClockDomain::SystemMonotonic);
        TimeStamp now = TimeStamp::now();
        MediaTimeStamp pts(now, dom);
        MediaTimeStamp dts(now, dom, Duration::fromMilliseconds(-100));

        pkt.setPts(pts);
        pkt.setDts(dts);
        pkt.setDuration(Duration::fromMilliseconds(33));

        CHECK(pkt.pts() == pts);
        CHECK(pkt.dts() == dts);
        CHECK(pkt.duration() == Duration::fromMilliseconds(33));
}

TEST_CASE("MediaPacket: corruption markers live in metadata") {
        VideoPacket pkt(Buffer::Ptr::create(4), PixelFormat(PixelFormat::H264));
        CHECK_FALSE(pkt.isCorrupt());
        CHECK(pkt.corruptReason().isEmpty());

        pkt.markCorrupt(String("checksum mismatch"));
        CHECK(pkt.isCorrupt());
        CHECK(pkt.corruptReason() == "checksum mismatch");
        CHECK(pkt.metadata().getAs<bool>(Metadata::Corrupt));
        CHECK(pkt.metadata().getAs<String>(Metadata::CorruptReason)
                == "checksum mismatch");
}

TEST_CASE("MediaConfig: video rate-control keys registered") {
        // Each new key must round-trip its default through spec() and
        // survive set()/getAs() like any other MediaConfig knob.  This
        // test doubles as a canary for the VariantSpec declarations.
        CHECK(MediaConfig::spec(MediaConfig::BitrateKbps) != nullptr);
        CHECK(MediaConfig::spec(MediaConfig::MaxBitrateKbps) != nullptr);
        CHECK(MediaConfig::spec(MediaConfig::VideoRcMode) != nullptr);
        CHECK(MediaConfig::spec(MediaConfig::GopLength) != nullptr);
        CHECK(MediaConfig::spec(MediaConfig::IdrInterval) != nullptr);
        CHECK(MediaConfig::spec(MediaConfig::BFrames) != nullptr);
        CHECK(MediaConfig::spec(MediaConfig::LookaheadFrames) != nullptr);
        CHECK(MediaConfig::spec(MediaConfig::VideoPreset) != nullptr);
        CHECK(MediaConfig::spec(MediaConfig::VideoProfile) != nullptr);
        CHECK(MediaConfig::spec(MediaConfig::VideoLevel) != nullptr);
        CHECK(MediaConfig::spec(MediaConfig::VideoQp) != nullptr);

        MediaConfig cfg;
        cfg.set(MediaConfig::BitrateKbps, int32_t(10000));
        cfg.set(MediaConfig::VideoRcMode, RateControlMode::CBR);
        cfg.set(MediaConfig::GopLength, int32_t(30));
        cfg.set(MediaConfig::VideoPreset, VideoEncoderPreset::LowLatency);
        cfg.set(MediaConfig::VideoProfile, String("high"));

        CHECK(cfg.getAs<int32_t>(MediaConfig::BitrateKbps) == 10000);
        CHECK(cfg.getAs<Enum>(MediaConfig::VideoRcMode)
                == RateControlMode::CBR);
        CHECK(cfg.getAs<int32_t>(MediaConfig::GopLength) == 30);
        CHECK(cfg.getAs<Enum>(MediaConfig::VideoPreset)
                == VideoEncoderPreset::LowLatency);
        CHECK(cfg.getAs<String>(MediaConfig::VideoProfile) == "high");
}

TEST_CASE("VideoPacket: multiple packets share one Buffer via BufferView slices") {
        // Simulates an NVENC access unit carrying three concatenated
        // NAL units (SPS / PPS / IDR slice) in one locked bitstream
        // buffer.  We build the AU once, then create three packets
        // that are non-overlapping views into it — no extra copies,
        // one shared underlying allocation.
        constexpr size_t kSpsLen = 16;
        constexpr size_t kPpsLen = 8;
        constexpr size_t kIdrLen = 40;
        constexpr size_t kTotal  = kSpsLen + kPpsLen + kIdrLen;

        auto au = Buffer::Ptr::create(kTotal);
        auto *bytes = static_cast<uint8_t *>(au.modify()->data());
        for(size_t i = 0; i < kSpsLen; ++i)           bytes[i] = 0x01;
        for(size_t i = 0; i < kPpsLen; ++i)           bytes[kSpsLen + i] = 0x02;
        for(size_t i = 0; i < kIdrLen; ++i)           bytes[kSpsLen + kPpsLen + i] = 0x03;
        au.modify()->setSize(kTotal);

        VideoPacket sps(BufferView(au, 0,                 kSpsLen),
                        PixelFormat(PixelFormat::H264));
        VideoPacket pps(BufferView(au, kSpsLen,           kPpsLen),
                        PixelFormat(PixelFormat::H264));
        VideoPacket idr(BufferView(au, kSpsLen + kPpsLen, kIdrLen),
                        PixelFormat(PixelFormat::H264));
        sps.addFlag(VideoPacket::ParameterSet);
        pps.addFlag(VideoPacket::ParameterSet);
        idr.addFlag(VideoPacket::Keyframe);

        // All three views reference the same underlying Buffer.
        CHECK(sps.buffer() == au);
        CHECK(pps.buffer() == au);
        CHECK(idr.buffer() == au);

        // Sizes and data pointers all line up non-overlapping.
        CHECK(sps.size() == kSpsLen);
        CHECK(pps.size() == kPpsLen);
        CHECK(idr.size() == kIdrLen);
        CHECK(sps.view().data() + kSpsLen == pps.view().data());
        CHECK(pps.view().data() + kPpsLen == idr.view().data());

        // Bytes through each view match what we wrote into the source
        // buffer, confirming no intermediate copy happened.
        CHECK(sps.view().data()[0] == 0x01);
        CHECK(pps.view().data()[0] == 0x02);
        CHECK(idr.view().data()[0] == 0x03);
}

TEST_CASE("VideoPacket: Buffer::Ptr ctor wraps the whole buffer as a view") {
        auto buf = Buffer::Ptr::create(128);
        buf.modify()->setSize(100);
        VideoPacket pkt(buf, PixelFormat(PixelFormat::HEVC));
        CHECK(pkt.isValid());
        CHECK(pkt.size() == 100);
        CHECK(pkt.view().offset() == 0);
        CHECK(pkt.view().size() == 100);
        CHECK(pkt.buffer() == buf);
}

TEST_CASE("VideoPacket: shared ownership via Ptr") {
        auto pkt = VideoPacket::Ptr::create(
                Buffer::Ptr::create(16), PixelFormat(PixelFormat::HEVC));
        pkt.modify()->addFlag(VideoPacket::Keyframe);

        VideoPacket::Ptr alias = pkt;
        CHECK(alias == pkt);
        CHECK(alias->isKeyframe());
        CHECK(alias->pixelFormat().id() == PixelFormat::HEVC);
}

TEST_CASE("MediaPacket::Ptr: implicit upcast from VideoPacket::Ptr") {
        VideoPacket::Ptr vp = VideoPacket::Ptr::create(
                Buffer::Ptr::create(8), PixelFormat(PixelFormat::H264));
        vp.modify()->addFlag(VideoPacket::Keyframe);

        // Cross-type conversion — no clone, same underlying object.
        MediaPacket::Ptr mp = vp;
        REQUIRE(mp.isValid());
        CHECK(mp->kind() == MediaPacket::Video);
        CHECK(mp.referenceCount() == 2);  // vp and mp share the object
}

TEST_CASE("sharedPointerCast: downcasts from base to derived") {
        MediaPacket::Ptr mp = VideoPacket::Ptr::create(
                Buffer::Ptr::create(8), PixelFormat(PixelFormat::H264));
        REQUIRE(mp.isValid());

        auto vp = sharedPointerCast<VideoPacket>(mp);
        REQUIRE(vp.isValid());
        CHECK(vp->pixelFormat().id() == PixelFormat::H264);

        // Wrong-type downcast returns an empty Ptr.
        auto ap = sharedPointerCast<AudioPacket>(mp);
        CHECK(ap.isNull());
}

// ---------------------------------------------------------------
// AudioPacket coverage
// ---------------------------------------------------------------

TEST_CASE("AudioPacket: default-constructed packet is invalid") {
        AudioPacket pkt;
        CHECK_FALSE(pkt.isValid());
        CHECK(pkt.size() == 0);
        CHECK(pkt.kind() == MediaPacket::Audio);
        CHECK_FALSE(pkt.audioCodec().isValid());
        CHECK_FALSE(pkt.isEndOfStream());
        CHECK_FALSE(pkt.isCorrupt());
}

TEST_CASE("AudioPacket: buffer + codec round-trips") {
        auto buf = Buffer::Ptr::create(32);
        buf.modify()->setSize(24);
        AudioPacket pkt(buf, AudioCodec(AudioCodec::Opus));

        CHECK(pkt.isValid());
        CHECK(pkt.size() == 24);
        CHECK(pkt.audioCodec().id() == AudioCodec::Opus);
        CHECK(pkt.buffer() == buf);
        CHECK(pkt.view().offset() == 0);
        CHECK(pkt.view().size() == 24);
}

TEST_CASE("AudioPacket: BufferView constructor preserves the view") {
        auto buf = Buffer::Ptr::create(64);
        buf.modify()->setSize(64);
        // Middle 32 bytes of the backing buffer.
        BufferView slice(buf, 16, 32);
        AudioPacket pkt(slice, AudioCodec(AudioCodec::Opus));

        CHECK(pkt.isValid());
        CHECK(pkt.size() == 32);
        CHECK(pkt.view().offset() == 16);
        CHECK(pkt.view().size() == 32);
        CHECK(pkt.buffer() == buf);
}

TEST_CASE("AudioPacket: invalid when codec is set but buffer is absent") {
        // isValid() must reject a packet whose view is empty even if
        // the codec identity has been set — the bytes-first contract
        // inherited from the base plus the audio-codec check.
        AudioPacket pkt;
        pkt.setAudioCodec(AudioCodec(AudioCodec::Opus));
        CHECK(pkt.audioCodec().isValid());
        CHECK_FALSE(pkt.isValid());
}

TEST_CASE("AudioPacket: setAudioCodec mutates identity without touching bytes") {
        auto buf = Buffer::Ptr::create(16);
        buf.modify()->setSize(16);
        AudioPacket pkt(buf, AudioCodec());
        CHECK_FALSE(pkt.isValid());  // codec still invalid
        CHECK(pkt.buffer() == buf);

        pkt.setAudioCodec(AudioCodec(AudioCodec::Opus));
        CHECK(pkt.isValid());
        CHECK(pkt.audioCodec().id() == AudioCodec::Opus);
        CHECK(pkt.size() == 16);
}

TEST_CASE("AudioPacket: shared ownership via Ptr") {
        auto pkt = AudioPacket::Ptr::create(
                Buffer::Ptr::create(16), AudioCodec(AudioCodec::Opus));
        AudioPacket::Ptr alias = pkt;
        CHECK(alias == pkt);
        CHECK(alias->audioCodec().id() == AudioCodec::Opus);
}

TEST_CASE("MediaPacket::Ptr: implicit upcast from AudioPacket::Ptr") {
        AudioPacket::Ptr ap = AudioPacket::Ptr::create(
                Buffer::Ptr::create(8), AudioCodec(AudioCodec::Opus));
        MediaPacket::Ptr mp = ap;
        REQUIRE(mp.isValid());
        CHECK(mp->kind() == MediaPacket::Audio);
        // The two Ptrs share one underlying object.
        CHECK(mp.referenceCount() == 2);
}

TEST_CASE("sharedPointerCast: downcasts MediaPacket::Ptr to AudioPacket") {
        MediaPacket::Ptr mp = AudioPacket::Ptr::create(
                Buffer::Ptr::create(8), AudioCodec(AudioCodec::Opus));
        REQUIRE(mp.isValid());

        auto ap = sharedPointerCast<AudioPacket>(mp);
        REQUIRE(ap.isValid());
        CHECK(ap->audioCodec().id() == AudioCodec::Opus);

        // The cross-kind downcast fails cleanly.
        auto vp = sharedPointerCast<VideoPacket>(mp);
        CHECK(vp.isNull());
}

// ---------------------------------------------------------------
// MediaPacket base coverage (reached via the concrete subclasses)
// ---------------------------------------------------------------

TEST_CASE("MediaPacket: setBuffer(null) clears the payload") {
        auto buf = Buffer::Ptr::create(16);
        buf.modify()->setSize(16);
        VideoPacket pkt(buf, PixelFormat(PixelFormat::H264));
        CHECK(pkt.isValid());

        pkt.setBuffer(Buffer::Ptr());
        CHECK_FALSE(pkt.view().isValid());
        CHECK(pkt.size() == 0);
        CHECK_FALSE(pkt.isValid());
}

TEST_CASE("MediaPacket: setView replaces the payload view") {
        auto first  = Buffer::Ptr::create(16);
        auto second = Buffer::Ptr::create(32);
        first.modify()->setSize(16);
        second.modify()->setSize(32);

        VideoPacket pkt(first, PixelFormat(PixelFormat::H264));
        REQUIRE(pkt.buffer() == first);

        pkt.setView(BufferView(second, 0, second->size()));
        CHECK(pkt.buffer() == second);
        CHECK(pkt.size() == 32);
        CHECK(pkt.isValid());
}

TEST_CASE("MediaPacket: markEndOfStream(false) clears the flag") {
        VideoPacket pkt(Buffer::Ptr::create(4), PixelFormat(PixelFormat::H264));
        pkt.markEndOfStream();
        REQUIRE(pkt.isEndOfStream());
        pkt.markEndOfStream(false);
        CHECK_FALSE(pkt.isEndOfStream());
}

TEST_CASE("MediaPacket: markCorrupt with empty reason leaves CorruptReason unset") {
        // Contract: the reason string is only recorded when it is
        // non-empty.  Passing an empty string still flips the Corrupt
        // flag but must not overwrite (or introduce) a reason key.
        VideoPacket pkt(Buffer::Ptr::create(4), PixelFormat(PixelFormat::H264));
        pkt.markCorrupt();
        CHECK(pkt.isCorrupt());
        CHECK(pkt.corruptReason().isEmpty());
        CHECK_FALSE(pkt.metadata().contains(Metadata::CorruptReason));
}

TEST_CASE("MediaPacket: mutable metadata() lets callers attach arbitrary keys") {
        VideoPacket pkt(Buffer::Ptr::create(4), PixelFormat(PixelFormat::H264));
        pkt.metadata().set(Metadata::EndOfStream, true);
        CHECK(pkt.isEndOfStream());
        // const overload returns the same state.
        const VideoPacket &cpkt = pkt;
        CHECK(cpkt.metadata().getAs<bool>(Metadata::EndOfStream));
}

TEST_CASE("VideoPacket: BufferView-only constructor leaves pixel format empty") {
        // The BufferView constructor takes a PixelFormat alongside, but
        // the base MediaPacket(BufferView) constructor is still reachable
        // via derived types that want to set the format separately —
        // exercise that path to make sure isValid() stays tight.
        auto buf = Buffer::Ptr::create(4);
        buf.modify()->setSize(4);
        VideoPacket pkt(BufferView(buf, 0, 4), PixelFormat());
        CHECK_FALSE(pkt.isValid());

        pkt.setPixelFormat(PixelFormat(PixelFormat::H264));
        CHECK(pkt.isValid());
}

TEST_CASE("VideoPacket: clone produces an independent copy with derived type") {
        auto orig = VideoPacket::Ptr::create(
                Buffer::Ptr::create(16), PixelFormat(PixelFormat::HEVC));
        orig.modify()->addFlag(VideoPacket::Keyframe);
        orig.modify()->markCorrupt(String("bad slice"));

        // Copy-on-write via modify() on a shared Ptr triggers the
        // virtual _promeki_clone() path — the result must still be a
        // VideoPacket (not a sliced MediaPacket) with all fields
        // preserved.
        VideoPacket::Ptr alias = orig;
        alias.modify()->setPts(MediaTimeStamp(
                TimeStamp::now(), ClockDomain(ClockDomain::SystemMonotonic)));

        // Both Ptrs carry their own distinct state now.
        CHECK(orig->kind() == MediaPacket::Video);
        CHECK(alias->kind() == MediaPacket::Video);
        CHECK(alias->isKeyframe());
        CHECK(alias->isCorrupt());
        CHECK(alias->corruptReason() == "bad slice");
        CHECK(alias->pixelFormat().id() == PixelFormat::HEVC);
}
