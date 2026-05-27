/**
 * @file      flvtag.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <promeki/flvtag.h>

using namespace promeki;

namespace {

Buffer makePayload(std::initializer_list<uint8_t> bytes) {
        Buffer b(bytes.size() == 0 ? 1 : bytes.size());
        if (bytes.size() > 0) {
                std::memcpy(b.data(), &*bytes.begin(), bytes.size());
                b.setSize(bytes.size());
        }
        return b;
}

bool buffersEqual(const Buffer &a, const Buffer &b) {
        if (a.size() != b.size()) return false;
        if (a.size() == 0) return true;
        return std::memcmp(a.data(), b.data(), a.size()) == 0;
}

bool roundTripVideo(const FlvVideoTag &in) {
        Buffer wire;
        if (in.pack(wire).isError()) return false;
        FlvVideoTag round;
        BufferView  v(wire, 0, wire.size());
        if (FlvVideoTag::unpack(v, round).isError()) return false;
        return round.frameType == in.frameType && round.codec == in.codec &&
               round.packetType == in.packetType &&
               round.compositionTimeOffsetMs == in.compositionTimeOffsetMs &&
               buffersEqual(round.data, in.data);
}

bool roundTripAudio(const FlvAudioTag &in) {
        Buffer wire;
        if (in.pack(wire).isError()) return false;
        FlvAudioTag round;
        BufferView  v(wire, 0, wire.size());
        if (FlvAudioTag::unpack(v, round).isError()) return false;
        return round.format == in.format && round.rate == in.rate && round.size == in.size &&
               round.channelType == in.channelType && round.aacPacketType == in.aacPacketType &&
               buffersEqual(round.data, in.data);
}

} // namespace

// ----------------------------------------------------------------------------
// FlvVideoTag — legacy AVC
// ----------------------------------------------------------------------------

TEST_CASE("FlvVideoTag: legacy AVC keyframe sequence header wire layout") {
        FlvVideoTag t;
        t.frameType  = FlvVideoTag::Keyframe;
        t.codec      = FlvVideoTag::Avc;
        t.packetType = FlvVideoTag::SequenceHeader;
        t.compositionTimeOffsetMs = 0;
        t.data       = makePayload({0x01, 0x42, 0xc0, 0x1f});  // pretend avcC fragment

        Buffer w;
        REQUIRE(t.pack(w).isOk());
        REQUIRE(w.size() == 1 + 1 + 3 + 4);
        const uint8_t *p = static_cast<const uint8_t *>(w.data());
        // (FrameType=1, Codec=7) packed: 1<<4|7 = 0x17
        CHECK(p[0] == 0x17);
        CHECK(p[1] == 0x00);  // SequenceHeader
        CHECK(p[2] == 0x00);  // CTO[0]
        CHECK(p[3] == 0x00);  // CTO[1]
        CHECK(p[4] == 0x00);  // CTO[2]
        CHECK(p[5] == 0x01);
        CHECK(p[6] == 0x42);
        CHECK(p[7] == 0xc0);
        CHECK(p[8] == 0x1f);
}

TEST_CASE("FlvVideoTag: legacy AVC NALU with positive composition-time offset") {
        FlvVideoTag t;
        t.frameType               = FlvVideoTag::InterFrame;
        t.codec                   = FlvVideoTag::Avc;
        t.packetType              = FlvVideoTag::Nalu;
        t.compositionTimeOffsetMs = 33;
        t.data                    = makePayload({0xde, 0xad, 0xbe, 0xef});

        Buffer w;
        REQUIRE(t.pack(w).isOk());
        const uint8_t *p = static_cast<const uint8_t *>(w.data());
        // 2<<4 | 7 = 0x27
        CHECK(p[0] == 0x27);
        CHECK(p[1] == 0x01);
        CHECK(p[2] == 0x00);
        CHECK(p[3] == 0x00);
        CHECK(p[4] == 0x21);  // 33
        CHECK(roundTripVideo(t));
}

TEST_CASE("FlvVideoTag: legacy AVC NALU with negative composition-time offset") {
        FlvVideoTag t;
        t.frameType               = FlvVideoTag::InterFrame;
        t.codec                   = FlvVideoTag::Avc;
        t.packetType              = FlvVideoTag::Nalu;
        t.compositionTimeOffsetMs = -42;
        t.data                    = makePayload({0xaa});
        CHECK(roundTripVideo(t));
}

TEST_CASE("FlvVideoTag: legacy AVC NALU with maximal positive CTO (24-bit signed)") {
        FlvVideoTag t;
        t.frameType               = FlvVideoTag::InterFrame;
        t.codec                   = FlvVideoTag::Avc;
        t.packetType              = FlvVideoTag::Nalu;
        t.compositionTimeOffsetMs = 0x7FFFFF;
        t.data                    = makePayload({});
        CHECK(roundTripVideo(t));
}

TEST_CASE("FlvVideoTag: legacy AVC NALU with maximal negative CTO (24-bit signed)") {
        FlvVideoTag t;
        t.frameType               = FlvVideoTag::InterFrame;
        t.codec                   = FlvVideoTag::Avc;
        t.packetType              = FlvVideoTag::Nalu;
        t.compositionTimeOffsetMs = -0x800000;
        t.data                    = makePayload({});
        CHECK(roundTripVideo(t));
}

TEST_CASE("FlvVideoTag: pack rejects out-of-range CTO") {
        FlvVideoTag t;
        t.frameType               = FlvVideoTag::InterFrame;
        t.codec                   = FlvVideoTag::Avc;
        t.packetType              = FlvVideoTag::Nalu;
        t.compositionTimeOffsetMs = 0x800000;  // one past max positive
        Buffer w;
        CHECK(t.pack(w) == Error::OutOfRange);
}

TEST_CASE("FlvVideoTag: legacy AVC EndOfSequence carries no payload") {
        FlvVideoTag t;
        t.frameType               = FlvVideoTag::Keyframe;
        t.codec                   = FlvVideoTag::Avc;
        t.packetType              = FlvVideoTag::EndOfSequence;
        t.compositionTimeOffsetMs = 0;
        CHECK(roundTripVideo(t));
}

TEST_CASE("FlvVideoTag: legacy non-AVC codec round-trip (no AVCPacketType byte)") {
        FlvVideoTag t;
        t.frameType = FlvVideoTag::Keyframe;
        t.codec     = FlvVideoTag::H263;
        t.data      = makePayload({0x55, 0x66, 0x77});
        Buffer w;
        REQUIRE(t.pack(w).isOk());
        // 1<<4 | 2 = 0x12, then payload directly.
        REQUIRE(w.size() == 1 + 3);
        CHECK(static_cast<const uint8_t *>(w.data())[0] == 0x12);
}

// ----------------------------------------------------------------------------
// FlvVideoTag — Enhanced RTMP (HEVC)
// ----------------------------------------------------------------------------

TEST_CASE("FlvVideoTag: Enhanced HEVC sequence header wire layout") {
        FlvVideoTag t;
        t.frameType  = FlvVideoTag::Keyframe;
        t.codec      = FlvVideoTag::ExHevc;
        t.packetType = FlvVideoTag::SequenceHeader;
        t.data       = makePayload({0x01, 0x02, 0x03});

        Buffer w;
        REQUIRE(t.pack(w).isOk());
        const uint8_t *p = static_cast<const uint8_t *>(w.data());
        // High bit set, packetType=0 (SequenceStart), frameType=1
        // 0x80 | (0<<3) | 1 = 0x81
        CHECK(p[0] == 0x81);
        // FourCC = 'hvc1'
        CHECK(p[1] == 'h');
        CHECK(p[2] == 'v');
        CHECK(p[3] == 'c');
        CHECK(p[4] == '1');
        // No CTO for SequenceHeader.  Payload follows immediately.
        CHECK(p[5] == 0x01);
        CHECK(roundTripVideo(t));
}

TEST_CASE("FlvVideoTag: Enhanced HEVC keyframe NALU with CTO") {
        FlvVideoTag t;
        t.frameType               = FlvVideoTag::Keyframe;
        t.codec                   = FlvVideoTag::ExHevc;
        t.packetType              = FlvVideoTag::Nalu;
        t.compositionTimeOffsetMs = 17;
        t.data                    = makePayload({0xab, 0xcd});
        Buffer w;
        REQUIRE(t.pack(w).isOk());
        const uint8_t *p = static_cast<const uint8_t *>(w.data());
        // 0x80 | (1<<3) | 1 = 0x89
        CHECK(p[0] == 0x89);
        CHECK(roundTripVideo(t));
}

TEST_CASE("FlvVideoTag: Enhanced HEVC end-of-sequence") {
        FlvVideoTag t;
        t.frameType  = FlvVideoTag::Keyframe;
        t.codec      = FlvVideoTag::ExHevc;
        t.packetType = FlvVideoTag::EndOfSequence;
        CHECK(roundTripVideo(t));
}

TEST_CASE("FlvVideoTag: Enhanced VP9 / AV1 round-trip") {
        FlvVideoTag t;
        t.frameType  = FlvVideoTag::Keyframe;
        t.codec      = FlvVideoTag::ExVp9;
        t.packetType = FlvVideoTag::SequenceHeader;
        t.data       = makePayload({0xaa});
        CHECK(roundTripVideo(t));

        t.codec = FlvVideoTag::ExAv1;
        CHECK(roundTripVideo(t));
}

TEST_CASE("FlvVideoTag: Enhanced PacketTypeCodedFramesX (3) parses with CTO=0") {
        // Hand-roll a wire blob: header 0x80 | (3<<3) | 1 = 0x99, FourCC, payload.
        uint8_t wire[] = {0x99, 'h', 'v', 'c', '1', 0x42, 0x43};
        Buffer  buf(sizeof(wire));
        std::memcpy(buf.data(), wire, sizeof(wire));
        buf.setSize(sizeof(wire));
        FlvVideoTag out;
        REQUIRE(FlvVideoTag::unpack(BufferView(buf, 0, buf.size()), out).isOk());
        CHECK(out.codec == FlvVideoTag::ExHevc);
        CHECK(out.packetType == FlvVideoTag::Nalu);
        CHECK(out.compositionTimeOffsetMs == 0);
        REQUIRE(out.data.size() == 2);
        CHECK(static_cast<const uint8_t *>(out.data.data())[0] == 0x42);
        CHECK(static_cast<const uint8_t *>(out.data.data())[1] == 0x43);
}

TEST_CASE("FlvVideoTag: Enhanced unknown FourCC is NotSupported") {
        uint8_t wire[] = {0x81, 'X', 'X', 'X', 'X'};
        Buffer  buf(sizeof(wire));
        std::memcpy(buf.data(), wire, sizeof(wire));
        buf.setSize(sizeof(wire));
        FlvVideoTag out;
        CHECK(FlvVideoTag::unpack(BufferView(buf, 0, buf.size()), out) == Error::NotSupported);
}

TEST_CASE("FlvVideoTag: Enhanced PacketTypeMetadata (4) is NotSupported") {
        uint8_t wire[] = {static_cast<uint8_t>(0x80 | (4 << 3) | 1), 'h', 'v', 'c', '1'};
        Buffer  buf(sizeof(wire));
        std::memcpy(buf.data(), wire, sizeof(wire));
        buf.setSize(sizeof(wire));
        FlvVideoTag out;
        CHECK(FlvVideoTag::unpack(BufferView(buf, 0, buf.size()), out) == Error::NotSupported);
}

TEST_CASE("FlvVideoTag: empty input returns OutOfRange") {
        Buffer      empty;
        FlvVideoTag out;
        CHECK(FlvVideoTag::unpack(BufferView(), out) == Error::OutOfRange);
}

TEST_CASE("FlvVideoTag: truncated AVC header returns OutOfRange") {
        uint8_t wire[] = {0x17, 0x00};  // missing 3-byte CTO
        Buffer  buf(sizeof(wire));
        std::memcpy(buf.data(), wire, sizeof(wire));
        buf.setSize(sizeof(wire));
        FlvVideoTag out;
        CHECK(FlvVideoTag::unpack(BufferView(buf, 0, buf.size()), out) == Error::OutOfRange);
}

// ----------------------------------------------------------------------------
// FlvAudioTag
// ----------------------------------------------------------------------------

TEST_CASE("FlvAudioTag: AAC raw frame wire layout") {
        FlvAudioTag t;
        t.format        = FlvAudioTag::Aac;
        t.rate          = FlvAudioTag::Rate44000;
        t.size          = FlvAudioTag::Bits16;
        t.channelType   = FlvAudioTag::Stereo;
        t.aacPacketType = FlvAudioTag::Raw;
        t.data          = makePayload({0xde, 0xad});

        Buffer w;
        REQUIRE(t.pack(w).isOk());
        const uint8_t *p = static_cast<const uint8_t *>(w.data());
        // (10 << 4) | (3 << 2) | (1 << 1) | 1 = 0xAF
        CHECK(p[0] == 0xAF);
        CHECK(p[1] == 0x01);
        CHECK(p[2] == 0xde);
        CHECK(p[3] == 0xad);
        CHECK(roundTripAudio(t));
}

TEST_CASE("FlvAudioTag: AAC AudioSpecificConfig packet") {
        FlvAudioTag t;
        t.format        = FlvAudioTag::Aac;
        t.aacPacketType = FlvAudioTag::AudioSpecificConfig;
        t.data          = makePayload({0x12, 0x10});  // minimal AudioSpecificConfig (AAC-LC, 44.1k, stereo)
        CHECK(roundTripAudio(t));
}

TEST_CASE("FlvAudioTag: non-AAC codec has no AacPacketType byte") {
        FlvAudioTag t;
        t.format      = FlvAudioTag::Mp3;
        t.rate        = FlvAudioTag::Rate44000;
        t.size        = FlvAudioTag::Bits16;
        t.channelType = FlvAudioTag::Stereo;
        t.data        = makePayload({0xff, 0xfb});  // mp3 frame head bytes
        Buffer w;
        REQUIRE(t.pack(w).isOk());
        // (2<<4) | (3<<2) | (1<<1) | 1 = 0x2F
        CHECK(static_cast<const uint8_t *>(w.data())[0] == 0x2F);
        REQUIRE(w.size() == 1 + 2);
        CHECK(roundTripAudio(t));
}

TEST_CASE("FlvAudioTag: empty input returns OutOfRange") {
        FlvAudioTag out;
        CHECK(FlvAudioTag::unpack(BufferView(), out) == Error::OutOfRange);
}

TEST_CASE("FlvAudioTag: truncated AAC packet returns OutOfRange") {
        uint8_t wire[] = {0xAF};  // missing AacPacketType
        Buffer  buf(sizeof(wire));
        std::memcpy(buf.data(), wire, sizeof(wire));
        buf.setSize(sizeof(wire));
        FlvAudioTag out;
        CHECK(FlvAudioTag::unpack(BufferView(buf, 0, buf.size()), out) == Error::OutOfRange);
}

// ----------------------------------------------------------------------------
// FlvScriptTag
// ----------------------------------------------------------------------------

TEST_CASE("FlvScriptTag: onMetaData round-trip") {
        FlvScriptTag t;
        t.name = "onMetaData";
        t.body = Amf0Value::ecmaArray({{"width", 1920.0},
                                       {"height", 1080.0},
                                       {"framerate", 30.0},
                                       {"videocodecid", 7.0},
                                       {"audiocodecid", 10.0}},
                                      5);

        Buffer w;
        REQUIRE(t.pack(w).isOk());

        FlvScriptTag round;
        REQUIRE(FlvScriptTag::unpack(BufferView(w, 0, w.size()), round).isOk());
        CHECK(round.name == "onMetaData");
        REQUIRE(round.body.isEcmaArray());
        CHECK(round.body == t.body);
}

TEST_CASE("FlvScriptTag: input with non-string name is CorruptData") {
        // Hand-roll: AMF0 number then null.
        Buffer     buf(64);
        buf.setSize(0);
        Amf0Writer w(buf);
        REQUIRE(w.writeNumber(1.0).isOk());
        REQUIRE(w.writeNull().isOk());

        FlvScriptTag out;
        CHECK(FlvScriptTag::unpack(BufferView(buf, 0, buf.size()), out) == Error::CorruptData);
}

TEST_CASE("FlvScriptTag: input with only one value is CorruptData") {
        Buffer     buf(32);
        buf.setSize(0);
        Amf0Writer w(buf);
        REQUIRE(w.writeString("onMetaData").isOk());

        FlvScriptTag out;
        CHECK(FlvScriptTag::unpack(BufferView(buf, 0, buf.size()), out) == Error::CorruptData);
}
