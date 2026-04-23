/**
 * @file      tests/videopacket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Direct coverage of the VideoPacket class — every constructor, flag
 * helper, and inherited MediaPacket helper that VideoPacket exposes.
 * VideoPacket is exercised end-to-end by the JpegVideoCodec / Opus /
 * passthrough tests, but those tests only touch the @c addFlag /
 * @c isKeyframe surface; this file pins down the rest of the API
 * (removeFlag, setFlags, isParameterSet, isDiscardable, copy/move,
 * polymorphic clone) so a regression on those helpers fails loudly.
 */

#include <doctest/doctest.h>
#include <promeki/videopacket.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/mediapacket.h>
#include <promeki/pixelformat.h>

using namespace promeki;

namespace {

Buffer::Ptr makeBuf(size_t n, char fill) {
        auto buf = Buffer::Ptr::create(n);
        buf.modify()->fill(fill);
        buf.modify()->setSize(n);
        return buf;
}

} // namespace

TEST_CASE("VideoPacket: default ctor is invalid, kind is Video") {
        VideoPacket p;
        CHECK_FALSE(p.isValid());
        CHECK(p.kind() == MediaPacket::Video);
        CHECK(p.size() == 0);
        CHECK(p.flags() == VideoPacket::None);
        CHECK_FALSE(p.pixelFormat().isValid());
}

TEST_CASE("VideoPacket: BufferView + PixelFormat ctor populates payload + identity") {
        Buffer::Ptr buf = makeBuf(64, 0x42);
        BufferView view(buf, 0, buf->size());
        PixelFormat pf(PixelFormat::H264);

        VideoPacket p(view, pf);
        CHECK(p.isValid());
        CHECK(p.size() == 64);
        CHECK(p.pixelFormat() == pf);
}

TEST_CASE("VideoPacket: BufferView ctor with sub-range") {
        Buffer::Ptr buf = makeBuf(256, 0xAA);
        BufferView view(buf, 32, 100);
        VideoPacket p(view, PixelFormat(PixelFormat::JPEG_RGB8_sRGB));
        CHECK(p.isValid());
        CHECK(p.size() == 100);
}

TEST_CASE("VideoPacket: Buffer::Ptr ctor wraps the whole buffer") {
        Buffer::Ptr buf = makeBuf(48, 0x11);
        VideoPacket p(buf, PixelFormat(PixelFormat::HEVC));
        CHECK(p.isValid());
        CHECK(p.size() == 48);
        CHECK(p.pixelFormat().id() == PixelFormat::HEVC);
}

TEST_CASE("VideoPacket: Buffer::Ptr ctor with null buffer yields invalid view") {
        Buffer::Ptr empty;
        VideoPacket p(empty, PixelFormat(PixelFormat::H264));
        CHECK_FALSE(p.isValid());
        CHECK(p.size() == 0);
        // PixelFormat is set, but payload is missing → packet invalid.
        CHECK(p.pixelFormat().isValid());
}

TEST_CASE("VideoPacket: isValid demands payload AND pixel format") {
        Buffer::Ptr buf = makeBuf(8, 0x55);

        VideoPacket noPf;
        noPf.setBuffer(buf);
        CHECK_FALSE(noPf.isValid());

        VideoPacket noPayload;
        noPayload.setPixelFormat(PixelFormat(PixelFormat::H264));
        CHECK_FALSE(noPayload.isValid());

        VideoPacket both;
        both.setBuffer(buf);
        both.setPixelFormat(PixelFormat(PixelFormat::H264));
        CHECK(both.isValid());
}

TEST_CASE("VideoPacket: setPixelFormat replaces the codec identity") {
        VideoPacket p(makeBuf(4, 0x33), PixelFormat(PixelFormat::H264));
        REQUIRE(p.pixelFormat().id() == PixelFormat::H264);
        p.setPixelFormat(PixelFormat(PixelFormat::HEVC));
        CHECK(p.pixelFormat().id() == PixelFormat::HEVC);
}

// ---------------------------------------------------------------------------
// Flags
// ---------------------------------------------------------------------------

TEST_CASE("VideoPacket: addFlag / hasFlag / removeFlag") {
        VideoPacket p;
        CHECK_FALSE(p.hasFlag(VideoPacket::Keyframe));

        p.addFlag(VideoPacket::Keyframe);
        CHECK(p.hasFlag(VideoPacket::Keyframe));
        CHECK(p.isKeyframe());

        p.addFlag(VideoPacket::Discardable);
        CHECK(p.hasFlag(VideoPacket::Discardable));
        CHECK(p.isDiscardable());

        p.addFlag(VideoPacket::ParameterSet);
        CHECK(p.isParameterSet());

        // removeFlag clears one bit and leaves the rest alone.
        p.removeFlag(VideoPacket::Keyframe);
        CHECK_FALSE(p.isKeyframe());
        CHECK(p.isDiscardable());
        CHECK(p.isParameterSet());

        // Removing a flag that wasn't set is a no-op.
        p.removeFlag(VideoPacket::Keyframe);
        CHECK_FALSE(p.isKeyframe());
}

TEST_CASE("VideoPacket: setFlags replaces the entire bitmask") {
        VideoPacket p;
        p.setFlags(VideoPacket::Keyframe | VideoPacket::ParameterSet);
        CHECK(p.flags() == (VideoPacket::Keyframe | VideoPacket::ParameterSet));
        CHECK(p.isKeyframe());
        CHECK(p.isParameterSet());
        CHECK_FALSE(p.isDiscardable());

        p.setFlags(VideoPacket::None);
        CHECK(p.flags() == VideoPacket::None);
        CHECK_FALSE(p.isKeyframe());
        CHECK_FALSE(p.isParameterSet());
}

TEST_CASE("VideoPacket: convenience predicates reflect flag bits") {
        VideoPacket p;
        CHECK_FALSE(p.isKeyframe());
        CHECK_FALSE(p.isParameterSet());
        CHECK_FALSE(p.isDiscardable());

        p.addFlag(VideoPacket::Keyframe);
        CHECK(p.isKeyframe());

        p.addFlag(VideoPacket::ParameterSet);
        CHECK(p.isParameterSet());

        p.addFlag(VideoPacket::Discardable);
        CHECK(p.isDiscardable());
}

// ---------------------------------------------------------------------------
// Copy / move / clone
// ---------------------------------------------------------------------------

TEST_CASE("VideoPacket: copy ctor and assignment preserve fields and flags") {
        VideoPacket src(makeBuf(16, 0xCC), PixelFormat(PixelFormat::H264));
        src.addFlag(VideoPacket::Keyframe);
        src.setDuration(Duration::fromMilliseconds(33));
        src.markEndOfStream();

        VideoPacket copy(src);
        CHECK(copy.isValid());
        CHECK(copy.pixelFormat().id() == PixelFormat::H264);
        CHECK(copy.size() == 16);
        CHECK(copy.isKeyframe());
        CHECK(copy.duration() == Duration::fromMilliseconds(33));
        CHECK(copy.isEndOfStream());

        VideoPacket assigned;
        assigned = src;
        CHECK(assigned.isKeyframe());
        CHECK(assigned.pixelFormat().id() == PixelFormat::H264);
}

TEST_CASE("VideoPacket: move ctor and assignment work") {
        VideoPacket src(makeBuf(8, 0x77), PixelFormat(PixelFormat::H264));
        src.addFlag(VideoPacket::Keyframe);
        VideoPacket moved(std::move(src));
        CHECK(moved.isValid());
        CHECK(moved.isKeyframe());

        VideoPacket assigned;
        VideoPacket src2(makeBuf(8, 0x88), PixelFormat(PixelFormat::HEVC));
        assigned = std::move(src2);
        CHECK(assigned.isValid());
        CHECK(assigned.pixelFormat().id() == PixelFormat::HEVC);
}

TEST_CASE("VideoPacket: _promeki_clone returns VideoPacket subtype") {
        VideoPacket orig(makeBuf(12, 0x33), PixelFormat(PixelFormat::H264));
        orig.addFlag(VideoPacket::Keyframe);
        VideoPacket::UPtr cloned = VideoPacket::UPtr::takeOwnership(orig._promeki_clone());
        REQUIRE(cloned != nullptr);
        CHECK(cloned->kind() == MediaPacket::Video);
        CHECK(cloned->isValid());
        CHECK(cloned->size() == 12);
        CHECK(cloned->isKeyframe());
        CHECK(cloned->pixelFormat().id() == PixelFormat::H264);
}

// ---------------------------------------------------------------------------
// Ptr / PtrList
// ---------------------------------------------------------------------------

TEST_CASE("VideoPacket: Ptr::create produces a polymorphic VideoPacket") {
        VideoPacket::Ptr p = VideoPacket::Ptr::create(makeBuf(20, 0x99),
                                                     PixelFormat(PixelFormat::H264));
        REQUIRE(p);
        CHECK(p->isValid());
        CHECK(p->kind() == MediaPacket::Video);
        CHECK(p->pixelFormat().id() == PixelFormat::H264);

        // Hand it off as a base MediaPacket::Ptr — the converting ctor
        // on SharedPtr must preserve the VideoPacket vtable.
        MediaPacket::Ptr base = p;
        REQUIRE(base);
        CHECK(base->kind() == MediaPacket::Video);
}

TEST_CASE("VideoPacket: PtrList holds shared VideoPacket references") {
        VideoPacket::PtrList list;
        list.pushToBack(VideoPacket::Ptr::create(makeBuf(4, 0xAA),
                                                 PixelFormat(PixelFormat::H264)));
        list.pushToBack(VideoPacket::Ptr::create(makeBuf(8, 0xBB),
                                                 PixelFormat(PixelFormat::HEVC)));
        REQUIRE(list.size() == 2);
        CHECK(list[0]->pixelFormat().id() == PixelFormat::H264);
        CHECK(list[1]->pixelFormat().id() == PixelFormat::HEVC);
        CHECK(list[1]->size() == 8);
}

TEST_CASE("VideoPacket: end-of-stream marker via base helpers") {
        VideoPacket p;
        p.setPixelFormat(PixelFormat(PixelFormat::H264));
        CHECK_FALSE(p.isEndOfStream());
        p.markEndOfStream();
        CHECK(p.isEndOfStream());
}
