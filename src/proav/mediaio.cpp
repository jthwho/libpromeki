/**
 * @file      mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaioclock.h>
#include <promeki/mediaioport.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaioreadcache.h>
#include <promeki/file.h>
#include <promeki/logger.h>
#include <promeki/mediapayload.h>
#include <promeki/videopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/buffer.h>
#include <promeki/clockdomain.h>
#include <promeki/colormodel.h>
#include <promeki/enums.h>
#include <promeki/mediatimestamp.h>
#include <promeki/timestamp.h>
#include <promeki/duration.h>
#include <promeki/stringlist.h>
#include <promeki/units.h>
#include <promeki/url.h>
#include <promeki/variantspec.h>
#include <cstdint>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MediaIO)

namespace {

// Stamp synthetic pts / duration on payloads that came back from the
// backend with no native timing — used by the Read completion path so
// downstream consumers see a fully-stamped frame regardless of how
// careful the backend was.  Mirror copy of the helper in
// mediaiosink.cpp's write path.
void ensurePayloadTiming(Frame::Ptr &frame, const MediaTimeStamp &synMts, const FrameRate &frameRate) {
        if (!frame.isValid()) return;
        const Duration oneFrame = frameRate.isValid() ? frameRate.frameDuration() : Duration();
        for (size_t i = 0; i < frame->payloadList().size(); ++i) {
                const MediaPayload::Ptr &p = frame->payloadList()[i];
                if (!p.isValid()) continue;
                MediaPayload *mp = frame.modify()->payloadList()[i].modify();
                if (!mp->pts().isValid()) mp->setPts(synMts);
                if (mp->hasDuration() && mp->duration().isZero() && !oneFrame.isZero()) {
                        mp->setDuration(oneFrame);
                }
        }
}

} // namespace

Error MediaIO::applyQueryToConfig(const Url &url, const Config::SpecMap &specs, Config *outConfig) {
        if (outConfig == nullptr) return Error::Invalid;

        // Iterate the query map in its natural (key-sorted) order
        // so error messages are deterministic — tests and users
        // see failures on a stable, lowest-key-first basis.
        for (const auto &[name, rawValue] : url.query()) {
                // ID::find does NOT register a new name — we want
                // exactly the opposite of the PROMEKI_DECLARE_ID
                // path, so unknown names stay unknown and surface
                // as a caller error rather than silently creating a
                // new entry in the global registry.
                ConfigID id = ConfigID::find(name);
                if (!id.isValid()) {
                        promekiWarn("MediaIO::applyQueryToConfig: "
                                    "unknown query key '%s'",
                                    name.cstr());
                        return Error::InvalidArgument;
                }

                // Key must also be part of the backend's declared
                // spec map — a key that exists in the global
                // registry but not in the backend's specs (for
                // example, a key declared by a different backend)
                // is still a caller error, because this backend
                // makes no promise to honor it.
                auto it = specs.find(id);
                if (it == specs.cend()) {
                        promekiWarn("MediaIO::applyQueryToConfig: "
                                    "key '%s' is not part of this "
                                    "backend's spec map",
                                    name.cstr());
                        return Error::InvalidArgument;
                }
                const VariantSpec &spec = it->second;

                Error   parseErr = Error::Ok;
                Variant value = spec.parseString(rawValue, &parseErr);
                if (parseErr.isError()) {
                        promekiWarn("MediaIO::applyQueryToConfig: "
                                    "failed to parse '%s=%s' as expected type",
                                    name.cstr(), rawValue.cstr());
                        // Normalize to ConversionFailed regardless of
                        // which specific failure parseString raised
                        // (bool path emits Invalid, numeric paths
                        // emit ConversionFailed).  Callers of
                        // applyQueryToConfig care about *that the
                        // value could not be coerced* — the exact
                        // sub-error is already in the warning.
                        return Error::ConversionFailed;
                }

                // Validate explicitly (rather than relying on
                // VariantDatabase::set's internal check) so we can
                // distinguish OutOfRange from other failures in the
                // caller's error code.  VariantDatabase::set's
                // default Warn validation would otherwise store the
                // bad value and only emit a log line.
                Error validateErr = Error::Ok;
                if (!spec.validate(value, &validateErr)) {
                        promekiWarn("MediaIO::applyQueryToConfig: "
                                    "'%s=%s' failed spec validation",
                                    name.cstr(), rawValue.cstr());
                        return validateErr.isError() ? validateErr : Error::InvalidArgument;
                }

                outConfig->set(id, value);
        }
        return Error::Ok;
}

static String extractExtension(const String &filename) {
        size_t dot = filename.rfind('.');
        if (dot == String::npos || dot + 1 >= filename.size()) return String();
        return filename.mid(dot + 1).toLower();
}

// Internal lookup: extension → factory.  Used by createForFileRead /
// createForFileWrite as the second-pass filter after path-based
// probes.  Walks the MediaIOFactory registry.
static const MediaIOFactory *findFactoryByExtension(const String &filename) {
        return MediaIOFactory::findByExtension(extractExtension(filename));
}

// Internal lookup: file read.  Three-pass dispatch — path probe,
// extension match, content probe — over the MediaIOFactory registry.
// Mirrors the legacy findFormatForFileRead but routes through the
// new factory model.
static const MediaIOFactory *findFactoryForFileRead(const String &filename) {
        const List<MediaIOFactory *> &list = MediaIOFactory::registeredFactories();

        // Pass 0: path-based probe (device nodes like /dev/video0)
        for (const MediaIOFactory *f : list) {
                if (f == nullptr) continue;
                if (!f->canBeSource()) continue;
                if (f->canHandlePath(filename)) return f;
        }

        // Pass 1: extension match (fast path)
        String ext = extractExtension(filename);
        if (!ext.isEmpty()) {
                for (const MediaIOFactory *f : list) {
                        if (f == nullptr) continue;
                        if (!f->canBeSource()) continue;
                        for (const String &e : f->extensions()) {
                                if (ext == e.toLower()) return f;
                        }
                }
        }

        // Pass 2: content-based probe
        File probeFile(filename);
        if (probeFile.open(IODevice::ReadOnly).isError()) return nullptr;
        const MediaIOFactory *result = nullptr;
        for (const MediaIOFactory *f : list) {
                if (f == nullptr) continue;
                if (!f->canBeSource()) continue;
                probeFile.seek(0);
                if (f->canHandleDevice(&probeFile)) {
                        result = f;
                        break;
                }
        }
        probeFile.close();
        return result;
}

// ============================================================================
// Factory
// ============================================================================

MediaIO *MediaIO::create(const Config &config, ObjectBase *parent) {
        const MediaIOFactory *factory = nullptr;

        if (config.contains(MediaConfig::Type)) {
                String typeName = config.getAs<String>(MediaConfig::Type);
                factory = MediaIOFactory::findByName(typeName);
                if (factory == nullptr) {
                        promekiWarn("MediaIO::create: unknown type '%s'", typeName.cstr());
                        return nullptr;
                }
        }

        if (factory == nullptr && config.contains(MediaConfig::Filename)) {
                String filename = config.getAs<String>(MediaConfig::Filename);
                factory = findFactoryByExtension(filename);
                if (factory == nullptr) {
                        promekiWarn("MediaIO::create: no backend for '%s'", filename.cstr());
                        return nullptr;
                }
        }

        if (factory == nullptr) {
                promekiWarn("MediaIO::create: config has neither Type nor Filename");
                return nullptr;
        }

        // The factory owns construction.  For legacy backends this
        // routes through @ref LegacyFactory, which builds a
        // the backend through @ref MediaIOFactory::create and wraps
        // it in a @ref MediaIO.  Native (Phase-12+) factories
        // construct their @ref MediaIO subclass directly.
        MediaIO *io = factory->create(config, parent);
        if (io == nullptr) {
                promekiWarn("MediaIO::create: factory for '%s' returned null", factory->name().cstr());
        }
        return io;
}

// Lightweight "does this look like a URL we can dispatch?" test used by
// the file-oriented factories below.  A positive answer means the input
// parses with a non-empty scheme AND we have a backend registered for
// that scheme — so a path like "C:/foo" on Windows doesn't accidentally
// get routed through URL dispatch just because it parses.
static const MediaIOFactory *tryResolveAsUrl(const String &maybeUrl, Url *outUrl) {
        // Fast path: a string with no ':' can never be a URL.
        if (maybeUrl.find(':') == String::npos) return nullptr;
        Result<Url> parsed = Url::fromString(maybeUrl);
        if (parsed.second().isError() || !parsed.first().isValid()) return nullptr;
        Url                   url = parsed.first();
        const MediaIOFactory *factory = MediaIOFactory::findByScheme(url.scheme());
        if (factory == nullptr) return nullptr;
        if (outUrl != nullptr) *outUrl = url;
        return factory;
}

MediaIO *MediaIO::createFromUrl(const Url &url, ObjectBase *parent) {
        if (!url.isValid()) {
                promekiWarn("MediaIO::createFromUrl: invalid URL");
                return nullptr;
        }
        const MediaIOFactory *factory = MediaIOFactory::findByScheme(url.scheme());
        if (factory == nullptr) {
                promekiWarn("MediaIO::createFromUrl: no backend for scheme '%s'", url.scheme().cstr());
                return nullptr;
        }
        // Start from the backend's full default config so every spec
        // key has a value, then let the URL translator overwrite the
        // subset it cares about.  This matches the pattern used by
        // createForFileRead / createForFileWrite.
        Config cfg = MediaIOFactory::defaultConfig(factory->name());
        cfg.set(MediaConfig::Type, factory->name());
        // Seed the live config with the parsed URL so downstream
        // consumers (logs, --stats, introspection tools) can show
        // the user "what URL opened this MediaIO" without the
        // factory having to hang a parallel string field off the
        // Config.
        cfg.set(MediaConfig::Url, url);

        // The backend's callback owns the authority / path translation
        // — those bits are scheme-specific and cannot be generalized
        // (pmfb://<name> ≠ rtp://<addr>:<port> ≠ file:///<path>).
        Error err = factory->urlToConfig(url, &cfg);
        if (err == Error::NotSupported) {
                promekiWarn("MediaIO::createFromUrl: backend '%s' claims scheme '%s' "
                            "but provides no urlToConfig translator",
                            factory->name().cstr(), url.scheme().cstr());
                return nullptr;
        }
        if (err.isError()) {
                promekiWarn("MediaIO::createFromUrl: '%s' rejected URL '%s'", factory->name().cstr(),
                            url.toString().cstr());
                return nullptr;
        }

        // Query parameters are handled generically: their keys are
        // the canonical MediaConfig IDs and their values run through
        // the same VariantSpec::parseString coercion as JSON / CLI
        // entry.  Unknown keys, bad values, and out-of-range values
        // all fail the open — the open() error path is the right
        // place for URL-level bugs, not a silent default substitution.
        if (!url.query().isEmpty()) {
                Config::SpecMap specs = factory->configSpecs();
                if (!specs.isEmpty()) {
                        Error qerr = applyQueryToConfig(url, specs, &cfg);
                        if (qerr.isError()) {
                                promekiWarn("MediaIO::createFromUrl: '%s' query "
                                            "application failed for '%s' (%s)",
                                            factory->name().cstr(), url.toString().cstr(), qerr.name().cstr());
                                return nullptr;
                        }
                }
        }

        return factory->create(cfg, parent);
}

MediaIO *MediaIO::createFromUrl(const String &url, ObjectBase *parent) {
        Result<Url> parsed = Url::fromString(url);
        if (parsed.second().isError() || !parsed.first().isValid()) {
                promekiWarn("MediaIO::createFromUrl: failed to parse '%s'", url.cstr());
                return nullptr;
        }
        return createFromUrl(parsed.first(), parent);
}

MediaIO *MediaIO::createForFileRead(const String &filename, ObjectBase *parent) {
        // URL takeover: if the input is a recognized scheme, route
        // through the URL factory.  Callers that need the file-based
        // behavior for a path that happens to start with "<alpha>:"
        // still get it, because MediaIOFactory::findByScheme returns
        // nullptr for any scheme no backend claims.
        Url url;
        if (const MediaIOFactory *urlFactory = tryResolveAsUrl(filename, &url)) {
                if (!urlFactory->canBeSource()) {
                        promekiWarn("MediaIO::createForFileRead: backend '%s' "
                                    "claims scheme '%s' but cannot be a source",
                                    urlFactory->name().cstr(), url.scheme().cstr());
                        return nullptr;
                }
                return createFromUrl(url, parent);
        }

        const MediaIOFactory *factory = findFactoryForFileRead(filename);
        if (factory == nullptr) {
                promekiWarn("MediaIO::createForFileRead: no backend for '%s'", filename.cstr());
                return nullptr;
        }
        if (!factory->canBeSource()) {
                promekiWarn("MediaIO::createForFileRead: '%s' does not support reading", factory->name().cstr());
                return nullptr;
        }
        // Seed the live config with the resolved backend name + the
        // file the caller passed in, so downstream consumers that
        // need to know "which backend is this?" can read it back
        // from io->config() without a second registry walk.
        Config cfg = MediaIOFactory::defaultConfig(factory->name());
        cfg.set(MediaConfig::Type, factory->name());
        cfg.set(MediaConfig::Filename, filename);
        return factory->create(cfg, parent);
}

MediaIO *MediaIO::createForFileWrite(const String &filename, ObjectBase *parent) {
        // Mirror the URL takeover behavior from createForFileRead.
        Url url;
        if (const MediaIOFactory *urlFactory = tryResolveAsUrl(filename, &url)) {
                if (!urlFactory->canBeSink()) {
                        promekiWarn("MediaIO::createForFileWrite: backend '%s' "
                                    "claims scheme '%s' but cannot be a sink",
                                    urlFactory->name().cstr(), url.scheme().cstr());
                        return nullptr;
                }
                return createFromUrl(url, parent);
        }

        const MediaIOFactory *factory = findFactoryByExtension(filename);
        if (factory == nullptr) {
                promekiWarn("MediaIO::createForFileWrite: no backend for '%s'", filename.cstr());
                return nullptr;
        }
        if (!factory->canBeSink()) {
                promekiWarn("MediaIO::createForFileWrite: '%s' does not support writing", factory->name().cstr());
                return nullptr;
        }
        // Same rationale as createForFileRead: seed the live config
        // with the backend's full default schema plus the type and
        // filename so callers that read io->config() back out see a
        // complete, discoverable picture.
        Config cfg = MediaIOFactory::defaultConfig(factory->name());
        cfg.set(MediaConfig::Type, factory->name());
        cfg.set(MediaConfig::Filename, filename);
        return factory->create(cfg, parent);
}

// ============================================================================
// Backend-author static helpers
// ============================================================================

PixelFormat MediaIO::defaultUncompressedPixelFormat(const PixelFormat &source) {
        // Both fallbacks are registered in the PixelFormat well-known
        // table and carry paint engines, so the planner can splice a
        // cheap one-hop CSC between the source and us.  A YCbCr source
        // stays in the YUV family to minimise that CSC cost.
        const bool isYuv = source.isValid() && source.colorModel().type() == ColorModel::TypeYCbCr;
        return isYuv ? PixelFormat(PixelFormat::YUV8_422_Rec709) : PixelFormat(PixelFormat::RGBA8_sRGB);
}

MediaDesc MediaIO::applyOutputOverrides(const MediaDesc &input, const MediaConfig &config) {
        MediaDesc out = input;

        // ---- Video: OutputPixelFormat ----
        // A valid PixelFormat replaces the pixel format on every image
        // layer.  An invalid (default-constructed) PixelFormat means
        // "inherit from input" — leave the per-image pixelFormat alone.
        if (config.contains(MediaConfig::OutputPixelFormat)) {
                const PixelFormat target = config.getAs<PixelFormat>(MediaConfig::OutputPixelFormat);
                if (target.isValid()) {
                        ImageDesc::List &imgs = out.imageList();
                        for (size_t i = 0; i < imgs.size(); ++i) {
                                imgs[i].setPixelFormat(target);
                        }
                }
        }

        // ---- Video: OutputFrameRate ----
        if (config.contains(MediaConfig::OutputFrameRate)) {
                const FrameRate fr = config.getAs<FrameRate>(MediaConfig::OutputFrameRate);
                if (fr.isValid()) out.setFrameRate(fr);
        }

        // ---- Audio: OutputAudioRate (Hz) ----
        // Zero (default) means "inherit from input".
        if (config.contains(MediaConfig::OutputAudioRate)) {
                const float hz = config.getAs<float>(MediaConfig::OutputAudioRate);
                if (hz > 0.0f) {
                        AudioDesc::List &auds = out.audioList();
                        for (size_t i = 0; i < auds.size(); ++i) {
                                auds[i].setSampleRate(hz);
                        }
                }
        }

        // ---- Audio: OutputAudioChannels ----
        // Zero (default) means "inherit from input".
        if (config.contains(MediaConfig::OutputAudioChannels)) {
                const int ch = config.getAs<int>(MediaConfig::OutputAudioChannels);
                if (ch > 0) {
                        AudioDesc::List &auds = out.audioList();
                        for (size_t i = 0; i < auds.size(); ++i) {
                                auds[i].setChannels(static_cast<unsigned int>(ch));
                        }
                }
        }

        // ---- Audio: OutputAudioDataType ----
        // Invalid (default) means "inherit from input".  The key is
        // typed as a TypeEnum bound to AudioDataType::Type so the value
        // lives as an Enum and we project it back through the
        // AudioDataType wrapper to get the corresponding
        // AudioFormat::ID.
        if (config.contains(MediaConfig::OutputAudioDataType)) {
                Error enumErr;
                Enum  adtEnum = config.get(MediaConfig::OutputAudioDataType).asEnum(AudioDataType::Type, &enumErr);
                if (enumErr.isOk()) {
                        const auto dt = static_cast<AudioFormat::ID>(adtEnum.value());
                        if (dt != AudioFormat::Invalid) {
                                AudioDesc::List &auds = out.audioList();
                                for (size_t i = 0; i < auds.size(); ++i) {
                                        auds[i].setFormat(dt);
                                }
                        }
                }
        }

        return out;
}

// ============================================================================
// Lifecycle
// ============================================================================

MediaIO::MediaIO(ObjectBase *parent) : ObjectBase(parent) {}

int64_t MediaIO::frameByteSize(const Frame::Ptr &frame) {
        // Walks the frame once and sums every payload's plane bytes.
        // "Logical" is important: we use BufferView::size() (content)
        // rather than allocSize() (allocation) so that partially-
        // filled scratch buffers do not inflate the reported rate.
        // The loop skips invalid pointers defensively because some
        // backends build frames lazily.
        if (!frame.isValid()) return 0;
        int64_t total = 0;
        for (const MediaPayload::Ptr &p : frame->payloadList()) {
                if (!p.isValid()) continue;
                for (size_t i = 0; i < p->planeCount(); ++i) {
                        total += static_cast<int64_t>(p->plane(i).size());
                }
        }
        return total;
}

void MediaIO::populateStandardStats(MediaIOStats &stats) const {
        // Per-group accounting (Phase 5).  A backend tick advances
        // the whole group at once, so the rate tracker and drop /
        // repeat / late counters live on each MediaIOPortGroup.  The
        // standard keys roll up totals across every group so a single
        // top-level stats() call still summarizes the whole MediaIO.
        int64_t bytesPerSecond = 0;
        double  framesPerSecond = 0.0;
        int64_t framesDropped = 0;
        int64_t framesRepeated = 0;
        int64_t framesLate = 0;
        for (const MediaIOPortGroup *g : _portGroups) {
                if (g == nullptr) continue;
                bytesPerSecond += g->bytesPerSecond();
                framesPerSecond += g->framesPerSecond();
                framesDropped += g->framesDroppedTotal();
                framesRepeated += g->framesRepeatedTotal();
                framesLate += g->framesLateTotal();
        }
        stats.set(MediaIOStats::BytesPerSecond, bytesPerSecond);
        stats.set(MediaIOStats::FramesPerSecond, framesPerSecond);
        stats.set(MediaIOStats::FramesDropped, FrameCount(framesDropped));
        stats.set(MediaIOStats::FramesRepeated, FrameCount(framesRepeated));
        stats.set(MediaIOStats::FramesLate, FrameCount(framesLate));

        // Backlog depth from the strand itself.  Telemetry callers
        // (e.g. mediaplay --stats) surface this to let operators see
        // when I/O is falling behind without every backend having to
        // reimplement "how many operations are pending".
        //
        // Per-command latency / processing time are now reported on
        // each command's own MediaIOStats container via
        // QueueWaitDurationNs / ExecuteDurationNs (populated by
        // submit() around the dispatch hook) — accessed via
        // MediaIORequest::stats() rather than the instance-wide
        // aggregate.
        // PendingOperations rolls up the strategy's queue depth via
        // a non-virtual conditional — strategies that own a
        // backlog-tracking executor (Strand, dedicated worker queue)
        // override @ref MediaIO::isIdle and surface the depth there
        // for telemetry consumers.  The base reports zero; nothing in
        // the legacy path consumes a non-zero value yet.
        stats.set(MediaIOStats::PendingOperations, INT64_C(0));
}

// ============================================================================
// Multi-port accessors — ports are populated by the backend during open() in
// Phase 4 of the multi-port refactor; until then these always report empty.
// ============================================================================

MediaIOSource *MediaIO::source(int N) const {
        if (N < 0 || N >= static_cast<int>(_sources.size())) return nullptr;
        return _sources[N];
}

int MediaIO::sourceCount() const {
        return static_cast<int>(_sources.size());
}

MediaIOSink *MediaIO::sink(int N) const {
        if (N < 0 || N >= static_cast<int>(_sinks.size())) return nullptr;
        return _sinks[N];
}

int MediaIO::sinkCount() const {
        return static_cast<int>(_sinks.size());
}

MediaIOPortGroup *MediaIO::portGroup(int N) const {
        if (N < 0 || N >= static_cast<int>(_portGroups.size())) return nullptr;
        return _portGroups[N];
}

int MediaIO::portGroupCount() const {
        return static_cast<int>(_portGroups.size());
}

MediaIO::~MediaIO() {
        // The strategy subclass destructor (e.g. the strategy subclass)
        // is responsible for closing while its v-table still routes
        // @ref submit and its backend-side state is still alive.  By
        // the time we run @c submit is pure-virtual again — calling
        // @ref close from here would terminate via "pure virtual
        // function called".  Subclass destructors that handle close
        // leave @ref isOpen returning false; if a caller skipped
        // close entirely we accept the leak rather than crash.
}

void MediaIO::completeCommand(MediaIOCommand::Ptr cmd) {
        // Centralized cache-update + signal-emit + request-resolve
        // path.  Order is fixed by contract (devplan §Cache + result
        // contract): cache update → signal emission → request
        // resolution.  .then() callbacks therefore always observe
        // up-to-date cached state.
        MediaIOCommand *raw = cmd.modify();
        switch (raw->kind()) {
                case MediaIOCommand::Open: {
                        auto *co = static_cast<MediaIOCommandOpen *>(raw);
                        if (cmd->result.isOk()) {
                                _open.setValue(true);
                                _originTime = TimeStamp::now();
                                _defaultSeekMode = co->defaultSeekMode;

                                // Populate the MediaIO-level cached
                                // desc from the ports the backend
                                // created during executeCmd(Open).
                                // Sources publish what they produce;
                                // sinks publish what they accept; the
                                // first source wins, falling back to
                                // the first sink for sink-only
                                // MediaIOs.
                                const MediaIOPort *primary = nullptr;
                                if (!_sources.isEmpty()) primary = _sources[0];
                                else if (!_sinks.isEmpty()) primary = _sinks[0];
                                if (primary != nullptr) {
                                        _mediaDesc = primary->mediaDesc();
                                        _audioDesc = primary->audioDesc();
                                        _metadata = primary->metadata();
                                }
                                if (!_portGroups.isEmpty() && _portGroups[0] != nullptr) {
                                        _frameRate = _portGroups[0]->frameRate();
                                }
                        }
                        // Open failure cleanup is handled by
                        // @ref CommandMediaIO::dispatch — the backend's
                        // Close handler runs there before the open
                        // result returns, so completeCommand sees the
                        // original error without needing a second
                        // dispatch.
                        break;
                }
                case MediaIOCommand::Close: {
                        // Push a single synthetic EOS read result via
                        // each source's read cache so signal-driven
                        // consumers receive exactly one trailing
                        // frameReady whose readFrame() returns a
                        // request resolving with Error::EndOfFile.
                        // The synthetic sits at the tail of the cache
                        // behind any real read results produced by
                        // prefetches that ran before close, so the
                        // consumer sees them all first and EOS last.
                        for (MediaIOSource *src : _sources) {
                                if (src == nullptr) continue;
                                src->_readCache.pushSyntheticResult(Error::EndOfFile);
                        }
                        resetClosedState();
                        closedSignal.emit(cmd->result);
                        break;
                }
                case MediaIOCommand::Read: {
                        auto *cr = static_cast<MediaIOCommandRead *>(raw);
                        if (cr->mediaDescChanged) {
                                _mediaDesc = cr->updatedMediaDesc;
                                _frameRate = _mediaDesc.frameRate();
                                if (!_mediaDesc.audioList().isEmpty()) {
                                        _audioDesc = _mediaDesc.audioList()[0];
                                }
                                _metadata = _mediaDesc.metadata();
                                descriptorChangedSignal.emit();
                        }
                        if (cr->group != nullptr) {
                                MediaIOPortGroup *g = cr->group;
                                if (cmd->result.isOk()) {
                                        // Update group navigation +
                                        // record bytes through the
                                        // rate tracker before stamping
                                        // the frame so per-frame
                                        // metadata sees the new
                                        // current-frame value.
                                        g->_currentFrame = cr->currentFrame;
                                        if (cr->frame.isValid()) {
                                                g->_rateTracker.record(MediaIO::frameByteSize(cr->frame));

                                                // Frame metadata stamp
                                                // + synthetic timing
                                                // fall-back so every
                                                // delivered frame
                                                // carries FrameNumber,
                                                // optional
                                                // MediaDescChanged,
                                                // and a payload pts.
                                                Frame::Ptr      &fp = cr->frame;
                                                const FrameRate &rate = g->frameRate();
                                                fp.modify()->metadata().set(Metadata::FrameNumber, g->_currentFrame);
                                                if (cr->mediaDescChanged) {
                                                        fp.modify()->metadata().set(Metadata::MediaDescChanged, true);
                                                }
                                                int64_t ns = rate.cumulativeTicks(
                                                        INT64_C(1000000000),
                                                        g->_currentFrame.isValid() ? g->_currentFrame.value() : 0);
                                                TimeStamp synTs =
                                                        g->originTime() + Duration::fromNanoseconds(ns);
                                                MediaTimeStamp synMts(synTs, ClockDomain::Synthetic);
                                                ensurePayloadTiming(fp, synMts, rate);
                                        }
                                } else if (cmd->result == Error::EndOfFile) {
                                        // Latch EOF on the group so
                                        // subsequent reads
                                        // short-circuit.  Also drain
                                        // every source's cache in the
                                        // group — any prefetched reads
                                        // from sibling sources are
                                        // stale relative to the EOF
                                        // boundary and would otherwise
                                        // surface to the consumer
                                        // ahead of the EOS that
                                        // already resolved this
                                        // request.
                                        g->_atEnd = true;
                                        for (MediaIOPort *p : g->ports()) {
                                                if (p == nullptr) continue;
                                                if (p->role() != MediaIOPort::Source) continue;
                                                auto *src = static_cast<MediaIOSource *>(p);
                                                src->_readCache.cancelAll();
                                        }
                                }
                        }
                        // Decrement the per-group in-flight counter
                        // here so it runs exactly once per cmd
                        // regardless of whether the cmd succeeded,
                        // errored, or was cancelled before reaching
                        // the backend.  The matching increment is in
                        // @ref MediaIOReadCache::submitOneLocked.
                        if (cr->group != nullptr) {
                                cr->group->_pendingReadCount.fetchAndSub(1);
                                // Notify every source cache in the
                                // group so the frameReady
                                // edge-detector arms on whichever
                                // cache actually held this cmd.
                                // Caches that don't hold the cmd
                                // simply re-evaluate against their
                                // own head and no-op.
                                for (MediaIOPort *p : cr->group->ports()) {
                                        if (p == nullptr) continue;
                                        if (p->role() != MediaIOPort::Source) continue;
                                        auto *src = static_cast<MediaIOSource *>(p);
                                        src->_readCache.onCommandCompleted();
                                }
                        }
                        // Mirror the Write→source-frameReady kick on
                        // the symmetric edge: a successful Read on a
                        // transform-style backend (CSC, SRC,
                        // VideoEncoder, …) drains the internal
                        // output queue, freeing capacity for more
                        // input.  Fire @c frameWantedSignal on every
                        // sink so any upstream pump parked at
                        // @c writesAccepted() <= 0 re-runs.  Pure
                        // source backends have no @c _sinks entries
                        // so this is a no-op there; pure sink
                        // backends never run Read cmds.  Spurious
                        // wakeups (Read returning a frame that did
                        // not free internal write-queue capacity)
                        // just round-trip back to the back-pressure
                        // gate at the top of pump and yield again.
                        if (cmd->result.isOk()) {
                                for (MediaIOSink *snk : _sinks) {
                                        if (snk != nullptr) snk->frameWantedSignal.emit();
                                }
                        }
                        break;
                }
                case MediaIOCommand::Write: {
                        auto *cw = static_cast<MediaIOCommandWrite *>(raw);
                        // Decrement the per-group in-flight counter
                        // (matches the increment in
                        // @ref MediaIOSink::writeFrame).  Runs on
                        // every termination — success, error, or
                        // cancel — so the counter never leaks.
                        if (cw->group != nullptr) {
                                cw->group->_pendingWriteCount.fetchAndSub(1);
                        }
                        if (cmd->result.isOk()) {
                                if (cw->group != nullptr) {
                                        cw->group->_rateTracker.record(MediaIO::frameByteSize(cw->frame));
                                }
                                if (cw->sink != nullptr) cw->sink->frameWantedSignal.emit();
                                // Transform-style backends (CSC, SRC,
                                // VideoEncoder, VideoDecoder, FrameSync,
                                // …) produce output on a source port
                                // when input arrives on a sink port.
                                // Kick every source on this MediaIO so
                                // any downstream consumer parked after
                                // a previous @c TryAgain re-runs its
                                // pump.  Pure source / pure sink
                                // backends emit a no-op (sources fire
                                // a signal nobody on the read side
                                // listens for; sinks have no @c
                                // _sources entries to iterate).
                                // Spurious wakeups (e.g. encoder
                                // buffering input without producing
                                // output yet) cost one strand
                                // round-trip back to @c TryAgain on
                                // the next Read; this is naturally
                                // rate-limited to the upstream's
                                // frame rate rather than the
                                // strand round-trip rate.
                                for (MediaIOSource *src : _sources) {
                                        if (src != nullptr) src->frameReadySignal.emit();
                                }
                        } else {
                                if (cw->sink != nullptr) cw->sink->writeErrorSignal.emit(cmd->result);
                        }
                        break;
                }
                case MediaIOCommand::Seek: {
                        auto *cs = static_cast<MediaIOCommandSeek *>(raw);
                        if (cs->group != nullptr && cmd->result.isOk()) {
                                cs->group->_currentFrame = cs->currentFrame;
                        }
                        break;
                }
                case MediaIOCommand::Params:
                        // No cache writes.  Result is delivered to the
                        // caller via the request.
                        break;
                case MediaIOCommand::Stats:
                        // The backend's executeCmd populated any
                        // backend-specific cumulative keys into
                        // cmd.stats; overlay the framework-managed
                        // standard keys (rate trackers, drop /
                        // repeat / late counters, strand backlog)
                        // so they are authoritative.  The
                        // per-command timing keys
                        // (ExecuteDurationNs / QueueWaitDurationNs)
                        // already populated by submit() are
                        // preserved — same container, additive.
                        populateStandardStats(raw->stats);
                        break;
                case MediaIOCommand::SetClock: {
                        // Framework owns the actual swap so backends
                        // never touch group->_clock directly.  On any
                        // error (including the default NotSupported)
                        // the existing clock stays in place, preserving
                        // the invariant that group->clock() always
                        // reflects what is actually in effect.
                        auto *csc = static_cast<MediaIOCommandSetClock *>(raw);
                        if (csc->group != nullptr && cmd->result.isOk()) {
                                csc->group->_clock = csc->clock;
                        }
                        break;
                }
        }
        // Notify any observability subscribers (pipeline-level stats
        // collectors in particular) that a command resolved.  Fires
        // before @ref markCompleted so a subscriber walking @c stats
        // sees the same container the request's @c .wait() / @c .then()
        // will surface — there is no observable lag between "command
        // available to slot" and "command resolved to caller".
        commandCompletedSignal.emit(cmd);
        // Resolution latch + waiter wake + one-shot continuation
        // dispatch all live on the command itself.  markCompleted()
        // is idempotent — the cancel path can race the dispatch
        // path safely.
        raw->markCompleted();
}

// ============================================================================
// Open / Close
// ============================================================================

MediaIORequest MediaIO::open() {
        if (isOpen()) return MediaIORequest::resolved(Error::AlreadyOpen);

        // The backend declares its ports via
        // @ref CommandMediaIO::addPortGroup / addSource / addSink (or
        // @ref CommandMediaIO helpers) during executeCmd(Open).
        // completeCommand picks up those ports on success to populate
        // the MediaIO-level cache; @ref CommandMediaIO::dispatch
        // automatically runs Close on failure so the backend can
        // release any half-allocated resources.
        auto *cmdOpen = new MediaIOCommandOpen();
        cmdOpen->config = _config;
        cmdOpen->pendingMediaDesc = _pendingMediaDesc;
        cmdOpen->pendingAudioDesc = _pendingAudioDesc;
        cmdOpen->pendingMetadata = _pendingMetadata;
        cmdOpen->videoTracks = _pendingVideoTracks;
        cmdOpen->audioTracks = _pendingAudioTracks;

        MediaIOCommand::Ptr  cmd = MediaIOCommand::Ptr::takeOwnership(cmdOpen);
        MediaIORequest req(cmd);
        submit(cmd);
        return req;
}

void MediaIO::resetClosedState() {
        _open.setValue(false);
        _mediaDesc = MediaDesc();
        _audioDesc = AudioDesc();
        _metadata = Metadata();
        _frameRate = FrameRate();
        _defaultSeekMode = SeekExact;
        _closing.setValue(false);
}

MediaIORequest MediaIO::close() {
        promekiDebug("MediaIO::close ENTER isOpen=%d _closing=%d", (int)isOpen(), (int)isClosing());
        if (!isOpen() || isClosing()) return MediaIORequest::resolved(Error::NotOpen);

        // Latch closing state.  This gates readFrame() from submitting
        // new prefetches and writeFrame() from accepting new writes,
        // while still letting readFrame() drain any results already
        // in flight plus the trailing EOS pushed by completeCommand.
        _closing.setValue(true);

        // Give the backend a chance to unwind any in-flight blocking
        // command from the caller's thread — backends whose executeCmd
        // can block on external signals (for example, a FrameBridge
        // publisher waiting for a consumer) would otherwise keep the
        // strand busy and starve the Close we're about to submit.
        cancelBlockingWork();

        // Graceful close: do NOT cancel pending strand work.  Any
        // reads/writes submitted before close() keep running to
        // completion — blocking callers unblock with their real
        // result, prefetched reads land in their source's
        // @ref MediaIOReadCache as usual.  The Close command goes
        // to the back of the strand queue so it only runs after
        // every prior task has completed, and completeCommand()
        // handles the EOS push, cache reset, and closedSignal
        // emission.  Callers that want to fire and
        // forget can simply discard the returned request — the
        // command stays alive in the strand entry until it runs.
        auto *cmdClose = new MediaIOCommandClose();
        MediaIOCommand::Ptr  cmd = MediaIOCommand::Ptr::takeOwnership(cmdClose);
        MediaIORequest req(cmd);
        submit(cmd);
        return req;
}

// ============================================================================
// ============================================================================
// Introspection / negotiation
// ============================================================================
//
// describe() pre-fills everything MediaIO already knows (backend
// identity from MediaIOFactory, instance identity, cached state) and
// then asks the task to supplement format-specific fields.  The
// proposeInput / proposeOutput wrappers are thin forwarders so
// callers can negotiate without touching the task layer.

Error MediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        *out = MediaIODescription();

        // Backend identity / role flags from the registered factory.
        // The Type config key is set by every create / createForFile*
        // path, so it is always present once the MediaIO has a task.
        const String typeName =
                _config.contains(MediaConfig::Type) ? _config.getAs<String>(MediaConfig::Type) : String();
        if (!typeName.isEmpty()) {
                const MediaIOFactory *factory = MediaIOFactory::findByName(typeName);
                if (factory != nullptr) {
                        out->setBackendName(factory->name());
                        out->setBackendDescription(factory->description());
                        out->setCanBeSource(factory->canBeSource());
                        out->setCanBeSink(factory->canBeSink());
                        out->setCanBeTransform(factory->canBeTransform());
                }
        }

        // Instance identity (always populated, even pre-task).
        out->setName(name());

        // Cached state — only meaningful while open.  Pre-open
        // backends will fill these via their describe() probe.
        if (isOpen()) {
                out->setFrameRate(_frameRate);
                out->setContainerMetadata(_metadata);
                if (_mediaDesc.isValid()) {
                        out->setPreferredFormat(_mediaDesc);
                }
        }

        // Backend supplement happens in the @ref MediaIO subclass
        // override (e.g. @ref MediaIO::describe forwards to
        // the wrapped task).  The base reports just the framework-
        // managed fields; @ref MediaIO::describe is virtual so the
        // override can chain up before adding its supplement.

        // Per-port-group snapshots.  The planner consults these for
        // cross-group dependency analysis (e.g. "this MediaIO has a
        // single sync group with both audio and video; treat them as
        // a unit").  Single-port-group MediaIOs still appear here as
        // a one-element list so the planner has a uniform view.
        for (const MediaIOPortGroup *grp : _portGroups) {
                if (grp == nullptr) continue;
                MediaIOPortGroupDescription gd;
                gd.name = grp->name();
                gd.frameRate = grp->frameRate();
                gd.frameCount = grp->frameCount();
                gd.canSeek = grp->canSeek();
                if (grp->clock().isValid()) {
                        gd.clockDescription = grp->clock()->domain().toString();
                }
                out->portGroups() += gd;
        }

        // Per-port snapshots — one entry per source / sink registered
        // with the MediaIO during open.  The planner uses
        // producibleFormats (sources) and acceptableFormats (sinks)
        // to decide where to splice in CSC / decoder / framesync
        // bridges.  Phase 6 only wires up identity + group references;
        // per-port format landscapes are filled in once each backend
        // is converted to populate them in @ref describe.
        auto portGroupIndexOf = [this](const MediaIOPortGroup *g) -> int {
                if (g == nullptr) return -1;
                for (int i = 0; i < static_cast<int>(_portGroups.size()); ++i) {
                        if (_portGroups[i] == g) return i;
                }
                return -1;
        };
        for (const MediaIOSource *src : _sources) {
                if (src == nullptr) continue;
                MediaIOPortDescription pd;
                pd.name = src->name();
                pd.index = src->index();
                pd.role = MediaIOPortDescription::Source;
                pd.portGroupIndex = portGroupIndexOf(src->group());
                out->sources() += pd;
        }
        for (const MediaIOSink *sink : _sinks) {
                if (sink == nullptr) continue;
                MediaIOPortDescription pd;
                pd.name = sink->name();
                pd.index = sink->index();
                pd.role = MediaIOPortDescription::Sink;
                pd.portGroupIndex = portGroupIndexOf(sink->group());
                pd.preferredFormat = sink->expectedDesc();
                out->sinks() += pd;
        }
        return Error::Ok;
}

Error MediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        // Default: accept whatever is offered (transparent
        // passthrough).  Sinks and transforms with format constraints
        // override to either narrow or refuse.  Matches the old
        // @c proposeInput default so passthrough sinks
        // (Inspector, FrameBridge, ...) that don't override behave
        // identically across the Phase-10/11 transition.
        if (preferred != nullptr) *preferred = offered;
        return Error::Ok;
}

Error MediaIO::proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const {
        // Default base behavior matches @ref proposeInput.
        (void)requested;
        if (achievable != nullptr) *achievable = MediaDesc();
        return Error::NotSupported;
}

Error MediaIO::setPendingMediaDesc(const MediaDesc &desc) {
        if (isOpen()) return Error::AlreadyOpen;
        _pendingMediaDesc = desc;
        return Error::Ok;
}

Error MediaIO::setPendingAudioDesc(const AudioDesc &desc) {
        if (isOpen()) return Error::AlreadyOpen;
        _pendingAudioDesc = desc;
        return Error::Ok;
}

Error MediaIO::setPendingMetadata(const Metadata &meta) {
        if (isOpen()) return Error::AlreadyOpen;
        _pendingMetadata = meta;
        return Error::Ok;
}

Error MediaIO::setVideoTracks(const List<int> &tracks) {
        if (isOpen()) return Error::AlreadyOpen;
        _pendingVideoTracks = tracks;
        return Error::Ok;
}

Error MediaIO::setAudioTracks(const List<int> &tracks) {
        if (isOpen()) return Error::AlreadyOpen;
        _pendingAudioTracks = tracks;
        return Error::Ok;
}

MediaIORequest MediaIO::sendParams(const String &name, const MediaIOParams &params) {
        if (!isOpen() || isClosing()) return MediaIORequest::resolved(Error::NotOpen);

        auto *cmdParams = new MediaIOCommandParams();
        cmdParams->name = name;
        cmdParams->params = params;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdParams);
        MediaIORequest req(cmd);
        submit(cmd);
        return req;
}

MediaIORequest MediaIO::stats() {
        if (!isOpen() || isClosing()) return MediaIORequest::resolved(Error::NotOpen);

        // Build a stats query command and dispatch via submit().
        // The backend populates cumulative aggregate keys into
        // cmd.stats from executeCmd(MediaIOCommandStats &); the
        // framework overlays standard keys in completeCommand.
        // Marked urgent so polling does not block behind a deep
        // queue of real I/O.
        auto *cmd = new MediaIOCommandStats();
        cmd->urgent = true;
        MediaIOCommand::Ptr cmdPtr = MediaIOCommand::Ptr::takeOwnership(cmd);
        MediaIORequest      req(cmdPtr);
        submit(cmdPtr);
        return req;
}


PROMEKI_NAMESPACE_END
