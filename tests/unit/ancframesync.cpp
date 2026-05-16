/**
 * @file      ancframesync.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Exercises AncFrameSync's apply() driver loop using a stub sync
 * policy registered against a private AncFormat::UserDefined ID.
 * The stub policy emits a one-byte buffer = repeatIndex on every
 * non-Drop call, so each test can verify per-frame dispatch by
 * reading that byte off the output.
 */

#include <doctest/doctest.h>
#include <promeki/ancdesc.h>
#include <promeki/ancformat.h>
#include <promeki/ancframesync.h>
#include <promeki/ancpacket.h>
#include <promeki/ancpayload.h>
#include <promeki/anctranslator.h>
#include <promeki/buffer.h>
#include <promeki/frame.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/list.h>
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

namespace {

        // ---------------------------------------------------------------------
        // Two private UserDefined formats: A has a stub sync policy registered
        // (dispatch path), B has no policy (default-fallback path).
        // ---------------------------------------------------------------------

        struct TestFormatA {
                        AncFormat::ID id;
                        TestFormatA() {
                                id = AncFormat::registerType();
                                AncFormat::Data d;
                                d.id = id;
                                d.name = String("AncFrameSyncTestFormatA");
                                d.desc = String("Private test format with a registered sync policy");
                                d.category = AncCategory::UserDefined;
                                d.canonicalTransport = AncTransport::St291;
                                AncFormat::registerData(std::move(d));
                        }
        };
        const TestFormatA &testFormatA() {
                static const TestFormatA tf;
                return tf;
        }

        struct TestFormatB {
                        AncFormat::ID id;
                        TestFormatB() {
                                id = AncFormat::registerType();
                                AncFormat::Data d;
                                d.id = id;
                                d.name = String("AncFrameSyncTestFormatB");
                                d.desc = String("Private test format with NO sync policy");
                                d.category = AncCategory::UserDefined;
                                d.canonicalTransport = AncTransport::St291;
                                AncFormat::registerData(std::move(d));
                        }
        };
        const TestFormatB &testFormatB() {
                static const TestFormatB tf;
                return tf;
        }

        // ---------------------------------------------------------------------
        // Stub policy: emits a one-byte buffer = repeatIndex on Play / Repeat,
        // returns an empty list on Drop.  Lets each test read repeatIndex
        // straight off the output packet's data buffer — no side channels.
        // ---------------------------------------------------------------------

        Result<List<AncPacket>> stubSyncPolicy(const AncPacket &pkt, FrameSyncDisposition d,
                                                uint8_t repeatIndex, const AncTranslateConfig & /*cfg*/) {
                List<AncPacket> out;
                if (d.kind() == FrameSyncDisposition::Drop) {
                        return makeResult<List<AncPacket>>(std::move(out));
                }
                Buffer  outBuf(static_cast<size_t>(1));
                uint8_t byte = repeatIndex;
                outBuf.copyFrom(&byte, 1);
                outBuf.setSize(1);
                out.pushToBack(AncPacket(pkt.format(), pkt.transport(), outBuf));
                return makeResult<List<AncPacket>>(std::move(out));
        }

        struct StubRegistrar {
                        StubRegistrar() {
                                AncTranslator::registerSyncPolicy(testFormatA().id, &stubSyncPolicy);
                        }
        };
        const StubRegistrar &ensureStub() {
                static StubRegistrar r;
                return r;
        }

        // ---------------------------------------------------------------------
        // A separate UserDefined format with a *stashing* sync policy:
        //   - On Drop:   returns one packet whose data buffer is `[0xAB]`,
        //                signalling "stash this for the next surviving frame."
        //   - On Play:   copies the input packet through unchanged.
        //   - On Repeat: copies through unchanged.
        //
        // The 0xAB marker lets tests distinguish stashed packets (which carry
        // 0xAB in their wire data) from a frame's natural packets (which the
        // tests construct with empty buffers).
        // ---------------------------------------------------------------------

        struct TestFormatC {
                        AncFormat::ID id;
                        TestFormatC() {
                                id = AncFormat::registerType();
                                AncFormat::Data d;
                                d.id = id;
                                d.name = String("AncFrameSyncTestFormatC");
                                d.desc = String("Private test format with a stashing sync policy");
                                d.category = AncCategory::UserDefined;
                                d.canonicalTransport = AncTransport::St291;
                                AncFormat::registerData(std::move(d));
                        }
        };
        const TestFormatC &testFormatC() {
                static const TestFormatC tf;
                return tf;
        }

        constexpr uint8_t kStashMarker = 0xAB;

        AncPacket makeStashMarkerPacket(AncFormat::ID id) {
                Buffer  marker(static_cast<size_t>(1));
                uint8_t b = kStashMarker;
                marker.copyFrom(&b, 1);
                marker.setSize(1);
                return AncPacket(AncFormat(id), AncTransport::St291, marker);
        }

        Result<List<AncPacket>> stashingSyncPolicy(const AncPacket &pkt, FrameSyncDisposition d,
                                                    uint8_t /*repeatIndex*/,
                                                    const AncTranslateConfig & /*cfg*/) {
                List<AncPacket> out;
                if (d.kind() == FrameSyncDisposition::Drop) {
                        out.pushToBack(makeStashMarkerPacket(pkt.format().id()));
                        return makeResult<List<AncPacket>>(std::move(out));
                }
                // Play / Repeat: copy the input packet through verbatim.
                out.pushToBack(pkt);
                return makeResult<List<AncPacket>>(std::move(out));
        }

        struct StashingStubRegistrar {
                        StashingStubRegistrar() {
                                AncTranslator::registerSyncPolicy(testFormatC().id, &stashingSyncPolicy);
                        }
        };
        const StashingStubRegistrar &ensureStashingStub() {
                static StashingStubRegistrar r;
                return r;
        }

        bool packetIsStashMarker(const AncPacket &pkt) {
                if (pkt.data().size() != 1) return false;
                return *static_cast<const uint8_t *>(pkt.data().data()) == kStashMarker;
        }

        // Read the one-byte recorded repeatIndex out of a stub-built packet.
        uint8_t recordedRepeatIndex(const AncPacket &pkt) {
                const Buffer &b = pkt.data();
                REQUIRE(b.size() == 1);
                return *static_cast<const uint8_t *>(b.data());
        }

        // Build a Frame carrying one AncPayload with `numPackets` stub packets
        // of `formatId`.  Packets are bare carriers — their data buffers are
        // empty since the stub policy ignores them.
        Frame makeAncFrame(AncFormat::ID formatId, size_t numPackets) {
                Frame           f;
                AncDesc         desc;
                AncPayload::Ptr ap = AncPayload::Ptr::create(desc);
                for (size_t i = 0; i < numPackets; ++i) {
                        Buffer    empty;
                        AncPacket pkt(AncFormat(formatId), AncTransport::St291, empty);
                        ap.modify()->addPacket(pkt);
                }
                f.addPayload(ap);
                return f;
        }

} // namespace

// ===========================================================================
// Play / Drop / Repeat output-frame counts
// ===========================================================================

TEST_CASE("AncFrameSync::apply Play returns one output frame") {
        ensureStub();
        AncFrameSync fs;
        Frame        in  = makeAncFrame(testFormatA().id, 1);
        auto         res = fs.apply(in, FrameSyncDisposition::play());
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 1);
}

TEST_CASE("AncFrameSync::apply Drop returns empty list") {
        ensureStub();
        AncFrameSync fs;
        Frame        in  = makeAncFrame(testFormatA().id, 1);
        auto         res = fs.apply(in, FrameSyncDisposition::drop());
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

TEST_CASE("AncFrameSync::apply Repeat[1] returns one output frame") {
        ensureStub();
        AncFrameSync fs;
        Frame        in  = makeAncFrame(testFormatA().id, 1);
        auto         res = fs.apply(in, FrameSyncDisposition::repeat(1));
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 1);
}

TEST_CASE("AncFrameSync::apply Repeat[3] returns three output frames") {
        ensureStub();
        AncFrameSync fs;
        Frame        in  = makeAncFrame(testFormatA().id, 1);
        auto         res = fs.apply(in, FrameSyncDisposition::repeat(3));
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 3);
}

// ===========================================================================
// Per-frame repeatIndex passed to the policy
// ===========================================================================

TEST_CASE("AncFrameSync::apply Repeat[3] feeds each output frame its own repeatIndex") {
        ensureStub();
        AncFrameSync fs;
        // Two packets so we also verify every packet within a frame sees the
        // same repeatIndex.
        Frame        in  = makeAncFrame(testFormatA().id, 2);
        auto         res = fs.apply(in, FrameSyncDisposition::repeat(3));
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 3);

        for (uint8_t i = 0; i < 3; ++i) {
                AncPayload::PtrList ancList = res.first()[i].ancPayloads();
                REQUIRE(ancList.size() == 1);
                const AncPacket::List &packets = ancList[0]->packets();
                REQUIRE(packets.size() == 2);
                CHECK(recordedRepeatIndex(packets[0]) == i);
                CHECK(recordedRepeatIndex(packets[1]) == i);
        }
}

TEST_CASE("AncFrameSync::apply Play uses repeatIndex = 0") {
        ensureStub();
        AncFrameSync fs;
        Frame        in  = makeAncFrame(testFormatA().id, 1);
        auto         res = fs.apply(in, FrameSyncDisposition::play());
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        AncPayload::PtrList ancList = res.first()[0].ancPayloads();
        REQUIRE(ancList.size() == 1);
        REQUIRE(ancList[0]->packets().size() == 1);
        CHECK(recordedRepeatIndex(ancList[0]->packets()[0]) == 0);
}

// ===========================================================================
// Default fallback (no policy registered)
// ===========================================================================

TEST_CASE("AncFrameSync::apply default-fallback: Play copies the packet through unchanged") {
        AncFrameSync fs;
        Frame        in  = makeAncFrame(testFormatB().id, 1);
        auto         res = fs.apply(in, FrameSyncDisposition::play());
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        AncPayload::PtrList ancList = res.first()[0].ancPayloads();
        REQUIRE(ancList.size() == 1);
        const AncPacket::List &packets = ancList[0]->packets();
        REQUIRE(packets.size() == 1);
        // Default fallback returns the packet unchanged — its data is the
        // original empty buffer, not the stub's repeatIndex byte.
        CHECK(packets[0].format().id() == testFormatB().id);
        CHECK(packets[0].data().size() == 0);
}

TEST_CASE("AncFrameSync::apply default-fallback: Drop loses the packet (no output frame)") {
        AncFrameSync fs;
        Frame        in  = makeAncFrame(testFormatB().id, 1);
        auto         res = fs.apply(in, FrameSyncDisposition::drop());
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

TEST_CASE("AncFrameSync::apply default-fallback: Repeat copies the packet through at every index") {
        AncFrameSync fs;
        Frame        in  = makeAncFrame(testFormatB().id, 1);
        auto         res = fs.apply(in, FrameSyncDisposition::repeat(4));
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 4);
        for (size_t i = 0; i < 4; ++i) {
                AncPayload::PtrList ancList = res.first()[i].ancPayloads();
                REQUIRE(ancList.size() == 1);
                const AncPacket::List &packets = ancList[0]->packets();
                REQUIRE(packets.size() == 1);
                CHECK(packets[0].format().id() == testFormatB().id);
                CHECK(packets[0].data().size() == 0);
        }
}

// ===========================================================================
// Video / audio payloads shared across Repeat output frames
// ===========================================================================

TEST_CASE("AncFrameSync::apply Repeat[3]: video payload pointer is shared across output frames") {
        ensureStub();
        AncFrameSync fs;

        // Build a frame with both a VideoPayload and an AncPayload.  The
        // VideoPayload's slot should never be .modify()'d by AncFrameSync,
        // so all output frames should hold the *same* VideoPayload pointer
        // (refcount-bumped, not deep-copied).
        Frame                          in;
        UncompressedVideoPayload::Ptr  vp = UncompressedVideoPayload::Ptr::create();
        in.addPayload(vp);
        AncDesc         desc;
        AncPayload::Ptr ap = AncPayload::Ptr::create(desc);
        Buffer          empty;
        ap.modify()->addPacket(AncPacket(AncFormat(testFormatA().id), AncTransport::St291, empty));
        in.addPayload(ap);

        auto res = fs.apply(in, FrameSyncDisposition::repeat(3));
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 3);

        // Find the video slot in each output frame and verify the underlying
        // pointer matches across all three frames.
        const MediaPayload *vp0 = nullptr;
        for (size_t i = 0; i < res.first().size(); ++i) {
                const MediaPayload::PtrList &payloads = res.first()[i].payloadList();
                const MediaPayload          *foundVp  = nullptr;
                for (const MediaPayload::Ptr &p : payloads) {
                        if (p.isValid() && p->kind() == MediaPayloadKind::Video) {
                                foundVp = p.ptr();
                                break;
                        }
                }
                REQUIRE(foundVp != nullptr);
                if (vp0 == nullptr) {
                        vp0 = foundVp;
                } else {
                        CHECK(vp0 == foundVp);
                }
        }
}

TEST_CASE("AncFrameSync::apply Repeat[3]: anc payload pointer differs across output frames") {
        ensureStub();
        AncFrameSync fs;
        Frame        in  = makeAncFrame(testFormatA().id, 1);
        auto         res = fs.apply(in, FrameSyncDisposition::repeat(3));
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 3);

        // Each output's AncPayload has been independently mutated via CoW
        // detach, so the underlying pointers must differ.
        const MediaPayload *ap0 = nullptr;
        const MediaPayload *ap1 = nullptr;
        const MediaPayload *ap2 = nullptr;
        for (const MediaPayload::Ptr &p : res.first()[0].payloadList()) {
                if (p.isValid() && p->kind() == MediaPayloadKind::AncillaryData) ap0 = p.ptr();
        }
        for (const MediaPayload::Ptr &p : res.first()[1].payloadList()) {
                if (p.isValid() && p->kind() == MediaPayloadKind::AncillaryData) ap1 = p.ptr();
        }
        for (const MediaPayload::Ptr &p : res.first()[2].payloadList()) {
                if (p.isValid() && p->kind() == MediaPayloadKind::AncillaryData) ap2 = p.ptr();
        }
        REQUIRE(ap0 != nullptr);
        REQUIRE(ap1 != nullptr);
        REQUIRE(ap2 != nullptr);
        CHECK(ap0 != ap1);
        CHECK(ap1 != ap2);
        CHECK(ap0 != ap2);
}

// ===========================================================================
// Input-frame independence: input is unchanged after apply()
// ===========================================================================

TEST_CASE("AncFrameSync::apply does not mutate the caller's input frame") {
        ensureStub();
        AncFrameSync fs;
        Frame        in = makeAncFrame(testFormatA().id, 1);

        // Snapshot the input AncPayload's packet count + the first packet's
        // data size before apply — this is what we'll re-check after.
        const AncPayload::PtrList beforeAnc = in.ancPayloads();
        REQUIRE(beforeAnc.size() == 1);
        const size_t beforePacketCount = beforeAnc[0]->packets().size();
        const size_t beforeDataSize    = beforeAnc[0]->packets()[0].data().size();

        auto res = fs.apply(in, FrameSyncDisposition::repeat(3));
        REQUIRE(res.second().isOk());

        // Input frame's AncPayload is untouched — same packet count, same
        // (empty) data — confirming Frame CoW is doing its job.
        const AncPayload::PtrList afterAnc = in.ancPayloads();
        REQUIRE(afterAnc.size() == 1);
        CHECK(afterAnc[0]->packets().size() == beforePacketCount);
        CHECK(afterAnc[0]->packets()[0].data().size() == beforeDataSize);
}

// ===========================================================================
// Config threading
// ===========================================================================

TEST_CASE("AncFrameSync threads AncTranslateConfig to the held translator") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AllowLossy, true);
        AncFrameSync fs(cfg);
        CHECK(fs.config().getAs<bool>(AncTranslateConfig::AllowLossy));
}

// ===========================================================================
// Stash mechanism: Drop policies that return non-empty packet lists capture
// those packets for the next surviving frame; latest-wins on collision;
// drains onto the first output slot of the next Play / Repeat.
// ===========================================================================

TEST_CASE("AncFrameSync stash: Drop captures stashing-policy returns") {
        ensureStashingStub();
        AncFrameSync fs;
        Frame        f1 = makeAncFrame(testFormatC().id, 1);

        auto res = fs.apply(f1, FrameSyncDisposition::drop());
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
        // One stash entry with one packet inside.
        CHECK(fs.stashedPacketCount() == 1u);
}

TEST_CASE("AncFrameSync stash: stashed packet emits on the next Play") {
        ensureStashingStub();
        AncFrameSync fs;

        // First slot: Drop populates stash.
        Frame f1 = makeAncFrame(testFormatC().id, 1);
        auto  r1 = fs.apply(f1, FrameSyncDisposition::drop());
        REQUIRE(r1.second().isOk());
        REQUIRE(r1.first().size() == 0);

        // Second slot: Play drains the stash onto the output frame's
        // AncPayload alongside the natural packet.
        Frame f2 = makeAncFrame(testFormatC().id, 1);
        auto  r2 = fs.apply(f2, FrameSyncDisposition::play());
        REQUIRE(r2.second().isOk());
        REQUIRE(r2.first().size() == 1);

        AncPayload::PtrList ancList = r2.first()[0].ancPayloads();
        REQUIRE(ancList.size() == 1);
        const AncPacket::List &packets = ancList[0]->packets();
        // Two packets: the natural packet (copied through, empty data) plus
        // the stashed marker packet (data = [0xAB], appended after the walk).
        REQUIRE(packets.size() == 2);
        CHECK_FALSE(packetIsStashMarker(packets[0])); // natural copy-through
        CHECK(packetIsStashMarker(packets[1]));       // stashed
        // Stash is drained.
        CHECK(fs.stashedPacketCount() == 0u);
}

TEST_CASE("AncFrameSync stash: latest-wins on a second Drop for the same format") {
        ensureStashingStub();
        AncFrameSync fs;

        // Drop, Drop — second drop's stash entry replaces the first.
        Frame f1 = makeAncFrame(testFormatC().id, 1);
        Frame f2 = makeAncFrame(testFormatC().id, 1);
        auto  r1 = fs.apply(f1, FrameSyncDisposition::drop());
        REQUIRE(r1.second().isOk());
        auto r2 = fs.apply(f2, FrameSyncDisposition::drop());
        REQUIRE(r2.second().isOk());

        // Still only one packet stashed (latest wins, no accumulation).
        CHECK(fs.stashedPacketCount() == 1u);

        // Drain confirms exactly one stashed packet shows up.
        Frame f3 = makeAncFrame(testFormatC().id, 1);
        auto  r3 = fs.apply(f3, FrameSyncDisposition::play());
        REQUIRE(r3.second().isOk());
        REQUIRE(r3.first().size() == 1);
        AncPayload::PtrList ancList = r3.first()[0].ancPayloads();
        REQUIRE(ancList.size() == 1);
        const AncPacket::List &packets = ancList[0]->packets();
        // 1 natural + 1 stashed = 2.
        CHECK(packets.size() == 2);
        CHECK(packetIsStashMarker(packets[1]));
}

TEST_CASE("AncFrameSync stash: drains onto Repeat[idx=0] only") {
        ensureStashingStub();
        AncFrameSync fs;

        // Drop, Repeat[3].
        Frame f1 = makeAncFrame(testFormatC().id, 1);
        REQUIRE(fs.apply(f1, FrameSyncDisposition::drop()).second().isOk());

        Frame f2  = makeAncFrame(testFormatC().id, 1);
        auto  res = fs.apply(f2, FrameSyncDisposition::repeat(3));
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 3);

        // Output[0]: 1 natural + 1 stashed = 2 packets.
        AncPayload::PtrList anc0 = res.first()[0].ancPayloads();
        REQUIRE(anc0.size() == 1);
        CHECK(anc0[0]->packets().size() == 2);
        // The stashed packet (marker 0xAB) is one of the two.
        bool foundMarkerOn0 = false;
        for (const AncPacket &p : anc0[0]->packets()) {
                if (packetIsStashMarker(p)) foundMarkerOn0 = true;
        }
        CHECK(foundMarkerOn0);

        // Output[1] and Output[2]: 1 natural packet only — no stashed
        // packet on subsequent Repeat slots.
        for (size_t i : {size_t(1), size_t(2)}) {
                AncPayload::PtrList anc = res.first()[i].ancPayloads();
                REQUIRE(anc.size() == 1);
                CHECK(anc[0]->packets().size() == 1);
                for (const AncPacket &p : anc[0]->packets()) {
                        CHECK_FALSE(packetIsStashMarker(p));
                }
        }
        CHECK(fs.stashedPacketCount() == 0u);
}

TEST_CASE("AncFrameSync stash: creates a fresh AncPayload when the next surviving frame has none") {
        ensureStashingStub();
        AncFrameSync fs;

        // Stash a packet via Drop on a frame that has an AncPayload.
        Frame f1 = makeAncFrame(testFormatC().id, 1);
        REQUIRE(fs.apply(f1, FrameSyncDisposition::drop()).second().isOk());
        REQUIRE(fs.stashedPacketCount() == 1u);

        // Next Play receives a frame with NO AncPayload (just an empty Frame
        // bumped into validity by adding a video payload).  drainStashInto
        // must create a fresh AncPayload to carry the stashed packet.
        Frame                          f2;
        UncompressedVideoPayload::Ptr  vp = UncompressedVideoPayload::Ptr::create();
        f2.addPayload(vp);
        REQUIRE(f2.ancPayloads().size() == 0);

        auto res = fs.apply(f2, FrameSyncDisposition::play());
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        AncPayload::PtrList ancList = res.first()[0].ancPayloads();
        REQUIRE(ancList.size() == 1); // fresh AncPayload
        const AncPacket::List &packets = ancList[0]->packets();
        REQUIRE(packets.size() == 1);
        CHECK(packetIsStashMarker(packets[0]));
}

TEST_CASE("AncFrameSync stash: Play with empty stash is a no-op (no fresh AncPayload created)") {
        ensureStashingStub();
        AncFrameSync fs;
        Frame        f = makeAncFrame(testFormatC().id, 1);
        auto         res = fs.apply(f, FrameSyncDisposition::play());
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().size() == 1);
        // Output frame should still have exactly one AncPayload (the original
        // one with the natural packet copied through).
        AncPayload::PtrList ancList = res.first()[0].ancPayloads();
        REQUIRE(ancList.size() == 1);
        CHECK(ancList[0]->packets().size() == 1);
}
