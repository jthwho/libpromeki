/**
 * @file      anctranslator.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Exercises AncTranslator's dispatch + registry behaviour using stub
 * parsers and builders registered against a runtime-allocated
 * AncFormat::UserDefined ID.  Avoids touching the well-known format
 * registry so this test never collides with codecs landed by later
 * phases.
 */

#include <stdexcept>

#include <doctest/doctest.h>
#include <promeki/ancafd.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/ancst2020audio.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/buffer.h>
#include <promeki/list.h>
#include <promeki/metadata.h>
#include <promeki/st291packet.h>

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

        AncTranslator::ParseResult stubParseA(const AncPacket & /*pkt*/, const AncTranslateConfig & /*cfg*/) {
                return makeResult<Variant>(Variant(String(kStubDecoded)));
        }

        AncTranslator::ParseResult stubParseB(const AncPacket & /*pkt*/, const AncTranslateConfig &cfg) {
                // Echo the AllowLossy bit so we can prove the cfg threaded through.
                bool   lossy = cfg.getAs<bool>(AncTranslateConfig::AllowLossy);
                String s(kStubDecoded);
                s += lossy ? "+lossy" : "+lossless";
                return makeResult<Variant>(Variant(s));
        }

        AncTranslator::PacketsResult stubBuildA(const Variant & /*v*/, const AncTranslateConfig & /*cfg*/) {
                Buffer payload;
                Metadata m;
                m.set(Metadata::declareID("AncTranslatorTest.Built",
                                          VariantSpec().setType(DataTypeString).setDefault(String())),
                      String(kStubBuiltA));
                AncPacket::List out;
                out.pushToBack(AncPacket(AncFormat(testFormat().id), xportA(), payload, m));
                return makeResult<AncPacket::List>(std::move(out));
        }

        AncTranslator::PacketsResult stubBuildB(const Variant & /*v*/, const AncTranslateConfig & /*cfg*/) {
                Buffer   payload(static_cast<size_t>(0));
                Metadata m;
                m.set(Metadata::declareID("AncTranslatorTest.Built",
                                          VariantSpec().setType(DataTypeString).setDefault(String())),
                      String(kStubBuiltB));
                AncPacket::List out;
                out.pushToBack(AncPacket(AncFormat(testFormat().id), xportB(), payload, m));
                return makeResult<AncPacket::List>(std::move(out));
        }

        AncTranslator::PacketsResult stubDirectAB(const AncPacket & /*pkt*/, AncTransport target,
                                              const AncTranslateConfig & /*cfg*/) {
                Buffer   payload(static_cast<size_t>(0));
                Metadata m;
                m.set(Metadata::declareID("AncTranslatorTest.Direct",
                                          VariantSpec().setType(DataTypeString).setDefault(String())),
                      String(kStubDirectAB));
                AncPacket::List out;
                out.pushToBack(AncPacket(AncFormat(testFormat().id), target, payload, m));
                return makeResult<AncPacket::List>(std::move(out));
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
        AncTranslator::ParseResult r = t.parse(pkt);
        CHECK(r.second().isOk());
        CHECK(r.first().get<String>() == kStubDecoded);
}

TEST_CASE("AncTranslator: parse propagates held config to handlers") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AllowLossy, true);
        AncTranslator   t(cfg);
        AncPacket       pkt = makeStubPacket(xportB());
        AncTranslator::ParseResult r = t.parse(pkt);
        CHECK(r.second().isOk());
        CHECK(r.first().get<String>() == String(kStubDecoded) + "+lossy");
}

TEST_CASE("AncTranslator: parse returns NotSupported when no parser registered") {
        AncTranslator   t;
        Buffer          payload(static_cast<size_t>(0));
        AncPacket       pkt(AncFormat(testFormat().id), AncTransport::RtmpAmf, payload);
        AncTranslator::ParseResult r = t.parse(pkt);
        CHECK(r.second().code() == Error::NotSupported);
}

TEST_CASE("AncTranslator: build dispatches to the registered handler") {
        ensureStubs();
        AncTranslator     t;
        AncTranslator::PacketsResult r = t.build(Variant(String("ignored")), AncFormat(testFormat().id), xportA());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first().front().transport() == xportA());
        CHECK(r.first().front().format().id() == testFormat().id);
}

TEST_CASE("AncTranslator: build returns NotSupported when no builder registered") {
        AncTranslator     t;
        AncTranslator::PacketsResult r = t.build(Variant(String("ignored")), AncFormat(testFormat().id),
                                             AncTransport::RtmpAmf);
        CHECK(r.second().code() == Error::NotSupported);
}

// ============================================================================
// translate() — identity, direct, composed
// ============================================================================

TEST_CASE("AncTranslator: translate identity short-circuit returns packet unchanged") {
        AncTranslator     t;
        AncPacket         pkt = makeStubPacket(xportA());
        AncTranslator::PacketsResult r = t.translate(pkt, xportA());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        // Same impl (handle compares cheaply via packet identity equality).
        CHECK(r.first().front() == pkt);
}

TEST_CASE("AncTranslator: translate falls back to composed parse+build path") {
        // Direct registrar deliberately NOT called: we want this path to go
        // through parse(B) → build(A).  The B-side stub parser returns a
        // String("STUB-DECODED+lossless") which the A-side stub builder
        // ignores; the test just verifies that the result packet ends up on
        // the requested target transport.
        AncTranslator     t;
        AncPacket         pkt = makeStubPacket(xportB());
        AncTranslator::PacketsResult r = t.translate(pkt, xportA());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first().front().transport() == xportA());
        CHECK(r.first().front().format().id() == testFormat().id);
}

TEST_CASE("AncTranslator: translate prefers direct translator when registered") {
        ensureDirect();
        CHECK(AncTranslator::hasDirectTranslator(AncFormat(testFormat().id), xportA(), xportB()));

        AncTranslator     t;
        AncPacket         pkt = makeStubPacket(xportA());
        AncTranslator::PacketsResult r = t.translate(pkt, xportB());
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().size() == 1);
        CHECK(r.first().front().transport() == xportB());
        // Direct path tagged the output meta with kStubDirectAB.
        Metadata::ID directKey = Metadata::declareID(
                "AncTranslatorTest.Direct",
                VariantSpec().setType(DataTypeString).setDefault(String()));
        CHECK(r.first().front().meta().getAs<String>(directKey) == String(kStubDirectAB));
}

TEST_CASE("AncTranslator: translate returns NotSupported when no path exists") {
        AncTranslator     t;
        Buffer            payload(static_cast<size_t>(0));
        AncPacket         pkt(AncFormat(testFormat().id), AncTransport::RtmpAmf, payload);
        AncTranslator::PacketsResult r = t.translate(pkt, AncTransport::HdmiInfoFrame);
        CHECK(r.second().code() == Error::NotSupported);
}

// ============================================================================
// describe()
// ============================================================================

namespace {
// A parser that returns a real decoded ANC value type (AncAfd) — one
// whose Variant carries a wired toString op, unlike a bare String
// payload — so describe() exercises the genuine render path.
AncTranslator::ParseResult afdStubParse(const AncPacket & /*pkt*/, const AncTranslateConfig & /*cfg*/) {
        return makeResult<Variant>(Variant(AncAfd(0x09, true)));
}
} // namespace

TEST_CASE("AncTranslator: describe renders the decoded value type as a string") {
        const AncFormat::ID id = AncFormat::registerType();
        AncFormat::Data d;
        d.id = id;
        d.name = String("DescribeTestFormat");
        d.category = AncCategory::UserDefined;
        d.canonicalTransport = AncTransport::St291;
        AncFormat::registerData(std::move(d));
        AncTranslator::registerParser(id, AncTransport::St291, &afdStubParse);

        Buffer    payload(static_cast<size_t>(0));
        AncPacket pkt(AncFormat(id), AncTransport::St291, payload);
        AncTranslator t;
        CHECK(t.describe(pkt) == AncAfd(0x09, true).toString());
        CHECK_FALSE(t.describe(pkt).isEmpty());
}

TEST_CASE("AncTranslator: describe returns empty when no parser is registered") {
        const AncFormat::ID id = AncFormat::registerType();
        AncFormat::Data d;
        d.id = id;
        d.name = String("DescribeNoParserFormat");
        d.category = AncCategory::UserDefined;
        d.canonicalTransport = AncTransport::St291;
        AncFormat::registerData(std::move(d));

        Buffer    payload(static_cast<size_t>(0));
        AncPacket pkt(AncFormat(id), AncTransport::St291, payload);
        AncTranslator t;
        CHECK(t.describe(pkt).isEmpty());
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

// ============================================================================
// P2-23: parseGroup / multi-parser dispatch
// ============================================================================

TEST_CASE("AncTranslator: P2-23 hasParser true when only a multi-parser is registered") {
        // The ST 2020 codec is the in-tree exemplar of a format that
        // registers a multi-parser but no single-parser.  hasParser
        // must report true for it on St291 so callers querying
        // capability before parsing don't conclude "no parser
        // available" and skip the format entirely.
        CHECK(AncTranslator::hasParser(AncFormat(AncFormat::Smpte2020Audio),
                                       AncTransport::St291));
}

TEST_CASE("AncTranslator: P2-23 parseGroup dispatches to the multi-parser") {
        // Hand-construct a minimal valid 2-packet ST 2020 split: each
        // packet carries the §5.4.3 descriptor byte (DOUBLE=1) plus
        // 254 bytes of zero MDF payload, on SDID=0x02 (channel pair
        // 1/2).
        const uint8_t kSdid = 0x02;
        auto makePkt = [&](bool second) -> AncPacket {
                List<uint16_t> udw;
                udw.resize(255);
                // Descriptor: VERSION=01b, DOUBLE=1, SECOND=second.
                udw[0] = static_cast<uint16_t>(0x08 | 0x04 | (second ? 0x02 : 0x00));
                for (size_t i = 1; i < 255; ++i) udw[i] = 0;
                St291Packet p = St291Packet::buildRaw(/*did*/ 0x45, kSdid, udw, /*line*/ 9);
                return p.packet();
        };

        AncPacket::List pkts;
        pkts.pushToBack(makePkt(false));
        pkts.pushToBack(makePkt(true));

        AncTranslator t;
        AncTranslator::ParseResult r = t.parseGroup(pkts);
        REQUIRE(r.second().isOk());
        // The decoded Variant carries an AncSt2020Audio with both
        // halves concatenated into a 508-byte MDF.
        CHECK(r.first().type() == DataTypeSt2020Audio);
}

// Helper: allocate a brand-new test format ID *and* register its
// AncFormat::Data so that @c AncFormat(id) resolves through
// @c AncFormat::lookupData back to @p id (otherwise the AncFormat
// ctor reports @c Invalid for IDs lacking a Data record, which
// silently breaks @c hasParser / @c hasBuilder lookups keyed on the
// returned AncFormat).  Mirrors the existing TestFormat fixture but
// scoped per-test so each P2-31 case gets a private (format,
// transport) slot.
static AncFormat::ID p2_31_allocFormatId(const char *nameSuffix) {
        AncFormat::ID id = AncFormat::registerType();
        AncFormat::Data d;
        d.id = id;
        d.name = String("AncTranslatorP2_31_") + String(nameSuffix);
        d.desc = String("Private test format for AncTranslator P2-31 tests");
        d.category = AncCategory::UserDefined;
        d.canonicalTransport = AncTransport::St291;
        AncFormat::registerData(std::move(d));
        return id;
}

TEST_CASE("AncTranslator: P2-31 re-registering the same fn is idempotent (no warn, no throw)") {
        // Idempotent carve-out: a library that registers itself twice
        // through a static-init graph, or a test that re-installs the
        // same stub, should not see a collision warn or a DEBUG-build
        // throw.  The dispatcher state is identical either way.
        AncFormat::ID id = p2_31_allocFormatId("idempotent");
        AncTranslator::registerParser(id, AncTransport::St291, &stubParseA);
        // Same fn pointer → idempotent.
        AncTranslator::registerParser(id, AncTransport::St291, &stubParseA);
        AncTranslator::registerBuilder(id, AncTransport::St291, &stubBuildA);
        AncTranslator::registerBuilder(id, AncTransport::St291, &stubBuildA);
        CHECK(AncTranslator::hasParser(AncFormat(id), AncTransport::St291));
        CHECK(AncTranslator::hasBuilder(AncFormat(id), AncTransport::St291));
}

#ifdef PROMEKI_DEBUG_ENABLE
TEST_CASE("AncTranslator: P2-31 DEBUG builds hard-fail on registry collision with a different fn") {
        // Static-init time collisions ("two TUs register the same
        // (format, transport) key with *different* fn pointers") are
        // programming errors — the dispatcher's final state becomes
        // link-order dependent.  In PROMEKI_DEBUG_ENABLE builds the
        // second register call throws via PROMEKI_ASSERT so a
        // duplicate registration never ships silently with
        // last-write-wins semantics.  In plain Release builds the
        // warn + last-wins path is preserved (not tested here because
        // the warn channel is not observable).
        AncFormat::ID collidingId = p2_31_allocFormatId("collision");

        // First registration must succeed cleanly.
        AncTranslator::registerParser(collidingId, AncTransport::St291, &stubParseA);

        // Second registration with a *different* fn must throw.
        CHECK_THROWS_AS(
                AncTranslator::registerParser(collidingId, AncTransport::St291, &stubParseB),
                std::runtime_error);

        // The same pattern holds for every other registry; spot-check
        // the builder path to confirm the helper fires across all
        // five entry points.
        AncTranslator::registerBuilder(collidingId, AncTransport::St291, &stubBuildA);
        CHECK_THROWS_AS(
                AncTranslator::registerBuilder(collidingId, AncTransport::St291, &stubBuildB),
                std::runtime_error);
}
#endif

TEST_CASE("AncTranslator: P2-23 parse on a DOUBLE_PKT packet returns InsufficientContext") {
        // ST 2020 codec deliberately signals "I need the multi-parser"
        // via Error::InsufficientContext when a single packet arrives
        // with DOUBLE=1.  Callers route the packet through parseGroup
        // after observing the error.
        List<uint16_t> udw;
        udw.resize(255);
        udw[0] = static_cast<uint16_t>(0x08 | 0x04); // VERSION=01b, DOUBLE=1.
        for (size_t i = 1; i < 255; ++i) udw[i] = 0;
        St291Packet p = St291Packet::buildRaw(/*did*/ 0x45, /*sdid*/ 0x02, udw, /*line*/ 9);

        AncTranslator t;
        AncTranslator::ParseResult r = t.parse(p.packet());
        CHECK(r.second().code() == Error::InsufficientContext);
}

// ============================================================================
// details() — registered detailer dispatch + generic fallback
// ============================================================================

namespace {
// A bespoke detailer that ignores the wire bytes and emits a fixed
// analysis — lets the test prove the registered detailer wins over the
// generic fallback.
AncDetails stubDetailer(const AncPacket & /*pkt*/, const AncTranslateConfig & /*cfg*/) {
        AncDetails d;
        d.addField("Stub", "DETAILED");
        d.addWarning("stub warning");
        return d;
}
} // namespace

TEST_CASE("AncTranslator: details dispatches to a registered detailer") {
        const AncFormat::ID id = p2_31_allocFormatId("detailer");
        AncTranslator::registerDetailer(id, AncTransport::St291, &stubDetailer);
        CHECK(AncTranslator::hasDetailer(AncFormat(id), AncTransport::St291));

        Buffer    payload(static_cast<size_t>(0));
        AncPacket pkt(AncFormat(id), AncTransport::St291, payload);
        AncTranslator t;
        AncDetails    d = t.details(pkt);

        CHECK(d.lines().contains(String("Stub = DETAILED")));
        CHECK(d.hasWarnings());
        CHECK(d.issueCount(AncDetailSeverity::Warning) == 1);
}

TEST_CASE("AncTranslator: details generic fallback renders header + parsed value") {
        // A format with a parser (returning a real AncAfd value, which has a
        // Variant toString op) but no detailer: details() takes the generic
        // fallback, which spells out the header and renders the parsed value.
        const AncFormat::ID id = p2_31_allocFormatId("fallback");
        AncTranslator::registerParser(id, AncTransport::St291, &afdStubParse);
        CHECK_FALSE(AncTranslator::hasDetailer(AncFormat(id), AncTransport::St291));

        Buffer        payload(static_cast<size_t>(0));
        AncPacket     pkt(AncFormat(id), AncTransport::St291, payload);
        AncTranslator t;
        AncDetails    d = t.details(pkt);

        // Transport-level header fields are always spelled out, with enum
        // values stringified (Category / Transport names, not raw ints).
        CHECK(d.lines().contains(String("Category = UserDefined")));
        CHECK(d.lines().contains(String("Transport = St291")));
        // The registered parser's value rendered through its toString.
        CHECK(d.lines().contains(String("Decoded = ") + AncAfd(0x09, true).toString()));
}

TEST_CASE("AncTranslator: details fallback notes when no decoder is registered") {
        // A format with neither a detailer nor a parser for the transport:
        // the fallback still emits the header and an Info issue rather than
        // failing.
        const AncFormat::ID id = p2_31_allocFormatId("nodecoder");
        Buffer        payload(static_cast<size_t>(0));
        AncPacket     pkt(AncFormat(id), AncTransport::RtmpAmf, payload);
        AncTranslator t;
        AncDetails    d = t.details(pkt);

        CHECK(d.lines().contains(String("Transport = RtmpAmf")));
        CHECK(d.issueCount(AncDetailSeverity::Info) >= 1);
        CHECK_FALSE(d.hasErrors());
}
