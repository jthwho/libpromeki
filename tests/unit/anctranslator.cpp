/**
 * @file      anctranslator.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Exercises AncTranslator's dispatch + registry behaviour using stub
 * parsers and builders registered against a runtime-allocated
 * AncFormat::UserDefined ID.  Avoids touching the well-known format
 * registry so this test never collides with codecs landed by later
 * phases.
 */

#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/buffer.h>
#include <promeki/metadata.h>

using namespace promeki;

// ---------------------------------------------------------------------------
// One-time test fixture: registers a private UserDefined format and a set of
// stub parsers/builders/translators for it.  Singleton-style so the static
// AncTranslator registries see them exactly once.
// ---------------------------------------------------------------------------

namespace {

        // Stub "decoded" Variant payload — we just stuff a String so the test
        // can spot it on the round-trip.
        constexpr const char *kStubDecoded = "STUB-DECODED";
        constexpr const char *kStubBuiltA = "STUB-BUILT-A";
        constexpr const char *kStubBuiltB = "STUB-BUILT-B";
        constexpr const char *kStubDirectAB = "STUB-DIRECT-AB";

        // We need a fixed pair of transports.  St291 and NdiXml are the two
        // long-supported entries; using them keeps the test wire-format-agnostic
        // while still going through real Enum values.
        static const AncTransport &xportA() { return AncTransport::St291; }
        static const AncTransport &xportB() { return AncTransport::NdiXml; }

        struct TestFormat {
                        AncFormat::ID id;

                        TestFormat() {
                                id = AncFormat::registerType();
                                AncFormat::Data d;
                                d.id = id;
                                d.name = String("AncTranslatorTestFormat");
                                d.desc = String("Private test format for AncTranslator unit tests");
                                d.category = AncCategory::UserDefined;
                                d.canonicalTransport = xportA();
                                AncFormat::registerData(std::move(d));
                        }
        };

        const TestFormat &testFormat() {
                static const TestFormat tf;
                return tf;
        }

        // ----- Stub handlers --------------------------------------------------

        Result<Variant> stubParseA(const AncPacket & /*pkt*/, const AncTranslateConfig & /*cfg*/) {
                return makeResult<Variant>(Variant(String(kStubDecoded)));
        }

        Result<Variant> stubParseB(const AncPacket & /*pkt*/, const AncTranslateConfig &cfg) {
                // Echo the AllowLossy bit so we can prove the cfg threaded through.
                bool   lossy = cfg.getAs<bool>(AncTranslateConfig::AllowLossy);
                String s(kStubDecoded);
                s += lossy ? "+lossy" : "+lossless";
                return makeResult<Variant>(Variant(s));
        }

        Result<AncPacket> stubBuildA(const Variant & /*v*/, const AncTranslateConfig & /*cfg*/) {
                Buffer payload;
                Metadata m;
                m.set(Metadata::declareID("AncTranslatorTest.Built",
                                          VariantSpec().setType(Variant::TypeString).setDefault(String())),
                      String(kStubBuiltA));
                return makeResult<AncPacket>(AncPacket(AncFormat(testFormat().id), xportA(), payload, m));
        }

        Result<AncPacket> stubBuildB(const Variant & /*v*/, const AncTranslateConfig & /*cfg*/) {
                Buffer   payload(static_cast<size_t>(0));
                Metadata m;
                m.set(Metadata::declareID("AncTranslatorTest.Built",
                                          VariantSpec().setType(Variant::TypeString).setDefault(String())),
                      String(kStubBuiltB));
                return makeResult<AncPacket>(AncPacket(AncFormat(testFormat().id), xportB(), payload, m));
        }

        Result<AncPacket> stubDirectAB(const AncPacket & /*pkt*/, AncTransport target,
                                        const AncTranslateConfig & /*cfg*/) {
                Buffer   payload(static_cast<size_t>(0));
                Metadata m;
                m.set(Metadata::declareID("AncTranslatorTest.Direct",
                                          VariantSpec().setType(Variant::TypeString).setDefault(String())),
                      String(kStubDirectAB));
                return makeResult<AncPacket>(AncPacket(AncFormat(testFormat().id), target, payload, m));
        }

        // One-time registration of the stub handlers.  doctest does not gate
        // static-init order, so we register lazily on first access from a test
        // case and trust the registry to record each entry exactly once.
        struct StubsRegistrar {
                        StubsRegistrar() {
                                AncFormat::ID id = testFormat().id;
                                AncTranslator::registerParser(id, xportA(), &stubParseA);
                                AncTranslator::registerParser(id, xportB(), &stubParseB);
                                AncTranslator::registerBuilder(id, xportA(), &stubBuildA);
                                AncTranslator::registerBuilder(id, xportB(), &stubBuildB);
                                // Note: direct translator is registered separately so a subset
                                // of tests can exercise the parse+build composed path.
                        }
        };
        const StubsRegistrar &ensureStubs() {
                static StubsRegistrar r;
                return r;
        }

        // A second registrar that lazily installs the direct translator.  Tests
        // that need it call this; tests that don't see the composed path.
        struct DirectRegistrar {
                        DirectRegistrar() {
                                AncTranslator::registerTranslator(testFormat().id, xportA(), xportB(), &stubDirectAB);
                        }
        };
        const DirectRegistrar &ensureDirect() {
                static DirectRegistrar r;
                return r;
        }

        AncPacket makeStubPacket(const AncTransport &t) {
                ensureStubs();
                Buffer payload;
                return AncPacket(AncFormat(testFormat().id), t, payload);
        }

} // namespace

// ============================================================================
// Capability queries
// ============================================================================

TEST_CASE("AncTranslator: hasParser / hasBuilder reflect registrations") {
        ensureStubs();
        AncFormat fmt(testFormat().id);

        CHECK(AncTranslator::hasParser(fmt, xportA()));
        CHECK(AncTranslator::hasParser(fmt, xportB()));
        CHECK(AncTranslator::hasBuilder(fmt, xportA()));
        CHECK(AncTranslator::hasBuilder(fmt, xportB()));

        // No parser for a transport we never registered against.
        CHECK_FALSE(AncTranslator::hasParser(fmt, AncTransport::RtmpAmf));
        CHECK_FALSE(AncTranslator::hasBuilder(fmt, AncTransport::RtmpAmf));
}

TEST_CASE("AncTranslator: canTranslate reports composed and direct paths") {
        ensureStubs();
        AncFormat fmt(testFormat().id);

        // Identity short-circuit always reports possible.
        CHECK(AncTranslator::canTranslate(fmt, xportA(), xportA()));
        // Composed: parser src=A and builder dst=B both registered.
        CHECK(AncTranslator::canTranslate(fmt, xportA(), xportB()));
        // Reverse: parser src=B and builder dst=A both registered.
        CHECK(AncTranslator::canTranslate(fmt, xportB(), xportA()));
        // No path when one leg is missing.
        CHECK_FALSE(AncTranslator::canTranslate(fmt, AncTransport::RtmpAmf, xportA()));
        CHECK_FALSE(AncTranslator::canTranslate(fmt, xportA(), AncTransport::RtmpAmf));
}

TEST_CASE("AncTranslator: registeredParserTransports enumerates entries") {
        ensureStubs();
        AncFormat          fmt(testFormat().id);
        List<AncTransport> parserSrcs = AncTranslator::registeredParserTransports(fmt);
        CHECK(parserSrcs.size() == 2);
        CHECK(parserSrcs.contains(xportA()));
        CHECK(parserSrcs.contains(xportB()));

        List<AncTransport> builderDsts = AncTranslator::registeredBuilderTransports(fmt);
        CHECK(builderDsts.size() == 2);
        CHECK(builderDsts.contains(xportA()));
        CHECK(builderDsts.contains(xportB()));
}

// ============================================================================
// parse() / build() round-trip
// ============================================================================

TEST_CASE("AncTranslator: parse dispatches to the registered handler") {
        AncTranslator   t;
        AncPacket       pkt = makeStubPacket(xportA());
        Result<Variant> r = t.parse(pkt);
        CHECK(r.second().isOk());
        CHECK(r.first().get<String>() == kStubDecoded);
}

TEST_CASE("AncTranslator: parse propagates held config to handlers") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AllowLossy, true);
        AncTranslator   t(cfg);
        AncPacket       pkt = makeStubPacket(xportB());
        Result<Variant> r = t.parse(pkt);
        CHECK(r.second().isOk());
        CHECK(r.first().get<String>() == String(kStubDecoded) + "+lossy");
}

TEST_CASE("AncTranslator: parse returns NotSupported when no parser registered") {
        AncTranslator   t;
        Buffer          payload(static_cast<size_t>(0));
        AncPacket       pkt(AncFormat(testFormat().id), AncTransport::RtmpAmf, payload);
        Result<Variant> r = t.parse(pkt);
        CHECK(r.second().code() == Error::NotSupported);
}

TEST_CASE("AncTranslator: build dispatches to the registered handler") {
        AncTranslator     t;
        Result<AncPacket> r = t.build(Variant(String("ignored")), AncFormat(testFormat().id), xportA());
        CHECK(r.second().isOk());
        CHECK(r.first().transport() == xportA());
        CHECK(r.first().format().id() == testFormat().id);
}

TEST_CASE("AncTranslator: build returns NotSupported when no builder registered") {
        AncTranslator     t;
        Result<AncPacket> r = t.build(Variant(String("ignored")), AncFormat(testFormat().id), AncTransport::RtmpAmf);
        CHECK(r.second().code() == Error::NotSupported);
}

// ============================================================================
// translate() — identity, direct, composed
// ============================================================================

TEST_CASE("AncTranslator: translate identity short-circuit returns packet unchanged") {
        AncTranslator     t;
        AncPacket         pkt = makeStubPacket(xportA());
        Result<AncPacket> r = t.translate(pkt, xportA());
        CHECK(r.second().isOk());
        // Same impl (handle compares cheaply via packet identity equality).
        CHECK(r.first() == pkt);
}

TEST_CASE("AncTranslator: translate falls back to composed parse+build path") {
        // Direct registrar deliberately NOT called: we want this path to go
        // through parse(B) → build(A).  The B-side stub parser returns a
        // String("STUB-DECODED+lossless") which the A-side stub builder
        // ignores; the test just verifies that the result packet ends up on
        // the requested target transport.
        AncTranslator     t;
        AncPacket         pkt = makeStubPacket(xportB());
        Result<AncPacket> r = t.translate(pkt, xportA());
        CHECK(r.second().isOk());
        CHECK(r.first().transport() == xportA());
        CHECK(r.first().format().id() == testFormat().id);
}

TEST_CASE("AncTranslator: translate prefers direct translator when registered") {
        ensureDirect();
        CHECK(AncTranslator::hasDirectTranslator(AncFormat(testFormat().id), xportA(), xportB()));

        AncTranslator     t;
        AncPacket         pkt = makeStubPacket(xportA());
        Result<AncPacket> r = t.translate(pkt, xportB());
        CHECK(r.second().isOk());
        CHECK(r.first().transport() == xportB());
        // Direct path tagged the output meta with kStubDirectAB.
        Metadata::ID directKey = Metadata::declareID(
                "AncTranslatorTest.Direct",
                VariantSpec().setType(Variant::TypeString).setDefault(String()));
        CHECK(r.first().meta().getAs<String>(directKey) == String(kStubDirectAB));
}

TEST_CASE("AncTranslator: translate returns NotSupported when no path exists") {
        AncTranslator     t;
        Buffer            payload(static_cast<size_t>(0));
        AncPacket         pkt(AncFormat(testFormat().id), AncTransport::RtmpAmf, payload);
        Result<AncPacket> r = t.translate(pkt, AncTransport::HdmiInfoFrame);
        CHECK(r.second().code() == Error::NotSupported);
}

// ============================================================================
// Config plumbing
// ============================================================================

TEST_CASE("AncTranslator: config getters / setters") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AllowLossy, true);
        AncTranslator t(cfg);
        CHECK(t.config().getAs<bool>(AncTranslateConfig::AllowLossy) == true);

        AncTranslateConfig cfg2;
        cfg2.set(AncTranslateConfig::AllowLossy, false);
        t.setConfig(cfg2);
        CHECK(t.config().getAs<bool>(AncTranslateConfig::AllowLossy) == false);
}
