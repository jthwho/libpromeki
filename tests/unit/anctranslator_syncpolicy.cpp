/**
 * @file      anctranslator_syncpolicy.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Exercises AncTranslator's SyncPolicy registry + applySyncPolicy
 * dispatch using stub policies registered against runtime-allocated
 * AncFormat::UserDefined IDs.  Two private formats: one with a policy
 * registered (dispatch path), one without (default-fallback path).
 * Avoids touching the well-known format registry so this test never
 * collides with codecs landed by Phase 4.5 step 4.
 */

#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/buffer.h>
#include <promeki/framesyncdisposition.h>
#include <promeki/list.h>

using namespace promeki;

namespace {

        // ---------------------------------------------------------------------
        // Two private UserDefined formats: A has a registered policy, B does
        // not.  Lazy initialisation so doctest's static-init ordering can't
        // bite us.
        // ---------------------------------------------------------------------

        struct TestFormatA {
                        AncFormat::ID id;
                        TestFormatA() {
                                id = AncFormat::registerType();
                                AncFormat::Data d;
                                d.id = id;
                                d.name = String("AncSyncPolicyTestFormatA");
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
                                d.name = String("AncSyncPolicyTestFormatB");
                                d.desc = String("Private test format with NO sync policy registered");
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
        // Stub policy with a side-channel call recorder.  Recorder lives in a
        // file-static List the tests clear at the start of each case.  Format A
        // is the only key that ever reaches this stub, so other tests in the
        // binary cannot pollute the recorder.
        // ---------------------------------------------------------------------

        struct RecordedCall {
                        FrameSyncDisposition disposition;
                        uint8_t              repeatIndex     = 0;
                        bool                 lossyConfigSeen = false;
        };

        List<RecordedCall> &recordedCalls() {
                static List<RecordedCall> r;
                return r;
        }

        AncTranslator::PacketsResult stubSyncPolicy(const AncPacket &pkt, FrameSyncDisposition d,
                                                uint8_t repeatIndex, const AncTranslateConfig &cfg) {
                RecordedCall c;
                c.disposition     = d;
                c.repeatIndex     = repeatIndex;
                c.lossyConfigSeen = cfg.getAs<bool>(AncTranslateConfig::AllowLossy);
                recordedCalls().pushToBack(c);
                AncPacket::List out;
                if (d.kind() != FrameSyncDisposition::Drop) {
                        out.pushToBack(pkt);
                }
                return makeResult<AncPacket::List>(std::move(out));
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

        AncPacket makePacket(AncFormat::ID id) {
                Buffer payload;
                return AncPacket(AncFormat(id), AncTransport::St291, payload);
        }

} // namespace

// ===========================================================================
// Capability query
// ===========================================================================

TEST_CASE("AncTranslator::hasSyncPolicy reflects registration") {
        ensureStub();
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(testFormatA().id)));
        CHECK_FALSE(AncTranslator::hasSyncPolicy(AncFormat(testFormatB().id)));
}

// ===========================================================================
// Registered-policy dispatch
// ===========================================================================

TEST_CASE("AncTranslator::applySyncPolicy dispatches Play to registered policy") {
        ensureStub();
        recordedCalls().clear();

        AncTranslator    t;
        AncPacket        pkt = makePacket(testFormatA().id);
        auto             res = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
        REQUIRE(res.second().isOk());

        const auto &out = res.first();
        CHECK(out.size() == 1);

        const auto &calls = recordedCalls();
        REQUIRE(calls.size() == 1);
        CHECK(calls[0].disposition.kind() == FrameSyncDisposition::Play);
        CHECK(calls[0].repeatIndex == 0);
}

TEST_CASE("AncTranslator::applySyncPolicy dispatches Drop to registered policy") {
        ensureStub();
        recordedCalls().clear();

        AncTranslator    t;
        AncPacket        pkt = makePacket(testFormatA().id);
        auto             res = t.applySyncPolicy(pkt, FrameSyncDisposition::drop(), 0);
        REQUIRE(res.second().isOk());

        const auto &out = res.first();
        CHECK(out.size() == 0);

        const auto &calls = recordedCalls();
        REQUIRE(calls.size() == 1);
        CHECK(calls[0].disposition.kind() == FrameSyncDisposition::Drop);
}

TEST_CASE("AncTranslator::applySyncPolicy dispatches Repeat with monotonic repeatIndex") {
        ensureStub();
        recordedCalls().clear();

        AncTranslator    t;
        AncPacket        pkt = makePacket(testFormatA().id);
        for (uint8_t i = 0; i < 3; ++i) {
                auto res = t.applySyncPolicy(pkt, FrameSyncDisposition::repeat(3), i);
                REQUIRE(res.second().isOk());
                CHECK(res.first().size() == 1);
        }

        const auto &calls = recordedCalls();
        REQUIRE(calls.size() == 3);
        for (uint8_t i = 0; i < 3; ++i) {
                CHECK(calls[i].disposition.kind() == FrameSyncDisposition::Repeat);
                CHECK(calls[i].disposition.repeatCount() == 3);
                CHECK(calls[i].repeatIndex == i);
        }
}

// ===========================================================================
// Default fallback (no registered policy)
// ===========================================================================

TEST_CASE("AncTranslator::applySyncPolicy default fallback: Play returns the packet") {
        AncTranslator t;
        AncPacket     pkt = makePacket(testFormatB().id);
        auto          res = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 1);
}

TEST_CASE("AncTranslator::applySyncPolicy default fallback: Drop returns empty list") {
        AncTranslator t;
        AncPacket     pkt = makePacket(testFormatB().id);
        auto          res = t.applySyncPolicy(pkt, FrameSyncDisposition::drop(), 0);
        REQUIRE(res.second().isOk());
        CHECK(res.first().size() == 0);
}

TEST_CASE("AncTranslator::applySyncPolicy default fallback: Repeat copies through at every index") {
        AncTranslator t;
        AncPacket     pkt = makePacket(testFormatB().id);
        for (uint8_t i = 0; i < 4; ++i) {
                auto res = t.applySyncPolicy(pkt, FrameSyncDisposition::repeat(4), i);
                REQUIRE(res.second().isOk());
                CHECK(res.first().size() == 1);
        }
}

// ===========================================================================
// Config threading
// ===========================================================================

TEST_CASE("AncTranslator::applySyncPolicy threads held config to the policy") {
        ensureStub();

        SUBCASE("Default config: AllowLossy is false") {
                recordedCalls().clear();
                AncTranslator t;
                AncPacket     pkt = makePacket(testFormatA().id);
                auto          res = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
                REQUIRE(res.second().isOk());
                REQUIRE(recordedCalls().size() == 1);
                CHECK_FALSE(recordedCalls()[0].lossyConfigSeen);
        }

        SUBCASE("AllowLossy=true is observable in the policy") {
                recordedCalls().clear();
                AncTranslateConfig cfg;
                cfg.set(AncTranslateConfig::AllowLossy, true);
                AncTranslator t(cfg);
                AncPacket     pkt = makePacket(testFormatA().id);
                auto          res = t.applySyncPolicy(pkt, FrameSyncDisposition::play(), 0);
                REQUIRE(res.second().isOk());
                REQUIRE(recordedCalls().size() == 1);
                CHECK(recordedCalls()[0].lossyConfigSeen);
        }
}

// ===========================================================================
// Re-registration warning (sanity — same key, same fn → idempotent)
// ===========================================================================

TEST_CASE("AncTranslator::registerSyncPolicy is idempotent on the same key") {
        // Register twice — second registration warns but succeeds, and the
        // dispatch path still resolves to the registered fn.
        AncTranslator::registerSyncPolicy(testFormatA().id, &stubSyncPolicy);
        AncTranslator::registerSyncPolicy(testFormatA().id, &stubSyncPolicy);
        CHECK(AncTranslator::hasSyncPolicy(AncFormat(testFormatA().id)));
}
