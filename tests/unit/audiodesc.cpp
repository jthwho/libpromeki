/**
 * @file      audiodesc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/audiodesc.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audioformat.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>
#include <promeki/sdpsession.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("AudioDesc_Default") {
        AudioDesc desc;
        CHECK(!desc.isValid());
        CHECK(desc.format().id() == AudioFormat::Invalid);
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("AudioDesc_Construct") {
        AudioDesc desc(48000.0f, 2);
        CHECK(desc.isValid());
        CHECK(desc.sampleRate() > 47999.0f);
        CHECK(desc.sampleRate() < 48001.0f);
        CHECK(desc.channels() == 2);
        CHECK(desc.isNative());
}

TEST_CASE("AudioDesc_ConstructWithType") {
        AudioDesc desc(AudioFormat::PCMI_S16LE, 44100.0f, 1);
        CHECK(desc.isValid());
        CHECK(desc.format().id() == AudioFormat::PCMI_S16LE);
        CHECK(desc.sampleRate() > 44099.0f);
        CHECK(desc.channels() == 1);
        CHECK(desc.bytesPerSample() == 2);
}

// ============================================================================
// Setters
// ============================================================================

TEST_CASE("AudioDesc_SetSampleRate") {
        AudioDesc desc(48000.0f, 2);
        desc.setSampleRate(96000.0f);
        CHECK(desc.sampleRate() > 95999.0f);
        CHECK(desc.sampleRate() < 96001.0f);
}

TEST_CASE("AudioDesc_SetChannels") {
        AudioDesc desc(48000.0f, 2);
        desc.setChannels(8);
        CHECK(desc.channels() == 8);
}

TEST_CASE("AudioDesc_SetFormat") {
        AudioDesc desc(48000.0f, 2);
        desc.setFormat(AudioFormat::PCMI_S16LE);
        CHECK(desc.format().id() == AudioFormat::PCMI_S16LE);
}

// ============================================================================
// Copy semantics (plain value, no internal COW)
// ============================================================================

TEST_CASE("AudioDesc_CopyIsIndependent") {
        AudioDesc d1(48000.0f, 2);
        AudioDesc d2 = d1;

        d2.setSampleRate(96000.0f);
        CHECK(d1.sampleRate() < 48001.0f);
        CHECK(d2.sampleRate() > 95999.0f);
}

TEST_CASE("AudioDesc_CopyChannelsIndependent") {
        AudioDesc d1(48000.0f, 2);
        AudioDesc d2 = d1;

        d2.setChannels(8);
        CHECK(d1.channels() == 2);
        CHECK(d2.channels() == 8);
}

// ============================================================================
// Buffer size calculation
// ============================================================================

TEST_CASE("AudioDesc_BufferSize") {
        AudioDesc desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        // 2 bytes per sample * 2 channels * 1024 samples = 4096
        CHECK(desc.bufferSize(1024) == 4096);
}

TEST_CASE("AudioDesc_BytesPerSampleStride") {
        AudioDesc desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        // Interleaved: bytesPerSample * channels = 2 * 2 = 4
        CHECK(desc.bytesPerSampleStride() == 4);
}

TEST_CASE("AudioDesc_BytesPerSampleStride_Planar") {
        AudioDesc desc(AudioFormat::PCMP_S16LE, 48000.0f, 2);
        // Planar: just bytesPerSample = 2
        CHECK(desc.bytesPerSampleStride() == 2);
}

// ============================================================================
// Working desc
// ============================================================================

TEST_CASE("AudioDesc_WorkingDesc") {
        AudioDesc desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        AudioDesc working = desc.workingDesc();
        CHECK(working.isValid());
        CHECK(working.isNative());
        CHECK(working.channels() == 2);
        CHECK(working.sampleRate() > 47999.0f);
}

// ============================================================================
// toString
// ============================================================================

TEST_CASE("AudioDesc_ToString") {
        AudioDesc desc(48000.0f, 2);
        String    s = desc.toString();
        CHECK(s.size() > 0);
}

// ============================================================================
// Metadata
// ============================================================================

TEST_CASE("AudioDesc_Metadata") {
        AudioDesc       desc(48000.0f, 2);
        const Metadata &cm = desc.metadata();
        CHECK(cm.isEmpty());

        desc.metadata().set(Metadata::Artist, String("Test Artist"));
        CHECK(!desc.metadata().isEmpty());
        CHECK(desc.metadata().get(Metadata::Artist).get<String>() == "Test Artist");
}

// ============================================================================
// Equality includes metadata
// ============================================================================

TEST_CASE("AudioDesc_EqualityWithoutMetadata") {
        AudioDesc d1(48000.0f, 2);
        AudioDesc d2(48000.0f, 2);
        CHECK(d1 == d2);
}

TEST_CASE("AudioDesc_EqualityDiffersWithMetadata") {
        AudioDesc d1(48000.0f, 2);
        AudioDesc d2(48000.0f, 2);
        d1.metadata().set(Metadata::Artist, String("Test"));
        CHECK_FALSE(d1 == d2);
}

TEST_CASE("AudioDesc_EqualityMatchingMetadata") {
        AudioDesc d1(48000.0f, 2);
        AudioDesc d2(48000.0f, 2);
        d1.metadata().set(Metadata::Artist, String("Test"));
        d2.metadata().set(Metadata::Artist, String("Test"));
        CHECK(d1 == d2);
}

TEST_CASE("AudioDesc_FormatEqualsIgnoresMetadata") {
        AudioDesc d1(48000.0f, 2);
        AudioDesc d2(48000.0f, 2);
        d1.metadata().set(Metadata::Artist, String("Test"));
        CHECK(d1.formatEquals(d2));
        CHECK_FALSE(d1 == d2);
}

// ============================================================================
// JSON round-trip
// ============================================================================

TEST_CASE("AudioDesc_ToJson") {
        AudioDesc  desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        JsonObject json = desc.toJson();
        CHECK(json.contains("Format"));
        CHECK(json.contains("SampleRate"));
        CHECK(json.contains("Channels"));
}

TEST_CASE("AudioDesc_FromJson") {
        AudioDesc  desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        JsonObject json = desc.toJson();
        AudioDesc  out = AudioDesc::fromJson(json);
        CHECK(out.isValid());
        CHECK(out.format().id() == AudioFormat::PCMI_S16LE);
        CHECK(out.sampleRate() == 48000.0f);
        CHECK(out.channels() == 2);
}

// ============================================================================
// fromSdp — derive an AudioDesc from an SDP media description
// ============================================================================

TEST_CASE("AudioDesc_fromSdp_L16_Stereo") {
        SdpMediaDescription md;
        md.setMediaType("audio");
        md.setPort(5006);
        md.setProtocol("RTP/AVP");
        md.addPayloadType(96);
        md.setAttribute("rtpmap", "96 L16/48000/2");

        AudioDesc ad = AudioDesc::fromSdp(md);
        CHECK(ad.isValid());
        CHECK(ad.format().id() == AudioFormat::PCMI_S16BE);
        CHECK(ad.sampleRate() == 48000.0f);
        CHECK(ad.channels() == 2);
}

TEST_CASE("AudioDesc_fromSdp_L24") {
        SdpMediaDescription md;
        md.setMediaType("audio");
        md.addPayloadType(97);
        md.setAttribute("rtpmap", "97 L24/48000/8");
        AudioDesc ad = AudioDesc::fromSdp(md);
        CHECK(ad.isValid());
        CHECK(ad.format().id() == AudioFormat::PCMI_S24BE);
        CHECK(ad.sampleRate() == 48000.0f);
        CHECK(ad.channels() == 8);
}

TEST_CASE("AudioDesc_fromSdp_L8") {
        SdpMediaDescription md;
        md.setMediaType("audio");
        md.addPayloadType(11);
        md.setAttribute("rtpmap", "11 L8/44100/1");
        AudioDesc ad = AudioDesc::fromSdp(md);
        CHECK(ad.isValid());
        CHECK(ad.format().id() == AudioFormat::PCMI_U8);
        CHECK(ad.sampleRate() == 44100.0f);
        CHECK(ad.channels() == 1);
}

TEST_CASE("AudioDesc_fromSdp_L16_DefaultsToMono") {
        // Channels default to 1 when omitted per RFC 3551.
        SdpMediaDescription md;
        md.setMediaType("audio");
        md.addPayloadType(96);
        md.setAttribute("rtpmap", "96 L16/48000");
        AudioDesc ad = AudioDesc::fromSdp(md);
        CHECK(ad.isValid());
        CHECK(ad.format().id() == AudioFormat::PCMI_S16BE);
        CHECK(ad.channels() == 1);
}

TEST_CASE("AudioDesc_fromSdp_NonAudioReturnsInvalid") {
        SdpMediaDescription md;
        md.setMediaType("video");
        md.setAttribute("rtpmap", "96 jxsv/90000");
        AudioDesc ad = AudioDesc::fromSdp(md);
        CHECK_FALSE(ad.isValid());
}

TEST_CASE("AudioDesc_fromSdp_UnknownEncodingReturnsInvalid") {
        SdpMediaDescription md;
        md.setMediaType("audio");
        md.addPayloadType(96);
        md.setAttribute("rtpmap", "96 opus/48000/2");
        AudioDesc ad = AudioDesc::fromSdp(md);
        // Opus isn't wired yet — fromSdp should decline cleanly.
        CHECK_FALSE(ad.isValid());
}

TEST_CASE("AudioDesc_fromSdp_MissingRtpmapReturnsInvalid") {
        SdpMediaDescription md;
        md.setMediaType("audio");
        md.addPayloadType(96);
        // No rtpmap attribute.
        AudioDesc ad = AudioDesc::fromSdp(md);
        CHECK_FALSE(ad.isValid());
}

TEST_CASE("AudioDesc_fromSdp_L16_HighChannelCount") {
        SdpMediaDescription md;
        md.setMediaType("audio");
        md.addPayloadType(96);
        md.setAttribute("rtpmap", "96 L16/96000/16");
        AudioDesc ad = AudioDesc::fromSdp(md);
        CHECK(ad.isValid());
        CHECK(ad.format().id() == AudioFormat::PCMI_S16BE);
        CHECK(ad.sampleRate() == 96000.0f);
        CHECK(ad.channels() == 16);
}

// ============================================================================
// Format name access (delegated to AudioFormat)
// ============================================================================

TEST_CASE("AudioDesc_FormatName_native") {
        AudioDesc     desc(48000.0f, 2);
        const String &name = desc.format().name();
        CHECK(name.contains("Float32"));
}

TEST_CASE("AudioDesc_FormatName_S16LE") {
        AudioDesc desc(AudioFormat::PCMI_S16LE, 44100.0f, 1);
        CHECK(desc.format().name() == "PCMI_S16LE");
}

TEST_CASE("AudioDesc_FormatName_invalid") {
        AudioDesc desc;
        CHECK(desc.format().name() == "Invalid");
}

TEST_CASE("AudioDesc_FormatName_roundtrips_through_AudioFormat_lookup") {
        // Every PCM format name should round-trip through
        // value(AudioFormat::lookup()) — this replaces the old
        // AudioDesc::stringToDataType mechanism.
        AudioFormat::ID types[] = {
                AudioFormat::PCMI_Float32LE, AudioFormat::PCMI_Float32BE, AudioFormat::PCMI_S8,
                AudioFormat::PCMI_U8,        AudioFormat::PCMI_S16LE,     AudioFormat::PCMI_U16LE,
                AudioFormat::PCMI_S16BE,     AudioFormat::PCMI_U16BE,     AudioFormat::PCMI_S24LE,
                AudioFormat::PCMI_U24LE,     AudioFormat::PCMI_S24BE,     AudioFormat::PCMI_U24BE,
                AudioFormat::PCMI_S32LE,     AudioFormat::PCMI_U32LE,     AudioFormat::PCMI_S32BE,
                AudioFormat::PCMI_U32BE,
        };
        for (auto id : types) {
                AudioFormat fmt(id);
                CAPTURE(fmt.name());
                CHECK(value(AudioFormat::lookup(fmt.name())).id() == id);
        }
}

// ============================================================================
// Compressed audio
// ============================================================================

TEST_CASE("AudioDesc_Compressed_Opus") {
        AudioDesc desc(AudioFormat::Opus, 48000.0f, 2);
        CHECK(desc.isValid());
        CHECK(desc.isCompressed());
        CHECK(desc.format().audioCodec().id() == AudioCodec::Opus);
}

TEST_CASE("AudioDesc_Uncompressed_IsNotCompressed") {
        AudioDesc desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        CHECK(desc.isValid());
        CHECK_FALSE(desc.isCompressed());
}

// ============================================================================
// Channel map (default and explicit)
// ============================================================================

TEST_CASE("AudioDesc: default constructor installs default channel map") {
        AudioDesc d(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        CHECK(d.channelMap().channels() == 2);
        CHECK(d.channelMap().wellKnownName() == "Stereo");
}

TEST_CASE("AudioDesc: 6 channels defaults to 5.1 layout") {
        AudioDesc d(AudioFormat::PCMI_S16LE, 48000.0f, 6);
        CHECK(d.channelMap().wellKnownName() == "5.1");
}

TEST_CASE("AudioDesc: explicit channel-map constructor sets channels from map") {
        AudioChannelMap map = value(AudioChannelMap::fromString("7.1"));
        AudioDesc       d(AudioFormat::PCMI_S16LE, 48000.0f, std::move(map));
        CHECK(d.channels() == 8);
        CHECK(d.channelMap().wellKnownName() == "7.1");
}

TEST_CASE("AudioDesc: setChannels resets the channel map to the new default") {
        AudioDesc d(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        d.setChannels(6);
        CHECK(d.channelMap().wellKnownName() == "5.1");
}

TEST_CASE("AudioDesc: setChannelMap rejects mismatched length") {
        AudioDesc       d(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        AudioChannelMap fiveOne = value(AudioChannelMap::fromString("5.1"));
        d.setChannelMap(fiveOne);
        // 6-channel map on a 2-channel desc — refuse, leave the default in place.
        CHECK(d.channelMap().wellKnownName() == "Stereo");
}

TEST_CASE("AudioDesc: formatEquals compares the channel map") {
        AudioDesc a(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        AudioDesc b(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        CHECK(a.formatEquals(b));
        // Reorder the stereo map and the descriptors no longer match.
        b.setChannelMap(AudioChannelMap{ChannelRole::FrontRight, ChannelRole::FrontLeft});
        CHECK_FALSE(a.formatEquals(b));
}

TEST_CASE("AudioDesc: JSON omits ChannelMap when default") {
        AudioDesc  d(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        JsonObject j = d.toJson();
        CHECK_FALSE(j.contains("ChannelMap"));
}

TEST_CASE("AudioDesc: JSON includes ChannelMap when non-default") {
        AudioDesc d(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        d.setChannelMap(AudioChannelMap{ChannelRole::FrontRight, ChannelRole::FrontLeft});
        JsonObject j = d.toJson();
        CHECK(j.contains("ChannelMap"));
        AudioDesc back = AudioDesc::fromJson(j);
        CHECK(back.channelMap() == d.channelMap());
}

TEST_CASE("AudioDesc: DataStream round-trip preserves explicit channel map") {
        AudioDesc d(AudioFormat::PCMI_S16LE, 48000.0f, 6);
        d.setChannelMap(AudioChannelMap{ChannelRole::FrontLeft, ChannelRole::FrontCenter, ChannelRole::FrontRight,
                                        ChannelRole::SideLeft, ChannelRole::SideRight, ChannelRole::LFE});

        Buffer         buf(1024);
        BufferIODevice dev(&buf);
        REQUIRE(dev.open(IODevice::ReadWrite).isOk());
        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << d;
        }
        dev.seek(0);
        DataStream rs = DataStream::createReader(&dev);
        AudioDesc  back;
        rs >> back;
        CHECK(back == d);
        CHECK(back.channelMap() == d.channelMap());
        // Confirm the non-canonical 5.1 ordering survived (not promoted to "5.1").
        CHECK(back.channelMap().wellKnownName().isEmpty());
}
