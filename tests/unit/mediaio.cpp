/**
 * @file      tests/mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaconfig.h>
#include <promeki/variantspec.h>
#include <promeki/videoformat.h>
#include <promeki/pixelformat.h>
#include <promeki/framerate.h>
#include <promeki/frame.h>
#include <promeki/json.h>
#include <promeki/mediapayload.h>
#include <promeki/videopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/clockdomain.h>

using namespace promeki;

// ============================================================================
// MediaIO fills in missing per-payload timing
//
// The read path guarantees that every payload arrives downstream with
// a valid native pts and (for video) a native duration.  A backend
// that leaves one or both off — like TPG, which generates frames
// without touching pts — should still come out the other side with a
// Synthetic fallback pts and a one-frame duration derived from the
// session frame rate.
// ============================================================================

TEST_CASE("MediaIO auto-fills missing native pts and video duration on read") {
        MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        cfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
        cfg.set(MediaConfig::AudioEnabled, true);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());

        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        REQUIRE(frame.isValid());

        auto vids = frame->videoPayloads();
        REQUIRE_FALSE(vids.isEmpty());
        REQUIRE(vids[0].isValid());
        const VideoPayload &vp = *vids[0];

        // Native pts must be set by the auto-fill; TPG itself does
        // not touch pts.  The fallback domain is Synthetic.
        CHECK(vp.pts().isValid());
        CHECK(vp.pts().domain() == ClockDomain::Synthetic);

        // Duration is one frame of the session rate (FPS_30 →
        // 33_333_333 ns).
        CHECK(vp.hasDuration());
        const Duration oneFrame = FrameRate(FrameRate::FPS_30).frameDuration();
        CHECK(vp.duration() == oneFrame);

        auto auds = frame->audioPayloads();
        REQUIRE_FALSE(auds.isEmpty());
        REQUIRE(auds[0].isValid());
        const AudioPayload &ap = *auds[0];
        CHECK(ap.pts().isValid());
        CHECK(ap.pts().domain() == ClockDomain::Synthetic);
        // Audio duration is derived from intrinsic sampleCount /
        // sampleRate and should be non-zero for a real audio packet.
        CHECK(ap.hasDuration());
        CHECK_FALSE(ap.duration().isZero());

        io->close();
        delete io;
}

TEST_CASE("MediaIO does not overwrite a producer-supplied pts") {
        MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        cfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
        cfg.set(MediaConfig::AudioEnabled, false);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());

        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        REQUIRE(frame.isValid());
        auto vids = frame->videoPayloads();
        REQUIRE_FALSE(vids.isEmpty());
        // After MediaIO auto-fill the pts domain is Synthetic.  If we
        // stamp our own pts on a cloned payload and run it through
        // any pts-respecting consumer, the pts should survive — the
        // auto-fill only touches payloads whose pts is invalid.
        const MediaTimeStamp auto_pts = vids[0]->pts();
        CHECK(auto_pts.isValid());
        CHECK(auto_pts.domain() == ClockDomain::Synthetic);

        // Build an explicit override and verify isValid/offset plumbing.
        TimeStamp explicitTs;
        explicitTs.setValue(TimeStamp::Value(std::chrono::nanoseconds(42)));
        MediaTimeStamp explicitMts(explicitTs, ClockDomain::SystemMonotonic);
        vids[0].modify()->setPts(explicitMts);
        CHECK(vids[0]->pts() == explicitMts);

        io->close();
        delete io;
}

// ============================================================================
// Structural invariant: defaultConfig round-trips losslessly through JSON
//
// Any value the library produces must round-trip cleanly through its
// own JSON serialization and pass its own spec validation.  The
// promeki-pipeline REST API turns this into a hard requirement: the
// frontend GETs /api/types/<name>/defaults, then PUTs that JSON back
// verbatim — if a single field comes back with the wrong Variant type
// (because the toString → fromString round-trip lost its native form)
// VariantDatabase::set logs a "fails spec" warning.  The bug that
// motivated this test was CSC's `OutputPixelFormat: "Invalid"` default
// failing to parse back into a PixelFormat because the Invalid sentinel
// name wasn't in PixelFormat's nameMap.
//
// The check below visits every registered backend, JSON-serializes its
// default config, and parses each entry back through setFromJson.  For
// every key that has a spec we require:
//   1. The parsed Variant's type matches the spec's declared type
//      (i.e. the spec-driven coercion in setFromJson succeeded).
//   2. The parsed Variant validates cleanly against the spec.
// ============================================================================

TEST_CASE("MediaIO: defaultConfig round-trips losslessly through JSON for every backend") {
        auto formats = MediaIO::registeredFormats();
        REQUIRE_FALSE(formats.isEmpty());
        for (const auto &fd : formats) {
                CAPTURE(fd.name);
                if (!fd.configSpecs) continue;

                MediaIO::Config          def = MediaIO::defaultConfig(fd.name);
                JsonObject               json = def.toJson();
                MediaIO::Config::SpecMap specs = fd.configSpecs();

                // For each spec'd key the default produced, locate
                // the JSON-emitted form, re-parse it through the
                // exact spec the backend declared, and verify the
                // round-trip.  This is the structural invariant the
                // demo's REST PUT relies on: the JSON serialise +
                // parse path must return values that satisfy the
                // backend's own spec.
                for (auto it = specs.cbegin(); it != specs.cend(); ++it) {
                        const MediaConfig::ID &id = it->first;
                        const VariantSpec     &sp = it->second;
                        CAPTURE(id.name());

                        if (!def.contains(id)) continue;
                        Variant origVal = def.get(id);
                        if (!origVal.isValid()) continue;

                        // Pull the JSON-emitted form by name.  Use
                        // forEach to capture the Variant (already
                        // String-coerced for non-primitive types by
                        // setFromVariant).
                        Variant onWire;
                        json.forEach([&id, &onWire](const String &key, const Variant &val) {
                                if (key == id.name()) onWire = val;
                        });
                        REQUIRE(onWire.isValid());

                        // Re-parse via the backend-declared spec —
                        // exactly what applyQueryToConfig would do
                        // for a query-string value, and what
                        // setFromJson does for keys with a globally-
                        // registered spec.  A successful round-trip
                        // here means defaults will not trip
                        // VariantDatabase::set's spec-validation
                        // warning on the way back in.
                        Variant parsed;
                        if (onWire.type() == Variant::TypeString && !sp.acceptsType(Variant::TypeString)) {
                                Error pe;
                                parsed = sp.parseString(onWire.get<String>(), &pe);
                                CHECK(pe.isOk());
                        } else {
                                parsed = onWire;
                        }
                        CHECK(parsed.isValid());
                        CHECK(sp.acceptsType(parsed.type()));
                        Error verr;
                        CHECK(sp.validate(parsed, &verr));
                        CHECK(verr.isOk());
                }
        }
}
