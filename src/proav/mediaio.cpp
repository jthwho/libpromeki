/**
 * @file      mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaio.h>
#include <promeki/mediaiotask.h>
#include <promeki/threadpool.h>
#include <promeki/file.h>
#include <promeki/logger.h>
#include <promeki/benchmarkreporter.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/buffer.h>
#include <promeki/stringlist.h>
#include <promeki/units.h>
#include <atomic>
#include <cstdint>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MediaIO)

// ============================================================================
// Per-instance local ID counter
// ============================================================================
//
// Process-wide atomic counter.  Every MediaIO instance that construction
// fires bumps it once to claim a monotonically increasing local ID.  The
// counter never resets and is the sole source of the default `Name`
// suffix, so two instances cannot collide even if one is destroyed and
// another is created.

static std::atomic<int> g_nextLocalId{0};

// ============================================================================
// Format registry
// ============================================================================

static MediaIO::FormatDescList &formatRegistry() {
        static MediaIO::FormatDescList list;
        return list;
}

ThreadPool &MediaIO::pool() {
        static ThreadPool *p = []() {
                auto *tp = new ThreadPool;
                tp->setNamePrefix("media");
                return tp;
        }();
        return *p;
}

int MediaIO::registerFormat(const FormatDesc &desc) {
        FormatDescList &list = formatRegistry();
        int ret = list.size();
        list.pushToBack(desc);
        promekiDebug("Registered MediaIO '%s'", desc.name.cstr());
        return ret;
}

const MediaIO::FormatDescList &MediaIO::registeredFormats() {
        return formatRegistry();
}

static const MediaIO::FormatDesc *findFormatByName(const String &name) {
        const MediaIO::FormatDescList &list = formatRegistry();
        for(const auto &desc : list) {
                if(desc.name == name) return &desc;
        }
        return nullptr;
}

static String extractExtension(const String &filename) {
        size_t dot = filename.rfind('.');
        if(dot == String::npos || dot + 1 >= filename.size()) return String();
        return filename.mid(dot + 1).toLower();
}

static const MediaIO::FormatDesc *findFormatByExtension(const String &filename) {
        String ext = extractExtension(filename);
        if(ext.isEmpty()) return nullptr;
        const MediaIO::FormatDescList &list = formatRegistry();
        for(const auto &desc : list) {
                for(const auto &e : desc.extensions) {
                        if(ext == e) return &desc;
                }
        }
        return nullptr;
}

static const MediaIO::FormatDesc *findFormatForFileRead(const String &filename) {
        const MediaIO::FormatDescList &list = formatRegistry();

        // Pass 0: path-based probe (device nodes like /dev/video0)
        for(const auto &desc : list) {
                if(!desc.canOutput) continue;
                if(desc.canHandlePath && desc.canHandlePath(filename)) return &desc;
        }

        // Pass 1: extension match (fast path)
        String ext = extractExtension(filename);
        if(!ext.isEmpty()) {
                for(const auto &desc : list) {
                        if(!desc.canOutput) continue;
                        for(const auto &e : desc.extensions) {
                                if(ext == e) return &desc;
                        }
                }
        }

        // Pass 2: content-based probe
        File probeFile(filename);
        if(probeFile.open(IODevice::ReadOnly).isError()) return nullptr;
        const MediaIO::FormatDesc *result = nullptr;
        for(const auto &desc : list) {
                if(!desc.canOutput) continue;
                if(!desc.canHandleDevice) continue;
                probeFile.seek(0);
                if(desc.canHandleDevice(&probeFile)) {
                        result = &desc;
                        break;
                }
        }
        probeFile.close();
        return result;
}

// ============================================================================
// Factory
// ============================================================================

MediaIO::Config MediaIO::defaultConfig(const String &typeName) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc == nullptr || !desc->configSpecs) return Config();
        Config cfg;
        cfg.setValidation(SpecValidation::None);
        Config::SpecMap specs = desc->configSpecs();
        for(auto it = specs.cbegin(); it != specs.cend(); ++it) {
                const Variant &def = it->second.defaultValue();
                if(def.isValid()) cfg.set(it->first, def);
        }
        cfg.setValidation(SpecValidation::Warn);
        cfg.set(MediaConfig::Type, typeName);
        return cfg;
}

MediaIO::Config::SpecMap MediaIO::configSpecs(const String &typeName) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc == nullptr || !desc->configSpecs) return Config::SpecMap();
        return desc->configSpecs();
}

Metadata MediaIO::defaultMetadata(const String &typeName) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc == nullptr || !desc->defaultMetadata) return Metadata();
        return desc->defaultMetadata();
}

StringList MediaIO::unknownConfigKeys(const String &typeName,
                                      const Config &cfg) {
        // Detection is intentionally spec-driven: pull the backend's
        // spec map once, and let VariantDatabase::unknownKeys fall back
        // to the global MediaConfig spec registry for common keys like
        // Filename / Type / Name / Uuid / EnableBenchmark.  No
        // task-specific knowledge is hard-coded here — a brand-new
        // backend gets key validation for free the moment it publishes
        // a FormatDesc::configSpecs callback.
        Config::SpecMap specs = configSpecs(typeName);
        return cfg.unknownKeys(specs);
}

Error MediaIO::validateConfigKeys(const String &typeName,
                                  const Config &cfg,
                                  ConfigValidation mode,
                                  const String &contextLabel) {
        StringList unknown = unknownConfigKeys(typeName, cfg);
        if(unknown.isEmpty()) return Error::Ok;

        // One log line per unknown key so a caller's grep-friendly log
        // shows each typo individually.  The contextLabel lets the
        // caller embed its own scope (e.g. "mediaplay: input[TPG]")
        // without the framework having to know anything about the
        // caller — MediaIO stays caller-agnostic.
        const char *modeTag = (mode == ConfigValidation::Strict)
                ? "rejecting"
                : "ignoring";
        for(size_t i = 0; i < unknown.size(); ++i) {
                if(contextLabel.isEmpty()) {
                        promekiWarn("MediaIO[%s]: unknown config key '%s' (%s)",
                                typeName.cstr(),
                                unknown[i].cstr(),
                                modeTag);
                } else {
                        promekiWarn("%s: MediaIO[%s]: unknown config key '%s' (%s)",
                                contextLabel.cstr(),
                                typeName.cstr(),
                                unknown[i].cstr(),
                                modeTag);
                }
        }
        return (mode == ConfigValidation::Strict)
                ? Error::InvalidArgument
                : Error::Ok;
}

MediaIO *MediaIO::create(const Config &config, ObjectBase *parent) {
        const FormatDesc *desc = nullptr;

        if(config.contains(MediaConfig::Type)) {
                String typeName = config.getAs<String>(MediaConfig::Type);
                desc = findFormatByName(typeName);
                if(desc == nullptr) {
                        promekiWarn("MediaIO::create: unknown type '%s'", typeName.cstr());
                        return nullptr;
                }
        }

        if(desc == nullptr && config.contains(MediaConfig::Filename)) {
                String filename = config.getAs<String>(MediaConfig::Filename);
                desc = findFormatByExtension(filename);
                if(desc == nullptr) {
                        promekiWarn("MediaIO::create: no backend for '%s'", filename.cstr());
                        return nullptr;
                }
        }

        if(desc == nullptr) {
                promekiWarn("MediaIO::create: config has neither Type nor Filename");
                return nullptr;
        }

        MediaIOTask *task = desc->create();
        if(task == nullptr) {
                promekiWarn("MediaIO::create: factory for '%s' returned null", desc->name.cstr());
                return nullptr;
        }
        MediaIO *io = new MediaIO(parent);
        io->_task = task;
        task->_owner = io;
        io->_config = config;
        return io;
}

MediaIO *MediaIO::createForFileRead(const String &filename, ObjectBase *parent) {
        const FormatDesc *desc = findFormatForFileRead(filename);
        if(desc == nullptr) {
                promekiWarn("MediaIO::createForFileRead: no backend for '%s'", filename.cstr());
                return nullptr;
        }
        if(!desc->canOutput) {
                promekiWarn("MediaIO::createForFileRead: '%s' does not support reading", desc->name.cstr());
                return nullptr;
        }
        MediaIOTask *task = desc->create();
        if(task == nullptr) return nullptr;
        MediaIO *io = new MediaIO(parent);
        io->_task = task;
        task->_owner = io;
        // Seed the live config with the resolved backend name + the
        // file the caller passed in, so downstream consumers that
        // need to know "which backend is this?" can read it back
        // from io->config() without a second registry walk.
        io->_config = desc->configSpecs ? defaultConfig(desc->name) : Config();
        io->_config.set(MediaConfig::Type, desc->name);
        io->_config.set(MediaConfig::Filename, filename);
        return io;
}

MediaIO *MediaIO::createForFileWrite(const String &filename, ObjectBase *parent) {
        const FormatDesc *desc = findFormatByExtension(filename);
        if(desc == nullptr) {
                promekiWarn("MediaIO::createForFileWrite: no backend for '%s'", filename.cstr());
                return nullptr;
        }
        if(!desc->canInput) {
                promekiWarn("MediaIO::createForFileWrite: '%s' does not support writing", desc->name.cstr());
                return nullptr;
        }
        MediaIOTask *task = desc->create();
        if(task == nullptr) return nullptr;
        MediaIO *io = new MediaIO(parent);
        io->_task = task;
        task->_owner = io;
        // Same rationale as createForFileRead: seed the live config
        // with the backend's full default schema plus the type and
        // filename so callers that read io->config() back out see a
        // complete, discoverable picture.
        io->_config = desc->configSpecs ? defaultConfig(desc->name) : Config();
        io->_config.set(MediaConfig::Type, desc->name);
        io->_config.set(MediaConfig::Filename, filename);
        return io;
}

StringList MediaIO::enumerate(const String &typeName) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc == nullptr || !desc->enumerate) return StringList();
        return desc->enumerate();
}

const MediaIO::FormatDesc *MediaIO::findFormatForPath(const String &path) {
        const FormatDescList &list = formatRegistry();
        for(const auto &desc : list) {
                if(desc.canHandlePath && desc.canHandlePath(path)) return &desc;
        }
        return nullptr;
}

List<MediaDesc> MediaIO::queryDevice(const String &typeName,
                                     const Config &config) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc == nullptr || !desc->queryDevice) return {};
        return desc->queryDevice(config);
}

void MediaIO::printDeviceInfo(const String &typeName,
                              const Config &config) {
        const FormatDesc *desc = findFormatByName(typeName);
        if(desc != nullptr && desc->printDeviceInfo) {
                desc->printDeviceInfo(config);
        }
}

// ============================================================================
// Lifecycle
// ============================================================================

MediaIO::MediaIO(ObjectBase *parent) : ObjectBase(parent) {
        // Claim a process-local instance ID.  Seeded immediately rather
        // than at open() so code that creates a MediaIO, reads its
        // identifiers, and then destroys it without ever opening still
        // sees a meaningful localId.
        _localId = g_nextLocalId.fetch_add(1);

        // Seed the default name from the local ID.  Callers can still
        // override via MediaConfig::Name before open(); the override is
        // applied in resolveIdentifiersAndBenchmark().
        _name = String("media") + String::number(_localId);

        // Every instance gets a fresh random UUID at construction so
        // cross-process pipeline correlation can start before open() is
        // even called.  An explicit MediaConfig::Uuid takes precedence
        // at open() time.
        _uuid = UUID::generate();
}

void MediaIO::resolveIdentifiersAndBenchmark() {
        // --- Name ---
        // Honor an explicit MediaConfig::Name if provided; otherwise
        // keep the constructor default ("media<localId>").  The resolved
        // value is written back into the live config so a subsequent
        // config() lookup sees the effective name rather than an empty
        // string.
        String cfgName = _config.getAs<String>(MediaConfig::Name, String());
        if(!cfgName.isEmpty()) {
                _name = cfgName;
        }
        _config.set(MediaConfig::Name, _name);

        // --- UUID ---
        // Same pattern: override from config if a valid UUID is
        // supplied, otherwise keep the constructor-assigned one.
        UUID cfgUuid = _config.getAs<UUID>(MediaConfig::Uuid, UUID());
        if(cfgUuid.isValid()) {
                _uuid = cfgUuid;
        }
        _config.set(MediaConfig::Uuid, _uuid);

        // --- Benchmark enable + stamp IDs ---
        // Read the opt-in flag once at open() so the per-frame stamp
        // sites can check a single boolean instead of walking the
        // VariantDatabase.  Stamp IDs are registered against the
        // Benchmark StringRegistry once per open() with the resolved
        // name as prefix, so reports correlate back to the stage.
        _benchmarkEnabled = _config.getAs<bool>(MediaConfig::EnableBenchmark, false);
        if(_benchmarkEnabled) {
                _idStampEnqueue   = Benchmark::Id(_name + ".enqueue");
                _idStampDequeue   = Benchmark::Id(_name + ".dequeue");
                _idStampTaskBegin = Benchmark::Id(_name + ".taskBegin");
                _idStampTaskEnd   = Benchmark::Id(_name + ".taskEnd");
                _idStampWorkBegin = Benchmark::Id(_name + ".workBegin");
                _idStampWorkEnd   = Benchmark::Id(_name + ".workEnd");
        }

        // Fresh telemetry window per open.  A reopen must not show
        // stale rate or drop counts from a previous session.
        _rateTracker.reset();
        _framesDroppedTotal.setValue(0);
        _framesRepeatedTotal.setValue(0);
        _framesLateTotal.setValue(0);
}

int64_t MediaIO::frameByteSize(const Frame::Ptr &frame) {
        // Walks the frame once and sums every plane and audio buffer's
        // logical size.  "Logical" is important: we use Buffer::size()
        // (content) rather than allocSize() (allocation) so that
        // partially-filled scratch buffers do not inflate the reported
        // rate.  The loop skips invalid pointers defensively because
        // some backends build frames lazily.
        if(!frame.isValid()) return 0;
        int64_t total = 0;
        for(const auto &imgPtr : frame->imageList()) {
                if(!imgPtr.isValid()) continue;
                for(const auto &planePtr : imgPtr->planes()) {
                        if(planePtr.isValid()) {
                                total += static_cast<int64_t>(planePtr->size());
                        }
                }
        }
        for(const auto &audPtr : frame->audioList()) {
                if(!audPtr.isValid()) continue;
                const Buffer::Ptr &buf = audPtr->buffer();
                if(buf.isValid()) {
                        total += static_cast<int64_t>(buf->size());
                }
        }
        return total;
}

void MediaIO::populateStandardStats(MediaIOStats &stats) const {
        // Rate-tracker derived standard keys.  These are authoritative
        // and overwrite any backend-contributed values: the base class
        // owns BytesPerSecond / FramesPerSecond so that every backend
        // gets them for free without reimplementing a rolling window.
        stats.set(MediaIOStats::BytesPerSecond, _rateTracker.bytesPerSecond());
        stats.set(MediaIOStats::FramesPerSecond, _rateTracker.framesPerSecond());

        // Lifetime drop / repeat / late counters.  Backends report
        // these through the MediaIOTask::noteFrameDropped family of
        // protected helpers, which simply increment these atomics.
        stats.set(MediaIOStats::FramesDropped,
                _framesDroppedTotal.value());
        stats.set(MediaIOStats::FramesRepeated,
                _framesRepeatedTotal.value());
        stats.set(MediaIOStats::FramesLate,
                _framesLateTotal.value());

        // Latency is only populated when the caller opted into
        // benchmarking and provided somewhere to aggregate the stamps.
        // Without those, the latency keys stay at their default 0.0
        // so callers can distinguish "not measured" from "zero".
        //
        // BenchmarkReporter tracks *consecutive* entry pairs only, so
        // we cannot directly ask for the full enqueue→taskEnd span.
        // Instead, we sum the individual work-phase deltas: the
        // writer path covers enqueue→dequeue (queue wait),
        // dequeue→taskBegin (strand dispatch), and taskBegin→taskEnd
        // (backend work), which together reconstruct the end-to-end
        // latency.  The reader path skips the enqueue hop because
        // reads are pulled by the strand worker, not pushed from the
        // user thread.
        if(_benchmarkEnabled && _benchmarkReporter != nullptr) {
                double avgMs  = 0.0;
                double peakMs = 0.0;
                uint64_t minCount = UINT64_MAX;

                auto addPair = [&](Benchmark::Id from, Benchmark::Id to) {
                        auto s = _benchmarkReporter->stepStats(from, to);
                        if(s.count == 0) return;
                        avgMs  += s.avg * 1000.0;
                        peakMs += s.max * 1000.0;
                        if(s.count < minCount) minCount = s.count;
                };

                // The enqueue→dequeue stamp exists only on the sink
                // path — writeFrame() emits an enqueue stamp from the
                // user thread right before handing the command to the
                // strand, which the worker then pairs with a dequeue
                // stamp.  The source path (readFrame) pulls from the
                // strand directly and has no equivalent step, so we
                // only pair this stamp when the MediaIO is accepting
                // frames.
                if(_mode == Input || _mode == InputAndOutput) {
                        addPair(_idStampEnqueue,   _idStampDequeue);
                }
                addPair(_idStampDequeue,   _idStampTaskBegin);
                addPair(_idStampTaskBegin, _idStampTaskEnd);

                if(minCount != UINT64_MAX && minCount > 0) {
                        stats.set(MediaIOStats::AverageLatencyMs, avgMs);
                        stats.set(MediaIOStats::PeakLatencyMs, peakMs);
                }

                // Processing time — the interval between the task's
                // workBegin and workEnd stamps.  Only populated when
                // the task actually calls stampWorkBegin/stampWorkEnd;
                // tasks that don't will simply omit these keys.
                auto ws = _benchmarkReporter->stepStats(
                        _idStampWorkBegin, _idStampWorkEnd);
                if(ws.count > 0) {
                        stats.set(MediaIOStats::AverageProcessingMs,
                                  ws.avg * 1000.0);
                        stats.set(MediaIOStats::PeakProcessingMs,
                                  ws.max * 1000.0);
                }
        }

        // Backlog depth from the strand itself.  Telemetry callers
        // (e.g. mediaplay --stats) surface this to let operators see
        // when I/O is falling behind without every backend having to
        // reimplement "how many operations are pending".
        stats.set(MediaIOStats::PendingOperations,
                  static_cast<int64_t>(_strand.pendingCount()));
}

String MediaIOStats::toString() const {
        // Compact single-line renderer for the standard telemetry
        // keys.  Callers like mediaplay used to hand-format every
        // key individually; centralizing it here keeps log output
        // consistent across tools and gives backends a canonical
        // "what do these stats look like" answer for free.
        //
        // Key ordering is fixed so periodic output stays scannable
        // in a terminal.  Cheap counters that are still zero are
        // elided so the line stays quiet under normal operation —
        // the interesting thing a reader wants to notice is when
        // something shows up that wasn't there before.
        StringList parts;

        if(contains(BytesPerSecond)) {
                parts.pushToBack(Units::fromBytesPerSec(
                        getAs<double>(BytesPerSecond)));
        }
        if(contains(FramesPerSecond)) {
                parts.pushToBack(String::format("{:.1f} fps",
                        getAs<double>(FramesPerSecond)));
        }
        if(contains(FramesDropped)) {
                parts.pushToBack(String::format("drop={}",
                        getAs<int64_t>(FramesDropped)));
        }
        if(contains(FramesRepeated)) {
                int64_t v = getAs<int64_t>(FramesRepeated);
                if(v > 0) parts.pushToBack(String::format("rep={}", v));
        }
        if(contains(FramesLate)) {
                int64_t v = getAs<int64_t>(FramesLate);
                if(v > 0) parts.pushToBack(String::format("late={}", v));
        }
        if(contains(AverageLatencyMs) || contains(PeakLatencyMs)) {
                parts.pushToBack(String::format("lat={:.2f}/{:.2f} ms",
                        getAs<double>(AverageLatencyMs),
                        getAs<double>(PeakLatencyMs)));
        }
        if(contains(AverageProcessingMs) || contains(PeakProcessingMs)) {
                parts.pushToBack(String::format("proc={:.2f}/{:.2f} ms",
                        getAs<double>(AverageProcessingMs),
                        getAs<double>(PeakProcessingMs)));
        }
        if(contains(QueueDepth) || contains(QueueCapacity)) {
                parts.pushToBack(String::format("q={}/{}",
                        getAs<int64_t>(QueueDepth),
                        getAs<int64_t>(QueueCapacity)));
        }
        if(contains(PendingOperations)) {
                parts.pushToBack(String::format("pend={}",
                        getAs<int64_t>(PendingOperations)));
        }
        if(contains(LastErrorMessage)) {
                String msg = getAs<String>(LastErrorMessage);
                if(!msg.isEmpty()) {
                        parts.pushToBack(String::format("err={}", msg));
                }
        }

        return parts.join(String("  "));
}

void MediaIO::ensureFrameBenchmark(Frame::Ptr &frame) {
        // Allocates a Benchmark on the frame if one is not already
        // attached.  Called from the hot path on every write and every
        // successful read when benchmarking is enabled; non-enabled
        // callers short-circuit before reaching here.  Takes a
        // non-const reference because `.modify()` triggers copy-on-write
        // on the caller's Frame::Ptr; callers that want their original
        // Ptr untouched pass a local copy (see writeFrame).
        if(!frame.isValid()) return;
        if(frame->benchmark().isValid()) return;
        frame.modify()->setBenchmark(Benchmark::Ptr::takeOwnership(new Benchmark()));
}

void MediaIO::submitBenchmarkIfSink(const Frame::Ptr &frame) {
        // Final step of the stamp pipeline for sink stages: hand the
        // completed Benchmark to the reporter so it folds into the
        // per-step statistics.  Non-sink stages (e.g. middle of a
        // MediaPipeline) stamp but don't submit — the terminal stage
        // sees all accumulated stamps and submits once.
        if(!_benchmarkIsSink) return;
        if(_benchmarkReporter == nullptr) return;
        if(!frame.isValid()) return;
        Benchmark::Ptr bm = frame->benchmark();
        if(!bm.isValid()) return;
        _benchmarkReporter->submit(*bm);
}

Error MediaIO::adoptTask(MediaIOTask *task) {
        if(isOpen()) return Error::AlreadyOpen;
        if(task == nullptr) return Error::Invalid;
        if(_task != nullptr) return Error::Invalid;
        _task = task;
        task->_owner = this;
        return Error::Ok;
}

MediaIO::~MediaIO() {
        if(isOpen()) close();
        // Wait for any in-flight strand task to complete before deleting
        // the task.  The Strand destructor would also wait, but doing it
        // here makes the order explicit: drain the strand first, then
        // delete the task.
        _strand.waitForIdle();
        delete _task;
}

// ============================================================================
// Command dispatch
// ============================================================================

Error MediaIO::dispatchCommand(MediaIOCommand::Ptr cmd) {
        MediaIOCommand *raw = cmd.modify();
        switch(raw->type()) {
                case MediaIOCommand::Open:
                        return _task->executeCmd(*static_cast<MediaIOCommandOpen *>(raw));
                case MediaIOCommand::Close:
                        return _task->executeCmd(*static_cast<MediaIOCommandClose *>(raw));
                case MediaIOCommand::Read:
                        return _task->executeCmd(*static_cast<MediaIOCommandRead *>(raw));
                case MediaIOCommand::Write:
                        return _task->executeCmd(*static_cast<MediaIOCommandWrite *>(raw));
                case MediaIOCommand::Seek:
                        return _task->executeCmd(*static_cast<MediaIOCommandSeek *>(raw));
                case MediaIOCommand::Params:
                        return _task->executeCmd(*static_cast<MediaIOCommandParams *>(raw));
                case MediaIOCommand::Stats:
                        return _task->executeCmd(*static_cast<MediaIOCommandStats *>(raw));
        }
        return Error::NotSupported;
}

Error MediaIO::submitAndWait(MediaIOCommand::Ptr cmd, bool urgent) {
        // Submit to the strand for serialized execution and wait for the
        // result.  The strand returns a Future<Error> for the dispatched
        // call's return value.  Urgent submissions jump ahead of any
        // tasks still in the pending queue — used by low-latency
        // telemetry probes like stats() so they don't block behind a
        // deep queue of real work.  Urgent tasks still serialize with
        // the currently-running task; they never run concurrently.
        auto runner = [this, cmd]() mutable {
                return dispatchCommand(cmd);
        };
        Future<Error> future = urgent
                ? _strand.submitUrgent(std::move(runner))
                : _strand.submit(std::move(runner));
        auto r = future.result();
        if(r.second().isError()) return r.second();
        return r.first();
}

void MediaIO::submitReadCommand() {
        // Atomically claim a prefetch slot.  fetchAndAdd is the
        // single-step counterpart to the loop check in readFrame() —
        // by the time we own the slot, no other code path can race
        // past the depth limit.
        _pendingReadCount.fetchAndAdd(1);

        auto *cmdRead = new MediaIOCommandRead();
        cmdRead->step = _step;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdRead);

        // Fire-and-forget: dispatch on the strand, push the result onto the
        // read result queue when done.  readFrame() consumes from there.
        _strand.submit(
                [this, cmd]() mutable {
                        MediaIOCommand *raw = cmd.modify();
                        auto *cr = static_cast<MediaIOCommandRead *>(raw);

                        // Reads have no enqueue stamp — the user thread
                        // never handed us an existing frame — but we
                        // can still capture dequeue/taskBegin, let the
                        // backend produce the frame, then attach the
                        // accumulated Benchmark and stamp taskEnd.
                        // Intermediate timestamps live in a local
                        // Benchmark until the frame exists.
                        Benchmark::Ptr readBm;
                        if(_benchmarkEnabled) {
                                readBm = Benchmark::Ptr::takeOwnership(new Benchmark());
                                readBm.modify()->stamp(_idStampDequeue);
                                readBm.modify()->stamp(_idStampTaskBegin);
                                _task->_activeBenchmark = readBm.modify();
                        }

                        cr->result = _task->executeCmd(*cr);
                        _task->_activeBenchmark = nullptr;

                        // Happy-path live-telemetry hook.  Successful
                        // reads that actually produced a frame feed
                        // their payload size into the rate tracker so
                        // BytesPerSecond / FramesPerSecond work for
                        // every backend with zero migration.
                        if(cr->result.isOk() && cr->frame.isValid()) {
                                _rateTracker.record(
                                        frameByteSize(cr->frame));
                        }

                        if(_benchmarkEnabled && readBm.isValid()) {
                                readBm.modify()->stamp(_idStampTaskEnd);
                                // Attach the collected benchmark to the
                                // produced frame so downstream stages
                                // (or the sink) see the read-side
                                // timestamps.  If the backend already
                                // populated a benchmark (rare), merge
                                // isn't supported — the fresh one wins
                                // since it has the correct stamp IDs
                                // scoped to this stage's name.
                                if(cr->frame.isValid()) {
                                        cr->frame.modify()->setBenchmark(readBm);
                                        submitBenchmarkIfSink(cr->frame);
                                }
                        }

                        _readResultQueue.push(cmd);
                        _pendingReadCount.fetchAndSub(1);
                        // Fire on every completion — success, EOF, or
                        // error.  Signal-driven consumers need the
                        // signal for terminal results too so they can
                        // observe EOF/errors via a subsequent
                        // readFrame(..., false) call that pops the
                        // queued result.  The signal is "a read
                        // finished", not "a frame is available".
                        frameReadySignal.emit();
                        // A successful read drained the task's output
                        // FIFO by one slot, which raises writesAccepted
                        // for stages with an internal output queue
                        // (Converter, VideoEncoder, VideoDecoder, ...).
                        // Without this emit, upstreams that bailed with
                        // writer-full would never be re-kicked: after
                        // all pending writes complete, frameWanted's
                        // write-side emit has already fired its last
                        // time, and only reads release remaining
                        // capacity.  Fire frameWanted here so the
                        // upstream pipeline resumes regardless of
                        // which side of the strand released the slot.
                        if(cr->result.isOk()) frameWantedSignal.emit();
                },
                [this]() {
                        // Cancellation cleanup: release the slot we
                        // claimed above so the in-flight count stays
                        // accurate.
                        _pendingReadCount.fetchAndSub(1);
                });
}

// ============================================================================
// Open / Close
// ============================================================================

Error MediaIO::open(Mode mode) {
        if(isOpen()) return Error::AlreadyOpen;
        if(mode == NotOpen) return Error::InvalidArgument;
        if(_task == nullptr) return Error::Invalid;

        // Resolve the Name / Uuid / EnableBenchmark defaults before
        // building the open command so the backend sees the final
        // values via _config and the stamp sites see meaningful IDs on
        // the first enqueued frame.
        resolveIdentifiersAndBenchmark();

        // Fill in the standard libpromeki write defaults (Date,
        // OriginationDateTime, Software, Originator, OriginatorReference,
        // UMID) when opening a writer.  Values already set by the caller
        // are preserved because applyMediaIOWriteDefaults() uses
        // setIfMissing internally.  The defaults are merged into both
        // the free-standing pending metadata and the media descriptor's
        // own metadata, so writer backends see the same information
        // regardless of which path they read from.
        if(mode == Output || mode == InputAndOutput) {
                _pendingMetadata.applyMediaIOWriteDefaults();
                _pendingMediaDesc.metadata().applyMediaIOWriteDefaults();
        }

        auto *cmdOpen = new MediaIOCommandOpen();
        cmdOpen->mode = mode;
        cmdOpen->config = _config;
        cmdOpen->pendingMediaDesc = _pendingMediaDesc;
        cmdOpen->pendingMetadata = _pendingMetadata;
        cmdOpen->pendingAudioDesc = _pendingAudioDesc;
        cmdOpen->videoTracks = _pendingVideoTracks;
        cmdOpen->audioTracks = _pendingAudioTracks;

        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdOpen);
        Error err = submitAndWait(cmd);
        if(err.isOk()) {
                _mode = mode;
                _mediaDesc = cmdOpen->mediaDesc;
                _audioDesc = cmdOpen->audioDesc;
                _metadata = cmdOpen->metadata;
                _frameRate = cmdOpen->frameRate;
                _canSeek = cmdOpen->canSeek;
                _frameCount = cmdOpen->frameCount;
                _currentFrame = 0;
                _originTime = TimeStamp::now();
                _writeFrameCount = 0;
                _step = cmdOpen->defaultStep;
                _defaultSeekMode = cmdOpen->defaultSeekMode;
                if(!_prefetchDepthExplicit) {
                        _prefetchDepth = cmdOpen->defaultPrefetchDepth;
                        if(_prefetchDepth < 1) _prefetchDepth = 1;
                }
                _writeDepth = cmdOpen->defaultWriteDepth;
                if(_writeDepth < 1) _writeDepth = 1;
        } else {
                // Open failed — give the task a chance to clean up any
                // partially-allocated resources via its Close handler.
                // Backends must tolerate Close from a failed-open state.
                auto *cmdClose = new MediaIOCommandClose();
                MediaIOCommand::Ptr closeCmd = MediaIOCommand::Ptr::takeOwnership(cmdClose);
                submitAndWait(closeCmd);  // ignore close error
        }
        return err;
}

Error MediaIO::close() {
        if(!isOpen()) return Error::NotOpen;

        auto *cmdClose = new MediaIOCommandClose();
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdClose);
        Error err = submitAndWait(cmd);

        // Wait for any in-flight strand tasks (e.g. trailing reads) to drain
        // before resetting state.
        _strand.waitForIdle();

        // Drain any unconsumed read results
        _readResultQueue.clear();
        _pendingReadCount.setValue(0);
        _pendingWriteCount.setValue(0);

        // Reset cache regardless of close result
        _mode = NotOpen;
        _mediaDesc = MediaDesc();
        _audioDesc = AudioDesc();
        _metadata = Metadata();
        _frameRate = FrameRate();
        _canSeek = false;
        _frameCount = 0;
        _currentFrame = 0;
        _defaultSeekMode = SeekExact;
        _prefetchDepth = 1;
        _prefetchDepthExplicit = false;
        _writeDepth = 4;
        _atEnd = false;

        // Clear the benchmark-enable latch so a reopen with a fresh
        // config can toggle it back on without carrying state from the
        // previous run.  The stamp IDs are re-registered from the
        // resolved name in the next open() anyway.
        _benchmarkEnabled = false;
        return err;
}

// ============================================================================
// Pre-open setters
// ============================================================================

Error MediaIO::setMediaDesc(const MediaDesc &desc) {
        if(isOpen()) return Error::AlreadyOpen;
        _pendingMediaDesc = desc;
        return Error::Ok;
}

Error MediaIO::setAudioDesc(const AudioDesc &desc) {
        if(isOpen()) return Error::AlreadyOpen;
        _pendingAudioDesc = desc;
        return Error::Ok;
}

Error MediaIO::setMetadata(const Metadata &meta) {
        if(isOpen()) return Error::AlreadyOpen;
        _pendingMetadata = meta;
        return Error::Ok;
}

Error MediaIO::setVideoTracks(const List<int> &tracks) {
        if(isOpen()) return Error::AlreadyOpen;
        _pendingVideoTracks = tracks;
        return Error::Ok;
}

Error MediaIO::setAudioTracks(const List<int> &tracks) {
        if(isOpen()) return Error::AlreadyOpen;
        _pendingAudioTracks = tracks;
        return Error::Ok;
}

void MediaIO::setPrefetchDepth(int n) {
        if(n < 1) n = 1;
        _prefetchDepth = n;
        _prefetchDepthExplicit = true;
}

// ============================================================================
// Frame I/O
// ============================================================================

bool MediaIO::frameAvailable() const {
        // True when there's a result waiting to be consumed.
        return !_readResultQueue.isEmpty();
}

int MediaIO::readyReads() const {
        return static_cast<int>(_readResultQueue.size());
}

int MediaIO::pendingReads() const {
        return _pendingReadCount.value();
}

int MediaIO::pendingWrites() const {
        return _pendingWriteCount.value();
}

int MediaIO::writesAccepted() const {
        int used = _pendingWriteCount.value();
        if(_task != nullptr) used += _task->pendingOutput();
        int avail = _writeDepth - used;
        return avail > 0 ? avail : 0;
}

Error MediaIO::readFrame(Frame::Ptr &frame, bool block) {
        if(!isOpen()) return Error::NotOpen;
        // readFrame() pulls a frame out of the MediaIO — the caller is
        // consuming the backend's output, so the backend must have
        // been opened in Output (source) or InputAndOutput mode.
        if(_mode != Output && _mode != InputAndOutput) return Error::NotSupported;

        // Once EOF has been hit, every subsequent read returns EOF
        // without going down to the backend.  Cleared on seek/close.
        if(_atEnd) return Error::EndOfFile;

        MediaIOCommand::Ptr resultCmd;
        bool gotResult = _readResultQueue.popOrFail(resultCmd);
        if(!gotResult) {
                // Top up the in-flight read queue to the desired depth.
                while(_pendingReadCount.value() < _prefetchDepth) {
                        submitReadCommand();
                }
                if(!block) return Error::TryAgain;
                // Block on the result queue until something arrives.
                auto [popped, popErr] = _readResultQueue.pop();
                if(popErr.isError()) return popErr;
                resultCmd = popped;
        } else {
                // We just consumed a prefetched result; top up again so
                // the next call has work waiting.
                while(_pendingReadCount.value() < _prefetchDepth) {
                        submitReadCommand();
                }
        }

        auto *cmdRead = static_cast<MediaIOCommandRead *>(resultCmd.modify());

        // If the backend pushed a mid-stream descriptor change, update
        // our cache and notify listeners.  We do this BEFORE handing the
        // frame back so the user sees the new descriptors immediately.
        if(cmdRead->mediaDescChanged) {
                _mediaDesc = cmdRead->updatedMediaDesc;
                _frameRate = _mediaDesc.frameRate();
                if(!_mediaDesc.audioList().isEmpty()) {
                        _audioDesc = _mediaDesc.audioList()[0];
                }
                _metadata = _mediaDesc.metadata();
                descriptorChangedSignal.emit();
        }

        if(cmdRead->result.isOk()) {
                frame = std::move(cmdRead->frame);
                _currentFrame = cmdRead->currentFrame;
                // Stamp the current frame number into the frame's metadata
                // so downstream consumers know which frame it is.
                if(frame.isValid()) {
                        frame.modify()->metadata().set(
                                Metadata::FrameNumber, _currentFrame);
                        if(cmdRead->mediaDescChanged) {
                                frame.modify()->metadata().set(
                                        Metadata::MediaDescChanged, true);
                        }
                        // Auto-stamp MediaTimeStamp on each essence if the
                        // backend did not provide one.
                        {
                                int64_t ns = _frameRate.cumulativeTicks(
                                        INT64_C(1000000000), _currentFrame);
                                TimeStamp synTs = _originTime +
                                        Duration::fromNanoseconds(ns);
                                MediaTimeStamp synMts(synTs,
                                        ClockDomain::Synthetic);
                                for(size_t i = 0;
                                    i < frame->imageList().size(); ++i) {
                                        const Image::Ptr &img =
                                                frame->imageList()[i];
                                        if(img.isValid() &&
                                           !img->metadata()
                                                .get(Metadata::MediaTimeStamp)
                                                .get<MediaTimeStamp>()
                                                .isValid()) {
                                                frame.modify()->imageList()[i]
                                                        .modify()->metadata()
                                                        .set(Metadata::MediaTimeStamp,
                                                             synMts);
                                        }
                                }
                                for(size_t i = 0;
                                    i < frame->audioList().size(); ++i) {
                                        const Audio::Ptr &aud =
                                                frame->audioList()[i];
                                        if(aud.isValid() &&
                                           !aud->metadata()
                                                .get(Metadata::MediaTimeStamp)
                                                .get<MediaTimeStamp>()
                                                .isValid()) {
                                                frame.modify()->audioList()[i]
                                                        .modify()->metadata()
                                                        .set(Metadata::MediaTimeStamp,
                                                             synMts);
                                        }
                                }
                        }
                }
        } else if(cmdRead->result == Error::EndOfFile) {
                // Latch EOF — stop submitting prefetches.  Drain any
                // already-queued results so we don't return them after
                // signalling EOF (the backend has said it's done).
                _atEnd = true;
                _strand.cancelPending();
                MediaIOCommand::Ptr drop;
                while(_readResultQueue.popOrFail(drop)) {}
        }
        return cmdRead->result;
}

bool MediaIO::isIdle() const {
        return !_strand.isBusy();
}

size_t MediaIO::cancelPending() {
        if(!isOpen()) return 0;
        // Cancel anything queued in the strand (the Strand's per-task
        // cancel callbacks balance any reference counts on our side,
        // such as _pendingReadCount).  Any prefetched read results that
        // the worker already pushed are also discarded so the next
        // readFrame() submits fresh work.
        size_t cancelled = _strand.cancelPending();
        size_t dropped = 0;
        MediaIOCommand::Ptr drop;
        while(_readResultQueue.popOrFail(drop)) dropped++;
        return cancelled + dropped;
}

Error MediaIO::writeFrame(const Frame::Ptr &frame, bool block) {
        if(!isOpen()) return Error::NotOpen;
        // writeFrame() pushes a frame into the MediaIO — the caller is
        // feeding the backend, so the backend must have been opened in
        // Input (sink) or InputAndOutput mode.
        if(_mode != Input && _mode != InputAndOutput) return Error::NotSupported;

        // Non-blocking capacity gate.  Per the documented contract,
        // non-blocking writeFrame only queues when @ref writesAccepted
        // reports capacity; when the queue is full we refuse up-front
        // with @c TryAgain so the caller can retry after a
        // @c frameWanted signal instead of letting the queue grow
        // unbounded behind its back.  Blocking writes deliberately
        // skip this gate — the caller is already pacing itself by
        // waiting on the future.
        if(!block && writesAccepted() <= 0) {
                return Error::TryAgain;
        }

        auto *cmdWrite = new MediaIOCommandWrite();
        cmdWrite->frame = frame;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdWrite);

        // Auto-stamp MediaTimeStamp on each essence if the caller
        // did not provide one.
        if(cmdWrite->frame.isValid()) {
                int64_t ns = _frameRate.cumulativeTicks(
                        INT64_C(1000000000), _writeFrameCount);
                TimeStamp synTs = _originTime +
                        Duration::fromNanoseconds(ns);
                MediaTimeStamp synMts(synTs, ClockDomain::Synthetic);
                for(size_t i = 0;
                    i < cmdWrite->frame->imageList().size(); ++i) {
                        const Image::Ptr &img =
                                cmdWrite->frame->imageList()[i];
                        if(img.isValid() &&
                           !img->metadata()
                                .get(Metadata::MediaTimeStamp)
                                .get<MediaTimeStamp>()
                                .isValid()) {
                                cmdWrite->frame.modify()->imageList()[i]
                                        .modify()->metadata()
                                        .set(Metadata::MediaTimeStamp,
                                             synMts);
                        }
                }
                for(size_t i = 0;
                    i < cmdWrite->frame->audioList().size(); ++i) {
                        const Audio::Ptr &aud =
                                cmdWrite->frame->audioList()[i];
                        if(aud.isValid() &&
                           !aud->metadata()
                                .get(Metadata::MediaTimeStamp)
                                .get<MediaTimeStamp>()
                                .isValid()) {
                                cmdWrite->frame.modify()->audioList()[i]
                                        .modify()->metadata()
                                        .set(Metadata::MediaTimeStamp,
                                             synMts);
                        }
                }
                _writeFrameCount++;
        }

        // Enqueue stamp — runs on the user thread right before the
        // command is handed to the strand.  This is the one stamp the
        // reader path has no analogue for because reads are pulled by
        // the strand worker rather than pushed from the user thread.
        //
        // We stamp via `cmdWrite->frame` rather than the caller's
        // const-ref `frame`; the former is the Ptr the command owns,
        // and the copy-on-write clone triggered by modify() only
        // affects our copy.  The caller's original Frame::Ptr stays
        // pristine.
        if(_benchmarkEnabled && cmdWrite->frame.isValid()) {
                ensureFrameBenchmark(cmdWrite->frame);
                Benchmark::Ptr &bmp = cmdWrite->frame.modify()->benchmark();
                if(bmp.isValid()) {
                        bmp.modify()->stamp(_idStampEnqueue);
                }
        }

        // Claim a pending-write slot before submit so pendingWrites()
        // reflects the new command immediately.  The strand task
        // releases the slot on completion, and the cancellation
        // callback releases it if the command is cancelled before it
        // runs.
        _pendingWriteCount.fetchAndAdd(1);

        Future<Error> future = _strand.submit(
                [this, cmd]() mutable {
                        MediaIOCommand *raw = cmd.modify();
                        auto *cw = static_cast<MediaIOCommandWrite *>(raw);

                        // Dequeue → taskBegin → executeCmd → taskEnd
                        // form the worker-side stamp sequence.
                        // Together with the enqueue stamp on the user
                        // thread, they cover queue-wait and work-time
                        // for every frame the writer sees.
                        if(_benchmarkEnabled && cw->frame.isValid()) {
                                // cw->frame is a non-const Ptr on a
                                // command we own, so modify() never
                                // clones here — the command holds the
                                // only live reference to this Frame
                                // object graph on the worker thread.
                                Benchmark::Ptr &bmp = cw->frame.modify()->benchmark();
                                if(bmp.isValid()) {
                                        Benchmark *bm = bmp.modify();
                                        bm->stamp(_idStampDequeue);
                                        bm->stamp(_idStampTaskBegin);
                                        _task->_activeBenchmark = bm;
                                        Error err = _task->executeCmd(*cw);
                                        _task->_activeBenchmark = nullptr;
                                        bm->stamp(_idStampTaskEnd);
                                        submitBenchmarkIfSink(cw->frame);
                                        if(err.isOk()) {
                                                _rateTracker.record(
                                                        frameByteSize(cw->frame));
                                                frameWantedSignal.emit();
                                        } else {
                                                writeErrorSignal.emit(err);
                                        }
                                        _pendingWriteCount.fetchAndSub(1);
                                        return err;
                                }
                        }

                        Error err = _task->executeCmd(*cw);
                        if(err.isOk()) {
                                _rateTracker.record(
                                        frameByteSize(cw->frame));
                                frameWantedSignal.emit();
                        } else {
                                writeErrorSignal.emit(err);
                        }
                        _pendingWriteCount.fetchAndSub(1);
                        return err;
                },
                [this]() {
                        // Cancellation cleanup: balance the slot we
                        // claimed above so pendingWrites() stays
                        // accurate if the command is dropped before
                        // running.
                        _pendingWriteCount.fetchAndSub(1);
                });

        // Non-blocking submit: the command is now in the strand's hands
        // and any failure arrives asynchronously on @c writeErrorSignal.
        if(!block) return Error::Ok;

        auto r = future.result();
        if(r.second().isError()) return r.second();
        Error err = r.first();
        if(err.isOk()) {
                _currentFrame = cmdWrite->currentFrame;
                _frameCount = cmdWrite->frameCount;
        }
        return err;
}

Error MediaIO::sendParams(const String &name, const MediaIOParams &params, MediaIOParams *result) {
        if(!isOpen()) return Error::NotOpen;

        // Base-class Benchmark commands short-circuit before reaching
        // the strand so they don't queue behind real work — callers
        // typically poll these to render a status display.  Without an
        // attached reporter there's nothing useful to report, so
        // surface NotSupported.
        if(name == ParamBenchmarkReport.name()) {
                if(_benchmarkReporter == nullptr) return Error::NotSupported;
                if(result != nullptr) {
                        result->set(ParamBenchmarkReport, _benchmarkReporter->summaryReport());
                }
                return Error::Ok;
        }
        if(name == ParamBenchmarkReset.name()) {
                if(_benchmarkReporter == nullptr) return Error::NotSupported;
                _benchmarkReporter->reset();
                return Error::Ok;
        }

        auto *cmdParams = new MediaIOCommandParams();
        cmdParams->name = name;
        cmdParams->params = params;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdParams);
        Error err = submitAndWait(cmd);
        if(result != nullptr) {
                *result = std::move(cmdParams->result);
        }
        return err;
}

MediaIOStats MediaIO::stats() {
        if(!isOpen()) return MediaIOStats();
        auto *cmdStats = new MediaIOCommandStats();
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdStats);
        // Urgent: telemetry pollers (UI overlays, live monitors) call
        // stats() on a cadence and shouldn't block behind a deep queue
        // of prefetched reads or other in-flight I/O.  Front-inserting
        // into the strand caps the latency at "one task duration"
        // instead of "full queue drain".
        Error err = submitAndWait(cmd, /*urgent=*/true);
        if(err.isError()) return MediaIOStats();
        // Let the base-class telemetry overlay the backend's stats.
        // populateStandardStats() writes the standard keys after the
        // backend has populated anything backend-specific, so drivers
        // that still set their own BytesPerSecond / FramesDropped
        // (legacy code) get overwritten by the authoritative base
        // values.
        populateStandardStats(cmdStats->stats);
        return std::move(cmdStats->stats);
}

// ============================================================================
// Navigation
// ============================================================================

void MediaIO::setStep(int val) {
        if(val == _step) return;
        // Outstanding prefetched reads were submitted with the old step;
        // they're stale relative to the new direction/speed.  Cancel them
        // and discard any results that already came back.  Also clear
        // the EOF latch — the new direction may make more frames available
        // (e.g. flipping from forward-EOF to reverse).
        if(isOpen()) {
                _strand.cancelPending();
                MediaIOCommand::Ptr drop;
                while(_readResultQueue.popOrFail(drop)) {}
                _atEnd = false;
        }
        _step = val;
}

Error MediaIO::seekToFrame(int64_t frameNumber, SeekMode mode) {
        if(!isOpen()) return Error::NotOpen;
        if(!_canSeek) return Error::IllegalSeek;

        // Cancel any prefetched reads from the old position before
        // submitting the seek.  Otherwise the next read would return
        // a stale frame from the pre-seek queue.
        _strand.cancelPending();
        MediaIOCommand::Ptr drop;
        while(_readResultQueue.popOrFail(drop)) {}
        // Seeking past EOF clears the EOF latch — the new position may
        // be re-readable.
        _atEnd = false;

        // Resolve Default to the task's preferred mode so the backend
        // always sees a concrete mode.
        if(mode == SeekDefault) mode = _defaultSeekMode;

        auto *cmdSeek = new MediaIOCommandSeek();
        cmdSeek->frameNumber = frameNumber;
        cmdSeek->mode = mode;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdSeek);
        Error err = submitAndWait(cmd);
        if(err.isOk()) {
                _currentFrame = cmdSeek->currentFrame;
        }
        return err;
}

PROMEKI_NAMESPACE_END
