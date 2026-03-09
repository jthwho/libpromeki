/**
 * @file      audiodesc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#include <promeki/unittest.h>
#include <promeki/audiodesc.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

PROMEKI_TEST_BEGIN(AudioDesc_Default)
    AudioDesc desc;
    PROMEKI_TEST(!desc.isValid());
    PROMEKI_TEST(desc.dataType() == AudioDesc::Invalid);
    PROMEKI_TEST(desc.referenceCount() == 1);
PROMEKI_TEST_END()

// ============================================================================
// Construction
// ============================================================================

PROMEKI_TEST_BEGIN(AudioDesc_Construct)
    AudioDesc desc(48000.0f, 2);
    PROMEKI_TEST(desc.isValid());
    PROMEKI_TEST(desc.sampleRate() > 47999.0f);
    PROMEKI_TEST(desc.sampleRate() < 48001.0f);
    PROMEKI_TEST(desc.channels() == 2);
    PROMEKI_TEST(desc.isNative());
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(AudioDesc_ConstructWithType)
    AudioDesc desc(AudioDesc::PCMI_S16LE, 44100.0f, 1);
    PROMEKI_TEST(desc.isValid());
    PROMEKI_TEST(desc.dataType() == AudioDesc::PCMI_S16LE);
    PROMEKI_TEST(desc.sampleRate() > 44099.0f);
    PROMEKI_TEST(desc.channels() == 1);
    PROMEKI_TEST(desc.bytesPerSample() == 2);
PROMEKI_TEST_END()

// ============================================================================
// Setters
// ============================================================================

PROMEKI_TEST_BEGIN(AudioDesc_SetSampleRate)
    AudioDesc desc(48000.0f, 2);
    desc.setSampleRate(96000.0f);
    PROMEKI_TEST(desc.sampleRate() > 95999.0f);
    PROMEKI_TEST(desc.sampleRate() < 96001.0f);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(AudioDesc_SetChannels)
    AudioDesc desc(48000.0f, 2);
    desc.setChannels(8);
    PROMEKI_TEST(desc.channels() == 8);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(AudioDesc_SetDataType)
    AudioDesc desc(48000.0f, 2);
    desc.setDataType(AudioDesc::PCMI_S16LE);
    PROMEKI_TEST(desc.dataType() == AudioDesc::PCMI_S16LE);
PROMEKI_TEST_END()

// ============================================================================
// Copy-on-write
// ============================================================================

PROMEKI_TEST_BEGIN(AudioDesc_CopyOnWrite)
    AudioDesc d1(48000.0f, 2);
    AudioDesc d2 = d1;
    PROMEKI_TEST(d1.referenceCount() == 2);

    d2.setSampleRate(96000.0f);
    PROMEKI_TEST(d1.referenceCount() == 1);
    PROMEKI_TEST(d2.referenceCount() == 1);
    PROMEKI_TEST(d1.sampleRate() < 48001.0f);
    PROMEKI_TEST(d2.sampleRate() > 95999.0f);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(AudioDesc_CopyOnWriteChannels)
    AudioDesc d1(48000.0f, 2);
    AudioDesc d2 = d1;
    PROMEKI_TEST(d1.referenceCount() == 2);

    d2.setChannels(8);
    PROMEKI_TEST(d1.referenceCount() == 1);
    PROMEKI_TEST(d1.channels() == 2);
    PROMEKI_TEST(d2.channels() == 8);
PROMEKI_TEST_END()

// ============================================================================
// Buffer size calculation
// ============================================================================

PROMEKI_TEST_BEGIN(AudioDesc_BufferSize)
    AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
    // 2 bytes per sample * 2 channels * 1024 samples = 4096
    PROMEKI_TEST(desc.bufferSize(1024) == 4096);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(AudioDesc_BytesPerSampleStride)
    AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
    // Interleaved: bytesPerSample * channels = 2 * 2 = 4
    PROMEKI_TEST(desc.bytesPerSampleStride() == 4);
PROMEKI_TEST_END()

// ============================================================================
// Working desc
// ============================================================================

PROMEKI_TEST_BEGIN(AudioDesc_WorkingDesc)
    AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
    AudioDesc working = desc.workingDesc();
    PROMEKI_TEST(working.isValid());
    PROMEKI_TEST(working.isNative());
    PROMEKI_TEST(working.channels() == 2);
    PROMEKI_TEST(working.sampleRate() > 47999.0f);
PROMEKI_TEST_END()

// ============================================================================
// toString
// ============================================================================

PROMEKI_TEST_BEGIN(AudioDesc_ToString)
    AudioDesc desc(48000.0f, 2);
    String s = desc.toString();
    PROMEKI_TEST(s.size() > 0);
PROMEKI_TEST_END()

// ============================================================================
// Metadata
// ============================================================================

PROMEKI_TEST_BEGIN(AudioDesc_Metadata)
    AudioDesc desc(48000.0f, 2);
    const Metadata &cm = desc.metadata();
    PROMEKI_TEST(cm.isEmpty());

    desc.metadata().set(Metadata::Artist, String("Test Artist"));
    PROMEKI_TEST(!desc.metadata().isEmpty());
    PROMEKI_TEST(desc.metadata().get(Metadata::Artist).get<String>() == "Test Artist");
PROMEKI_TEST_END()

// ============================================================================
// JSON round-trip
// ============================================================================

PROMEKI_TEST_BEGIN(AudioDesc_ToJson)
    AudioDesc desc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
    JsonObject json = desc.toJson();
    PROMEKI_TEST(json.contains("DataType"));
    PROMEKI_TEST(json.contains("SampleRate"));
    PROMEKI_TEST(json.contains("Channels"));
PROMEKI_TEST_END()
