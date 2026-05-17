/**
 * @file      cases/videocarrier.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Video-signal-carrier integration test (Phase 5 of
 * @c devplan/proav/video-signal-carriers.md).
 *
 * Builds a @ref MediaConfig populated with one entry per new
 * carrier-level key (@c SdiInputSignal, @c SdiOutputSignal,
 * @c HdmiInputSignal, @c HdmiOutputSignal, @c VideoReference),
 * round-trips that config through @ref MediaConfig::toJson /
 * @ref MediaConfig::fromJson and the @ref DataStream path, and
 * field-checks the recovered values.  The case exists to prove the
 * new types are first-class through the existing config plumbing
 * even though no MediaIO backend acts on them yet — the NTV2 work
 * (@c devplan/proav/ntv2.md) will be the first real consumer.
 */

#include "cases.h"
#include "../testcontext.h"
#include "../testrunner.h"

#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/datatype.h>
#include <promeki/error.h>
#include <promeki/hdmisignalconfig.h>
#include <promeki/json.h>
#include <promeki/mediaconfig.h>
#include <promeki/result.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/string.h>
#include <promeki/variant.h>
#include <promeki/videoportref.h>
#include <promeki/videoreferenceconfig.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                // Builds the canonical fixture every variant of the test
                // checks against — one populated value per new carrier
                // key, chosen to exercise the multi-segment string forms
                // (quad-link SDI, HDMI 2.1 version hint, FromSignal
                // reference with a paired port).
                struct CarrierFixture {
                                SdiSignalConfig      sdiIn;
                                SdiSignalConfig      sdiOut;
                                HdmiSignalConfig     hdmiIn;
                                HdmiSignalConfig     hdmiOut;
                                VideoReferenceConfig reference;
                };

                CarrierFixture buildFixture() {
                        CarrierFixture f;
                        f.sdiIn  = SdiSignalConfig::singleLink(SdiLinkStandard::SL_12G,
                                                               VideoPortRef(VideoConnectorKind::Sdi, 1));
                        f.sdiOut = SdiSignalConfig::quadLink(SdiLinkStandard::QL_3G_2SI,
                                                             VideoPortRef(VideoConnectorKind::Sdi, 1),
                                                             VideoPortRef(VideoConnectorKind::Sdi, 2),
                                                             VideoPortRef(VideoConnectorKind::Sdi, 3),
                                                             VideoPortRef(VideoConnectorKind::Sdi, 4));
                        f.hdmiIn  = HdmiSignalConfig(VideoPortRef(VideoConnectorKind::Hdmi, 1), HdmiSpecVersion::Hdmi21);
                        f.hdmiOut = HdmiSignalConfig(VideoPortRef(VideoConnectorKind::Hdmi, 2), HdmiSpecVersion::Hdmi20);
                        f.reference =
                                VideoReferenceConfig(VideoReferenceSource::FromSignal, VideoReferenceRateFamily::Fractional);
                        f.reference.setSignalPort(VideoPortRef(VideoConnectorKind::Sdi, 1));
                        return f;
                }

                // Populates @p cfg with one entry per carrier key from
                // the fixture so subsequent serialization paths have a
                // consistent body to round-trip.
                void populate(MediaConfig &cfg, const CarrierFixture &f) {
                        cfg.set(MediaConfig::SdiInputSignal, f.sdiIn);
                        cfg.set(MediaConfig::SdiOutputSignal, f.sdiOut);
                        cfg.set(MediaConfig::HdmiInputSignal, f.hdmiIn);
                        cfg.set(MediaConfig::HdmiOutputSignal, f.hdmiOut);
                        cfg.set(MediaConfig::VideoReference, f.reference);
                }

                // Returns the recovered value for @p id, or an empty
                // Variant when the key is absent.  The keys are pulled
                // out of the database one-by-one so the per-key error
                // path can be reported back through TestContext.
                template <typename T> bool checkKey(TestContext &ctx, const MediaConfig &cfg, MediaConfig::ID id,
                                                    const T &expected, const String &keyName) {
                        if (!cfg.contains(id)) {
                                ctx.setFail(String("key absent after round-trip: ") + keyName);
                                return false;
                        }
                        Variant      v       = cfg.get(id);
                        const T     *direct  = v.peek<T>();
                        if (direct == nullptr) {
                                ctx.setFail(String("key has wrong type after round-trip: ") + keyName +
                                            String(" (got DataType 0x") + String::number(static_cast<int64_t>(v.type()), 16) +
                                            String(")"));
                                return false;
                        }
                        if (!(*direct == expected)) {
                                ctx.setFail(String("value mismatch after round-trip: ") + keyName +
                                            String(" (got '") + direct->toString() +
                                            String("', expected '") + expected.toString() + String("')"));
                                return false;
                        }
                        return true;
                }

                bool verifyAll(TestContext &ctx, const MediaConfig &cfg, const CarrierFixture &f) {
                        return checkKey(ctx, cfg, MediaConfig::SdiInputSignal,   f.sdiIn,   String("SdiInputSignal"))
                            && checkKey(ctx, cfg, MediaConfig::SdiOutputSignal,  f.sdiOut,  String("SdiOutputSignal"))
                            && checkKey(ctx, cfg, MediaConfig::HdmiInputSignal,  f.hdmiIn,  String("HdmiInputSignal"))
                            && checkKey(ctx, cfg, MediaConfig::HdmiOutputSignal, f.hdmiOut, String("HdmiOutputSignal"))
                            && checkKey(ctx, cfg, MediaConfig::VideoReference,   f.reference, String("VideoReference"));
                }

                // ---------------------------------------------------------------
                // JSON round-trip
                // ---------------------------------------------------------------

                void runJsonRoundtrip(TestContext &ctx) {
                        const CarrierFixture f = buildFixture();
                        MediaConfig original;
                        populate(original, f);

                        JsonObject json = original.toJson();
                        const String dump = json.toString();
                        ctx.setDetail(String("json"), Variant(dump));

                        // Parse the JSON text back through the standard
                        // string→JsonObject pipeline so the test covers
                        // both the value-side and the string-side of the
                        // serialization stack.
                        Error      parseErr;
                        JsonObject parsed = JsonObject::parse(dump, &parseErr);
                        if (parseErr.isError()) {
                                ctx.setFail(String("JSON re-parse failed: ") + parseErr.name());
                                return;
                        }
                        // MediaConfig inherits the base @c fromJson which
                        // hands back a @ref VariantDatabase, so rather
                        // than slicing the result, walk the JsonObject
                        // and route every entry through @ref setFromJson
                        // — the same coercion path the base
                        // implementation uses.
                        MediaConfig restored;
                        parsed.forEach([&restored](const String &key, const Variant &val) {
                                restored.setFromJson(MediaConfig::ID(key), val);
                        });
                        if (!verifyAll(ctx, restored, f)) return;

                        ctx.setDetail(String("keys"), Variant(static_cast<int64_t>(5)));
                        ctx.setPass();
                }

                // ---------------------------------------------------------------
                // DataStream round-trip
                // ---------------------------------------------------------------

                void runDataStreamRoundtrip(TestContext &ctx) {
                        const CarrierFixture f = buildFixture();
                        MediaConfig original;
                        populate(original, f);

                        Buffer         storage(16 * 1024);
                        BufferIODevice dev(&storage);
                        dev.open(IODevice::ReadWrite);
                        {
                                DataStream w = DataStream::createWriter(&dev);
                                original.writeTo(w);
                                if (w.status() != DataStream::Ok) {
                                        ctx.setFail(String("DataStream write failed: ") + w.toError().name());
                                        return;
                                }
                        }
                        dev.seek(0);
                        MediaConfig restored;
                        {
                                DataStream r = DataStream::createReader(&dev);
                                restored.readFrom(r);
                                if (r.status() != DataStream::Ok) {
                                        ctx.setFail(String("DataStream read failed: ") + r.toError().name());
                                        return;
                                }
                        }
                        if (!verifyAll(ctx, restored, f)) return;
                        ctx.setPass();
                }

                // ---------------------------------------------------------------
                // Variant <-> String converter coverage
                //
                // Round-trips every key through DataTypeOf<T>::id via the
                // spec-driven parseString path that @c MediaConfig::fromJson
                // uses for non-native primitives.  Catches a regression
                // where the DataType registry lost a fromString slot or
                // the registered name drifted.
                // ---------------------------------------------------------------

                void runVariantStringRoundtrip(TestContext &ctx) {
                        const CarrierFixture f = buildFixture();
                        struct Pair {
                                        const char *name;
                                        DataTypeID  id;
                                        String      text;
                                        Variant     original;
                        };
                        const Pair cases[] = {
                                {"SdiInputSignal",   DataTypeSdiSignalConfig,      f.sdiIn.toString(),     Variant(f.sdiIn)},
                                {"SdiOutputSignal",  DataTypeSdiSignalConfig,      f.sdiOut.toString(),    Variant(f.sdiOut)},
                                {"HdmiInputSignal",  DataTypeHdmiSignalConfig,     f.hdmiIn.toString(),    Variant(f.hdmiIn)},
                                {"HdmiOutputSignal", DataTypeHdmiSignalConfig,     f.hdmiOut.toString(),   Variant(f.hdmiOut)},
                                {"VideoReference",   DataTypeVideoReferenceConfig, f.reference.toString(), Variant(f.reference)},
                        };
                        for (const auto &c : cases) {
                                Variant sv(c.text);
                                Error   err;
                                Variant parsed = sv.convertTo(c.id, &err);
                                if (err.isError()) {
                                        ctx.setFail(String("String->Variant convert failed for ") + String(c.name) +
                                                    String(": ") + err.name());
                                        return;
                                }
                                if (parsed.type() != c.id) {
                                        ctx.setFail(String("String->Variant convert yielded wrong type for ") +
                                                    String(c.name));
                                        return;
                                }
                                // Re-serialise the parsed Variant back to String and
                                // compare the canonical form — this catches a
                                // round-trip mismatch even when operator== happens to
                                // match (defensive belt-and-suspenders).
                                String reEmitted = parsed.toString(&err);
                                if (err.isError()) {
                                        ctx.setFail(String("Variant->String failed for ") + String(c.name));
                                        return;
                                }
                                if (reEmitted != c.text) {
                                        ctx.setFail(String("String<->Variant round-trip mismatch for ") +
                                                    String(c.name) + String(" (got '") + reEmitted +
                                                    String("', expected '") + c.text + String("')"));
                                        return;
                                }
                        }
                        ctx.setDetail(String("cases"), Variant(static_cast<int64_t>(sizeof(cases) / sizeof(cases[0]))));
                        ctx.setPass();
                }

        } // anonymous namespace

        void registerVideoCarrierCases() {
                TestRunner::registerCase(TestCase(
                        String("videocarrier.mediaconfig.json_roundtrip"),
                        String("MediaConfig with every new video-carrier key survives JSON round-trip."),
                        TestCase::Function(runJsonRoundtrip)));
                TestRunner::registerCase(TestCase(
                        String("videocarrier.mediaconfig.datastream_roundtrip"),
                        String("MediaConfig with every new video-carrier key survives DataStream round-trip."),
                        TestCase::Function(runDataStreamRoundtrip)));
                TestRunner::registerCase(TestCase(
                        String("videocarrier.variant.string_roundtrip"),
                        String("Each carrier type round-trips through the Variant<->String converter."),
                        TestCase::Function(runVariantStringRoundtrip)));
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
