/**
 * @file      mediaplay/stage.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "stage.h"

#include <cstdio>
#include <cstdlib>

#include <promeki/color.h>
#include <promeki/colormodel.h>
#include <promeki/config.h>
#include <promeki/datetime.h>
#include <promeki/framerate.h>
#include <promeki/mediaiotask_imagefile.h>
#include <promeki/pixeldesc.h>
#include <promeki/pixelformat.h>
#include <promeki/rational.h>
#include <promeki/size2d.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/socketaddress.h>
#endif
#include <promeki/timecode.h>

using namespace promeki;

namespace mediaplay {

const char *const kStageSdl  = "SDL";
const char *const kStageFile = "__file__";

// --------------------------------------------------------------------------
// SDL pseudo-backend schema
// --------------------------------------------------------------------------

// SDL player config IDs live on @ref MediaConfig (SdlPaced /
// SdlAudioEnabled / SdlWindowSize / SdlWindowTitle).  The CLI surfaces
// the unprefixed string aliases below in --help text, but the actual
// MediaConfig keys carry the Sdl prefix to avoid colliding with
// non-SDL audio/paced/window options.

MediaIO::Config sdlDefaultConfig() {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, String(kStageSdl));
        // Paced: when true the player uses audio-led pacing (audio
        // clock drives video presentation) with a wall-clock fallback
        // for video-only streams.  When false the player runs as
        // fast as possible with audio dropped entirely.
        cfg.set(MediaConfig::SdlPaced, true);
        // Audio: when true the player opens an SDL audio device and
        // plays the stream's first audio track.  When false the audio
        // device is never opened.  Implicitly false when Paced=false.
        cfg.set(MediaConfig::SdlAudioEnabled, true);
        // WindowSize / WindowTitle control the SDLWindow that mediaplay
        // allocates before adopting the SDLPlayerTask.
        cfg.set(MediaConfig::SdlWindowSize,  Size2Du32(1280, 720));
        cfg.set(MediaConfig::SdlWindowTitle, String("mediaplay"));
        return cfg;
}

Metadata sdlDefaultMetadata() {
        // The SDL player doesn't consume container-level metadata —
        // it just renders images and plays audio.  The schema is
        // intentionally empty so the --help dump says "(none)".
        return Metadata();
}

const char *sdlDescription() {
        return "SDL video + audio player (real-time display sink)";
}

// --------------------------------------------------------------------------
// Listing helpers
// --------------------------------------------------------------------------

void listMediaIOBackendsAndExit() {
        fprintf(stdout, "Registered MediaIO backends:\n");
        for(const auto &desc : MediaIO::registeredFormats()) {
                String caps;
                if(desc.canRead) caps += "R";
                if(desc.canWrite) caps += "W";
                if(desc.canReadWrite) caps += "RW";
                fprintf(stdout, "  %-16s [%-3s]  %s\n",
                        desc.name.cstr(), caps.cstr(), desc.description.cstr());
        }
        // SDL is a mediaplay-local pseudo-backend (see the note in
        // stage.h) — it isn't in MediaIO::registeredFormats() but it
        // accepts the same -o / --oc CLI surface, so list it here too
        // to keep the --help picture honest.
        fprintf(stdout, "  %-16s [%-3s]  %s\n",
                kStageSdl, "W", sdlDescription());
        std::exit(0);
}

void listEnumTypeAndExit(const String &keyLabel, Enum::Type type) {
        fprintf(stdout, "%s (Enum %s):\n", keyLabel.cstr(), type.name().cstr());
        Enum::ValueList vals = Enum::values(type);
        for(size_t i = 0; i < vals.size(); i++) {
                fprintf(stdout, "  %-24s (%d)\n",
                        vals[i].first().cstr(), vals[i].second());
        }
        std::exit(0);
}

void listPixelFormatsAndExit(const String &keyLabel) {
        fprintf(stdout, "%s (PixelDesc):\n", keyLabel.cstr());
        PixelDesc::IDList ids = PixelDesc::registeredIDs();
        for(size_t i = 0; i < ids.size(); i++) {
                PixelDesc pd(ids[i]);
                fprintf(stdout, "  %-32s %s\n",
                        pd.name().cstr(), pd.desc().cstr());
        }
        std::exit(0);
}

// --------------------------------------------------------------------------
// parseConfigValue — string → Variant matching the target type
// --------------------------------------------------------------------------

Variant parseConfigValue(const String &keyLabel,
                         const String &str,
                         const Variant &templateValue,
                         Error *err) {
        auto fail = [&](Error e, const char *detail) {
                if(err != nullptr) *err = e;
                fprintf(stderr, "Error: %s = '%s': %s\n",
                        keyLabel.cstr(), str.cstr(), detail);
                return Variant();
        };
        if(err != nullptr) *err = Error::Ok;

        // `list` → enumerate and exit for types that support it.
        if(str == "list") {
                switch(templateValue.type()) {
                        case Variant::TypeEnum: {
                                Enum existing = templateValue.get<Enum>();
                                listEnumTypeAndExit(keyLabel, existing.type());
                        }
                        case Variant::TypePixelDesc:
                                listPixelFormatsAndExit(keyLabel);
                        default:
                                return fail(Error::NotSupported,
                                            "'list' is only supported for Enum / PixelDesc keys");
                }
        }

        // Template may be invalid when the backend didn't declare the
        // key in its defaultConfig — accept as a raw string so
        // late-bound keys still work.
        if(!templateValue.isValid()) return Variant(str);

        // Wrap the raw string in a transient Variant so Variant::get<T>
        // can use its String->T conversion matrix.
        Variant src(str);
        Error ce;

        switch(templateValue.type()) {
                case Variant::TypeBool: {
                        String low = str.toLower();
                        if(low == "true" || low == "yes" || low == "1" || low == "on")
                                return Variant(true);
                        if(low == "false" || low == "no" || low == "0" || low == "off")
                                return Variant(false);
                        return fail(Error::Invalid, "expected true/false/yes/no/1/0");
                }
                case Variant::TypeU8:  { auto v = src.get<uint8_t >(&ce); if(ce.isError()) return fail(ce, "not a uint8");  return Variant(v); }
                case Variant::TypeS8:  { auto v = src.get<int8_t  >(&ce); if(ce.isError()) return fail(ce, "not an int8"); return Variant(v); }
                case Variant::TypeU16: { auto v = src.get<uint16_t>(&ce); if(ce.isError()) return fail(ce, "not a uint16"); return Variant(v); }
                case Variant::TypeS16: { auto v = src.get<int16_t >(&ce); if(ce.isError()) return fail(ce, "not an int16"); return Variant(v); }
                case Variant::TypeU32: { auto v = src.get<uint32_t>(&ce); if(ce.isError()) return fail(ce, "not a uint32"); return Variant(v); }
                case Variant::TypeS32: { auto v = src.get<int32_t >(&ce); if(ce.isError()) return fail(ce, "not an int32"); return Variant(v); }
                case Variant::TypeU64: { auto v = src.get<uint64_t>(&ce); if(ce.isError()) return fail(ce, "not a uint64"); return Variant(v); }
                case Variant::TypeS64: { auto v = src.get<int64_t >(&ce); if(ce.isError()) return fail(ce, "not an int64"); return Variant(v); }
                case Variant::TypeFloat:  { auto v = src.get<float >(&ce); if(ce.isError()) return fail(ce, "not a float");  return Variant(v); }
                case Variant::TypeDouble: { auto v = src.get<double>(&ce); if(ce.isError()) return fail(ce, "not a double"); return Variant(v); }
                case Variant::TypeString:
                        return Variant(str);
                case Variant::TypeSize2D: {
                        auto r = Size2Du32::fromString(str);
                        if(r.second().isError() || !r.first().isValid())
                                return fail(Error::Invalid, "expected WxH");
                        return Variant(r.first());
                }
                case Variant::TypeFrameRate: {
                        auto r = FrameRate::fromString(str);
                        if(r.second().isError() || !r.first().isValid())
                                return fail(Error::Invalid, "invalid frame rate");
                        return Variant(r.first());
                }
                case Variant::TypeRational: {
                        // Rational has no fromString; parse "N/D" by hand.
                        size_t slash = str.find('/');
                        if(slash == String::npos)
                                return fail(Error::Invalid, "expected N/D");
                        Error ne, de;
                        int n = str.left(slash).to<int>(&ne);
                        int d = str.mid(slash + 1).to<int>(&de);
                        if(ne.isError() || de.isError() || d == 0)
                                return fail(Error::Invalid, "expected N/D with non-zero D");
                        return Variant(Rational<int>(n, d));
                }
                case Variant::TypeTimecode: {
                        auto r = Timecode::fromString(str);
                        if(r.second().isError())
                                return fail(Error::Invalid, "invalid timecode");
                        return Variant(r.first());
                }
                case Variant::TypeDateTime: {
                        Error de;
                        DateTime dt = DateTime::fromString(str, DateTime::DefaultFormat, &de);
                        if(de.isError())
                                return fail(Error::Invalid, "invalid datetime");
                        return Variant(dt);
                }
                case Variant::TypeColor: {
                        Color c = Color::fromString(str);
                        if(!c.isValid())
                                return fail(Error::Invalid, "invalid color");
                        return Variant(c);
                }
                case Variant::TypePixelDesc: {
                        PixelDesc pd = PixelDesc::lookup(str);
                        if(!pd.isValid())
                                return fail(Error::Invalid, "unknown PixelDesc name");
                        return Variant(pd);
                }
                case Variant::TypePixelFormat: {
                        PixelFormat pf = PixelFormat::lookup(str);
                        if(!pf.isValid())
                                return fail(Error::Invalid, "unknown PixelFormat name");
                        return Variant(pf);
                }
                case Variant::TypeColorModel: {
                        ColorModel cm = ColorModel::lookup(str);
                        if(!cm.isValid())
                                return fail(Error::Invalid, "unknown ColorModel name");
                        return Variant(cm);
                }
                case Variant::TypeEnum: {
                        Enum existing = templateValue.get<Enum>();
                        Enum::Type t = existing.type();
                        Enum e(t, str);
                        if(!e.hasListedValue()) {
                                // Fall back to "TypeName::ValueName" fully
                                // qualified form so users can copy --help
                                // lines verbatim.
                                Error lookErr;
                                Enum fq = Enum::lookup(str, &lookErr);
                                if(lookErr.isOk() && fq.type() == t && fq.hasListedValue())
                                        return Variant(fq);
                                return fail(Error::Invalid, "unknown enum value");
                        }
                        return Variant(e);
                }
                case Variant::TypeStringList: {
                        return Variant(str.split(","));
                }
#if PROMEKI_ENABLE_NETWORK
                case Variant::TypeSocketAddress: {
                        auto r = SocketAddress::fromString(str);
                        if(r.second().isError() || r.first().isNull())
                                return fail(Error::Invalid, "expected host:port");
                        return Variant(r.first());
                }
#endif
                default:
                        break;
        }

        return fail(Error::NotSupported,
                    "config key uses a Variant type mediaplay doesn't know how to parse");
}

bool splitKeyValue(const String &arg, String &key, String &val) {
        size_t colon = arg.find(':');
        if(colon == String::npos) return false;
        key = arg.left(colon);
        val = arg.mid(colon + 1);
        return !key.isEmpty();
}

Error applyStageConfig(StageSpec &stage, const String &stageLabel) {
        if(stage.rawKeyValues.isEmpty()) return Error::Ok;

        // Load the type schema.  Registered backends come from the
        // MediaIO registry; the SDL pseudo-backend has its schema
        // hard-coded in sdlDefaultConfig(); the synthetic __file__
        // marker has no schema at classify time, though buildFileSink
        // later re-applies config against the resolved backend name.
        MediaIO::Config schema;
        if(stage.type == kStageSdl) {
                schema = sdlDefaultConfig();
        } else if(stage.type != kStageFile) {
                schema = MediaIO::defaultConfig(stage.type);
        }

        for(const auto &kv : stage.rawKeyValues) {
                String key, val;
                if(!splitKeyValue(kv, key, val)) {
                        fprintf(stderr,
                                "Error: %s '%s' is not a Key:Value pair\n",
                                stageLabel.cstr(), kv.cstr());
                        return Error::InvalidArgument;
                }
                MediaIO::ConfigID id(key);
                Variant templateValue = schema.get(id);  // invalid if absent
                Error pe;
                String label = stageLabel + "." + key;
                Variant value = parseConfigValue(label, val, templateValue, &pe);
                if(pe.isError()) return pe;
                stage.config.set(id, std::move(value));
        }
        return Error::Ok;
}

Error applyStageMetadata(StageSpec &stage, const String &stageLabel) {
        if(stage.rawMetaKeyValues.isEmpty()) return Error::Ok;

        // Load the metadata schema — same lookup rules as
        // applyStageConfig above.
        Metadata schema;
        if(stage.type == kStageSdl) {
                schema = sdlDefaultMetadata();
        } else if(stage.type != kStageFile) {
                schema = MediaIO::defaultMetadata(stage.type);
        }

        for(const auto &kv : stage.rawMetaKeyValues) {
                String key, val;
                if(!splitKeyValue(kv, key, val)) {
                        fprintf(stderr,
                                "Error: %s '%s' is not a Key:Value pair\n",
                                stageLabel.cstr(), kv.cstr());
                        return Error::InvalidArgument;
                }
                Metadata::ID id(key);
                Variant templateValue = schema.get(id);  // invalid if absent
                Error pe;
                String label = stageLabel + "." + key;
                Variant value = parseConfigValue(label, val, templateValue, &pe);
                if(pe.isError()) return pe;
                stage.metadata.set(id, std::move(value));
        }
        return Error::Ok;
}

Error classifyStageArg(const String &arg, StageSpec &stage) {
        if(arg == kStageSdl) {
                stage.type = kStageSdl;
                return Error::Ok;
        }
        // Registered backend?
        for(const auto &desc : MediaIO::registeredFormats()) {
                if(desc.name == arg) {
                        stage.type = arg;
                        return Error::Ok;
                }
        }
        // Fall back to "filesystem path".
        stage.type = kStageFile;
        stage.path = arg;
        return Error::Ok;
}

// --------------------------------------------------------------------------
// Stage builders
// --------------------------------------------------------------------------

MediaIO *buildSource(const StageSpec &spec) {
        StageSpec working = spec;
        MediaIO *io = nullptr;
        // See buildFileSink for the rationale behind resolvedType:
        // for file paths we have to read the real backend name back
        // from the live config after auto-detection has run, so that
        // applyStageConfig / applyStageMetadata load the right schema.
        String resolvedType;
        if(working.type == kStageFile) {
                io = MediaIO::createForFileRead(working.path);
                if(io == nullptr) {
                        fprintf(stderr, "Error: no readable MediaIO backend for '%s'\n",
                                working.path.cstr());
                        return nullptr;
                }
                resolvedType = io->config().getAs<String>(MediaConfig::Type);
        } else {
                MediaIO::Config cfg = MediaIO::defaultConfig(working.type);
                cfg.set(MediaConfig::Type, working.type);
                working.config = cfg;
                io = MediaIO::create(working.config);
                if(io == nullptr) {
                        fprintf(stderr, "Error: failed to create input backend '%s'\n",
                                working.type.cstr());
                        return nullptr;
                }
                resolvedType = working.type;
        }
        MediaIO::Config cfg = io->config();
        StageSpec applyStage;
        applyStage.type             = resolvedType.isEmpty() ? working.type : resolvedType;
        applyStage.rawKeyValues     = working.rawKeyValues;
        applyStage.rawMetaKeyValues = working.rawMetaKeyValues;
        applyStage.config           = cfg;
        Error ae = applyStageConfig(applyStage, String("--ic[") + working.type + "]");
        if(ae.isError()) {
                delete io;
                return nullptr;
        }
        io->setConfig(applyStage.config);
        Error me = applyStageMetadata(applyStage, String("--im[") + working.type + "]");
        if(me.isError()) {
                delete io;
                return nullptr;
        }
        if(!applyStage.metadata.isEmpty()) {
                io->setMetadata(applyStage.metadata);
        }
        return io;
}

MediaIO *buildConverter(const StageSpec &spec) {
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        cfg.set(MediaConfig::Type, String("Converter"));
        StageSpec applyStage;
        applyStage.type             = "Converter";
        applyStage.rawKeyValues     = spec.rawKeyValues;
        applyStage.rawMetaKeyValues = spec.rawMetaKeyValues;
        applyStage.config           = cfg;
        Error ae = applyStageConfig(applyStage, String("--cc"));
        if(ae.isError()) return nullptr;
        MediaIO *io = MediaIO::create(applyStage.config);
        if(io == nullptr) {
                fprintf(stderr, "Error: failed to create Converter stage\n");
                return nullptr;
        }
        Error me = applyStageMetadata(applyStage, String("--cm"));
        if(me.isError()) {
                delete io;
                return nullptr;
        }
        if(!applyStage.metadata.isEmpty()) {
                io->setMetadata(applyStage.metadata);
        }
        return io;
}

MediaIO *buildFileSink(const StageSpec &spec,
                       const MediaDesc &srcDesc,
                       const AudioDesc &srcAudioDesc,
                       const Metadata &srcMetadata,
                       String &labelOut) {
        MediaIO *io = nullptr;
        // Once io is valid, resolvedType holds the real backend name
        // the config points at.  For file paths we read it back from
        // io->config() after createForFileWrite has auto-detected the
        // backend; for explicit backend names it's just spec.type.
        // applyStageConfig / applyStageMetadata need the resolved
        // name so they find the right defaultConfig / defaultMetadata
        // schema, not the synthetic __file__ marker.
        String resolvedType;
        if(spec.type == kStageFile) {
                io = MediaIO::createForFileWrite(spec.path);
                if(io == nullptr) {
                        fprintf(stderr,
                                "Error: no writable MediaIO backend for '%s'\n",
                                spec.path.cstr());
                        return nullptr;
                }
                labelOut = String("file:") + spec.path;
                resolvedType = io->config().getAs<String>(MediaConfig::Type);
        } else {
                MediaIO::Config cfg = MediaIO::defaultConfig(spec.type);
                cfg.set(MediaConfig::Type, spec.type);
                io = MediaIO::create(cfg);
                if(io == nullptr) {
                        fprintf(stderr, "Error: failed to create output backend '%s'\n",
                                spec.type.cstr());
                        return nullptr;
                }
                labelOut = spec.type;
                resolvedType = spec.type;
        }

        MediaIO::Config cfg = io->config();
        // Stamp the effective frame rate into the config — the
        // AudioFile backend strictly reads ConfigFrameRate from the
        // config, while ImageFile is happy with pendingMediaDesc.
        // The "FrameRate" ConfigID string is shared across backends
        // via the registry.
        if(srcDesc.frameRate().isValid()) {
                cfg.set(MediaConfig::FrameRate, srcDesc.frameRate());
        }
        StageSpec applyStage;
        applyStage.type             = resolvedType.isEmpty() ? spec.type : resolvedType;
        applyStage.rawKeyValues     = spec.rawKeyValues;
        applyStage.rawMetaKeyValues = spec.rawMetaKeyValues;
        applyStage.config           = cfg;
        Error ae = applyStageConfig(applyStage, String("--oc[") + labelOut + "]");
        if(ae.isError()) {
                delete io;
                return nullptr;
        }
        io->setConfig(applyStage.config);

        io->setMediaDesc(srcDesc);
        if(srcAudioDesc.isValid()) io->setAudioDesc(srcAudioDesc);

        // Metadata precedence: start from the upstream's metadata so
        // the sink inherits everything the source produced, then
        // overlay any --om user overrides on top.
        Metadata merged = srcMetadata;
        Error me = applyStageMetadata(applyStage, String("--om[") + labelOut + "]");
        if(me.isError()) {
                delete io;
                return nullptr;
        }
        if(!applyStage.metadata.isEmpty()) {
                merged.merge(applyStage.metadata);
        }
        if(!merged.isEmpty()) io->setMetadata(merged);
        return io;
}

} // namespace mediaplay
