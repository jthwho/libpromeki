/**
 * @file      mediaiodescription.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/framerate.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiodescription.h>
#include <promeki/metadata.h>
#include <promeki/pixeldesc.h>
#include <promeki/uuid.h>

using namespace promeki;

namespace {

// Builds a fully-populated description so round-trip tests cover
// every member.  Uses a couple of representative MediaDescs in each
// of the producible / acceptable lists so list serialization is
// exercised.
MediaDesc makeUncompressedDesc(uint32_t w, uint32_t h, const PixelDesc &pd) {
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_29_97));
        ImageDesc id(Size2Du32(w, h), pd);
        md.imageList().pushToBack(id);
        return md;
}

MediaIODescription makeSample() {
        MediaIODescription d;
        d.setBackendName("TPG");
        d.setBackendDescription("Synthetic test pattern generator");
        d.setName("tpg-1");
        d.setUuid(UUID::generate(4));
        d.setLocalId(7);
        d.setCanBeSource(true);
        d.setCanBeSink(false);
        d.setCanBeTransform(false);

        d.producibleFormats().pushToBack(
                makeUncompressedDesc(1920, 1080, PixelDesc(PixelDesc::RGBA8_sRGB)));
        d.producibleFormats().pushToBack(
                makeUncompressedDesc(1280,  720, PixelDesc(PixelDesc::RGBA8_sRGB)));
        d.setPreferredFormat(
                makeUncompressedDesc(1920, 1080, PixelDesc(PixelDesc::RGBA8_sRGB)));

        d.setCanSeek(true);
        d.setFrameCount(MediaIODescription::FrameCountInfinite);
        d.setFrameRate(FrameRate(FrameRate::FPS_29_97));

        Metadata md;
        md.set(Metadata::Title, String("MediaIODescription sample"));
        d.setContainerMetadata(md);

        d.setProbeStatus(Error::Ok);
        return d;
}

} // namespace

TEST_CASE("MediaIODescription_Default") {
        MediaIODescription d;
        CHECK(d.backendName().isEmpty());
        CHECK(d.backendDescription().isEmpty());
        CHECK(d.name().isEmpty());
        CHECK_FALSE(d.uuid().isValid());
        CHECK(d.localId() == -1);
        CHECK_FALSE(d.canBeSource());
        CHECK_FALSE(d.canBeSink());
        CHECK_FALSE(d.canBeTransform());
        CHECK(d.producibleFormats().isEmpty());
        CHECK(d.acceptableFormats().isEmpty());
        CHECK_FALSE(d.preferredFormat().isValid());
        CHECK_FALSE(d.canSeek());
        CHECK(d.frameCount() == FrameCount::unknown());
        CHECK_FALSE(d.frameRate().isValid());
        CHECK(d.containerMetadata().isEmpty());
        CHECK(d.probeStatus() == Error::Ok);
        CHECK(d.probeMessage().isEmpty());
}

TEST_CASE("MediaIODescription_Setters") {
        MediaIODescription d;
        d.setBackendName("V4L2");
        d.setBackendDescription("Video4Linux2 device");
        d.setName("video0");
        UUID u = UUID::generate(4);
        d.setUuid(u);
        d.setLocalId(3);
        d.setCanBeSource(true);
        d.setCanBeTransform(true);
        d.setCanSeek(true);
        d.setFrameCount(1234);
        d.setFrameRate(FrameRate(FrameRate::FPS_25));

        CHECK(d.backendName() == "V4L2");
        CHECK(d.backendDescription() == "Video4Linux2 device");
        CHECK(d.name() == "video0");
        CHECK(d.uuid() == u);
        CHECK(d.localId() == 3);
        CHECK(d.canBeSource());
        CHECK_FALSE(d.canBeSink());
        CHECK(d.canBeTransform());
        CHECK(d.canSeek());
        CHECK(d.frameCount() == FrameCount(1234));
        CHECK(d.frameRate() == FrameRate(FrameRate::FPS_25));
}

TEST_CASE("MediaIODescription_Equality") {
        MediaIODescription a = makeSample();
        MediaIODescription b = makeSample();

        // The sample uses UUID::generate, so two fresh samples differ
        // in their UUID — explicitly equalize before comparing.
        b.setUuid(a.uuid());

        CHECK(a == b);
        CHECK_FALSE(a != b);

        b.setName("different");
        CHECK(a != b);
        CHECK_FALSE(a == b);

        b = makeSample();
        b.setUuid(a.uuid());
        b.setProbeStatus(Error::Invalid);
        CHECK(a != b);

        b = makeSample();
        b.setUuid(a.uuid());
        b.acceptableFormats().pushToBack(
                makeUncompressedDesc(640, 480, PixelDesc(PixelDesc::RGBA8_sRGB)));
        CHECK(a != b);
}

TEST_CASE("MediaIODescription_Summary") {
        MediaIODescription d = makeSample();
        const StringList lines = d.summary();
        REQUIRE(lines.size() > 0);

        // Header line must include the backend name and the
        // user-assigned instance name.
        CHECK(lines[0].contains("TPG"));
        CHECK(lines[0].contains("tpg-1"));

        // Roles line must list the source role and only that role.
        bool foundRoles = false;
        bool foundProducible = false;
        for(size_t i = 0; i < lines.size(); ++i) {
                if(lines[i].startsWith("  Roles:")) {
                        CHECK(lines[i].contains("source"));
                        CHECK_FALSE(lines[i].contains("sink"));
                        foundRoles = true;
                }
                if(lines[i].startsWith("  Producible")) foundProducible = true;
        }
        CHECK(foundRoles);
        CHECK(foundProducible);
}

TEST_CASE("MediaIODescription_DataStreamRoundTrip") {
        MediaIODescription orig = makeSample();
        Buffer buf(16384);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream writer = DataStream::createWriter(&dev);
                writer << orig;
                CHECK(writer.status() == DataStream::Ok);
        }

        dev.seek(0);

        MediaIODescription round;
        {
                DataStream reader = DataStream::createReader(&dev);
                reader >> round;
                CHECK(reader.status() == DataStream::Ok);
        }

        // The DataStream round-trip is the canonical lossless
        // representation — every field must come back identical.
        CHECK(round == orig);
}

TEST_CASE("MediaIODescription_JsonShape") {
        MediaIODescription orig = makeSample();
        JsonObject j = orig.toJson();

        // Identity fields surface verbatim.
        CHECK(j.getString("backendName") == "TPG");
        CHECK(j.getString("name")         == "tpg-1");
        CHECK(j.getInt("localId")         == 7);

        // Role flags appear as a string array.
        REQUIRE(j.valueIsArray("roles"));
        JsonArray roles = j.getArray("roles");
        CHECK(roles.size() == 1);
        CHECK(roles.getString(0) == "source");

        // Format lists are emitted as readable summary strings (one-way).
        REQUIRE(j.valueIsArray("producibleFormats"));
        CHECK(j.getArray("producibleFormats").size() == 2);
        const String preferred = j.getString("preferredFormat");
        const bool hasRaster = preferred.contains("1920");
        const bool hasPixel  = preferred.contains("RGBA");
        CHECK((hasRaster || hasPixel));

        // Capabilities surface only when non-default.
        CHECK(j.getBool("canSeek"));
        CHECK(j.getString("frameCount") == FrameCount::infinity().toString());
        CHECK(!j.getString("frameRate").isEmpty());
}

TEST_CASE("MediaIODescription_JsonElidesDefaults") {
        MediaIODescription d;
        // Only set a backend name; everything else is at default.  The
        // JSON must omit empty / default fields so consumers can rely
        // on absence == default.
        d.setBackendName("Empty");
        JsonObject j = d.toJson();

        CHECK(j.getString("backendName") == "Empty");
        CHECK_FALSE(j.contains("name"));
        CHECK_FALSE(j.contains("uuid"));
        CHECK_FALSE(j.contains("localId"));
        CHECK_FALSE(j.contains("roles"));
        CHECK_FALSE(j.contains("producibleFormats"));
        CHECK_FALSE(j.contains("acceptableFormats"));
        CHECK_FALSE(j.contains("preferredFormat"));
        CHECK_FALSE(j.contains("canSeek"));
        CHECK_FALSE(j.contains("frameCount"));
        CHECK_FALSE(j.contains("frameRate"));
        CHECK_FALSE(j.contains("containerMetadata"));
        CHECK_FALSE(j.contains("probeStatusCode"));
        CHECK_FALSE(j.contains("probeMessage"));
}

TEST_CASE("MediaIODescription_JsonScalarRoundTrip") {
        MediaIODescription orig = makeSample();
        JsonObject j = orig.toJson();

        Error err;
        MediaIODescription round = MediaIODescription::fromJson(j, &err);
        CHECK(err.isOk());

        // Scalar / single-value fields round-trip lossless via JSON.
        CHECK(round.backendName()        == orig.backendName());
        CHECK(round.backendDescription() == orig.backendDescription());
        CHECK(round.name()               == orig.name());
        CHECK(round.uuid()               == orig.uuid());
        CHECK(round.localId()            == orig.localId());
        CHECK(round.canBeSource()        == orig.canBeSource());
        CHECK(round.canBeSink()          == orig.canBeSink());
        CHECK(round.canBeTransform()     == orig.canBeTransform());
        CHECK(round.canSeek()            == orig.canSeek());
        CHECK(round.frameCount()         == orig.frameCount());
        CHECK(round.frameRate()          == orig.frameRate());
        CHECK(round.containerMetadata()  == orig.containerMetadata());
        CHECK(round.probeStatus()        == orig.probeStatus());
        CHECK(round.probeMessage()       == orig.probeMessage());

        // Format lists are intentionally one-way through JSON; the
        // canonical lossless path is the DataStream operators.  After
        // a JSON round-trip the format lists are empty even though
        // the JSON itself preserves their summary strings for human
        // / tooling display.
        CHECK(round.producibleFormats().isEmpty());
        CHECK(round.acceptableFormats().isEmpty());
        CHECK_FALSE(round.preferredFormat().isValid());
}

TEST_CASE("MediaIODescription_FrameCountSentinels") {
        MediaIODescription d;

        d.setFrameCount(FrameCount::unknown());
        // Unknown is the default — JSON should omit it.
        CHECK_FALSE(d.toJson().contains("frameCount"));

        d.setFrameCount(FrameCount::infinity());
        CHECK(d.toJson().getString("frameCount") == FrameCount::infinity().toString());

        d.setFrameCount(FrameCount(42));
        CHECK(d.toJson().getString("frameCount") == String("42f"));
}

TEST_CASE("MediaIODescription_ProbeFailure") {
        MediaIODescription d;
        d.setBackendName("ImageFile");
        d.setProbeStatus(Error::NotExist);
        d.setProbeMessage("file not found");

        // Summary surfaces the probe diagnostic.
        const StringList lines = d.summary();
        bool found = false;
        for(size_t i = 0; i < lines.size(); ++i) {
                if(lines[i].contains("Probe:") && lines[i].contains("NotExist")) {
                        CHECK(lines[i].contains("file not found"));
                        found = true;
                }
        }
        CHECK(found);

        // JSON round-trip preserves the probe code and message.
        JsonObject j = d.toJson();
        Error err;
        MediaIODescription round = MediaIODescription::fromJson(j, &err);
        CHECK(err.isOk());
        CHECK(round.probeStatus() == Error::NotExist);
        CHECK(round.probeMessage() == "file not found");
}
