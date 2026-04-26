/**
 * @file      tests/mediapayload.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Exercises the abstract @ref MediaPayload hierarchy — the
 * @ref VideoPayload / @ref AudioPayload intermediates and their
 * concrete uncompressed / compressed leaves.
 */

#include <doctest/doctest.h>
#include <promeki/mediapayload.h>
#include <promeki/videopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/audiocodec.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>
#include <promeki/bufferview.h>
#include <promeki/clockdomain.h>
#include <promeki/metadata.h>
#include <promeki/timestamp.h>
#include <promeki/enums.h>

using namespace promeki;

// ---------------------------------------------------------------
// MediaPayloadKind / FrameType TypedEnum wiring
// ---------------------------------------------------------------

TEST_CASE("MediaPayloadKind: enum values round-trip through Enum registry") {
        CHECK(MediaPayloadKind::Video.valueName() == "Video");
        CHECK(MediaPayloadKind::Audio.valueName() == "Audio");
        CHECK(MediaPayloadKind::Metadata.valueName() == "Metadata");
        CHECK(MediaPayloadKind::Subtitle.valueName() == "Subtitle");
        CHECK(MediaPayloadKind::AncillaryData.valueName() == "AncillaryData");
        CHECK(MediaPayloadKind::Custom.valueName() == "Custom");

        CHECK(MediaPayloadKind::Video.value() == 0);
        CHECK(MediaPayloadKind::Custom.value() == 5);
}

TEST_CASE("FrameType: enum values round-trip through Enum registry") {
        CHECK(FrameType::Unknown.valueName() == "Unknown");
        CHECK(FrameType::I.valueName() == "I");
        CHECK(FrameType::P.valueName() == "P");
        CHECK(FrameType::B.valueName() == "B");
        CHECK(FrameType::IDR.valueName() == "IDR");
        CHECK(FrameType::BRef.valueName() == "BRef");

        CHECK(FrameType::Unknown.value() == 0);
        CHECK(FrameType::BRef.value() == 5);
}

// ---------------------------------------------------------------
// UncompressedVideoPayload
// ---------------------------------------------------------------

TEST_CASE("UncompressedVideoPayload: default-constructed is invalid") {
        UncompressedVideoPayload p;
        CHECK_FALSE(p.isValid());
        CHECK(p.planeCount() == 0);
        CHECK(p.size() == 0);
        CHECK_FALSE(p.isCompressed());
        CHECK(p.isKeyframe()); // trivially true for uncompressed
        CHECK(p.kind() == MediaPayloadKind::Video);
}

TEST_CASE("UncompressedVideoPayload: desc + plane list round-trips") {
        ImageDesc desc(1920, 1080, PixelFormat(PixelFormat::RGBA8_sRGB));
        auto      buf = Buffer::Ptr::create(1920 * 1080 * 4);
        buf.modify()->setSize(1920 * 1080 * 4);
        BufferView plane0(buf, 0, buf->size());

        UncompressedVideoPayload p(desc, plane0);
        CHECK(p.isValid());
        CHECK(p.planeCount() == 1);
        CHECK(p.plane(0).buffer() == buf);
        CHECK(p.size() == 1920u * 1080u * 4u);
        CHECK(p.desc().pixelFormat().id() == PixelFormat::RGBA8_sRGB);
        CHECK(p.desc().size().width() == 1920);
        CHECK_FALSE(p.isCompressed());
        CHECK(p.isKeyframe());
}

TEST_CASE("UncompressedVideoPayload: multi-plane planar layout") {
        ImageDesc desc(1920, 1080, PixelFormat(PixelFormat::YUV8_420_Planar_Rec709));
        // Simulate a single-allocation planar frame.
        size_t yBytes = 1920 * 1080;
        size_t uBytes = 1920 * 1080 / 4;
        size_t vBytes = 1920 * 1080 / 4;
        auto   buf = Buffer::Ptr::create(yBytes + uBytes + vBytes);
        buf.modify()->setSize(yBytes + uBytes + vBytes);

        BufferView y(buf, 0, yBytes);
        BufferView u(buf, yBytes, uBytes);
        BufferView v(buf, yBytes + uBytes, vBytes);

        UncompressedVideoPayload p(desc, BufferView({y, u, v}));
        CHECK(p.isValid());
        CHECK(p.planeCount() == 3);
        CHECK(p.plane(0).buffer() == buf);
        CHECK(p.plane(1).buffer() == buf);
        CHECK(p.plane(2).buffer() == buf);
        CHECK(p.plane(1).offset() == yBytes);
        CHECK(p.plane(2).offset() == yBytes + uBytes);
        CHECK(p.size() == yBytes + uBytes + vBytes);
}

TEST_CASE("UncompressedVideoPayload: isKeyframe stays true even without the flag") {
        UncompressedVideoPayload p;
        CHECK(p.isKeyframe());
        // Explicitly clearing does not change the override's return value —
        // generic seekers don't need to downcast to get the right answer.
        p.removeFlag(MediaPayload::Keyframe);
        CHECK(p.isKeyframe());
}

// ---------------------------------------------------------------
// CompressedVideoPayload
// ---------------------------------------------------------------

TEST_CASE("CompressedVideoPayload: default-constructed is invalid") {
        CompressedVideoPayload p;
        CHECK_FALSE(p.isValid());
        CHECK(p.kind() == MediaPayloadKind::Video);
        CHECK(p.isCompressed());
        CHECK_FALSE(p.isKeyframe());
        CHECK(p.frameType() == FrameType::Unknown);
}

TEST_CASE("CompressedVideoPayload: buffer ctor wraps the whole buffer as one plane") {
        ImageDesc desc(1920, 1080, PixelFormat(PixelFormat::H264));
        auto      buf = Buffer::Ptr::create(4096);
        buf.modify()->setSize(2048);

        CompressedVideoPayload p(desc, buf);
        CHECK(p.isValid());
        CHECK(p.isCompressed());
        CHECK(p.planeCount() == 1);
        CHECK(p.plane(0).buffer() == buf);
        CHECK(p.plane(0).offset() == 0);
        CHECK(p.plane(0).size() == 2048);
        CHECK(p.size() == 2048);
        CHECK(p.desc().pixelFormat().id() == PixelFormat::H264);
}

TEST_CASE("CompressedVideoPayload: frameType falls through to IDR when Keyframe is set") {
        ImageDesc              desc(1280, 720, PixelFormat(PixelFormat::H264));
        CompressedVideoPayload p(desc, Buffer::Ptr::create(32));
        // Default: no explicit frame type, no keyframe flag.
        CHECK(p.frameType() == FrameType::Unknown);

        p.addFlag(MediaPayload::Keyframe);
        CHECK(p.frameType() == FrameType::IDR);

        p.setFrameType(FrameType::I); // explicit override
        CHECK(p.frameType() == FrameType::I);
}

TEST_CASE("CompressedVideoPayload: parameter-set flag lives in high bits") {
        ImageDesc              desc(1920, 1080, PixelFormat(PixelFormat::H264));
        CompressedVideoPayload p(desc, Buffer::Ptr::create(16));
        CHECK_FALSE(p.isParameterSet());

        p.markParameterSet();
        CHECK(p.isParameterSet());
        // The ParameterSet bit lives above the generic Flag range so it
        // coexists with Keyframe without collision.
        p.addFlag(MediaPayload::Keyframe);
        CHECK(p.isParameterSet());
        CHECK(p.isKeyframe());

        p.markParameterSet(false);
        CHECK_FALSE(p.isParameterSet());
        CHECK(p.isKeyframe());
}

TEST_CASE("CompressedVideoPayload: inBandCodecData round-trips") {
        ImageDesc              desc(1920, 1080, PixelFormat(PixelFormat::H264));
        CompressedVideoPayload p(desc);
        CHECK(p.inBandCodecData().isNull());

        auto sps = Buffer::Ptr::create(32);
        p.setInBandCodecData(sps);
        CHECK(p.inBandCodecData() == sps);
}

TEST_CASE("CompressedVideoPayload: invalid when pixel format is not compressed") {
        ImageDesc desc(1920, 1080, PixelFormat(PixelFormat::RGBA8_sRGB));
        auto      buf = Buffer::Ptr::create(16);
        buf.modify()->setSize(16);
        CompressedVideoPayload p(desc, buf);
        // Bytes present, descriptor valid — but the format is raster,
        // not compressed, so this fails the compressed-specific tighten.
        CHECK_FALSE(p.isValid());
}

// ---------------------------------------------------------------
// PcmAudioPayload
// ---------------------------------------------------------------

TEST_CASE("PcmAudioPayload: default-constructed is invalid") {
        PcmAudioPayload p;
        CHECK_FALSE(p.isValid());
        CHECK(p.kind() == MediaPayloadKind::Audio);
        CHECK_FALSE(p.isCompressed());
        CHECK(p.isKeyframe());
        CHECK(p.sampleCount() == 0);
}

TEST_CASE("PcmAudioPayload: desc + sample count + plane list") {
        AudioDesc desc(AudioFormat(AudioFormat::PCMI_Float32LE), 48000.0f, 2);
        size_t    samples = 1024;
        auto      buf = Buffer::Ptr::create(desc.bufferSize(samples));
        buf.modify()->setSize(desc.bufferSize(samples));

        PcmAudioPayload p(desc, samples, BufferView({BufferView(buf, 0, buf->size())}));
        CHECK(p.isValid());
        CHECK(p.sampleCount() == samples);
        CHECK(p.desc().sampleRate() == 48000.0f);
        CHECK(p.desc().channels() == 2);
        CHECK_FALSE(p.isCompressed());
}

// ---------------------------------------------------------------
// CompressedAudioPayload
// ---------------------------------------------------------------

TEST_CASE("CompressedAudioPayload: desc + buffer round-trips") {
        AudioDesc desc(AudioFormat(AudioFormat::Opus), 48000.0f, 2);
        auto      buf = Buffer::Ptr::create(256);
        buf.modify()->setSize(200);

        CompressedAudioPayload p(desc, buf);
        CHECK(p.isValid());
        CHECK(p.isCompressed());
        CHECK(p.kind() == MediaPayloadKind::Audio);
        CHECK(p.planeCount() == 1);
        CHECK(p.size() == 200);
        CHECK(p.desc().format().id() == AudioFormat::Opus);
}

TEST_CASE("CompressedAudioPayload: invalid when format is PCM") {
        AudioDesc desc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2);
        auto      buf = Buffer::Ptr::create(32);
        buf.modify()->setSize(32);
        CompressedAudioPayload p(desc, buf);
        // Bytes present, descriptor valid — but the format is PCM, so
        // the compressed-specific tighten fails.
        CHECK_FALSE(p.isValid());
}

// ---------------------------------------------------------------
// MediaPayload base: timing, flags, metadata, SharedPtr semantics
// ---------------------------------------------------------------

TEST_CASE("MediaPayload: timestamps and duration on any subclass") {
        UncompressedVideoPayload p;
        ClockDomain              dom(ClockDomain::SystemMonotonic);
        TimeStamp                now = TimeStamp::now();
        MediaTimeStamp           pts(now, dom);
        MediaTimeStamp           dts(now, dom, Duration::fromMilliseconds(-100));

        p.setPts(pts);
        p.setDts(dts);
        // hasDuration is a type-level predicate: VideoPayload
        // supports durations unconditionally.  A fresh payload's
        // duration is zero until a stage stamps one.
        CHECK(p.hasDuration());
        CHECK(p.duration().isZero());
        Error e = p.setDuration(Duration::fromMilliseconds(33));
        CHECK(e.isOk());

        CHECK(p.pts() == pts);
        CHECK(p.dts() == dts);
        CHECK(p.hasDuration());
        CHECK(p.duration() == Duration::fromMilliseconds(33));
}

TEST_CASE("MediaPayload: audio duration is derived from sample count and rate") {
        AudioDesc       desc(AudioFormat(AudioFormat::PCMI_Float32LE), 48000.0f, 2);
        PcmAudioPayload p(desc, 960);
        CHECK(p.hasDuration());
        // 960 samples @ 48000 Hz = 20 ms.
        CHECK(p.duration() == Duration::fromMilliseconds(20));
        // setDuration on audio is NotSupported — the value is
        // derived, not stored.
        Error e = p.setDuration(Duration::fromMilliseconds(5));
        CHECK(e.code() == Error::NotSupported);
        CHECK(p.duration() == Duration::fromMilliseconds(20));
}

TEST_CASE("MediaPayload: hasDuration is a type-level predicate") {
        // Video: supports durations regardless of whether one has
        // been stamped.
        UncompressedVideoPayload uvp;
        CHECK(uvp.hasDuration());
        CHECK(uvp.duration().isZero());
        CompressedVideoPayload cvp;
        CHECK(cvp.hasDuration());
        CHECK(cvp.duration().isZero());

        // Audio: supports durations regardless of sample count.
        PcmAudioPayload empty;
        CHECK(empty.hasDuration());
        CHECK(empty.duration().isZero());
}

TEST_CASE("MediaPayload: generic flag manipulation") {
        CompressedVideoPayload p;
        CHECK(p.flags() == 0);
        CHECK_FALSE(p.hasFlag(MediaPayload::Keyframe));

        p.addFlag(MediaPayload::Keyframe);
        CHECK(p.isKeyframe());
        CHECK(p.flags() == MediaPayload::Keyframe);

        p.addFlag(MediaPayload::Discardable);
        CHECK(p.isDiscardable());
        CHECK(p.isKeyframe());

        p.removeFlag(MediaPayload::Keyframe);
        CHECK_FALSE(p.isKeyframe());
        CHECK(p.isDiscardable());

        p.setFlags(0);
        CHECK(p.flags() == 0);
}

TEST_CASE("MediaPayload: setFlag(Flag, bool) conditional set/clear") {
        CompressedVideoPayload p;
        CHECK(p.flags() == 0);

        p.setFlag(MediaPayload::Keyframe, true);
        CHECK(p.isKeyframe());
        p.setFlag(MediaPayload::Keyframe, true);
        CHECK(p.isKeyframe()); // idempotent

        p.setFlag(MediaPayload::Discardable, false);
        CHECK_FALSE(p.isDiscardable()); // clearing an unset bit is a no-op
        CHECK(p.isKeyframe());          // unaffected

        p.setFlag(MediaPayload::Keyframe, false);
        CHECK_FALSE(p.isKeyframe());
}

TEST_CASE("MediaPayload: IntraRefresh flag is distinct from Keyframe") {
        CompressedVideoPayload p;
        CHECK_FALSE(p.isIntraRefresh());
        CHECK_FALSE(p.isKeyframe());

        p.addFlag(MediaPayload::IntraRefresh);
        CHECK(p.isIntraRefresh());
        CHECK_FALSE(p.isKeyframe()); // gradual refresh is NOT a random-access point
        CHECK_FALSE(p.isSafeCutPoint());

        p.addFlag(MediaPayload::Keyframe);
        CHECK(p.isIntraRefresh());
        CHECK(p.isKeyframe()); // the two can coexist, but typically do not
}

TEST_CASE("MediaPayload: flag bitmask is 64 bits wide") {
        CompressedVideoPayload p;

        // Set a base flag in the low bits and the subclass ParameterSet
        // flag in bit 16 — both must round-trip losslessly.
        p.addFlag(MediaPayload::Keyframe);
        p.markParameterSet(true);
        CHECK(p.isKeyframe());
        CHECK(p.isParameterSet());

        // Exercise a high bit that would truncate under a 32-bit mask.
        const uint64_t highBit = 1ull << 40;
        p.setFlags(p.flags() | highBit);
        CHECK((p.flags() & highBit) == highBit);
        CHECK(p.isKeyframe()); // older bits preserved
        CHECK(p.isParameterSet());
}

TEST_CASE("MediaPayload: flagName returns mnemonic for known bits") {
        CHECK(String(MediaPayload::flagName(MediaPayload::None)) == "None");
        CHECK(String(MediaPayload::flagName(MediaPayload::Keyframe)) == "Keyframe");
        CHECK(String(MediaPayload::flagName(MediaPayload::Discardable)) == "Discardable");
        CHECK(String(MediaPayload::flagName(MediaPayload::Corrupt)) == "Corrupt");
        CHECK(String(MediaPayload::flagName(MediaPayload::EndOfStream)) == "EndOfStream");
        CHECK(String(MediaPayload::flagName(MediaPayload::IntraRefresh)) == "IntraRefresh");

        // A compound mask is not a recognised single-bit value.
        const auto compound = static_cast<MediaPayload::Flag>(MediaPayload::Keyframe | MediaPayload::Corrupt);
        CHECK(MediaPayload::flagName(compound) == nullptr);
}

TEST_CASE("MediaPayload: EndOfStream flag") {
        UncompressedVideoPayload p;
        CHECK_FALSE(p.isEndOfStream());
        p.markEndOfStream();
        CHECK(p.isEndOfStream());
        p.markEndOfStream(false);
        CHECK_FALSE(p.isEndOfStream());
}

TEST_CASE("MediaPayload: markCorrupt records reason in metadata") {
        UncompressedVideoPayload p;
        CHECK_FALSE(p.isCorrupt());
        CHECK(p.corruptReason().isEmpty());

        p.markCorrupt(String("checksum mismatch"));
        CHECK(p.isCorrupt());
        CHECK(p.corruptReason() == "checksum mismatch");
        CHECK(p.metadata().getAs<String>(Metadata::CorruptReason) == "checksum mismatch");
}

TEST_CASE("MediaPayload: markCorrupt with empty reason leaves CorruptReason unset") {
        UncompressedVideoPayload p;
        p.markCorrupt();
        CHECK(p.isCorrupt());
        CHECK(p.corruptReason().isEmpty());
        CHECK_FALSE(p.metadata().contains(Metadata::CorruptReason));
}

TEST_CASE("MediaPayload: stream index") {
        CompressedAudioPayload p;
        CHECK(p.streamIndex() == 0);
        p.setStreamIndex(3);
        CHECK(p.streamIndex() == 3);
}

TEST_CASE("MediaPayload: setData(BufferView) single-view convenience") {
        auto buf = Buffer::Ptr::create(16);
        buf.modify()->setSize(16);
        UncompressedVideoPayload p;
        p.setData(BufferView(buf, 0, 16));
        CHECK(p.planeCount() == 1);
        CHECK(p.plane(0).size() == 16);
}

TEST_CASE("MediaPayload::Ptr: implicit upcast from UncompressedVideoPayload::Ptr") {
        ImageDesc desc(1920, 1080, PixelFormat(PixelFormat::RGBA8_sRGB));
        auto      buf = Buffer::Ptr::create(1920 * 1080 * 4);
        buf.modify()->setSize(1920 * 1080 * 4);

        UncompressedVideoPayload::Ptr vp =
                UncompressedVideoPayload::Ptr::create(desc, BufferView({BufferView(buf, 0, buf->size())}));

        MediaPayload::Ptr mp = vp;
        REQUIRE(mp.isValid());
        CHECK(mp->kind() == MediaPayloadKind::Video);
        CHECK_FALSE(mp->isCompressed());
        CHECK(mp.referenceCount() == 2);
}

TEST_CASE("MediaPayload::Ptr: implicit upcast from CompressedAudioPayload::Ptr") {
        AudioDesc                   desc(AudioFormat(AudioFormat::Opus), 48000.0f, 2);
        CompressedAudioPayload::Ptr ap = CompressedAudioPayload::Ptr::create(desc, Buffer::Ptr::create(16));

        MediaPayload::Ptr mp = ap;
        REQUIRE(mp.isValid());
        CHECK(mp->kind() == MediaPayloadKind::Audio);
        CHECK(mp->isCompressed());
        CHECK(mp.referenceCount() == 2);
}

TEST_CASE("sharedPointerCast: downcasts MediaPayload::Ptr to the concrete leaf") {
        ImageDesc         desc(1920, 1080, PixelFormat(PixelFormat::H264));
        MediaPayload::Ptr mp = CompressedVideoPayload::Ptr::create(desc, Buffer::Ptr::create(16));
        REQUIRE(mp.isValid());

        auto cvp = sharedPointerCast<CompressedVideoPayload>(mp);
        REQUIRE(cvp.isValid());
        CHECK(cvp->isCompressed());
        CHECK(cvp->desc().pixelFormat().id() == PixelFormat::H264);

        // Wrong-type downcast returns an empty Ptr.
        auto cap = sharedPointerCast<CompressedAudioPayload>(mp);
        CHECK(cap.isNull());
}

TEST_CASE("MediaPayload::as<T>: dynamic downcast returns null on miss") {
        AudioDesc       desc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2);
        PcmAudioPayload p(desc);

        const MediaPayload *mp = &p;
        CHECK(mp->as<AudioPayload>() != nullptr);
        CHECK(mp->as<PcmAudioPayload>() != nullptr);
        CHECK(mp->as<VideoPayload>() == nullptr);
        CHECK(mp->as<CompressedAudioPayload>() == nullptr);
}

TEST_CASE("CompressedVideoPayload: clone preserves derived type") {
        ImageDesc desc(1920, 1080, PixelFormat(PixelFormat::HEVC));
        auto      orig = CompressedVideoPayload::Ptr::create(desc, Buffer::Ptr::create(32));
        orig.modify()->addFlag(MediaPayload::Keyframe);
        orig.modify()->setFrameType(FrameType::IDR);
        orig.modify()->markCorrupt(String("bad slice"));

        // Copy-on-write via modify() on a shared Ptr triggers
        // _promeki_clone — the result must still be a
        // CompressedVideoPayload with all fields preserved.
        CompressedVideoPayload::Ptr alias = orig;
        alias.modify()->setPts(MediaTimeStamp(TimeStamp::now(), ClockDomain(ClockDomain::SystemMonotonic)));

        CHECK(orig->kind() == MediaPayloadKind::Video);
        CHECK(alias->kind() == MediaPayloadKind::Video);
        CHECK(alias->isKeyframe());
        CHECK(alias->frameType() == FrameType::IDR);
        CHECK(alias->isCorrupt());
        CHECK(alias->corruptReason() == "bad slice");
        CHECK(alias->desc().pixelFormat().id() == PixelFormat::HEVC);
}

// ---------------------------------------------------------------
// isSafeCutPoint — distinct from isKeyframe
// ---------------------------------------------------------------

TEST_CASE("isSafeCutPoint: uncompressed video is always safe") {
        UncompressedVideoPayload p;
        CHECK(p.isSafeCutPoint());
        // Flag state is irrelevant for uncompressed — the override
        // short-circuits the underlying bitmask.
        p.removeFlag(MediaPayload::Keyframe);
        CHECK(p.isSafeCutPoint());
}

TEST_CASE("isSafeCutPoint: uncompressed audio is always safe") {
        PcmAudioPayload p;
        CHECK(p.isSafeCutPoint());
}

TEST_CASE("isSafeCutPoint: compressed intra-only video is always safe") {
        // JPEG is intra-only (every image is its own access unit).
        ImageDesc desc(1920, 1080, PixelFormat(PixelFormat::JPEG_RGB8_sRGB));
        auto      buf = Buffer::Ptr::create(256);
        buf.modify()->setSize(200);
        CompressedVideoPayload p(desc, buf);
        REQUIRE(p.isValid());
        // No keyframe flag set — still safe because intra-only.
        CHECK_FALSE(p.isKeyframe());
        CHECK(p.isSafeCutPoint());
}

TEST_CASE("isSafeCutPoint: compressed temporal video requires keyframe flag") {
        ImageDesc desc(1920, 1080, PixelFormat(PixelFormat::H264));
        auto      buf = Buffer::Ptr::create(256);
        buf.modify()->setSize(256);
        CompressedVideoPayload p(desc, buf);
        REQUIRE(p.isValid());

        // Default: no keyframe flag → not safe to cut.
        CHECK_FALSE(p.isSafeCutPoint());

        p.addFlag(MediaPayload::Keyframe);
        CHECK(p.isSafeCutPoint());

        p.removeFlag(MediaPayload::Keyframe);
        CHECK_FALSE(p.isSafeCutPoint());
}

TEST_CASE("isSafeCutPoint: invalid compressed video payload is never safe") {
        CompressedVideoPayload p; // empty descriptor, empty data
        CHECK_FALSE(p.isSafeCutPoint());
}

TEST_CASE("isSafeCutPoint: compressed audio honours codec PacketIndependence") {
        // Opus: every packet decodes standalone → always safe.
        AudioDesc opusDesc(AudioFormat(AudioFormat::Opus), 48000.0f, 2);
        auto      buf = Buffer::Ptr::create(64);
        buf.modify()->setSize(64);
        CompressedAudioPayload opus(opusDesc, buf);
        REQUIRE(opus.isValid());
        // AudioCodec::Opus is known to have PacketIndependenceEvery.
        CHECK(opus.isSafeCutPoint());
}

TEST_CASE("isSafeCutPoint: isKeyframe stays orthogonal to cut semantics") {
        // Keyframe and isSafeCutPoint can diverge — e.g. a compressed
        // video payload with no codec identity registered has an
        // isKeyframe() of whatever the flag says, but isSafeCutPoint
        // is false because we can't reason about the codec.
        ImageDesc desc(1920, 1080, PixelFormat(PixelFormat::H264));
        auto      buf = Buffer::Ptr::create(16);
        buf.modify()->setSize(16);
        CompressedVideoPayload p(desc, buf);
        p.addFlag(MediaPayload::Keyframe);
        CHECK(p.isKeyframe());
        CHECK(p.isSafeCutPoint());

        // Clearing the flag: keyframe false, cut point also false (H.264 is temporal).
        p.removeFlag(MediaPayload::Keyframe);
        CHECK_FALSE(p.isKeyframe());
        CHECK_FALSE(p.isSafeCutPoint());
}

TEST_CASE("MediaPayload::PtrList: mixed payloads in one list") {
        ImageDesc vdesc(1920, 1080, PixelFormat(PixelFormat::RGBA8_sRGB));
        AudioDesc adesc(AudioFormat(AudioFormat::PCMI_S16LE), 48000.0f, 2);
        auto      vbuf = Buffer::Ptr::create(1920 * 1080 * 4);
        vbuf.modify()->setSize(1920 * 1080 * 4);
        auto abuf = Buffer::Ptr::create(adesc.bufferSize(1024));
        abuf.modify()->setSize(adesc.bufferSize(1024));

        MediaPayload::PtrList list;
        list.pushToBack(UncompressedVideoPayload::Ptr::create(vdesc, BufferView({BufferView(vbuf, 0, vbuf->size())})));
        list.pushToBack(
                PcmAudioPayload::Ptr::create(adesc, size_t(1024), BufferView({BufferView(abuf, 0, abuf->size())})));

        REQUIRE(list.size() == 2);
        CHECK(list[0]->kind() == MediaPayloadKind::Video);
        CHECK(list[1]->kind() == MediaPayloadKind::Audio);

        // The filter-by-type idiom a migration adapter would use.
        size_t videoCount = 0;
        size_t audioCount = 0;
        for (const auto &p : list) {
                if (p->kind() == MediaPayloadKind::Video) ++videoCount;
                if (p->kind() == MediaPayloadKind::Audio) ++audioCount;
        }
        CHECK(videoCount == 1);
        CHECK(audioCount == 1);
}
