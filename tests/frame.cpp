/**
 * @file      frame.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/frame.h>
#include <promeki/mediapacket.h>
#include <promeki/buffer.h>
#include <promeki/pixeldesc.h>

using namespace promeki;

TEST_CASE("Frame: default construction") {
        Frame f;
        CHECK(f.imageList().isEmpty());
        CHECK(f.audioList().isEmpty());
        CHECK(f.packetList().isEmpty());
}

TEST_CASE("Frame: metadata access") {
        Frame f;
        const auto &md = f.metadata();
        CHECK(md.isEmpty());
}

TEST_CASE("Frame: packetList carries compressed access units") {
        Frame f;
        auto buf = Buffer::Ptr::create(32);
        buf.modify()->setSize(16);
        auto pkt = MediaPacket::Ptr::create(buf, PixelDesc(PixelDesc::H264));
        pkt.modify()->addFlag(MediaPacket::Keyframe);

        f.packetList().pushToBack(pkt);

        // Const accessor returns the same element.
        const Frame &cf = f;
        REQUIRE(cf.packetList().size() == 1);
        CHECK(cf.packetList().at(0)->isKeyframe());
        CHECK(cf.packetList().at(0)->pixelDesc().id() == PixelDesc::H264);
}
