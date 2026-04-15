/**
 * @file      audiocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/audiocodec.h>
#include <promeki/atomic.h>
#include <promeki/map.h>

PROMEKI_NAMESPACE_BEGIN

static Atomic<int> _nextType{AudioCodec::UserDefined};

AudioCodec::ID AudioCodec::registerType() {
        return static_cast<ID>(_nextType.fetchAndAdd(1));
}

// ---------------------------------------------------------------------------
// Well-known codec Data records
// ---------------------------------------------------------------------------

static AudioCodec::Data makeInvalid() {
        AudioCodec::Data d;
        d.id   = AudioCodec::Invalid;
        d.name = "Invalid";
        d.desc = "Invalid or uninitialised audio codec";
        return d;
}

static AudioCodec::Data makePcm(AudioCodec::ID id, const char *name,
                                const char *desc) {
        AudioCodec::Data d;
        d.id   = id;
        d.name = name;
        d.desc = desc;
        return d;
}

static AudioCodec::Data makeAAC() {
        AudioCodec::Data d;
        d.id   = AudioCodec::AAC;
        d.name = "AAC";
        d.desc = "Advanced Audio Coding (ISO/IEC 14496-3)";
        d.fourccList = { "mp4a", "aac " };
        return d;
}

static AudioCodec::Data makeOpus() {
        AudioCodec::Data d;
        d.id   = AudioCodec::Opus;
        d.name = "Opus";
        d.desc = "Opus (RFC 6716)";
        d.fourccList = { "Opus", "opus" };
        return d;
}

static AudioCodec::Data makeFLAC() {
        AudioCodec::Data d;
        d.id   = AudioCodec::FLAC;
        d.name = "FLAC";
        d.desc = "FLAC — Free Lossless Audio Codec";
        d.fourccList = { "fLaC", "flac" };
        return d;
}

static AudioCodec::Data makeMP3() {
        AudioCodec::Data d;
        d.id   = AudioCodec::MP3;
        d.name = "MP3";
        d.desc = "MPEG-1 Audio Layer III";
        d.fourccList = { "mp3 ", ".mp3" };
        return d;
}

static AudioCodec::Data makeAC3() {
        AudioCodec::Data d;
        d.id   = AudioCodec::AC3;
        d.name = "AC3";
        d.desc = "Dolby Digital (AC-3)";
        d.fourccList = { "ac-3", "AC-3" };
        return d;
}

// ---------------------------------------------------------------------------
// Construct-on-first-use registry.  Uniquely named to avoid linker-
// level collisions with sibling TypeRegistry helpers.
// ---------------------------------------------------------------------------

struct AudioCodecRegistry {
        Map<AudioCodec::ID, AudioCodec::Data> entries;
        Map<String, AudioCodec::ID>           nameMap;

        AudioCodecRegistry() {
                add(makeInvalid());
                add(makePcm(AudioCodec::PCMI_S16LE,     "PCMI_S16LE",
                            "Interleaved PCM signed 16-bit little-endian"));
                add(makePcm(AudioCodec::PCMI_S16BE,     "PCMI_S16BE",
                            "Interleaved PCM signed 16-bit big-endian"));
                add(makePcm(AudioCodec::PCMI_S24LE,     "PCMI_S24LE",
                            "Interleaved PCM signed 24-bit little-endian"));
                add(makePcm(AudioCodec::PCMI_S24BE,     "PCMI_S24BE",
                            "Interleaved PCM signed 24-bit big-endian"));
                add(makePcm(AudioCodec::PCMI_S32LE,     "PCMI_S32LE",
                            "Interleaved PCM signed 32-bit little-endian"));
                add(makePcm(AudioCodec::PCMI_S32BE,     "PCMI_S32BE",
                            "Interleaved PCM signed 32-bit big-endian"));
                add(makePcm(AudioCodec::PCMI_Float32LE, "PCMI_Float32LE",
                            "Interleaved PCM 32-bit IEEE 754 float little-endian"));
                add(makePcm(AudioCodec::PCMI_Float32BE, "PCMI_Float32BE",
                            "Interleaved PCM 32-bit IEEE 754 float big-endian"));
                add(makeAAC());
                add(makeOpus());
                add(makeFLAC());
                add(makeMP3());
                add(makeAC3());
        }

        void add(AudioCodec::Data d) {
                AudioCodec::ID id = d.id;
                if(id != AudioCodec::Invalid) nameMap[d.name] = id;
                entries[id] = std::move(d);
        }
};

static AudioCodecRegistry &registry() {
        static AudioCodecRegistry reg;
        return reg;
}

// ---------------------------------------------------------------------------
// Static methods
// ---------------------------------------------------------------------------

const AudioCodec::Data *AudioCodec::lookupData(ID id) {
        auto &reg = registry();
        auto it = reg.entries.find(id);
        if(it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

void AudioCodec::registerData(Data &&data) {
        auto &reg = registry();
        if(data.id != Invalid) reg.nameMap[data.name] = data.id;
        reg.entries[data.id] = std::move(data);
}

AudioCodec AudioCodec::lookup(const String &name) {
        auto &reg = registry();
        auto it = reg.nameMap.find(name);
        return (it != reg.nameMap.end()) ? AudioCodec(it->second)
                                         : AudioCodec(Invalid);
}

AudioCodec::IDList AudioCodec::registeredIDs() {
        auto &reg = registry();
        IDList ret;
        for(const auto &[id, data] : reg.entries) {
                if(id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
