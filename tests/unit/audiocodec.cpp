/**
 * @file      tests/audiocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <doctest/doctest.h>
#include <promeki/audiocodec.h>

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

TEST_CASE("AudioCodec: PCM variants are first-class and distinct") {
        AudioCodec s16le(AudioCodec::PCMI_S16LE);
        AudioCodec s16be(AudioCodec::PCMI_S16BE);
        AudioCodec f32le(AudioCodec::PCMI_Float32LE);
        CHECK(s16le != s16be);
        CHECK(s16le != f32le);
        CHECK(s16le.name() == "PCMI_S16LE");
        CHECK(s16be.name() == "PCMI_S16BE");
        CHECK(f32le.name() == "PCMI_Float32LE");
}

TEST_CASE("AudioCodec: lookup by name returns the registered entry") {
        CHECK(AudioCodec::lookup("AAC")           == AudioCodec(AudioCodec::AAC));
        CHECK(AudioCodec::lookup("Opus")          == AudioCodec(AudioCodec::Opus));
        CHECK(AudioCodec::lookup("PCMI_S24LE")    == AudioCodec(AudioCodec::PCMI_S24LE));
        CHECK_FALSE(AudioCodec::lookup("not-a-real-codec").isValid());
}

TEST_CASE("AudioCodec: registeredIDs() enumerates every well-known codec") {
        auto ids = AudioCodec::registeredIDs();
        CHECK(ids.contains(AudioCodec::AAC));
        CHECK(ids.contains(AudioCodec::Opus));
        CHECK(ids.contains(AudioCodec::FLAC));
        CHECK(ids.contains(AudioCodec::PCMI_S16LE));
        for(auto id : ids) CHECK(id != AudioCodec::Invalid);
}

TEST_CASE("AudioCodec: user-registered codecs flow through register/lookup") {
        AudioCodec::ID myId = AudioCodec::registerType();
        AudioCodec::Data d;
        d.id   = myId;
        d.name = "test-custom-audio-codec";
        d.desc = "Custom audio codec registered from a unit test";
        AudioCodec::registerData(std::move(d));

        AudioCodec by_id(myId);
        CHECK(by_id.isValid());
        CHECK(by_id.name() == "test-custom-audio-codec");

        AudioCodec by_name = AudioCodec::lookup("test-custom-audio-codec");
        CHECK(by_name == by_id);
}
