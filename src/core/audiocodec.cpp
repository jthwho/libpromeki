/**
 * @file      audiocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/audiocodec.h>
#include <promeki/audioformat.h>
#include <promeki/atomic.h>
#include <promeki/enums.h>
#include <promeki/map.h>
#include <promeki/stringlist.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered codecs
// ---------------------------------------------------------------------------

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

// Common rate-control list for modern lossy codecs that support all three.
static List<RateControlMode> commonLossyRateControls() {
        return {
                RateControlMode::CBR,
                RateControlMode::VBR,
                RateControlMode::ABR,
        };
}

static AudioCodec::Data makeAAC() {
        AudioCodec::Data d;
        d.id                 = AudioCodec::AAC;
        d.name               = "AAC";
        d.desc               = "Advanced Audio Coding (ISO/IEC 14496-3)";
        d.fourccList         = { "mp4a", "aac " };
        d.compressionType    = AudioCodec::CompressionLossy;
        d.rateControlModes   = commonLossyRateControls();
        // AAC packets depend on prior-packet state at the bit-reservoir
        // level for low-delay profiles; otherwise every AU is
        // independently decodable.  Tag as Inter for safety.
        d.packetIndependence = AudioCodec::PacketIndependenceInter;
        d.isStreamable       = true;   // AAC-LATM / HLS friendly
        return d;
}

static AudioCodec::Data makeOpus() {
        AudioCodec::Data d;
        d.id                 = AudioCodec::Opus;
        d.name               = "Opus";
        d.desc               = "Opus (RFC 6716)";
        d.fourccList         = { "Opus", "opus" };
        d.compressionType    = AudioCodec::CompressionLossy;
        d.rateControlModes   = { RateControlMode::CBR, RateControlMode::VBR };
        d.packetIndependence = AudioCodec::PacketIndependenceEvery;
        d.isStreamable       = true;
        d.hasBuiltInSilence  = true;   // DTX
        // libopus-accepted PCM rates (the Opus spec's internal resampler
        // accepts only these).  Backends can re-register tighter Data
        // under the same ID if they implement a subset.
        d.supportedSampleRates   = { 8000.0f, 12000.0f, 16000.0f,
                                     24000.0f, 48000.0f };
        d.supportedChannelCounts = { 1, 2 };
        d.maxChannels            = 255;  // Opus multistream extension
        d.supportedSampleFormats = {
                static_cast<int>(AudioFormat::PCMI_S16LE),
                static_cast<int>(AudioFormat::PCMI_S16BE),
                static_cast<int>(AudioFormat::PCMI_Float32LE),
                static_cast<int>(AudioFormat::PCMI_Float32BE),
        };
        // Opus permits these sample counts per packet at 48 kHz
        // (2.5 / 5 / 10 / 20 / 40 / 60 ms of audio).  At other input
        // rates the counts scale proportionally but the ms choices
        // are the same.
        d.frameSizeSamples = { 120, 240, 480, 960, 1920, 2880 };
        return d;
}

static AudioCodec::Data makeFLAC() {
        AudioCodec::Data d;
        d.id                 = AudioCodec::FLAC;
        d.name               = "FLAC";
        d.desc               = "FLAC — Free Lossless Audio Codec";
        d.fourccList         = { "fLaC", "flac" };
        d.compressionType    = AudioCodec::CompressionLossless;
        d.rateControlModes   = { RateControlMode::VBR };  // inherent
        d.packetIndependence = AudioCodec::PacketIndependenceEvery;  // FLAC frames are independent
        d.isStreamable       = true;
        return d;
}

static AudioCodec::Data makeMP3() {
        AudioCodec::Data d;
        d.id                 = AudioCodec::MP3;
        d.name               = "MP3";
        d.desc               = "MPEG-1 Audio Layer III";
        d.fourccList         = { "mp3 ", ".mp3" };
        d.compressionType    = AudioCodec::CompressionLossy;
        d.rateControlModes   = commonLossyRateControls();
        d.packetIndependence = AudioCodec::PacketIndependenceInter;  // bit reservoir
        d.isStreamable       = true;
        return d;
}

static AudioCodec::Data makeAC3() {
        AudioCodec::Data d;
        d.id                 = AudioCodec::AC3;
        d.name               = "AC3";
        d.desc               = "Dolby Digital (AC-3)";
        d.fourccList         = { "ac-3", "AC-3" };
        d.compressionType    = AudioCodec::CompressionLossy;
        d.rateControlModes   = { RateControlMode::CBR };
        d.packetIndependence = AudioCodec::PacketIndependenceEvery;
        d.isStreamable       = true;
        d.supportsDRC        = true;
        d.maxChannels        = 6;  // up to 5.1
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
        if(data.id != Invalid && !data.name.isIdentifier()) {
                promekiWarn("AudioCodec::registerData rejected name '%s' "
                            "(must be a valid C identifier)",
                            data.name.cstr());
                return;
        }
        auto &reg = registry();
        if(data.id != Invalid) reg.nameMap[data.name] = data.id;
        reg.entries[data.id] = std::move(data);
}

Result<AudioCodec> AudioCodec::lookup(const String &name) {
        auto &reg = registry();
        auto it = reg.nameMap.find(name);
        if(it == reg.nameMap.end()) return makeError<AudioCodec>(Error::IdNotFound);
        return makeResult(AudioCodec(it->second));
}

AudioCodec::IDList AudioCodec::registeredIDs() {
        auto &reg = registry();
        IDList ret;
        for(const auto &[id, data] : reg.entries) {
                if(id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

// ---------------------------------------------------------------------------
// Backend name/ID registry — typed handles via StringRegistry
// ---------------------------------------------------------------------------

Result<AudioCodec::Backend> AudioCodec::registerBackend(const String &name) {
        if(!name.isIdentifier()) {
                return makeError<Backend>(Error::Invalid);
        }
        uint64_t id = AudioCodecBackendRegistry::instance().findOrCreateProbe(name);
        return makeResult(Backend::fromId(id));
}

Result<AudioCodec::Backend> AudioCodec::lookupBackend(const String &name) {
        if(!name.isIdentifier()) {
                return makeError<Backend>(Error::Invalid);
        }
        uint64_t id = AudioCodecBackendRegistry::instance().findId(name);
        if(id == AudioCodecBackendRegistry::InvalidID) {
                return makeError<Backend>(Error::IdNotFound);
        }
        return makeResult(Backend::fromId(id));
}

// ---------------------------------------------------------------------------
// String form parsing / emission
// ---------------------------------------------------------------------------

Result<AudioCodec> AudioCodec::fromString(const String &spec) {
        if(spec.isEmpty()) return makeError<AudioCodec>(Error::Invalid);
        // Split on ':' — at most one colon, dividing codec-name from
        // backend-name.  Anything more exotic is malformed.
        StringList parts = spec.split(":");
        if(parts.size() > 2) {
                return makeError<AudioCodec>(Error::Invalid);
        }
        auto codecResult = lookup(parts[0]);
        if(error(codecResult).isError()) {
                return makeError<AudioCodec>(error(codecResult));
        }
        AudioCodec codec = value(codecResult);
        if(parts.size() == 1) return makeResult(codec);

        auto backendResult = lookupBackend(parts[1]);
        if(error(backendResult).isError()) {
                return makeError<AudioCodec>(error(backendResult));
        }
        return makeResult(AudioCodec(codec.id(), value(backendResult)));
}

String AudioCodec::toString() const {
        if(!isValid()) return String();
        if(_backend.isValid()) {
                String bn = _backend.name();
                if(!bn.isEmpty()) return name() + ":" + bn;
        }
        return name();
}

PROMEKI_NAMESPACE_END
