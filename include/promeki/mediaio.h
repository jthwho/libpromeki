/**
 * @file      mediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cassert>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/queue.h>
#include <promeki/atomic.h>
#include <promeki/sharedptr.h>
#include <promeki/promise.h>
#include <promeki/future.h>
#include <promeki/strand.h>
#include <promeki/frame.h>
#include <promeki/mediadesc.h>
#include <promeki/audiodesc.h>
#include <promeki/metadata.h>
#include <promeki/framerate.h>
#include <promeki/variantdatabase.h>

/**
 * @brief Macro to register a MediaIO backend at static initialization time.
 * @ingroup proav
 *
 * The backend class must provide a static `formatDesc()` method returning
 * a MediaIO::FormatDesc.
 *
 * @param ClassName The MediaIOTask subclass to register.
 */
#define PROMEKI_REGISTER_MEDIAIO(ClassName) \
        [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_mediaio_, PROMEKI_UNIQUE_ID) = \
                MediaIO::registerFormat(ClassName::formatDesc());

PROMEKI_NAMESPACE_BEGIN

class IODevice;
class ThreadPool;
class MediaIOTask;

// ============================================================================
// Free types — used by both commands and MediaIO so commands can be
// defined before MediaIO without circular dependencies.
// ============================================================================

/** @brief Open mode for a media resource. */
enum MediaIOMode {
        MediaIO_NotOpen = 0, ///< @brief Resource is not open.
        MediaIO_Reader,      ///< @brief Open for reading.
        MediaIO_Writer,      ///< @brief Open for writing.
        MediaIO_ReadWrite    ///< @brief Open for both reading and writing.
};

/**
 * @brief Seek-mode hint for seekToFrame().
 *
 * Use the @c MediaIO::Seek* constants (e.g. @c MediaIO::SeekExact) at
 * call sites — the free enum exists so commands defined before
 * MediaIO can reference the type.  Most backends interpret these to
 * decide whether to land on an exact frame or the nearest keyframe.
 * Default lets the task pick the most efficient mode (Exact for
 * sample-accurate sources, KeyframeBefore for compressed streams with
 * B-frames, etc.).
 */
enum MediaIOSeekMode {
        MediaIO_SeekDefault = 0,        ///< @brief Backend picks (resolved per task).
        MediaIO_SeekExact,              ///< @brief Land on the exact requested frame.
        MediaIO_SeekNearestKeyframe,    ///< @brief Land on the nearest keyframe in either direction.
        MediaIO_SeekKeyframeBefore,     ///< @brief Land on the closest keyframe at or before.
        MediaIO_SeekKeyframeAfter       ///< @brief Land on the closest keyframe at or after.
};

/** @brief Phantom tag for the MediaIO config StringRegistry. */
struct MediaIOConfigTag {};

/** @brief Configuration database for MediaIO. */
using MediaIOConfig = VariantDatabase<MediaIOConfigTag>;

/** @brief Configuration ID type. */
using MediaIOConfigID = MediaIOConfig::ID;

/** @brief Phantom tag for the MediaIO stats StringRegistry. */
struct MediaIOStatsTag {};

/**
 * @brief Statistics container for MediaIO backends.
 * @ingroup proav
 *
 * A distinct VariantDatabase type for runtime stats reported by
 * backends.  Has its own StringRegistry so stats keys don't collide
 * with config or param keys.  Backends populate instances
 * of this in @c executeCmd(MediaIOCommandStats&); users receive them
 * from @c MediaIO::stats().
 *
 * Standard key names are defined as static members; backends are
 * free to add their own backend-specific keys for data not covered
 * by the standard set.
 */
class MediaIOStats : public VariantDatabase<MediaIOStatsTag> {
        public:
                using Base = VariantDatabase<MediaIOStatsTag>;
                using Base::Base;

                /// @brief int64_t — total frames dropped since open.
                static inline const ID FramesDropped{"FramesDropped"};
                /// @brief int64_t — total frames repeated due to underrun.
                static inline const ID FramesRepeated{"FramesRepeated"};
                /// @brief int64_t — total frames that arrived late.
                static inline const ID FramesLate{"FramesLate"};
                /// @brief int64_t — current depth of internal buffer.
                static inline const ID QueueDepth{"QueueDepth"};
                /// @brief int64_t — capacity of internal buffer.
                static inline const ID QueueCapacity{"QueueCapacity"};
                /// @brief double — current data rate.
                static inline const ID BytesPerSecond{"BytesPerSecond"};
                /// @brief double — average end-to-end latency.
                static inline const ID AverageLatencyMs{"AverageLatencyMs"};
                /// @brief double — peak observed latency.
                static inline const ID PeakLatencyMs{"PeakLatencyMs"};
                /// @brief String — most recent error description.
                static inline const ID LastErrorMessage{"LastErrorMessage"};
};

/** @brief Phantom tag for the MediaIO parameterized-command StringRegistry. */
struct MediaIOParamsTag {};

/**
 * @brief Parameter / result container for MediaIO parameterized commands.
 *
 * A distinct VariantDatabase type for backend-specific parameterized
 * command payloads.  Has its own StringRegistry so param keys don't
 * collide with config or stats keys.  Has no predefined keys — the
 * key set is entirely backend-defined.  Backends that want to expose
 * named parameters typically define static const IDs on their task
 * class.
 */
using MediaIOParams = VariantDatabase<MediaIOParamsTag>;

/** @brief Parameterized command ID type. */
using MediaIOParamsID = MediaIOParams::ID;

// ============================================================================
// Command hierarchy — all I/O operations are submitted as commands to the
// MediaIO worker thread.  Each command carries inputs (set by MediaIO) and
// outputs (set by the task) so that all data flow is lock-free.
// ============================================================================

/**
 * @brief Boilerplate for derived MediaIOCommand classes.
 * @ingroup proav
 *
 * Injects the `type()` override and an asserting `_promeki_clone()`
 * required by the SharedPtr proxy machinery.  Use it as the first
 * line of every concrete command class.
 *
 * @param NAME      The class name.
 * @param TYPE_TAG  The MediaIOCommand::Type enum value.
 */
#define PROMEKI_MEDIAIO_COMMAND(NAME, TYPE_TAG)                       \
        public:                                                       \
                Type type() const override { return TYPE_TAG; }       \
                NAME *_promeki_clone() const {                        \
                        assert(false && #NAME " should not be cloned"); \
                        return nullptr;                               \
                }

/**
 * @brief Base class for MediaIO commands.
 * @ingroup proav
 *
 * Commands are shared via SharedPtr<MediaIOCommand, false> with COW
 * disabled — derived types are owned via the proxy path and never
 * cloned.  The Promise is fulfilled by the worker thread when the
 * command completes; the caller waits on the associated Future.
 */
class MediaIOCommand {
        public:
                /** @brief Concrete command type. */
                enum Type {
                        Open,   ///< @brief MediaIOCommandOpen
                        Close,  ///< @brief MediaIOCommandClose
                        Read,   ///< @brief MediaIOCommandRead
                        Write,  ///< @brief MediaIOCommandWrite
                        Seek,   ///< @brief MediaIOCommandSeek
                        Params, ///< @brief MediaIOCommandParams
                        Stats   ///< @brief MediaIOCommandStats
                };

                /** @brief Shared pointer type for command sharing. */
                using Ptr = SharedPtr<MediaIOCommand, false>;

                /** @brief Reference count (managed by SharedPtrProxy). */
                RefCount _promeki_refct;

                /**
                 * @brief Clone hook required by SharedPtr machinery.
                 *
                 * Commands are never cloned (COW is disabled).  The
                 * implementation asserts to catch accidental clone calls.
                 */
                MediaIOCommand *_promeki_clone() const {
                        assert(false && "MediaIOCommand should never be cloned");
                        return nullptr;
                }

                /** @brief Constructs an empty command. */
                MediaIOCommand() = default;

                /** @brief Virtual destructor for polymorphic ownership. */
                virtual ~MediaIOCommand() = default;

                /**
                 * @brief Returns the concrete command type.
                 * @return The Type enum value.
                 */
                virtual Type type() const = 0;

                /** @brief Promise fulfilled by the worker when the command completes. */
                Promise<Error> promise;
};

/**
 * @brief Command to open a media resource.
 * @ingroup proav
 */
class MediaIOCommandOpen : public MediaIOCommand {
        PROMEKI_MEDIAIO_COMMAND(MediaIOCommandOpen, Open)
        public:
                // ---- Inputs ----
                MediaIOMode             mode = MediaIO_NotOpen;
                MediaIOConfig           config;
                MediaDesc               pendingMediaDesc;
                Metadata                pendingMetadata;
                AudioDesc               pendingAudioDesc;
                List<int>               videoTracks;     ///< @brief Empty = task default (first video track).
                List<int>               audioTracks;     ///< @brief Empty = task default (first audio track).

                // ---- Outputs ----
                MediaDesc               mediaDesc;
                AudioDesc               audioDesc;
                Metadata                metadata;
                FrameRate               frameRate;
                bool                    canSeek = false;
                int64_t                 frameCount = 0;
                int                     defaultStep = 1;             ///< @brief Backend's preferred default step.
                int                     defaultPrefetchDepth = 1;    ///< @brief Backend's preferred prefetch depth.
                MediaIOSeekMode         defaultSeekMode = MediaIO_SeekExact; ///< @brief Backend's resolution of @c SeekDefault.
};

/**
 * @brief Command to close a media resource.
 * @ingroup proav
 */
class MediaIOCommandClose : public MediaIOCommand {
        PROMEKI_MEDIAIO_COMMAND(MediaIOCommandClose, Close)
};

/**
 * @brief Command to read the next frame.
 * @ingroup proav
 */
class MediaIOCommandRead : public MediaIOCommand {
        PROMEKI_MEDIAIO_COMMAND(MediaIOCommandRead, Read)
        public:
                // ---- Input ----
                int                     step = 1;

                // ---- Outputs ----
                Frame::Ptr              frame;
                int64_t                 currentFrame = 0;
                Error                   result;       ///< @brief Set by worker; carries success/error/EOF.

                // ---- Optional outputs (mid-stream descriptor change) ----
                /**
                 * @brief Set true when the backend wants to update the
                 * MediaDesc that MediaIO caches.
                 *
                 * Used for variable-frame-rate or segmented streams whose
                 * format changes mid-playback.  When true, MediaIO copies
                 * @c updatedMediaDesc into its cache, stamps
                 * @c Metadata::MediaDescChanged on the returned frame,
                 * and emits the @c descriptorChanged signal.
                 */
                bool                    mediaDescChanged = false;
                /** @brief New MediaDesc — only valid when @c mediaDescChanged is true. */
                MediaDesc               updatedMediaDesc;
};

/**
 * @brief Command to write a frame.
 * @ingroup proav
 */
class MediaIOCommandWrite : public MediaIOCommand {
        PROMEKI_MEDIAIO_COMMAND(MediaIOCommandWrite, Write)
        public:
                // ---- Input ----
                Frame::Ptr              frame;

                // ---- Outputs ----
                int64_t                 currentFrame = 0;
                int64_t                 frameCount = 0;
};

/**
 * @brief Command to seek to a frame position.
 * @ingroup proav
 */
class MediaIOCommandSeek : public MediaIOCommand {
        PROMEKI_MEDIAIO_COMMAND(MediaIOCommandSeek, Seek)
        public:
                // ---- Inputs ----
                int64_t                 frameNumber = 0;
                MediaIOSeekMode         mode = MediaIO_SeekDefault;

                // ---- Output ----
                int64_t                 currentFrame = 0;
};

/**
 * @brief Backend-specific parameterized command.
 * @ingroup proav
 *
 * Carries an arbitrary operation name plus parameter and result
 * containers, allowing backends to expose operations beyond the
 * standard open/close/read/write/seek set.  Examples: setting
 * device gain, querying device temperature, triggering a one-shot
 * capture, retrieving codec parameters.
 *
 * Backends override @c executeCmd(MediaIOCommandParams &) and
 * dispatch on @c name.  Unrecognized names should return
 * @c Error::NotSupported.  Default implementation returns
 * @c Error::NotSupported.
 */
class MediaIOCommandParams : public MediaIOCommand {
        PROMEKI_MEDIAIO_COMMAND(MediaIOCommandParams, Params)
        public:
                // ---- Inputs ----
                String                  name;       ///< @brief Operation name (backend-defined).
                MediaIOParams            params;     ///< @brief Operation parameters.

                // ---- Output ----
                MediaIOParams            result;     ///< @brief Operation result fields.
};

/**
 * @brief Backend statistics query command.
 * @ingroup proav
 *
 * Backends populate @c stats with whatever runtime metrics they
 * track (frames dropped, queue depth, bytes/sec, latency, last
 * error, etc.).  See @c MediaIOStats for the standard key
 * conventions.  Backends are free to add backend-specific keys.
 *
 * The default implementation returns Error::Ok with an empty
 * @c stats.
 */
class MediaIOCommandStats : public MediaIOCommand {
        PROMEKI_MEDIAIO_COMMAND(MediaIOCommandStats, Stats)
        public:
                // ---- Output ----
                MediaIOStats            stats;      ///< @brief Stats keys/values populated by the backend.
};

// ============================================================================
// MediaIO controller class
// ============================================================================

/**
 * @brief Controller for media I/O backends.
 * @ingroup proav
 *
 * MediaIO drives a MediaIOTask backend through a serialized command
 * queue running on a shared ThreadPool.  All operations — open, close,
 * read, write, seek — are dispatched as MediaIOCommand instances.
 * The task implements only the executeCmd virtuals; MediaIO handles
 * threading, signals, and result caching.
 *
 * Backends register themselves via PROMEKI_REGISTER_MEDIAIO and are
 * instantiated through the config-driven factory create() or the
 * convenience helpers createForFileRead() / createForFileWrite().
 *
 * @par Cached state
 *
 * MediaIO caches the descriptor and navigation state reported by the
 * task (mediaDesc, audioDesc, metadata, frameRate, canSeek, frameCount,
 * currentFrame).  The cache is updated only on the user thread after
 * a command's future resolves, eliminating the need for mutexes around
 * task data.
 *
 * @par Non-blocking I/O
 *
 * readFrame() and writeFrame() take an optional @p block parameter
 * (default true).  When false, the command is enqueued and the call
 * returns Error::TryAgain immediately.  Use frameAvailable() to poll
 * for ready frames and isIdle() to check whether the strand is busy.
 *
 * @par Threading model
 *
 * MediaIO is intended to be driven from a single user thread.
 * Public methods (open/close/readFrame/writeFrame/seekToFrame/setStep
 * etc.) are not safe to call concurrently from multiple threads.
 * The cached state (mediaDesc, frameCount, currentFrame, ...) is
 * read and written only from the user thread; backend execution
 * happens on the strand worker thread.  Cross-thread notifications
 * use signals (@c frameAvailable, @c frameWanted, @c writeError),
 * which the signal/slot system marshals via the receiver's
 * EventLoop.
 */
class MediaIO : public ObjectBase {
        PROMEKI_OBJECT(MediaIO, ObjectBase)
        public:
                /** @brief Open mode (alias for MediaIOMode). */
                using Mode = MediaIOMode;

                static constexpr Mode NotOpen   = MediaIO_NotOpen;
                static constexpr Mode Reader    = MediaIO_Reader;
                static constexpr Mode Writer    = MediaIO_Writer;
                static constexpr Mode ReadWrite = MediaIO_ReadWrite;

                /** @brief Seek mode (alias for MediaIOSeekMode). */
                using SeekMode = MediaIOSeekMode;

                static constexpr SeekMode SeekDefault         = MediaIO_SeekDefault;
                static constexpr SeekMode SeekExact           = MediaIO_SeekExact;
                static constexpr SeekMode SeekNearestKeyframe = MediaIO_SeekNearestKeyframe;
                static constexpr SeekMode SeekKeyframeBefore  = MediaIO_SeekKeyframeBefore;
                static constexpr SeekMode SeekKeyframeAfter   = MediaIO_SeekKeyframeAfter;

                /** @brief Configuration database type. */
                using Config = MediaIOConfig;

                /** @brief Configuration ID type. */
                using ConfigID = MediaIOConfigID;

                /** @brief Frame count is not yet known. */
                static constexpr int64_t FrameCountUnknown  = -1;

                /** @brief Source is unbounded (generators, live devices). */
                static constexpr int64_t FrameCountInfinite = -2;

                /** @brief Frame count unavailable due to error. */
                static constexpr int64_t FrameCountError    = -3;

                /** @brief Config ID for the filename. */
                static const ConfigID ConfigFilename;

                /** @brief Config ID for the format type name. */
                static const ConfigID ConfigType;

                /**
                 * @brief Describes a registered media I/O backend.
                 */
                struct FormatDesc {
                        /** @brief Factory function that creates a new task instance. */
                        using CreateFunc = std::function<MediaIOTask *()>;
                        /** @brief Returns the default configuration for this backend. */
                        using DefaultConfigFunc = std::function<Config ()>;
                        /**
                         * @brief Returns the metadata schema this backend honors.
                         *
                         * The returned @ref Metadata lists every
                         * @ref Metadata::ID the backend understands, pre-populated
                         * with empty / default values.  It is the metadata
                         * counterpart to @ref DefaultConfigFunc — callers that
                         * want to know which metadata keys are honored (for
                         * @c --help dumps, GUI editors, etc.) can walk the
                         * returned container without having to hard-code a
                         * per-backend list.  Backends that do not consume
                         * any container-level metadata may leave this
                         * callable null — @ref MediaIO::defaultMetadata
                         * then returns an empty @ref Metadata.
                         */
                        using DefaultMetadataFunc = std::function<Metadata ()>;
                        /**
                         * @brief Optional content probe via IODevice.
                         *
                         * @param device An open, seekable IODevice positioned at 0.
                         * @return true if this backend can handle the content.
                         */
                        using ProbeFunc = std::function<bool(IODevice *device)>;
                        /**
                         * @brief Optional callback that lists available device instances.
                         *
                         * Returns the locator strings (suitable for use as
                         * @c MediaIO::ConfigFilename) for each available
                         * instance of this backend.  File-based backends
                         * generally leave this null; device backends
                         * provide an implementation that scans the system.
                         */
                        using EnumerateFunc = std::function<StringList()>;

                        String              name;            ///< @brief Backend name (e.g. "MXF", "VideoDevice").
                        String              description;     ///< @brief Human-readable description.
                        StringList          extensions;      ///< @brief Supported file extensions (no dots).
                        bool                canRead;         ///< @brief Whether the backend supports reading.
                        bool                canWrite;        ///< @brief Whether the backend supports writing.
                        bool                canReadWrite;    ///< @brief Whether the backend supports bidirectional mode.
                        CreateFunc          create;          ///< @brief Backend factory.
                        DefaultConfigFunc   defaultConfig;   ///< @brief Default configuration provider.
                        DefaultMetadataFunc defaultMetadata; ///< @brief Honored metadata schema (may be null).
                        ProbeFunc           canHandleDevice; ///< @brief Content-based probe.
                        EnumerateFunc       enumerate;       ///< @brief Instance enumerator.
                };

                using FormatDescList = List<FormatDesc>;

                /**
                 * @brief Registers a backend format descriptor.
                 * @param desc The FormatDesc to register.
                 * @return The index of the newly registered entry.
                 */
                static int registerFormat(const FormatDesc &desc);

                /**
                 * @brief Returns the list of all registered format descriptors.
                 * @return A const reference to the registry list.
                 */
                static const FormatDescList &registeredFormats();

                /**
                 * @brief Returns the default configuration for the named backend.
                 * @param typeName The registered backend name (e.g. "TPG").
                 * @return A Config populated with default values.
                 */
                static Config defaultConfig(const String &typeName);

                /**
                 * @brief Returns the metadata schema the named backend honors.
                 *
                 * The returned @ref Metadata contains one entry per
                 * @ref Metadata::ID the backend knows how to consume,
                 * each populated with an empty / default value.  Use
                 * it to discover which metadata keys a backend accepts
                 * (for example when building a @c --help dump or a
                 * config GUI) without having to hard-code a per-backend
                 * list.  Backends that do not consume any metadata
                 * return an empty container.
                 *
                 * @param typeName The registered backend name.
                 * @return A @ref Metadata listing the honored keys.
                 */
                static Metadata defaultMetadata(const String &typeName);

                /**
                 * @brief Creates a MediaIO instance from a configuration.
                 * @param config The configuration describing the desired backend.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr on failure.
                 */
                static MediaIO *create(const Config &config, ObjectBase *parent = nullptr);

                /**
                 * @brief Creates a MediaIO reader for the given filename.
                 * @param filename The path to the media file.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr on failure.
                 */
                static MediaIO *createForFileRead(const String &filename, ObjectBase *parent = nullptr);

                /**
                 * @brief Creates a MediaIO writer for the given filename.
                 * @param filename The path to the media file.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr on failure.
                 */
                static MediaIO *createForFileWrite(const String &filename, ObjectBase *parent = nullptr);

                /**
                 * @brief Returns the shared thread pool used for the worker.
                 *
                 * All MediaIO instances share a single static ThreadPool.
                 * Each instance has its own Strand on top of the pool, so
                 * different MediaIOs run concurrently while their own
                 * commands stay serialized.
                 *
                 * @par Sizing
                 *
                 * The default thread count is
                 * @c std::thread::hardware_concurrency().  Backends that
                 * block in @c executeCmd (e.g. live capture devices
                 * waiting for the next frame) hold a pool thread for the
                 * duration of the read.  If you have many such backends
                 * active simultaneously, the pool can starve.  Resize
                 * before opening the first MediaIO via:
                 *
                 * @code
                 * MediaIO::pool().setThreadCount(N);
                 * @endcode
                 *
                 * @return A reference to the static ThreadPool instance.
                 */
                static ThreadPool &pool();

                /**
                 * @brief Lists available instances for the named backend.
                 *
                 * For device-style backends (capture cards, video cameras),
                 * this returns the locator strings (e.g. @c "video0",
                 * @c "video1") that can be used as
                 * @c MediaIO::ConfigFilename.  Returns an empty list if
                 * the backend doesn't support enumeration or the type is
                 * unknown.
                 *
                 * @param typeName The registered backend name.
                 * @return A list of available instance locators.
                 */
                static StringList enumerate(const String &typeName);

                /**
                 * @brief Constructs a MediaIO with an optional parent.
                 * @param parent The parent object, or nullptr.
                 */
                MediaIO(ObjectBase *parent = nullptr);

                /**
                 * @brief Destructor.  Closes if still open and deletes the task.
                 */
                ~MediaIO() override;

                /**
                 * @brief Injects an externally constructed backend task.
                 *
                 * Normally MediaIO obtains its task from the registered
                 * factory via @c create().  Some backends, however, need
                 * constructor arguments that can't be expressed in a
                 * Config — for example, the SDL player task needs raw
                 * pointers to an SDLVideoWidget and an SDLAudioOutput
                 * supplied by the application.  For those cases the
                 * caller constructs the task directly (via a free
                 * factory function in the backend's library) and hands
                 * it to MediaIO via this method.
                 *
                 * The MediaIO must not be open when this is called, and
                 * must not already own a task.  Ownership of @p task
                 * transfers to the MediaIO, which will delete it in the
                 * destructor.  Passing @c nullptr is an error.
                 *
                 * @param task The pre-constructed backend task.
                 * @return Error::Ok on success, Error::AlreadyOpen if the
                 *         resource is open, or Error::Invalid if a task
                 *         is already adopted or @p task is null.
                 */
                Error adoptTask(MediaIOTask *task);

                /** @brief Returns the configuration. */
                const Config &config() const { return _config; }

                /** @brief Returns true if the resource is open. */
                bool isOpen() const { return _mode != NotOpen; }

                /** @brief Returns the current open mode. */
                Mode mode() const { return _mode; }

                /** @brief Returns the current step increment. */
                int step() const { return _step; }

                /**
                 * @brief Sets the step increment.
                 *
                 * The step value is copied into each CmdRead so the task
                 * can advance position accordingly.  Common values:
                 * - 1: normal forward (default)
                 * - -1: reverse
                 * - 2: 2x fast-forward
                 * - 0: hold (re-read same frame)
                 *
                 * If the new step differs from the old, any prefetched
                 * read commands (which were submitted with the old step)
                 * are cancelled and any pending read results are
                 * discarded.  Subsequent reads will use the new step.
                 *
                 * @param val The new step value.
                 */
                void setStep(int val);

                /**
                 * @brief Returns the current read prefetch depth.
                 *
                 * Reflects the task's preferred default after open(),
                 * or whatever the user set via setPrefetchDepth() if
                 * called explicitly.
                 *
                 * @return The current prefetch depth (≥ 1).
                 */
                int prefetchDepth() const { return _prefetchDepth; }

                /**
                 * @brief Sets the number of read commands to keep in flight.
                 *
                 * MediaIO will top up the strand queue to this many
                 * outstanding CmdReads in @c readFrame().  Larger values
                 * give the user thread more headroom for high-throughput
                 * sources (e.g. capture devices); a value of 1 is fine
                 * for files and lightweight sources.
                 *
                 * Calling this marks the depth as user-set; subsequent
                 * @c open() calls will not overwrite it with the task's
                 * default until @c close() resets the override.
                 *
                 * @param n New depth, clamped to ≥ 1.
                 */
                void setPrefetchDepth(int n);

                /**
                 * @brief Returns the task's preferred default seek mode.
                 *
                 * Captured from the backend during open().  Used to
                 * resolve @c MediaIO::SeekDefault into a concrete mode
                 * for each seekToFrame() call.
                 *
                 * @return The default seek mode for the current backend.
                 */
                SeekMode defaultSeekMode() const { return _defaultSeekMode; }

                /**
                 * @brief Selects which video tracks to decode (pre-open).
                 *
                 * An empty list means the backend uses its default
                 * (typically the first video track).  Backends without
                 * a track concept ignore this setting.
                 *
                 * @param tracks Track indices to decode.
                 * @return Error::Ok or Error::AlreadyOpen.
                 */
                Error setVideoTracks(const List<int> &tracks);

                /**
                 * @brief Selects which audio tracks to decode (pre-open).
                 *
                 * An empty list means the backend uses its default
                 * (typically the first audio track).  Backends without
                 * a track concept ignore this setting.
                 *
                 * @param tracks Track indices to decode.
                 * @return Error::Ok or Error::AlreadyOpen.
                 */
                Error setAudioTracks(const List<int> &tracks);

                /**
                 * @brief Opens the media resource.
                 *
                 * Starts the worker thread, enqueues a CmdOpen, and blocks
                 * until it completes.  On success, copies descriptor and
                 * navigation state from the command into the cache.
                 *
                 * @param mode Reader, Writer, or ReadWrite.
                 * @return Error::Ok on success, or an error.
                 */
                Error open(Mode mode);

                /**
                 * @brief Closes the media resource.
                 *
                 * Enqueues a CmdClose and blocks until the worker exits.
                 *
                 * @return Error::Ok on success, or an error.
                 */
                Error close();

                // ---- Cached descriptor accessors ----

                /**
                 * @brief Returns the cached media description.
                 *
                 * The reference is valid until the next call to
                 * @c open(), @c close(), or destruction.
                 */
                const MediaDesc &mediaDesc() const { return _mediaDesc; }

                /**
                 * @brief Returns the cached frame rate.
                 *
                 * Reference lifetime is the same as @c mediaDesc().
                 */
                const FrameRate &frameRate() const { return _frameRate; }

                /**
                 * @brief Returns the cached primary audio description.
                 *
                 * Reference lifetime is the same as @c mediaDesc().
                 */
                const AudioDesc &audioDesc() const { return _audioDesc; }

                /**
                 * @brief Returns the cached container metadata.
                 *
                 * Reference lifetime is the same as @c mediaDesc().
                 */
                const Metadata &metadata() const { return _metadata; }

                /**
                 * @brief Sets the media description for writing.
                 *
                 * Stored locally and passed to the task in the next CmdOpen.
                 *
                 * @param desc The media description.
                 * @return Error::Ok on success, or AlreadyOpen if open.
                 */
                Error setMediaDesc(const MediaDesc &desc);

                /**
                 * @brief Sets the audio description for writing.
                 *
                 * @param desc The audio description.
                 * @return Error::Ok on success, or AlreadyOpen if open.
                 */
                Error setAudioDesc(const AudioDesc &desc);

                /**
                 * @brief Sets the container-level metadata for writing.
                 *
                 * @param meta The metadata.
                 * @return Error::Ok on success, or AlreadyOpen if open.
                 */
                Error setMetadata(const Metadata &meta);

                // ---- Frame I/O ----

                /**
                 * @brief Returns true if a read result is waiting to be consumed.
                 *
                 * Specifically: a previously-submitted CmdRead has
                 * completed and its frame is sitting in the read result
                 * queue.  When true, @c readFrame() returns immediately
                 * without blocking.  When false, @c readFrame() will
                 * either block on the next prefetch or return
                 * @c Error::TryAgain in non-blocking mode.
                 *
                 * Equivalent to @c readyReads() @c > 0.
                 *
                 * @return true if a frame is ready to be picked up.
                 */
                bool frameAvailable() const;

                /**
                 * @brief Returns the number of completed read results
                 *        waiting in the result queue.
                 *
                 * Each entry is an already-executed @c CmdRead whose
                 * outcome (success, @c TryAgain, @c EndOfFile, or
                 * another error) will be returned verbatim by the next
                 * @c readFrame() call.  A non-zero value means
                 * @c readFrame() will not block; it does @em not
                 * guarantee that the next pop carries a valid frame —
                 * callers must still check the returned @c Error.
                 *
                 * @return The number of queued read results (>= 0).
                 */
                int readyReads() const;

                /**
                 * @brief Returns the number of @c CmdRead commands in
                 *        flight on the strand (queued or running).
                 *
                 * Counts commands submitted via prefetch that have not
                 * yet pushed a result onto the ready queue.  Useful for
                 * signal-driven consumers that want to know whether the
                 * next read is already on its way or whether they need
                 * to issue a fresh @c readFrame() to kick prefetch.
                 *
                 * @return The number of read commands in flight (>= 0).
                 */
                int pendingReads() const;

                /**
                 * @brief Returns the number of @c CmdWrite commands in
                 *        flight on the strand (queued or running).
                 *
                 * Incremented when @c writeFrame() submits a write and
                 * decremented when the write completes (or is
                 * cancelled).  Producers can use this to throttle
                 * themselves without relying on blocking writes.
                 *
                 * @return The number of write commands in flight (>= 0).
                 */
                int pendingWrites() const;

                /**
                 * @brief Reads the next synchronized frame.
                 *
                 * Submits a CmdRead with the current step, then either
                 * blocks waiting for the result or returns
                 * Error::TryAgain immediately.
                 *
                 * @par EOF semantics
                 *
                 * Once a read returns Error::EndOfFile, MediaIO marks
                 * the stream as exhausted and stops issuing further
                 * prefetch reads.  Subsequent @c readFrame() calls
                 * keep returning Error::EndOfFile (without going down
                 * to the backend) until the user calls @c seekToFrame
                 * or @c close to reset the state.  This avoids
                 * spamming the backend with reads it has already said
                 * are done.
                 *
                 * @param frame Receives the frame on success.
                 * @param block If true (default), blocks until ready.
                 * @return Error::Ok, Error::TryAgain, Error::EndOfFile, or another error.
                 */
                Error readFrame(Frame::Ptr &frame, bool block = true);

                /**
                 * @brief Returns true if the strand has no pending or
                 *        in-flight commands.
                 *
                 * Useful as a general "is this MediaIO doing anything?"
                 * check — for example, before submitting a non-blocking
                 * write to know whether it'll be queued behind other work.
                 *
                 * @return true if the underlying strand is idle.
                 */
                bool isIdle() const;

                /**
                 * @brief Cancels all pending (not-yet-running) commands.
                 *
                 * Each cancelled command's Future is fulfilled with
                 * Error::Cancelled, so any blocking caller unblocks
                 * cleanly.  Pending read results sitting in the read
                 * result queue (already completed by the worker) are
                 * also discarded.  Any command currently in flight is
                 * left to complete normally.
                 *
                 * @return The number of pending commands that were cancelled.
                 */
                size_t cancelPending();

                /**
                 * @brief Writes a frame to the media resource.
                 *
                 * Submits a CmdWrite, then either blocks for the result or
                 * returns Error::TryAgain.
                 *
                 * @param frame The Frame to write.
                 * @param block If true (default), blocks until the write completes.
                 * @return Error::Ok, Error::TryAgain, or an error.
                 */
                Error writeFrame(const Frame::Ptr &frame, bool block = true);

                // ---- Navigation ----

                /** @brief Returns the cached canSeek flag. */
                bool canSeek() const { return _canSeek; }

                /**
                 * @brief Seeks to the given frame number.
                 *
                 * Cancels any pending strand work and discards prefetched
                 * read results (they're stale relative to the new
                 * position), then submits a CmdSeek and blocks until it
                 * completes.  @c SeekDefault is resolved to the task's
                 * preferred mode (see @c defaultSeekMode()) before
                 * dispatch, so the backend always sees a concrete seek
                 * mode.
                 *
                 * @param frameNumber The zero-based frame number.
                 * @param mode How to interpret the seek target.
                 * @return Error::Ok or Error::IllegalSeek.
                 */
                Error seekToFrame(int64_t frameNumber, SeekMode mode = SeekDefault);

                /** @brief Returns the cached frame count. */
                int64_t frameCount() const { return _frameCount; }

                /** @brief Returns the cached current frame. */
                int64_t currentFrame() const { return _currentFrame; }

                /** @brief Sets the configuration. */
                void setConfig(const Config &config) { _config = config; }

                /**
                 * @brief Sends a backend-specific parameterized command.
                 *
                 * Parameterized commands let backends expose operations
                 * beyond the standard open/close/read/write/seek set —
                 * for example, setting device gain, querying device
                 * temperature, or retrieving codec parameters.  The
                 * meaning of @p name and the contents of @p params /
                 * @p result are entirely backend-defined.
                 *
                 * The command is dispatched on the strand and blocks
                 * until the backend completes it.  Backends that don't
                 * recognize @p name return @c Error::NotSupported.
                 *
                 * @param name   Operation name.
                 * @param params Input parameters (may be empty).
                 * @param result Optional output to receive results.
                 * @return Error::Ok on success, NotSupported if the
                 *         backend doesn't recognize the name, or another
                 *         error from the backend.
                 */
                Error sendParams(const String &name,
                                 const MediaIOParams &params = MediaIOParams(),
                                 MediaIOParams *result = nullptr);

                /**
                 * @brief Queries backend runtime statistics.
                 *
                 * Dispatches @c MediaIOCommandStats on the strand and
                 * returns the populated stats.  See @c MediaIOStats for
                 * the standard key conventions.  Backends with no stats
                 * return an empty result.  Returns an empty result if
                 * not open.
                 *
                 * @return A MediaIOStats containing whatever metrics
                 *         the backend reports (may be empty).
                 */
                MediaIOStats stats();

                /** @brief Emitted when an error occurs. @signal */
                PROMEKI_SIGNAL(errorOccurred, Error);

                /**
                 * @brief Emitted whenever a read command completes.
                 * @signal
                 *
                 * Fires after every @c CmdRead completion — success,
                 * end-of-file, or error.  Signal-driven consumers
                 * should respond by draining the ready queue with
                 * repeated @c readFrame(frame, false) calls until it
                 * returns @c Error::TryAgain (no more data available
                 * yet), @c Error::EndOfFile (stream exhausted), or
                 * another error.  The signal intentionally does not
                 * distinguish success from failure: consumers always
                 * discover the outcome by reading from the queue.
                 */
                PROMEKI_SIGNAL(frameReady);

                /** @brief Emitted when the backend is ready for another frame. @signal */
                PROMEKI_SIGNAL(frameWanted);

                /**
                 * @brief Emitted when a non-blocking write completes with an error.
                 * @signal
                 *
                 * Non-blocking writes (writeFrame(frame, false)) discard
                 * their direct result; this signal is the only way for
                 * the user to learn about write errors that happen on
                 * the strand worker thread.
                 */
                PROMEKI_SIGNAL(writeError, Error);

                /**
                 * @brief Emitted when the cached MediaDesc has changed mid-stream.
                 * @signal
                 *
                 * Fires when a backend reports a descriptor change in
                 * @c MediaIOCommandRead and MediaIO updates its cache.
                 * Listeners can call @c mediaDesc() / @c frameRate() to
                 * get the new values.  The triggering frame also has
                 * @c Metadata::MediaDescChanged stamped on it.
                 */
                PROMEKI_SIGNAL(descriptorChanged);

        private:
                Error dispatchCommand(MediaIOCommand::Ptr cmd);
                Error submitAndWait(MediaIOCommand::Ptr cmd);
                void  submitReadCommand();

                MediaIOTask                *_task = nullptr;
                Config                      _config;
                Mode                        _mode = NotOpen;
                int                         _step = 1;
                int                         _prefetchDepth = 1;
                bool                        _prefetchDepthExplicit = false;
                bool                        _atEnd = false;

                // Cached state — only read/written by the user thread
                MediaDesc                   _mediaDesc;
                AudioDesc                   _audioDesc;
                Metadata                    _metadata;
                FrameRate                   _frameRate;
                bool                        _canSeek = false;
                int64_t                     _frameCount = 0;
                int64_t                     _currentFrame = 0;
                SeekMode                    _defaultSeekMode = SeekExact;

                // Pre-open settings (passed into CmdOpen)
                MediaDesc                   _pendingMediaDesc;
                Metadata                    _pendingMetadata;
                AudioDesc                   _pendingAudioDesc;
                List<int>                   _pendingVideoTracks;
                List<int>                   _pendingAudioTracks;

                // Strand serializes all backend command execution onto
                // the shared ThreadPool.  Each command is a separate task
                // submitted to the strand; the strand ensures only one
                // runs at a time per MediaIO instance, while returning
                // pool threads between tasks.
                Strand                      _strand{pool()};

                // Inbound read results (worker pushes, user pops).
                // Filled by the strand task when a CmdRead completes.
                Queue<MediaIOCommand::Ptr>  _readResultQueue;

                // Number of CmdRead commands currently in flight (queued
                // or being processed).  Used to avoid duplicate submissions
                // when readFrame() is called repeatedly while a read is
                // pending.
                Atomic<int>                 _pendingReadCount;

                // Number of CmdWrite commands currently in flight
                // (queued or being processed).  Incremented in
                // writeFrame() before strand submit; decremented in
                // the strand task and the cancellation callback so
                // the count stays accurate whether writes succeed,
                // fail, or are cancelled.
                Atomic<int>                 _pendingWriteCount;
};

PROMEKI_NAMESPACE_END
