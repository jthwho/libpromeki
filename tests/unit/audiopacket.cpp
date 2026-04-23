/**
 * @file      tests/audiopacket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Direct coverage of the AudioPacket class — every constructor,
 * accessor, and inherited MediaPacket helper that AudioPacket exposes.
 * AudioPacket is also exercised end-to-end through the AudioEncoder
 * passthrough tests, but those tests can't reach the codec-identity
 * branch in @ref AudioPacket::isValid or the @c BufferView constructor
 * variant — which is what this file is for.
 */

#include <doctest/doctest.h>
#include <promeki/audiopacket.h>
#include <promeki/audiocodec.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/mediapacket.h>
#include <promeki/metadata.h>

using namespace promeki;

namespace {

Buffer::Ptr makeBuf(size_t n, char fill) {
        auto buf = Buffer::Ptr::create(n);
        buf.modify()->fill(fill);
        buf.modify()->setSize(n);
        return buf;
}

} // namespace

TEST_CASE("AudioPacket: default ctor yields invalid packet") {
        AudioPacket p;
        CHECK_FALSE(p.isValid());
        CHECK(p.kind() == MediaPacket::Audio);
        CHECK(p.size() == 0);
        CHECK_FALSE(p.audioCodec().isValid());
}

TEST_CASE("AudioPacket: BufferView + AudioCodec ctor") {
        Buffer::Ptr buf = makeBuf(64, 0x42);
        BufferView view(buf, 0, buf->size());
        AudioCodec codec(AudioCodec::Opus);

        AudioPacket p(view, codec);
        CHECK(p.isValid());
        CHECK(p.kind() == MediaPacket::Audio);
        CHECK(p.size() == 64);
        CHECK(p.audioCodec() == codec);
        CHECK(p.view().data() == buf->data());
}

TEST_CASE("AudioPacket: BufferView ctor with sub-range") {
        Buffer::Ptr buf = makeBuf(128, 0x00);
        BufferView view(buf, 16, 32);
        AudioPacket p(view, AudioCodec(AudioCodec::AAC));
        CHECK(p.isValid());
        CHECK(p.size() == 32);
}

TEST_CASE("AudioPacket: Buffer::Ptr + AudioCodec ctor wraps whole buffer") {
        Buffer::Ptr buf = makeBuf(48, 0x55);
        AudioPacket p(buf, AudioCodec(AudioCodec::FLAC));
        CHECK(p.isValid());
        CHECK(p.size() == 48);
        CHECK(p.audioCodec().id() == AudioCodec::FLAC);
}

TEST_CASE("AudioPacket: Buffer::Ptr ctor with null buffer yields invalid view") {
        Buffer::Ptr empty;
        AudioPacket p(empty, AudioCodec(AudioCodec::Opus));
        CHECK_FALSE(p.isValid());
        CHECK(p.size() == 0);
        // Codec is valid but payload is missing — overall packet is invalid.
        CHECK(p.audioCodec().isValid());
}

TEST_CASE("AudioPacket: isValid demands payload AND codec") {
        Buffer::Ptr buf = makeBuf(8, 0xFF);

        // Payload present, codec missing.
        AudioPacket noCodec;
        noCodec.setBuffer(buf);
        CHECK_FALSE(noCodec.isValid());

        // Codec set, payload missing.
        AudioPacket noPayload;
        noPayload.setAudioCodec(AudioCodec(AudioCodec::Opus));
        CHECK_FALSE(noPayload.isValid());

        // Both present.
        AudioPacket both;
        both.setBuffer(buf);
        both.setAudioCodec(AudioCodec(AudioCodec::Opus));
        CHECK(both.isValid());
}

TEST_CASE("AudioPacket: setAudioCodec replaces codec identity") {
        AudioPacket p(makeBuf(4, 0x11), AudioCodec(AudioCodec::Opus));
        REQUIRE(p.audioCodec().id() == AudioCodec::Opus);
        p.setAudioCodec(AudioCodec(AudioCodec::AAC));
        CHECK(p.audioCodec().id() == AudioCodec::AAC);
}

TEST_CASE("AudioPacket: copy ctor and assignment preserve fields") {
        AudioPacket orig(makeBuf(16, 0xAA), AudioCodec(AudioCodec::Opus));
        orig.setPts(MediaTimeStamp());
        orig.setDuration(Duration::fromMilliseconds(20));
        orig.markEndOfStream();

        AudioPacket copy(orig);
        CHECK(copy.isValid());
        CHECK(copy.audioCodec().id() == AudioCodec::Opus);
        CHECK(copy.size() == 16);
        CHECK(copy.duration() == Duration::fromMilliseconds(20));
        CHECK(copy.isEndOfStream());

        AudioPacket assigned;
        assigned = orig;
        CHECK(assigned.isValid());
        CHECK(assigned.audioCodec().id() == AudioCodec::Opus);
        CHECK(assigned.isEndOfStream());
}

TEST_CASE("AudioPacket: move ctor and assignment work") {
        AudioPacket src(makeBuf(8, 0x77), AudioCodec(AudioCodec::FLAC));
        AudioPacket moved(std::move(src));
        CHECK(moved.isValid());
        CHECK(moved.audioCodec().id() == AudioCodec::FLAC);

        AudioPacket assigned;
        AudioPacket src2(makeBuf(8, 0x88), AudioCodec(AudioCodec::MP3));
        assigned = std::move(src2);
        CHECK(assigned.isValid());
        CHECK(assigned.audioCodec().id() == AudioCodec::MP3);
}

TEST_CASE("AudioPacket: _promeki_clone returns AudioPacket subtype") {
        AudioPacket orig(makeBuf(12, 0x33), AudioCodec(AudioCodec::Opus));
        orig.setDuration(Duration::fromMilliseconds(10));
        std::unique_ptr<AudioPacket> cloned(orig._promeki_clone());
        REQUIRE(cloned != nullptr);
        CHECK(cloned->kind() == MediaPacket::Audio);
        CHECK(cloned->isValid());
        CHECK(cloned->size() == 12);
        CHECK(cloned->audioCodec().id() == AudioCodec::Opus);
        CHECK(cloned->duration() == Duration::fromMilliseconds(10));
}

TEST_CASE("AudioPacket: end-of-stream marker via base helpers") {
        AudioPacket p;
        p.setAudioCodec(AudioCodec(AudioCodec::Opus));
        CHECK_FALSE(p.isEndOfStream());
        p.markEndOfStream();
        CHECK(p.isEndOfStream());
        // Even with EOS marked, isValid still requires a payload.
        CHECK_FALSE(p.isValid());
}

TEST_CASE("AudioPacket: corruption marker round-trips") {
        AudioPacket p(makeBuf(4, 0x44), AudioCodec(AudioCodec::Opus));
        CHECK_FALSE(p.isCorrupt());
        p.markCorrupt("decoder lost sync");
        CHECK(p.isCorrupt());
        CHECK(p.corruptReason() == "decoder lost sync");
}

TEST_CASE("AudioPacket: Ptr::create produces a polymorphic AudioPacket") {
        AudioPacket::Ptr p = AudioPacket::Ptr::create(makeBuf(20, 0x99),
                                                     AudioCodec(AudioCodec::Opus));
        REQUIRE(p);
        CHECK(p->isValid());
        CHECK(p->kind() == MediaPacket::Audio);
        CHECK(p->audioCodec().id() == AudioCodec::Opus);

        // Hand it off as a base MediaPacket::Ptr — the converting ctor
        // on SharedPtr must preserve the AudioPacket vtable.
        MediaPacket::Ptr base = p;
        REQUIRE(base);
        CHECK(base->kind() == MediaPacket::Audio);
}

TEST_CASE("AudioPacket: PtrList holds shared AudioPacket references") {
        AudioPacket::PtrList list;
        list.pushToBack(AudioPacket::Ptr::create(makeBuf(4, 0xAA),
                                                 AudioCodec(AudioCodec::Opus)));
        list.pushToBack(AudioPacket::Ptr::create(makeBuf(8, 0xBB),
                                                 AudioCodec(AudioCodec::AAC)));
        REQUIRE(list.size() == 2);
        CHECK(list[0]->audioCodec().id() == AudioCodec::Opus);
        CHECK(list[1]->audioCodec().id() == AudioCodec::AAC);
        CHECK(list[1]->size() == 8);
}
