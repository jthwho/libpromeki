/**
 * @file      audiodesc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/audiodesc.h>
#include <promeki/sdpsession.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("AudioDesc_Default") {
    AudioDesc desc;
    CHECK(!desc.isValid());
    CHECK(desc.dataType() == AudioDesc::Invalid);
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
    AudioDesc desc(AudioDesc::PCMI_S16LE, 44100.0f, 1);
    CHECK(desc.isValid());
    CHECK(desc.dataType() == AudioDesc::PCMI_S16LE);
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

TEST_CASE("AudioDesc_SetDataType") {
    AudioDesc desc(48000.0f, 2);
    desc.setDataType(AudioDesc::PCMI_S16LE);
    CHECK(desc.dataType() == AudioDesc::PCMI_S16LE);
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
    AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
    // 2 bytes per sample * 2 channels * 1024 samples = 4096
    CHECK(desc.bufferSize(1024) == 4096);
}

TEST_CASE("AudioDesc_BytesPerSampleStride") {
    AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
    // Interleaved: bytesPerSample * channels = 2 * 2 = 4
    CHECK(desc.bytesPerSampleStride() == 4);
}

// ============================================================================
// Working desc
// ============================================================================

TEST_CASE("AudioDesc_WorkingDesc") {
    AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
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
    String s = desc.toString();
    CHECK(s.size() > 0);
}

// ============================================================================
// Metadata
// ============================================================================

TEST_CASE("AudioDesc_Metadata") {
    AudioDesc desc(48000.0f, 2);
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
    AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
    JsonObject json = desc.toJson();
    CHECK(json.contains("DataType"));
    CHECK(json.contains("SampleRate"));
    CHECK(json.contains("Channels"));
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
    CHECK(ad.dataType() == AudioDesc::PCMI_S16BE);
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
    CHECK(ad.dataType() == AudioDesc::PCMI_S24BE);
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
    CHECK(ad.dataType() == AudioDesc::PCMI_U8);
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
    CHECK(ad.dataType() == AudioDesc::PCMI_S16BE);
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
    CHECK(ad.dataType() == AudioDesc::PCMI_S16BE);
    CHECK(ad.sampleRate() == 96000.0f);
    CHECK(ad.channels() == 16);
}

// ============================================================================
// dataTypeName
// ============================================================================

TEST_CASE("AudioDesc_dataTypeName_native") {
    AudioDesc desc(48000.0f, 2);
    const String &name = desc.dataTypeName();
    CHECK(name.contains("Float32"));
}

TEST_CASE("AudioDesc_dataTypeName_S16LE") {
    AudioDesc desc(AudioDesc::PCMI_S16LE, 44100.0f, 1);
    CHECK(desc.dataTypeName() == "PCMI_S16LE");
}

TEST_CASE("AudioDesc_dataTypeName_invalid") {
    AudioDesc desc;
    CHECK(desc.dataTypeName() == "InvalidAudioFormat");
}

TEST_CASE("AudioDesc_dataTypeName_roundtrips_with_stringToDataType") {
    // Every PCM DataType should round-trip through dataTypeName → stringToDataType
    AudioDesc::DataType types[] = {
        AudioDesc::PCMI_Float32LE, AudioDesc::PCMI_Float32BE,
        AudioDesc::PCMI_S8,       AudioDesc::PCMI_U8,
        AudioDesc::PCMI_S16LE,    AudioDesc::PCMI_U16LE,
        AudioDesc::PCMI_S16BE,    AudioDesc::PCMI_U16BE,
        AudioDesc::PCMI_S24LE,    AudioDesc::PCMI_U24LE,
        AudioDesc::PCMI_S24BE,    AudioDesc::PCMI_U24BE,
        AudioDesc::PCMI_S32LE,    AudioDesc::PCMI_U32LE,
        AudioDesc::PCMI_S32BE,    AudioDesc::PCMI_U32BE,
    };
    for(auto dt : types) {
        AudioDesc desc(dt, 48000.0f, 2);
        CAPTURE(desc.dataTypeName());
        CHECK(AudioDesc::stringToDataType(desc.dataTypeName()) == dt);
    }
}
