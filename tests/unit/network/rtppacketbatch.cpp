/**
 * @file      rtppacketbatch.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/duration.h>
#include <promeki/framenumber.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppacketbatch.h>
#include <promeki/timestamp.h>

using namespace promeki;

TEST_CASE("RtpPacketBatch: default-constructed shape") {
        RtpPacketBatch b;
        CHECK(b.packets.isEmpty());
        CHECK_FALSE(b.frameIndex.isValid());
        CHECK(b.clockRate == 0u);
        CHECK(b.markerOnLast == true);
        CHECK(b.rateCapBps == 0u);
}

TEST_CASE("RtpPacketBatch: round-trip a populated batch") {
        RtpPacketBatch b;
        b.packets = RtpPacket::createList(/*count=*/3, /*packetSize=*/100);
        b.frameIndex = FrameNumber(42);
        b.clockRate = 90000u;
        b.markerOnLast = true;
        b.rateCapBps = 50'000'000u;
        b.enqueuedAt = TimeStamp::now();

        // Plain value type — copy must duplicate every field.
        RtpPacketBatch c = b;
        CHECK(c.packets.size() == 3);
        CHECK(c.frameIndex.value() == 42);
        CHECK(c.clockRate == 90000u);
        CHECK(c.markerOnLast);
        CHECK(c.rateCapBps == 50'000'000u);
        CHECK(c.enqueuedAt == b.enqueuedAt);

        // Each packet entry shares the underlying buffer that
        // createList allocated, so a copy of the batch is a
        // refcount bump on the shared buffer — no deep copy.  This
        // is what makes Queue<RtpPacketBatch> handoffs cheap.
        CHECK(c.packets[0].data() == b.packets[0].data());
}

TEST_CASE("RtpPacketBatch: markerOnLast false is preserved") {
        RtpPacketBatch b;
        b.markerOnLast = false;
        RtpPacketBatch c = b;
        CHECK_FALSE(c.markerOnLast);
}

TEST_CASE("RtpPacketBatch: move construction transfers packet list") {
        RtpPacketBatch src;
        src.packets = RtpPacket::createList(2, 80);
        src.frameIndex = FrameNumber(7);
        src.clockRate = 48000u;
        const void *bufPtr = src.packets[0].data();

        RtpPacketBatch dst = std::move(src);
        CHECK(dst.packets.size() == 2);
        CHECK(dst.frameIndex.value() == 7);
        CHECK(dst.clockRate == 48000u);
        // The destination should reference the same buffer instance
        // — move did not perform a deep copy.
        CHECK(dst.packets[0].data() == bufPtr);
}
