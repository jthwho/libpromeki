/**
 * @file      mediaplay/stage.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "stage.h"

#include <cstdio>
#include <cstdlib>

#include <promeki/imagefilemediaio.h>
#include <promeki/audiocodec.h>
#include <promeki/mediaiofactory.h>
#include <promeki/enums.h>
#include <promeki/pixelformat.h>
#include <promeki/set.h>
#include <promeki/size2d.h>
#include <promeki/videocodec.h>
#include <promeki/textstream.h>
#include <promeki/variantspec.h>

using namespace promeki;

namespace mediaplay {

        const char *const kStageSdl = "SDL";
        const char *const kStageFile = "__file__";

        // --------------------------------------------------------------------------
        // SDL pseudo-backend schema
        // --------------------------------------------------------------------------

        // SDL player config IDs live on @ref MediaConfig (SdlTimingSource /
        // SdlWindowSize / SdlWindowTitle).  The CLI surfaces the unprefixed
        // string aliases below in --help text, but the actual MediaConfig
        // keys carry the Sdl prefix to avoid colliding with non-SDL options.

        MediaIO::Config::SpecMap sdlConfigSpecs() {
                MediaIO::Config::SpecMap specs;
                auto                     s = [&specs](MediaConfig::ID id, const Variant &def) {
                        const VariantSpec *gs = MediaConfig::spec(id);
                        specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                };
                s(MediaConfig::SdlTimingSource, String("audio"));
                s(MediaConfig::SdlWindowSize, Size2Du32(1280, 720));
                s(MediaConfig::SdlWindowTitle, String("mediaplay"));
                return specs;
        }

        MediaIO::Config sdlDefaultConfig() {
                MediaIO::Config cfg;
                cfg.setValidation(SpecValidation::None);
                MediaIO::Config::SpecMap specs = sdlConfigSpecs();
                for (auto it = specs.cbegin(); it != specs.cend(); ++it) {
                        const Variant &def = it->second.defaultValue();
                        if (def.isValid()) cfg.set(it->first, def);
                }
                cfg.setValidation(SpecValidation::Warn);
                cfg.set(MediaConfig::Type, String(kStageSdl));
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
                for (const MediaIOFactory *desc : MediaIOFactory::registeredFactories()) {
                        if (desc == nullptr) continue;
                        String caps;
                        if (desc->canBeSink()) caps += "I";
                        if (desc->canBeSource()) caps += "O";
                        if (desc->canBeTransform() && caps.isEmpty()) caps = "IO";
                        fprintf(stdout, "  %-16s [%-3s]  %s\n", desc->name().cstr(), caps.cstr(),
                                desc->description().cstr());
                }
                // SDL is a mediaplay-local pseudo-backend (see the note in
                // stage.h) — it isn't in MediaIOFactory::registeredFactories()
                // but it accepts the same -d / --dc CLI surface, so list it
                // here too to keep the --help picture honest.
                fprintf(stdout, "  %-16s [%-3s]  %s\n", kStageSdl, "I", sdlDescription());
                std::exit(0);
        }

        void listEnumTypeAndExit(const String &keyLabel, Enum::Type type, bool isEnumList) {
                // The kind tag mirrors the VariantSpec::typeName() output so a
                // user who dumps both `Key:list` and `Key:help` sees consistent
                // labels — "Enum Foo" for single-value keys and "EnumList Foo"
                // for per-channel / per-slot list keys.  The bottom-of-block
                // usage hint spells out how an EnumList accepts its value so
                // callers don't have to go hunt for the syntax.
                const char *kind = isEnumList ? "EnumList" : "Enum";
                fprintf(stdout, "%s (%s %s):\n", keyLabel.cstr(), kind, type.name().cstr());
                Enum::ValueList vals = Enum::values(type);
                for (size_t i = 0; i < vals.size(); i++) {
                        fprintf(stdout, "  %-24s (%d)\n", vals[i].first().cstr(), vals[i].second());
                }
                if (isEnumList) {
                        fprintf(stdout,
                                "\nPass a comma-separated list of names to set "
                                "multiple slots, e.g.\n  %s:%s,%s\n",
                                keyLabel.cstr(), vals.isEmpty() ? "" : vals[0].first().cstr(),
                                vals.size() > 1 ? vals[1].first().cstr()
                                                : (vals.isEmpty() ? "" : vals[0].first().cstr()));
                }
                std::exit(0);
        }

        void listPixelFormatsAndExit(const String &keyLabel) {
                fprintf(stdout, "%s (PixelFormat):\n", keyLabel.cstr());
                PixelFormat::IDList ids = PixelFormat::registeredIDs();
                for (size_t i = 0; i < ids.size(); i++) {
                        PixelFormat pd(ids[i]);
                        fprintf(stdout, "  %-32s %s\n", pd.name().cstr(), pd.desc().cstr());
                }
                std::exit(0);
        }

        void listVideoCodecsAndExit(const String &keyLabel) {
                fprintf(stdout, "%s (VideoCodec):\n", keyLabel.cstr());
                VideoCodec::IDList ids = VideoCodec::registeredIDs();
                for (size_t i = 0; i < ids.size(); i++) {
                        VideoCodec vc(ids[i]);
                        String     caps;
                        if (vc.canEncode()) caps += "E";
                        if (vc.canDecode()) caps += "D";
                        if (caps.isEmpty()) caps = "-";
                        fprintf(stdout, "  %-16s [%-2s] %s\n", vc.name().cstr(), caps.cstr(), vc.description().cstr());
                }
                std::exit(0);
        }

        namespace {

                // Emit a single "kind\tcodec\tbackend\tenc\tdec" line.  The backend
                // handle's name() is looked up through its StringRegistry, so the
                // value round-trips through `VideoCodec::fromString("name:backend")`
                // in mediaplay's --cc VideoCodec:<name:backend> flow without any
                // case massaging.
                template <typename CodecT>
                void emitCodecBackendRow(const char *kind, const CodecT &codec, const typename CodecT::Backend &backend,
                                         bool canEnc, bool canDec) {
                        fprintf(stdout, "%s\t%s\t%s\t%s\t%s\n", kind, codec.name().cstr(), backend.name().cstr(),
                                canEnc ? "yes" : "no", canDec ? "yes" : "no");
                }

                // Walk one codec family's registry and emit one row per (codec,
                // backend) pair that has at least one direction (enc or dec)
                // registered.  Backends with no encoder AND no decoder for the codec
                // are omitted — by construction every emitted row's combined flags
                // are at least "yes no" or "no yes".
                template <typename CodecT> void printCodecFamilyTsv(const char *kind) {
                        auto ids = CodecT::registeredIDs();
                        for (size_t i = 0; i < ids.size(); ++i) {
                                CodecT codec(ids[i]);
                                if (!codec.isValid()) continue;

                                auto encBackends = codec.availableEncoderBackends();
                                auto decBackends = codec.availableDecoderBackends();

                                // Union of encoder and decoder backend sets, in a
                                // stable order: encoder list first (preserving its
                                // registration order), then decoder-only backends
                                // appended afterwards.  A Set<uint64_t> of IDs tracks
                                // which backends we've already emitted so the
                                // decoder pass can skip duplicates without needing
                                // hash ordering on Backend itself.
                                Set<uint64_t> seen;
                                for (size_t j = 0; j < encBackends.size(); ++j) {
                                        const auto &b = encBackends[j];
                                        seen.insert(b.id());
                                        bool canEnc = true;
                                        bool canDec = false;
                                        for (size_t k = 0; k < decBackends.size(); ++k) {
                                                if (decBackends[k] == b) {
                                                        canDec = true;
                                                        break;
                                                }
                                        }
                                        emitCodecBackendRow(kind, codec, b, canEnc, canDec);
                                }
                                for (size_t j = 0; j < decBackends.size(); ++j) {
                                        const auto &b = decBackends[j];
                                        if (!seen.insert(b.id()).second()) continue;
                                        emitCodecBackendRow(kind, codec, b, false, true);
                                }
                        }
                }

        } // namespace

        void printCodecsTsv(CodecTsvKind kind) {
                if (kind == CodecTsvKind::Video || kind == CodecTsvKind::All) {
                        printCodecFamilyTsv<VideoCodec>("video");
                }
                if (kind == CodecTsvKind::Audio || kind == CodecTsvKind::All) {
                        printCodecFamilyTsv<AudioCodec>("audio");
                }
        }

        void listAudioCodecsAndExit(const String &keyLabel) {
                fprintf(stdout, "%s (AudioCodec):\n", keyLabel.cstr());
                AudioCodec::IDList ids = AudioCodec::registeredIDs();
                for (size_t i = 0; i < ids.size(); i++) {
                        AudioCodec ac(ids[i]);
                        String     caps;
                        if (ac.canEncode()) caps += "E";
                        if (ac.canDecode()) caps += "D";
                        if (caps.isEmpty()) caps = "-";
                        fprintf(stdout, "  %-16s [%-2s] %s\n", ac.name().cstr(), caps.cstr(), ac.description().cstr());
                }
                std::exit(0);
        }

        bool splitKeyValue(const String &arg, String &key, String &val) {
                size_t colon = arg.find(':');
                if (colon == String::npos) return false;
                key = arg.left(colon);
                val = arg.mid(colon + 1);
                return !key.isEmpty();
        }

        Error applyStageConfig(StageSpec &stage, const String &stageLabel) {
                if (stage.rawKeyValues.isEmpty()) return Error::Ok;

                // Load the config specs for this backend.
                MediaIO::Config::SpecMap specs;
                if (stage.type == kStageSdl) {
                        specs = sdlConfigSpecs();
                } else if (stage.type != kStageFile) {
                        specs = MediaIOFactory::configSpecs(stage.type);
                }

                for (const auto &kv : stage.rawKeyValues) {
                        String key, val;
                        if (!splitKeyValue(kv, key, val)) {
                                fprintf(stderr, "Error: %s '%s' is not a Key:Value pair\n", stageLabel.cstr(),
                                        kv.cstr());
                                return Error::InvalidArgument;
                        }
                        MediaIO::ConfigID id(key);
                        String            label = stageLabel + "." + key;

                        // Look up the spec: backend-specific first, then global.
                        const VariantSpec *spec = nullptr;
                        VariantSpec        specStorage;
                        auto               it = specs.find(id);
                        if (it != specs.end()) {
                                spec = &it->second;
                        } else {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                if (gs) {
                                        specStorage = *gs;
                                        spec = &specStorage;
                                }
                        }

                        // Handle sentinels.
                        if (val == "list") {
                                if (spec && spec->hasEnumType()) {
                                        const bool isEnumList = spec->acceptsType(Variant::TypeEnumList);
                                        listEnumTypeAndExit(label, spec->enumType(), isEnumList);
                                }
                                if (spec && spec->acceptsType(Variant::TypePixelFormat)) {
                                        listPixelFormatsAndExit(label);
                                }
                                if (spec && spec->acceptsType(Variant::TypeVideoCodec)) {
                                        listVideoCodecsAndExit(label);
                                }
                                if (spec && spec->acceptsType(Variant::TypeAudioCodec)) {
                                        listAudioCodecsAndExit(label);
                                }
                                fprintf(stderr,
                                        "Error: %s: 'list' is only supported for Enum, "
                                        "EnumList, PixelFormat, VideoCodec, and AudioCodec keys\n",
                                        label.cstr());
                                return Error::NotSupported;
                        }
                        if (val == "help") {
                                if (spec) {
                                        // Compose the same three-column layout
                                        // VariantDatabase::writeSpecMapHelp uses —
                                        // padding isn't meaningful for a single
                                        // entry, so just emit the fields inline.
                                        TextStream ts(stdout);
                                        ts << "  " << key << "  " << spec->detailsString();
                                        if (!spec->description().isEmpty()) {
                                                ts << "  " << spec->description();
                                        }
                                        ts << endl;
                                } else {
                                        fprintf(stdout, "%s: no spec registered for this key\n", key.cstr());
                                }
                                std::exit(0);
                        }

                        // Parse the value using the spec.
                        if (spec) {
                                Error   pe;
                                Variant value = spec->parseString(val, &pe);
                                if (pe.isError()) {
                                        fprintf(stderr, "Error: %s = '%s': failed to parse as %s\n", label.cstr(),
                                                val.cstr(), spec->typeName().cstr());
                                        return pe;
                                }
                                stage.config.set(id, std::move(value));
                        } else {
                                // No spec — store as a raw string so the strict
                                // validator below can report a single unified
                                // "unknown key" error.  Parsing as String also
                                // keeps the config-dump output on --help honest
                                // when someone forwards a tolerant build.
                                stage.config.set(id, Variant(val));
                        }
                }

                // Strict key validation: any config key still missing a spec
                // after the per-key loop is a user typo — fail the open rather
                // than let it reach a backend that will silently ignore the
                // key.  Uses the same spec set as the per-key loop (backend
                // specs for MediaIO backends, sdlConfigSpecs() for SDL); both
                // fall back to the global MediaConfig registry via
                // VariantDatabase::unknownKeys.
                StringList unknown = stage.config.unknownKeys(specs);
                if (!unknown.isEmpty()) {
                        for (size_t i = 0; i < unknown.size(); ++i) {
                                fprintf(stderr, "Error: %s: unknown config key '%s' for backend '%s'\n",
                                        stageLabel.cstr(), unknown[i].cstr(), stage.type.cstr());
                        }
                        return Error::InvalidArgument;
                }
                return Error::Ok;
        }

        Error applyStageMetadata(StageSpec &stage, const String &stageLabel) {
                if (stage.rawMetaKeyValues.isEmpty()) return Error::Ok;

                for (const auto &kv : stage.rawMetaKeyValues) {
                        String key, val;
                        if (!splitKeyValue(kv, key, val)) {
                                fprintf(stderr, "Error: %s '%s' is not a Key:Value pair\n", stageLabel.cstr(),
                                        kv.cstr());
                                return Error::InvalidArgument;
                        }
                        Metadata::ID id(key);
                        String       label = stageLabel + "." + key;

                        // Look up the global spec for this metadata key.
                        const VariantSpec *spec = Metadata::spec(id);

                        if (spec) {
                                Error   pe;
                                Variant value = spec->parseString(val, &pe);
                                if (pe.isError()) {
                                        fprintf(stderr, "Error: %s = '%s': failed to parse as %s\n", label.cstr(),
                                                val.cstr(), spec->typeName().cstr());
                                        return pe;
                                }
                                stage.metadata.set(id, std::move(value));
                        } else {
                                // No spec — accept as string.
                                stage.metadata.set(id, Variant(val));
                        }
                }
                return Error::Ok;
        }

        Error classifyStageArg(const String &arg, StageSpec &stage) {
                if (arg == kStageSdl) {
                        stage.type = kStageSdl;
                        return Error::Ok;
                }
                // Registered backend?
                if (MediaIOFactory::findByName(arg) != nullptr) {
                        stage.type = arg;
                        return Error::Ok;
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
                MediaIO  *io = nullptr;
                // See buildFileSink for the rationale behind resolvedType:
                // for file paths we have to read the real backend name back
                // from the live config after auto-detection has run, so that
                // applyStageConfig / applyStageMetadata load the right schema.
                String resolvedType;
                if (working.type == kStageFile) {
                        io = MediaIO::createForFileRead(working.path);
                        if (io == nullptr) {
                                fprintf(stderr, "Error: no readable MediaIO backend for '%s'\n", working.path.cstr());
                                return nullptr;
                        }
                        resolvedType = io->config().getAs<String>(MediaConfig::Type);
                } else {
                        MediaIO::Config cfg = MediaIOFactory::defaultConfig(working.type);
                        cfg.set(MediaConfig::Type, working.type);
                        working.config = cfg;
                        io = MediaIO::create(working.config);
                        if (io == nullptr) {
                                fprintf(stderr, "Error: failed to create input backend '%s'\n", working.type.cstr());
                                return nullptr;
                        }
                        resolvedType = working.type;
                }
                MediaIO::Config cfg = io->config();
                StageSpec       applyStage;
                applyStage.type = resolvedType.isEmpty() ? working.type : resolvedType;
                applyStage.rawKeyValues = working.rawKeyValues;
                applyStage.rawMetaKeyValues = working.rawMetaKeyValues;
                applyStage.config = cfg;
                Error ae = applyStageConfig(applyStage, String("--sc[") + working.type + "]");
                if (ae.isError()) {
                        delete io;
                        return nullptr;
                }
                io->setConfig(applyStage.config);
                Error me = applyStageMetadata(applyStage, String("--sm[") + working.type + "]");
                if (me.isError()) {
                        delete io;
                        return nullptr;
                }
                if (!applyStage.metadata.isEmpty()) {
                        io->setPendingMetadata(applyStage.metadata);
                }
                return io;
        }

        MediaIO *buildIntermediateStage(const StageSpec &spec) {
                const String &typeName = spec.type;
                if (typeName.isEmpty()) {
                        fprintf(stderr, "Error: intermediate stage has no backend name\n");
                        return nullptr;
                }
                MediaIO::Config cfg = MediaIOFactory::defaultConfig(typeName);
                cfg.set(MediaConfig::Type, typeName);
                StageSpec applyStage;
                applyStage.type = typeName;
                applyStage.rawKeyValues = spec.rawKeyValues;
                applyStage.rawMetaKeyValues = spec.rawMetaKeyValues;
                applyStage.config = cfg;
                Error ae = applyStageConfig(applyStage, String("--cc[") + typeName + "]");
                if (ae.isError()) return nullptr;
                MediaIO *io = MediaIO::create(applyStage.config);
                if (io == nullptr) {
                        fprintf(stderr, "Error: failed to create '%s' stage\n", typeName.cstr());
                        return nullptr;
                }
                Error me = applyStageMetadata(applyStage, String("--cm[") + typeName + "]");
                if (me.isError()) {
                        delete io;
                        return nullptr;
                }
                if (!applyStage.metadata.isEmpty()) {
                        io->setPendingMetadata(applyStage.metadata);
                }
                return io;
        }

        MediaIO *buildFileSink(const StageSpec &spec, const MediaDesc &srcDesc, const AudioDesc &srcAudioDesc,
                               const Metadata &srcMetadata, String &labelOut) {
                MediaIO *io = nullptr;
                // Once io is valid, resolvedType holds the real backend name
                // the config points at.  For file paths we read it back from
                // io->config() after createForFileWrite has auto-detected the
                // backend; for explicit backend names it's just spec.type.
                // applyStageConfig / applyStageMetadata need the resolved
                // name so they find the right defaultConfig / defaultMetadata
                // schema, not the synthetic __file__ marker.
                String resolvedType;
                if (spec.type == kStageFile) {
                        io = MediaIO::createForFileWrite(spec.path);
                        if (io == nullptr) {
                                fprintf(stderr, "Error: no writable MediaIO backend for '%s'\n", spec.path.cstr());
                                return nullptr;
                        }
                        labelOut = String("file:") + spec.path;
                        resolvedType = io->config().getAs<String>(MediaConfig::Type);
                } else {
                        MediaIO::Config cfg = MediaIOFactory::defaultConfig(spec.type);
                        cfg.set(MediaConfig::Type, spec.type);
                        io = MediaIO::create(cfg);
                        if (io == nullptr) {
                                fprintf(stderr, "Error: failed to create output backend '%s'\n", spec.type.cstr());
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
                if (srcDesc.frameRate().isValid()) {
                        cfg.set(MediaConfig::FrameRate, srcDesc.frameRate());
                }
                StageSpec applyStage;
                applyStage.type = resolvedType.isEmpty() ? spec.type : resolvedType;
                applyStage.rawKeyValues = spec.rawKeyValues;
                applyStage.rawMetaKeyValues = spec.rawMetaKeyValues;
                applyStage.config = cfg;
                Error ae = applyStageConfig(applyStage, String("--dc[") + labelOut + "]");
                if (ae.isError()) {
                        delete io;
                        return nullptr;
                }
                io->setConfig(applyStage.config);

                io->setPendingMediaDesc(srcDesc);
                if (srcAudioDesc.isValid()) io->setPendingAudioDesc(srcAudioDesc);

                // Metadata precedence: start from the upstream's metadata so
                // the sink inherits everything the source produced, then
                // overlay any --dm user overrides on top.
                Metadata merged = srcMetadata;
                Error    me = applyStageMetadata(applyStage, String("--dm[") + labelOut + "]");
                if (me.isError()) {
                        delete io;
                        return nullptr;
                }
                if (!applyStage.metadata.isEmpty()) {
                        merged.merge(applyStage.metadata);
                }
                if (!merged.isEmpty()) io->setPendingMetadata(merged);
                return io;
        }

        // --------------------------------------------------------------------------
        // CLI → MediaPipelineConfig::Stage resolver
        // --------------------------------------------------------------------------

        Error resolveStagePlan(const StageSpec &rawSpec, MediaPipelineConfig::StageRole role, const String &stageName,
                               const String &scopeLabel, MediaPipelineConfig::Stage &out) {
                StageSpec working = rawSpec;

                // Seed the config with the backend default so CLI overrides
                // land on top of a fully-populated spec set.  For file paths
                // we don't know the target backend until open() time; for
                // SDL we use the local pseudo-backend schema; otherwise we
                // ask the registry.
                if (working.type == kStageFile) {
                        // No backend defaults available — applyStageConfig
                        // falls back to the global MediaConfig registry for
                        // spec lookup.
                } else if (working.type == kStageSdl) {
                        working.config = sdlDefaultConfig();
                } else {
                        MediaIO::Config cfg = MediaIOFactory::defaultConfig(working.type);
                        cfg.set(MediaConfig::Type, working.type);
                        working.config = cfg;
                }

                Error ae = applyStageConfig(working, scopeLabel);
                if (ae.isError()) return ae;
                Error me = applyStageMetadata(working, scopeLabel);
                if (me.isError()) return me;

                // Fill the library-facing stage spec.  File stages carry
                // @c type="" so MediaPipeline resolves the real backend via
                // createForFileRead / createForFileWrite.  SDL stages keep
                // @c type="SDL" so the caller can spot them during
                // injectStage().
                out.name = stageName;
                out.type = (working.type == kStageFile) ? String() : working.type;
                out.path = working.path;
                out.role = role;
                // Sinks need OpenMode::Write in the config so file-based
                // backends (ImageFile, AudioFile, QuickTime, ...) open for
                // writing.  Source mode is the registered default, so
                // there's no need to set it explicitly for Source / Transform
                // stages.
                if (role == MediaPipelineConfig::StageRole::Sink) {
                        working.config.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
                }
                out.config = working.config;
                out.metadata = working.metadata;
                return Error::Ok;
        }

} // namespace mediaplay
