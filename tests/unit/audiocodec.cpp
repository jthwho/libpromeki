/**
 * @file      tests/audiocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <doctest/doctest.h>
#include <promeki/audiocodec.h>
#include <promeki/audioformat.h>

using namespace promeki;

TEST_CASE("AudioCodec: well-known codecs resolve by ID") {
        AudioCodec aac(AudioCodec::AAC);
        CHECK(aac.isValid());
        CHECK(aac.name() == "AAC");
        CHECK_FALSE(aac.description().isEmpty());

        AudioCodec opus(AudioCodec::Opus);
        CHECK(opus.isValid());
        CHECK(opus.name() == "Opus");
        CHECK(opus != aac);
}

TEST_CASE("AudioCodec: lookup by name returns the registered entry") {
        CHECK(value(AudioCodec::lookup("AAC"))  == AudioCodec(AudioCodec::AAC));
        CHECK(value(AudioCodec::lookup("Opus")) == AudioCodec(AudioCodec::Opus));
        CHECK(value(AudioCodec::lookup("FLAC")) == AudioCodec(AudioCodec::FLAC));
        CHECK(value(AudioCodec::lookup("MP3"))  == AudioCodec(AudioCodec::MP3));
        CHECK(value(AudioCodec::lookup("AC3"))  == AudioCodec(AudioCodec::AC3));
        CHECK(error(AudioCodec::lookup("not-a-real-codec")).isError());
}

TEST_CASE("AudioCodec: registeredIDs() enumerates every well-known codec") {
        auto ids = AudioCodec::registeredIDs();
        CHECK(ids.contains(AudioCodec::AAC));
        CHECK(ids.contains(AudioCodec::Opus));
        CHECK(ids.contains(AudioCodec::FLAC));
        CHECK(ids.contains(AudioCodec::MP3));
        CHECK(ids.contains(AudioCodec::AC3));
        for(auto id : ids) CHECK(id != AudioCodec::Invalid);
}

TEST_CASE("AudioCodec: does not claim any PCM sample formats") {
        // PCM is represented by AudioFormat, not AudioCodec.  Every
        // registered AudioCodec should be a real (compressed) codec.
        // This test catches regressions where someone tries to add PCM
        // entries back into the codec registry.
        auto ids = AudioCodec::registeredIDs();
        for(auto id : ids) {
                AudioCodec c(id);
                CAPTURE(c.name());
                CHECK_FALSE(c.name().startsWith("PCMI_"));
                CHECK_FALSE(c.name().startsWith("PCMP_"));
        }
}

TEST_CASE("AudioCodec: Opus carries libopus's documented format restrictions") {
        AudioCodec opus(AudioCodec::Opus);
        REQUIRE(opus.isValid());

        const auto &rates = opus.supportedSampleRates();
        CHECK(rates.contains(8000.0f));
        CHECK(rates.contains(12000.0f));
        CHECK(rates.contains(16000.0f));
        CHECK(rates.contains(24000.0f));
        CHECK(rates.contains(48000.0f));
        // libopus does not accept e.g. 44.1 kHz.
        CHECK_FALSE(rates.contains(44100.0f));

        const auto &chans = opus.supportedChannelCounts();
        CHECK(chans.contains(1));
        CHECK(chans.contains(2));
        CHECK_FALSE(chans.contains(8));

        // Opus accepts both 16-bit signed and 32-bit float input PCM.
        const auto fmts = opus.supportedSampleFormats();
        CHECK(fmts.contains(AudioFormat(AudioFormat::PCMI_S16LE)));
        CHECK(fmts.contains(AudioFormat(AudioFormat::PCMI_Float32LE)));
}

TEST_CASE("AudioCodec: empty supported-formats lists mean 'no constraint'") {
        // AAC's metadata lists no format restrictions yet — the
        // matching backend (when one lands) should re-register richer
        // Data to advertise what it actually supports.
        AudioCodec aac(AudioCodec::AAC);
        REQUIRE(aac.isValid());
        CHECK(aac.supportedSampleFormats().isEmpty());
        CHECK(aac.supportedSampleRates().isEmpty());
        CHECK(aac.supportedChannelCounts().isEmpty());
}

TEST_CASE("AudioCodec: user-registered codecs flow through register/lookup") {
        AudioCodec::ID myId = AudioCodec::registerType();
        AudioCodec::Data d;
        d.id   = myId;
        d.name = "TestCustomAudioCodec";
        d.desc = "Custom audio codec registered from a unit test";
        d.supportedSampleFormats = { static_cast<int>(AudioFormat::PCMI_S24LE) };
        d.supportedSampleRates   = { 96000.0f };
        d.supportedChannelCounts = { 6 };
        AudioCodec::registerData(std::move(d));

        AudioCodec by_id(myId);
        CHECK(by_id.isValid());
        CHECK(by_id.name() == "TestCustomAudioCodec");
        const auto fmts = by_id.supportedSampleFormats();
        REQUIRE(fmts.size() == 1);
        CHECK(fmts.front() == AudioFormat(AudioFormat::PCMI_S24LE));
        CHECK(by_id.supportedSampleRates().contains(96000.0f));
        CHECK(by_id.supportedChannelCounts().contains(6));

        AudioCodec by_name = value(AudioCodec::lookup("TestCustomAudioCodec"));
        CHECK(by_name == by_id);
}

// ---------------------------------------------------------------------------
// Default-constructed wrapper / Invalid ID
// ---------------------------------------------------------------------------

TEST_CASE("AudioCodec: default ctor is invalid") {
        AudioCodec c;
        CHECK_FALSE(c.isValid());
        // Methods that touch the underlying Data are still safe — they
        // resolve to the Invalid record, but the codec reports itself
        // as not valid so callers know to skip them.
        CHECK_FALSE(c.canEncode());
        CHECK_FALSE(c.canDecode());
        CHECK(c.availableEncoderBackends().isEmpty());
        CHECK(c.availableDecoderBackends().isEmpty());
        CHECK(c.encoderSupportedInputs().isEmpty());
        CHECK(c.decoderSupportedOutputs().isEmpty());
        CHECK(c.supportedSampleFormats().isEmpty());
        CHECK(c.toString().isEmpty());
}

TEST_CASE("AudioCodec: equality compares both Data pointer and pinned backend") {
        AudioCodec opusUnpinned(AudioCodec::Opus);
        AudioCodec opusUnpinned2(AudioCodec::Opus);
        CHECK(opusUnpinned == opusUnpinned2);

        // Pin the Opus codec to a fresh backend so the inequality
        // branch fires regardless of whether opusaudiocodec.cpp is
        // built in (the test only needs the backend handle to differ).
        auto bk = AudioCodec::registerBackend("AudioCodecTest_Backend1");
        REQUIRE(isOk(bk));
        AudioCodec opusPinned(AudioCodec::Opus, value(bk));
        CHECK(opusPinned != opusUnpinned);
        CHECK(opusPinned.id() == opusUnpinned.id());
}

// ---------------------------------------------------------------------------
// Backend registry — registerBackend / lookupBackend
// ---------------------------------------------------------------------------

TEST_CASE("AudioCodec::registerBackend rejects names that aren't C identifiers") {
        auto bad = AudioCodec::registerBackend("contains-hyphen");
        CHECK(error(bad).isError());
        CHECK(error(bad) == Error::Invalid);

        auto empty = AudioCodec::registerBackend("");
        CHECK(error(empty) == Error::Invalid);

        auto digit = AudioCodec::registerBackend("9starts");
        CHECK(error(digit) == Error::Invalid);
}

TEST_CASE("AudioCodec::registerBackend is idempotent") {
        auto a = AudioCodec::registerBackend("AudioCodecTest_RegRepeat");
        auto b = AudioCodec::registerBackend("AudioCodecTest_RegRepeat");
        REQUIRE(isOk(a));
        REQUIRE(isOk(b));
        CHECK(value(a) == value(b));
}

TEST_CASE("AudioCodec::lookupBackend returns IdNotFound when the name isn't registered") {
        auto res = AudioCodec::lookupBackend("AudioCodecTest_NeverRegistered");
        CHECK(error(res).isError());
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("AudioCodec::lookupBackend rejects invalid identifiers") {
        auto res = AudioCodec::lookupBackend("contains spaces");
        CHECK(error(res) == Error::Invalid);
}

TEST_CASE("AudioCodec::lookupBackend resolves what registerBackend wrote") {
        auto reg = AudioCodec::registerBackend("AudioCodecTest_LookupRoundTrip");
        REQUIRE(isOk(reg));
        auto look = AudioCodec::lookupBackend("AudioCodecTest_LookupRoundTrip");
        REQUIRE(isOk(look));
        CHECK(value(reg) == value(look));
}

// ---------------------------------------------------------------------------
// fromString / toString
// ---------------------------------------------------------------------------

TEST_CASE("AudioCodec::fromString: codec-only form returns an unpinned wrapper") {
        auto res = AudioCodec::fromString("Opus");
        REQUIRE(isOk(res));
        AudioCodec c = value(res);
        CHECK(c.id() == AudioCodec::Opus);
        CHECK_FALSE(c.backend().isValid());
}

TEST_CASE("AudioCodec::fromString: codec:backend form pins the backend") {
        // Register a sentinel backend so fromString has something to
        // resolve regardless of which codec backends are linked in.
        auto bk = AudioCodec::registerBackend("AudioCodecTest_FromStringBackend");
        REQUIRE(isOk(bk));

        auto res = AudioCodec::fromString("Opus:AudioCodecTest_FromStringBackend");
        REQUIRE(isOk(res));
        AudioCodec c = value(res);
        CHECK(c.id() == AudioCodec::Opus);
        CHECK(c.backend() == value(bk));
        CHECK(c.toString() == "Opus:AudioCodecTest_FromStringBackend");
}

TEST_CASE("AudioCodec::fromString: empty input is Error::Invalid") {
        auto res = AudioCodec::fromString("");
        CHECK(error(res) == Error::Invalid);
}

TEST_CASE("AudioCodec::fromString: too many colons is Error::Invalid") {
        auto res = AudioCodec::fromString("Opus:A:B");
        CHECK(error(res) == Error::Invalid);
}

TEST_CASE("AudioCodec::fromString: unknown codec is Error::IdNotFound") {
        auto res = AudioCodec::fromString("DefinitelyNotARealCodec");
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("AudioCodec::fromString: known codec + unknown backend is Error::IdNotFound") {
        auto res = AudioCodec::fromString("Opus:NeverRegisteredBackend");
        CHECK(error(res).isError());
}

TEST_CASE("AudioCodec::toString: invalid codec returns an empty string") {
        AudioCodec c;
        CHECK(c.toString().isEmpty());
}

TEST_CASE("AudioCodec::toString: unpinned codec returns its name") {
        AudioCodec c(AudioCodec::Opus);
        CHECK(c.toString() == "Opus");
}

TEST_CASE("AudioCodec::toString: pinned codec returns Name:Backend") {
        auto bk = AudioCodec::registerBackend("AudioCodecTest_ToStringBackend");
        REQUIRE(isOk(bk));
        AudioCodec c(AudioCodec::Opus, value(bk));
        CHECK(c.toString() == "Opus:AudioCodecTest_ToStringBackend");
}

// ---------------------------------------------------------------------------
// registerData — name validation
// ---------------------------------------------------------------------------

TEST_CASE("AudioCodec::registerData drops malformed names") {
        AudioCodec::ID myId = AudioCodec::registerType();
        AudioCodec::Data d;
        d.id   = myId;
        d.name = "name-with-hyphens";  // not a C identifier
        d.desc = "Should be rejected";
        AudioCodec::registerData(std::move(d));

        // The malformed name must NOT be reachable through lookup —
        // registerData prints a warning and drops it.
        auto res = AudioCodec::lookup("name-with-hyphens");
        CHECK(error(res).isError());
}
