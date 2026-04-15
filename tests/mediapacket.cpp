/**
 * @file      tests/mediapacket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <doctest/doctest.h>
#include <promeki/mediapacket.h>
#include <promeki/mediaconfig.h>
#include <promeki/bufferview.h>
#include <promeki/clockdomain.h>
#include <promeki/timestamp.h>

using namespace promeki;

TEST_CASE("MediaPacket: default-constructed packet is invalid") {
        MediaPacket pkt;
        CHECK_FALSE(pkt.isValid());
        CHECK(pkt.size() == 0);
        CHECK(pkt.flags() == 0);
        CHECK_FALSE(pkt.isKeyframe());
        CHECK_FALSE(pkt.isEndOfStream());
}

TEST_CASE("MediaPacket: buffer + pixel desc round-trips") {
        auto buf = Buffer::Ptr::create(64);
        buf.modify()->setSize(32);
        MediaPacket pkt(buf, PixelDesc(PixelDesc::H264));

        CHECK(pkt.isValid());
        CHECK(pkt.size() == 32);
        CHECK(pkt.pixelDesc().id() == PixelDesc::H264);
        CHECK(pkt.pixelDesc().videoCodec().id() == VideoCodec::H264);
        CHECK(pkt.buffer() == buf);
}

TEST_CASE("MediaPacket: flag manipulation") {
        MediaPacket pkt;
        CHECK_FALSE(pkt.hasFlag(MediaPacket::Keyframe));

        pkt.addFlag(MediaPacket::Keyframe);
        CHECK(pkt.isKeyframe());
        CHECK(pkt.flags() == MediaPacket::Keyframe);

        pkt.addFlag(MediaPacket::EndOfStream);
        CHECK(pkt.isKeyframe());
        CHECK(pkt.isEndOfStream());

        pkt.removeFlag(MediaPacket::Keyframe);
        CHECK_FALSE(pkt.isKeyframe());
        CHECK(pkt.isEndOfStream());

        pkt.setFlags(MediaPacket::ParameterSet | MediaPacket::Discardable);
        CHECK(pkt.isParameterSet());
        CHECK(pkt.hasFlag(MediaPacket::Discardable));
        CHECK_FALSE(pkt.isKeyframe());
        CHECK_FALSE(pkt.isEndOfStream());
}

TEST_CASE("MediaPacket: timestamps and duration") {
        MediaPacket pkt;
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
        cfg.set(MediaConfig::VideoRcMode, VideoRateControl::CBR);
        cfg.set(MediaConfig::GopLength, int32_t(30));
        cfg.set(MediaConfig::VideoPreset, VideoEncoderPreset::LowLatency);
        cfg.set(MediaConfig::VideoProfile, String("high"));

        CHECK(cfg.getAs<int32_t>(MediaConfig::BitrateKbps) == 10000);
        CHECK(cfg.getAs<Enum>(MediaConfig::VideoRcMode)
                == VideoRateControl::CBR);
        CHECK(cfg.getAs<int32_t>(MediaConfig::GopLength) == 30);
        CHECK(cfg.getAs<Enum>(MediaConfig::VideoPreset)
                == VideoEncoderPreset::LowLatency);
        CHECK(cfg.getAs<String>(MediaConfig::VideoProfile) == "high");
}

TEST_CASE("MediaPacket: multiple packets share one Buffer via BufferView slices") {
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

        MediaPacket sps(BufferView(au, 0,                 kSpsLen),
                        PixelDesc(PixelDesc::H264));
        MediaPacket pps(BufferView(au, kSpsLen,           kPpsLen),
                        PixelDesc(PixelDesc::H264));
        MediaPacket idr(BufferView(au, kSpsLen + kPpsLen, kIdrLen),
                        PixelDesc(PixelDesc::H264));
        sps.addFlag(MediaPacket::ParameterSet);
        pps.addFlag(MediaPacket::ParameterSet);
        idr.addFlag(MediaPacket::Keyframe);

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

TEST_CASE("MediaPacket: Buffer::Ptr ctor wraps the whole buffer as a view") {
        auto buf = Buffer::Ptr::create(128);
        buf.modify()->setSize(100);
        MediaPacket pkt(buf, PixelDesc(PixelDesc::HEVC));
        CHECK(pkt.isValid());
        CHECK(pkt.size() == 100);
        CHECK(pkt.view().offset() == 0);
        CHECK(pkt.view().size() == 100);
        CHECK(pkt.buffer() == buf);
}

TEST_CASE("MediaPacket: shared ownership via Ptr") {
        auto pkt = MediaPacket::Ptr::create(
                Buffer::Ptr::create(16), PixelDesc(PixelDesc::HEVC));
        pkt.modify()->addFlag(MediaPacket::Keyframe);

        MediaPacket::Ptr alias = pkt;
        CHECK(alias == pkt);
        CHECK(alias->isKeyframe());
        CHECK(alias->pixelDesc().id() == PixelDesc::HEVC);
}
