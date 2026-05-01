/**
 * @file      mediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/atomic.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/uniqueptr.h>
#include <promeki/frame.h>
#include <promeki/mediadesc.h>
#include <promeki/audiodesc.h>
#include <promeki/metadata.h>
#include <promeki/framerate.h>
#include <promeki/framecount.h>
#include <promeki/mediaconfig.h>
#include <promeki/url.h>
#include <promeki/mediatimestamp.h>
#include <promeki/mediaiotypes.h>
#include <promeki/mediaiocommand.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;
class Clock;
class MediaIODescription;
class MediaIOFactory;
class MediaIOPort;
class MediaIOSink;
class MediaIOSource;
class MediaIOPortGroup;

// MediaIORequest is forward-declared so MediaIO can return it from public
// methods without pulling in the full mediaiorequest.h here.  Callers that
// consume the returned request (.wait(), .then()) include mediaiorequest.h
// themselves.
class MediaIORequest;

// ============================================================================
// MediaIO controller class
// ============================================================================

/**
 * @brief Abstract controller for media I/O backends.
 * @ingroup mediaio_user
 *
 * MediaIO is the abstract root of the strategy hierarchy:
 *
 * @code
 * MediaIO                       (abstract — public API + cached state +
 *                                ports + signals; one virtual: submit())
 * └─ CommandMediaIO             (executeCmd virtuals + dispatch helper)
 *    ├─ InlineMediaIO           (submit runs inline on the calling thread)
 *    ├─ SharedThreadMediaIO     (submit posts to a ThreadPool Strand)
 *    └─ DedicatedThreadMediaIO  (submit posts to an owned worker thread)
 * @endcode
 *
 * All operations — open, close, read, write, seek — are dispatched
 * as @ref MediaIOCommand instances through the single
 * @ref submit virtual.  Each public method on @ref MediaIO,
 * @ref MediaIOSource, @ref MediaIOSink, and @ref MediaIOPortGroup
 * builds a concrete command, wraps it in a @ref MediaIORequest, and
 * calls @ref submit to dispatch.  Strategy subclasses choose the
 * thread on which the command runs.
 *
 * Backends register themselves via @c PROMEKI_REGISTER_MEDIAIO_FACTORY
 * (declaring a concrete @ref MediaIOFactory subclass) and are
 * instantiated through the config-driven factory @ref create or the
 * convenience helpers @ref createForFileRead / @ref createForFileWrite.
 *
 * @par Cached state
 *
 * MediaIO owns the cached descriptor and navigation state populated
 * from per-command Output fields.  The cache is updated only by
 * @ref completeCommand (running on whatever thread the strategy
 * chose), in this fixed order: cache update → signal emission →
 * request resolution.  @c .then() callbacks therefore always observe
 * up-to-date cached state.
 *
 * @par Non-blocking I/O
 *
 * Per the always-async API rule every public method returns a
 * @ref MediaIORequest.  Callers that want synchronous behavior
 * append @c .wait().  @ref MediaIOSink::writeFrame applies an
 * always-on capacity gate that returns @c Error::TryAgain when the
 * sink is full; consumers wait on @c frameWanted before retrying.
 *
 * @par Thread Safety
 *
 * Inherits @ref ObjectBase: thread-affine.  MediaIO is intended
 * to be driven from a single user thread.  Public methods
 * (@c open / @c close / @c readFrame / @c writeFrame /
 * @c seekToFrame / @c setStep etc.) are not safe to call
 * concurrently from multiple threads.  Cached state is updated
 * inside @ref completeCommand on the strategy's chosen execution
 * thread; user-thread reads of cached accessors must observe the
 * cache only after the relevant request resolves.  Cross-thread
 * notifications use signals (@c frameReady, @c frameWanted,
 * @c writeError), which the signal/slot system marshals via the
 * receiver's EventLoop.
 */
class MediaIO : public ObjectBase {
                PROMEKI_OBJECT(MediaIO, ObjectBase)
        public:
                /** @brief Unique-ownership pointer to a MediaIO. */
                using UPtr = UniquePtr<MediaIO>;

                /** @brief Seek mode (alias for MediaIOSeekMode). */
                using SeekMode = MediaIOSeekMode;

                static constexpr SeekMode SeekDefault = MediaIO_SeekDefault;
                static constexpr SeekMode SeekExact = MediaIO_SeekExact;
                static constexpr SeekMode SeekNearestKeyframe = MediaIO_SeekNearestKeyframe;
                static constexpr SeekMode SeekKeyframeBefore = MediaIO_SeekKeyframeBefore;
                static constexpr SeekMode SeekKeyframeAfter = MediaIO_SeekKeyframeAfter;

                /** @brief Configuration database type. */
                using Config = MediaConfig;

                /** @brief Configuration ID type. */
                using ConfigID = MediaConfigID;

                /** @brief Frame count is not yet known. */
                static constexpr FrameCount FrameCountUnknown = FrameCount::unknown();

                /** @brief Source is unbounded (generators, live devices). */
                static constexpr FrameCount FrameCountInfinite = FrameCount::infinity();


                /**
                 * @brief Creates a MediaIO instance from a configuration.
                 * @param config The configuration describing the desired backend.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr on failure.
                 */
                static MediaIO *create(const Config &config, ObjectBase *parent = nullptr);

                /**
                 * @brief Creates a MediaIO reader for the given filename.
                 *
                 * If @p filename parses as a URL whose scheme matches a
                 * registered backend, dispatches to @ref createFromUrl
                 * in @ref Source mode.  Otherwise treats the argument
                 * as a filesystem path and picks a backend by path
                 * probe, extension, or content probe (in that order).
                 *
                 * @param filename The path or URL to the media resource.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr on failure.
                 */
                static MediaIO *createForFileRead(const String &filename, ObjectBase *parent = nullptr);

                /**
                 * @brief Creates a MediaIO writer for the given filename.
                 *
                 * URL strings are dispatched identically to
                 * @ref createForFileRead but in @ref Sink mode.
                 *
                 * @param filename The path or URL to the media resource.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr on failure.
                 */
                static MediaIO *createForFileWrite(const String &filename, ObjectBase *parent = nullptr);

                /**
                 * @brief Creates a MediaIO from a URL.
                 *
                 * Looks up the backend that registered @p url's scheme
                 * (see @ref MediaIOFactory::schemes), runs its
                 * @ref MediaIOFactory::urlToConfig callback to
                 * translate the URL into a @ref Config, seeds
                 * @ref MediaConfig::Type with the backend name, and
                 * instantiates the task.  The returned MediaIO is not
                 * yet opened — call @ref open with the desired mode.
                 *
                 * @param url    The parsed URL.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr on failure.
                 */
                static MediaIO *createFromUrl(const Url &url, ObjectBase *parent = nullptr);

                /**
                 * @brief Convenience overload that parses @p url first.
                 * @param url    The URL string.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr if @p url
                 *         fails to parse, has no known scheme, or the
                 *         backend's URL translator reports an error.
                 */
                static MediaIO *createFromUrl(const String &url, ObjectBase *parent = nullptr);

                /**
                 * @brief Applies URL query parameters to a config via
                 *        the backend's spec map.
                 *
                 * Every key in @p url.query() is looked up in @p specs
                 * by its declared name (the identifier passed to
                 * @ref PROMEKI_DECLARE_ID).  The associated raw string
                 * value is coerced to the spec's expected Variant type
                 * via @ref VariantSpec::parseString and range-checked
                 * via @ref VariantSpec::validate before being written
                 * to @p outConfig.  Values flow through the same
                 * coercion path as JSON/CLI entry, so a URL, a JSON
                 * config file, and a command-line flag all have
                 * identical type rules.
                 *
                 * This is automatically invoked by @ref createFromUrl
                 * after the backend's own
                 * @ref MediaIOFactory::urlToConfig has handled the
                 * authority / path components, so most backends never
                 * need to call it directly.
                 *
                 * @par Case sensitivity
                 * Query keys and values are matched
                 * @b case-sensitively.  A URL like
                 * @c "pmfb://x?framebridgeringdepth=4" is rejected
                 * with @ref Error::InvalidArgument; the canonical
                 * @c "FrameBridgeRingDepth" spelling is required.
                 * This matches the formal RFC 3986 position (query
                 * components are case-sensitive) and — more
                 * importantly — closes the door on a class of silent
                 * bugs where a near-match name could resolve to the
                 * wrong key (e.g. @c FrameBridgeSynch vs
                 * @c FrameBridgeSync).  The spec ID spelling is
                 * canonical in log lines, JSON configs, and
                 * @c --help output, so requiring the same spelling in
                 * URLs keeps grep/search coherent across all four
                 * layers.  Scheme and host remain case-insensitive
                 * per RFC 3986 and are handled by @ref Url::fromString
                 * itself.
                 *
                 * @par Failure cases
                 * All failures are fatal — on the first bad key the
                 * function logs a single warning and returns the
                 * corresponding error without writing that key (or any
                 * subsequent keys) to @p outConfig:
                 *   - @c Error::Invalid &mdash; @p outConfig is null.
                 *   - @c Error::InvalidArgument &mdash; a query key is
                 *     not declared in any
                 *     @ref PROMEKI_DECLARE_ID for this Config type
                 *     (including case-variant spellings), or is
                 *     declared globally but not present in @p specs
                 *     (i.e. the backend does not honor it).
                 *   - @c Error::ConversionFailed &mdash;
                 *     @ref VariantSpec::parseString could not coerce
                 *     the raw string into the spec's expected type.
                 *   - @c Error::OutOfRange &mdash; the parsed value
                 *     lies outside the spec's declared @c [min, max].
                 *
                 * @param url       The URL whose query map to apply.
                 * @param specs     The spec map defining accepted keys.
                 * @param outConfig Config to populate (must be non-null).
                 * @return @c Error::Ok on success, or one of the error
                 *         codes listed above.
                 */
                static Error applyQueryToConfig(const Url &url, const Config::SpecMap &specs, Config *outConfig);

                /**
                 * @brief Applies the standard @c Output* overrides from
                 *        @p config to @p input, returning the resulting
                 *        @ref MediaDesc.
                 *
                 * Every transform backend (CSC, FrameSync, SRC,
                 * VideoEncoder, VideoDecoder) consumes the same family
                 * of @ref MediaConfig keys to express "given this input
                 * shape, produce this output":
                 *
                 *  - @ref MediaConfig::OutputPixelFormat      — replaces the
                 *    @ref ImageDesc::pixelFormat of every video image.
                 *  - @ref MediaConfig::OutputFrameRate      — replaces
                 *    @ref MediaDesc::frameRate.
                 *  - @ref MediaConfig::OutputAudioRate      — replaces
                 *    @ref AudioDesc::sampleRate on every audio entry.
                 *  - @ref MediaConfig::OutputAudioChannels  — replaces
                 *    @ref AudioDesc::channels on every audio entry.
                 *  - @ref MediaConfig::OutputAudioDataType  — replaces
                 *    @ref AudioDesc::format on every audio entry.
                 *
                 * Keys at their default (invalid PixelFormat, invalid
                 * FrameRate, zero audio rate / channels, invalid Enum)
                 * mean "inherit from input" — the corresponding field
                 * passes through unchanged.
                 *
                 * The pipeline planner uses this helper to compute
                 * what shape a configured transform will produce
                 * without instantiating it.  Backend authors call it
                 * from @ref proposeOutput / @ref describe so every
                 * transform answers the planner uniformly.
                 *
                 * @param input  The MediaDesc the transform will consume.
                 * @param config The transform's MediaConfig (read-only).
                 * @return The MediaDesc the transform will produce.
                 */
                static MediaDesc applyOutputOverrides(const MediaDesc &input, const MediaConfig &config);

                /**
                 * @brief Returns a canonical uncompressed PixelFormat that
                 *        stays in the same family as @p source.
                 *
                 * Transform backends that refuse compressed or
                 * paint-engine-less input (Burn, FrameSync, ...) use
                 * this to ask the pipeline planner for a same-family
                 * uncompressed substitute in @c proposeInput.  The
                 * planner's VideoDecoder / CSC bridges then close the
                 * gap.  The helper keeps the mapping in one place so
                 * the fallback does not drift between backends.
                 *
                 *  - YCbCr sources → @ref PixelFormat::YUV8_422_Rec709
                 *  - RGB / other → @ref PixelFormat::RGBA8_sRGB
                 *
                 * @param source The compressed or paint-engine-less
                 *               input PixelFormat.
                 * @return A canonical uncompressed PixelFormat.
                 */
                static PixelFormat defaultUncompressedPixelFormat(const PixelFormat &source);

                /**
                 * @brief Constructs a MediaIO with an optional parent.
                 * @param parent The parent object, or nullptr.
                 */
                MediaIO(ObjectBase *parent = nullptr);

                /**
                 * @brief Destructor.  Closes if still open and deletes the task.
                 */
                ~MediaIO() override;

                /** @brief Returns the configuration. */
                const Config &config() const { return _config; }

                // ---- Ports (multi-port surface) ----

                /**
                 * @brief Returns the @c N-th source port, or null.
                 *
                 * Sources have their own per-type index space — @c source(0)
                 * and @c sink(0) are distinct ports.  Ports are populated
                 * by the backend during @c open() and torn down on
                 * @c close().  Returns null when @p N is out of range or
                 * the resource is not open.
                 *
                 * @param N The per-type source index.
                 */
                MediaIOSource *source(int N) const;

                /** @brief Returns the number of source ports currently exposed. */
                int sourceCount() const;

                /**
                 * @brief Returns the @c N-th sink port, or null.
                 *
                 * Sinks have their own per-type index space — @c sink(0)
                 * and @c source(0) are distinct ports.  Ports are populated
                 * by the backend during @c open() and torn down on
                 * @c close().  Returns null when @p N is out of range or
                 * the resource is not open.
                 *
                 * @param N The per-type sink index.
                 */
                MediaIOSink *sink(int N) const;

                /** @brief Returns the number of sink ports currently exposed. */
                int sinkCount() const;

                /**
                 * @brief Returns the @c N-th port group, or null.
                 *
                 * Every port belongs to exactly one
                 * @ref MediaIOPortGroup; the group holds the timing
                 * reference (@ref Clock), step, current frame, and
                 * seek state shared by every port in it.
                 * Single-port groups represent independent ports;
                 * multi-port groups represent ports that advance in
                 * lockstep.  Returns null when @p N is out of range.
                 *
                 * @param N The port group index.
                 */
                MediaIOPortGroup *portGroup(int N) const;

                /** @brief Returns the number of port groups currently exposed. */
                int portGroupCount() const;

                /**
                 * @brief True if any source ports are present.
                 *
                 * Convenience wrapper around @c sourceCount() > 0 — a
                 * @ref MediaIO is "a source" when it has at least one
                 * source port.  Pure sinks return false.
                 */
                bool isSource() const { return sourceCount() > 0; }

                /**
                 * @brief True if any sink ports are present.
                 *
                 * Convenience wrapper around @c sinkCount() > 0 — a
                 * @ref MediaIO is "a sink" when it has at least one
                 * sink port.  Pure sources return false.
                 */
                bool isSink() const { return sinkCount() > 0; }

                // ---- Per-instance identifier ----

                /**
                 * @brief Returns the human-readable instance name.
                 *
                 * Reads @ref MediaConfig::Name straight from the live
                 * config so @c name() and @c config() always agree.
                 * Empty when no name has been assigned (the
                 * @ref MediaPipeline path always supplies the stage
                 * name; standalone callers may set it explicitly via
                 * @ref MediaConfig::Name, @ref setName, or leave it
                 * empty).
                 *
                 * @return The instance name.
                 */
                String name() const { return _config.getAs<String>(MediaConfig::Name, String()); }

                /**
                 * @brief Updates the instance name in the live config.
                 *
                 * Convenience over @c config().set(MediaConfig::Name,
                 * ...).  Used by @ref MediaPipeline::injectStage to
                 * stamp the resolved stage name onto the IO so
                 * @c io->name() agrees with the pipeline's view.
                 *
                 * @param val The new instance name (may be empty to clear).
                 */
                void setName(const String &val) { _config.set(MediaConfig::Name, val); }

                /** @brief Returns true if the resource is open. */
                bool isOpen() const { return _open.value(); }

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
                 * @brief Sets the @ref MediaDesc the backend should expect on its input.
                 *
                 * Pre-open hint that flows to the backend via
                 * @ref MediaIOCommandOpen::pendingMediaDesc.  Used by
                 * the pipeline planner to communicate the upstream
                 * stage's output shape to a downstream sink or
                 * transform before its sinks exist.  Phase 8 of the
                 * multi-port refactor replaces this with per-port
                 * negotiation through @ref MediaIOPortConnection.
                 *
                 * @param desc The expected input MediaDesc.
                 * @return Error::Ok or Error::AlreadyOpen.
                 */
                Error setPendingMediaDesc(const MediaDesc &desc);

                /** @brief Returns the @ref MediaDesc pre-open hint. */
                const MediaDesc &pendingMediaDesc() const { return _pendingMediaDesc; }

                /** @brief Sets the @ref AudioDesc pre-open hint (companion to @ref setPendingMediaDesc). */
                Error setPendingAudioDesc(const AudioDesc &desc);

                /** @brief Returns the @ref AudioDesc pre-open hint. */
                const AudioDesc &pendingAudioDesc() const { return _pendingAudioDesc; }

                /** @brief Sets the container @ref Metadata pre-open hint. */
                Error setPendingMetadata(const Metadata &meta);

                /** @brief Returns the @ref Metadata pre-open hint. */
                const Metadata &pendingMetadata() const { return _pendingMetadata; }

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
                 * Builds an @ref MediaIOCommandOpen, dispatches it via
                 * @ref submit, and returns a request the caller can
                 * @c .wait() on or attach a @c .then() continuation to.
                 *
                 * The backend's @c executeCmd declares the ports it
                 * exposes via @ref CommandMediaIO::addPortGroup /
                 * @c addSource / @c addSink — the resulting @ref MediaIO
                 * is a source if any sources were added, a sink if any
                 * sinks were added, or both (a transform) if both kinds
                 * were.  When the open succeeds, @ref completeCommand
                 * populates the cached @ref mediaDesc /
                 * @ref audioDesc / @ref metadata / @ref frameRate from
                 * the primary port; on failure it auto-dispatches a
                 * cleanup Close on the same backend instance.
                 *
                 * Per the always-async API rule, callers that want
                 * synchronous behavior write @c io->open().wait().
                 *
                 * @return A request resolving with @c Error::Ok on
                 *         success, @c Error::AlreadyOpen if the
                 *         MediaIO is already open, @c Error::Invalid
                 *         if no backend is attached, or whatever the
                 *         backend's @c executeCmd returns.
                 */
                MediaIORequest open();

                /**
                 * @brief Closes the media resource.
                 *
                 * Submits a CmdClose to the strand.  When the close
                 * completes, @ref completeCommand pushes a single
                 * synthetic EOS read result onto each source's queue
                 * (so signal-driven consumers receive exactly one
                 * trailing @c frameReady whose pop returns
                 * @c Error::EndOfFile), resets the cached descriptor
                 * state, and emits @ref closedSignal with the close
                 * result.
                 *
                 * Callers that want synchronous behavior write
                 * @c io->close().wait().  Callers that want to
                 * fire-and-forget can just discard the returned
                 * request; the close still runs to completion (the
                 * strand keeps the command alive).
                 *
                 * While the close is in flight @ref isClosing returns
                 * true and @ref readFrame / @ref writeFrame stop
                 * submitting new work (reads still drain any queued
                 * results so the consumer can observe the trailing
                 * EOS).  Callers must not touch the cached descriptor
                 * accessors (@ref mediaDesc, @ref frameRate, etc.)
                 * between this call returning and @ref closedSignal
                 * firing.
                 *
                 * @return A request resolving with @c Error::Ok on
                 *         success or @c Error::NotOpen if the
                 *         MediaIO isn't open or is already closing.
                 */
                MediaIORequest close();

                /**
                 * @brief Returns true while an async close is in flight.
                 *
                 * Set by @ref close on entry, cleared by the finalize
                 * step just before @ref closedSignal fires.  Always
                 * false outside of an async close window.
                 */
                bool isClosing() const { return _closing.value(); }

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

                // ---- Introspection / negotiation ----

                /**
                 * @brief Populates @p out with a snapshot of this
                 *        MediaIO's identity, role, and format landscape.
                 *
                 * The wrapper fills in:
                 *  - Backend identity (name, description) from the
                 *    registered @ref MediaIOFactory.
                 *  - Role flags (canBeSource / canBeSink /
                 *    canBeTransform) from the same @ref MediaIOFactory.
                 *  - Instance identity (@c name) from this MediaIO.
                 *  - Cached @c canSeek, @c frameCount, @c frameRate,
                 *    @c containerMetadata if open.
                 *
                 * Then dispatches to the backend's @ref describe
                 * to add format-specific fields (@c producibleFormats,
                 * @c acceptableFormats, @c preferredFormat) and any
                 * pre-open probe results.
                 *
                 * Synchronous; safe to call any time (pre-open, while
                 * open, after close).  Cheap when open (cached state
                 * is reused) and as cheap as the backend's probe
                 * implementation otherwise.
                 *
                 * @param out The description to populate (overwritten).
                 * @return @c Error::Ok on success, or the probe error
                 *         from the backend (also stamped into
                 *         @p out's @c probeStatus field).
                 */
                virtual Error describe(MediaIODescription *out) const;

                /**
                 * @brief Asks the backend whether it can directly consume @p offered.
                 *
                 * Pre-open backend-level forwarder.  Once open, prefer
                 * @c sink(N)->proposeInput for the per-sink answer.
                 * Default accepts whatever is offered (transparent
                 * passthrough); sinks and transforms with format
                 * constraints override to either narrow or refuse.
                 *
                 * @param offered    The MediaDesc the planner would route in.
                 * @param preferred  Receives the desc the backend wants.
                 * @return @c Error::Ok or @c Error::NotSupported.
                 */
                virtual Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const;

                /**
                 * @brief Asks the backend whether it can produce @p requested.
                 *
                 * Pre-open backend-level forwarder; symmetric with
                 * @ref proposeInput.  Once open, prefer
                 * @c source(N)->proposeOutput for the per-source
                 * answer.  Default returns @c Error::NotSupported with
                 * @p achievable cleared; the legacy shim and Phase-12+
                 * native backends override.
                 *
                 * @param requested   The MediaDesc the planner would prefer.
                 * @param achievable  Receives the desc the backend can produce.
                 * @return @c Error::Ok or @c Error::NotSupported.
                 */
                virtual Error proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const;

                /**
                 * @brief Returns true when the underlying executor has
                 *        no pending or in-flight commands.
                 *
                 * Useful as a general "is this MediaIO doing anything?"
                 * check — for example, before tearing down to know
                 * whether work is still in flight.  Default: @c true
                 * (no executor → always idle).  Strategy subclasses
                 * (@ref SharedThreadMediaIO,
                 * @ref DedicatedThreadMediaIO) override.
                 *
                 * @return true if the underlying executor is idle.
                 */
                virtual bool isIdle() const { return true; }

                /**
                 * @brief Requests that any in-flight blocking work unwind.
                 *
                 * Called from the close path on a non-worker thread
                 * before the Close command is submitted.  Default is
                 * a no-op; @ref DedicatedThreadMediaIO -based backends
                 * with blocking syscalls override.  Must be thread-safe
                 * with respect to whatever thread is running
                 * @c executeCmd.
                 */
                virtual void cancelBlockingWork() {}

                /**
                 * @brief Cancels every queued-but-not-yet-dispatched
                 *        command on the strategy's executor.
                 *
                 * Called by @ref MediaIOPortGroup::seekToFrame and
                 * @ref MediaIOPortGroup::setStep to drop stale
                 * prefetched reads before submitting the new
                 * navigation command.  Default is a no-op (no
                 * executor backlog to clear); strategies with their
                 * own queue (Strand, dedicated worker queue) override
                 * to flush the backlog through the cancellation path.
                 */
                virtual void cancelPendingWork() {}

                /**
                 * @brief Returns the number of frames the backend is
                 *        holding internally beyond what
                 *        @c pendingWrites tracks.
                 *
                 * Used by @ref MediaIOSink::writesAccepted to account
                 * for frames processed by the write side but not yet
                 * consumed by the read side (e.g. a converter's output
                 * FIFO).  Default returns 0.  Called from the user
                 * thread — implementations must be safe against
                 * concurrent strand activity.
                 */
                virtual int pendingInternalWrites() const { return 0; }

                /** @brief Sets the configuration. */
                void setConfig(const Config &config) { _config = config; }

                /**
                 * @brief Sends a backend-specific parameterized command.
                 *
                 * Parameterized commands let backends expose operations
                 * beyond the standard open/close/read/write/seek set —
                 * for example, setting device gain, querying device
                 * temperature, or retrieving codec parameters.  The
                 * meaning of @p name and the contents of @p params and
                 * the resolved @ref MediaIOParams output are entirely
                 * backend-defined.
                 *
                 * Per the always-async API rule, the result is
                 * delivered through the returned request — callers
                 * write @c io->sendParams("Foo").wait() to read it
                 * synchronously, or attach a @c .then() continuation.
                 * Backends that don't recognize @p name return
                 * @c Error::NotSupported.
                 *
                 * @param name   Operation name.
                 * @param params Input parameters (may be empty).
                 * @return A request resolving with the operation's
                 *         output @ref MediaIOParams plus an
                 *         @ref Error.  The @c MediaIOParams payload
                 *         is empty on failure.
                 */
                MediaIORequest sendParams(const String &name, const MediaIOParams &params = MediaIOParams());

                /**
                 * @brief Queries the cumulative-aggregate runtime statistics.
                 *
                 * Builds a @ref MediaIOCommandStats command, dispatches
                 * it via @ref submit, and returns the request.  The
                 * resulting @ref MediaIORequest::stats() (after
                 * @c .wait()) is the populated @ref MediaIOStats —
                 * backend-specific cumulative keys filled in by
                 * @c CommandMediaIO::executeCmd(MediaIOCommandStats &)
                 * and the framework-managed standard keys (rate
                 * trackers, drop / repeat / late counters, strand
                 * backlog) overlaid by @ref completeCommand.
                 * Marked urgent so telemetry pollers do not block
                 * behind a deep queue of real I/O.
                 *
                 * The same @ref MediaIOStats type appears on every
                 * command's @ref MediaIOCommand::stats — for
                 * standard commands it carries per-command
                 * telemetry instead.  See
                 * @ref MediaIORequest::stats() for the unified
                 * accessor.
                 *
                 * @return A request that resolves with @c Error::Ok
                 *         on success or @c Error::NotOpen when the
                 *         MediaIO is not open or is closing.
                 */
                MediaIORequest stats();

                /** @brief Emitted when an error occurs. @signal */
                PROMEKI_SIGNAL(errorOccurred, Error);

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

                /**
                 * @brief Emitted when a @ref close request completes.
                 * @signal
                 *
                 * Fires after the backend's Close command has run, the
                 * synthetic trailing EOS has been pushed to the read
                 * queue, and the cached state has been reset.  Carries
                 * the close completion @ref Error.  Fires for both
                 * blocking and non-blocking closes — blocking callers
                 * simply see it before @ref close returns.
                 */
                PROMEKI_SIGNAL(closed, Error);

                /**
                 * @brief Emitted after every command resolves through @ref completeCommand.
                 * @signal
                 *
                 * Carries the just-completed @ref MediaIOCommand::Ptr — its
                 * @c result, @c stats, and any typed Output fields are
                 * fully populated by the time this fires.  Fires on the
                 * strategy's chosen execution thread (strand / dedicated
                 * worker / inline), so subscribers must either be
                 * thread-safe or rely on signal/slot cross-thread
                 * marshalling via @ref EventLoop.
                 *
                 * Intended as the observability hook for pipeline-level
                 * telemetry collectors (@ref MediaPipelineStatsCollector
                 * subscribes per stage to roll per-command timings into
                 * a windowed ring) — anyone needing to observe every
                 * command without subclassing the backend can hang
                 * behaviour off this signal.  Fires unconditionally
                 * regardless of @c result, including cancellation, so
                 * subscribers must inspect @c result themselves when
                 * filtering out failures matters.
                 */
                PROMEKI_SIGNAL(commandCompleted, MediaIOCommand::Ptr);

        protected:
                /**
                 * @brief The single virtual gate every command flows through.
                 *
                 * Public API methods on @ref MediaIO, @ref MediaIOSource,
                 * @ref MediaIOSink, and @ref MediaIOPortGroup build a
                 * concrete command, wrap it in a @ref MediaIORequest,
                 * and call this virtual to dispatch it.  Strategy
                 * subclasses (@ref InlineMediaIO,
                 * @ref SharedThreadMediaIO,
                 * @ref DedicatedThreadMediaIO) implement it; @ref MediaIO
                 * itself is abstract.
                 *
                 * Strategies are responsible for invoking
                 * @ref CommandMediaIO::dispatch to drive the backend's
                 * @c executeCmd, recording the result in
                 * @ref MediaIOCommand::result, and finally calling
                 * @ref completeCommand to apply cache writes, emit
                 * cache-derived signals, and resolve the request.
                 *
                 * Backends never see this virtual.
                 */
                virtual void submit(MediaIOCommand::Ptr cmd) = 0;

                /**
                 * @brief Centralized cache-update + future-resolution path.
                 *
                 * Called by the strategy after @c executeCmd returns
                 * (and after @ref MediaIOCommand::result has been
                 * populated).  Walks the command's typed Output fields,
                 * applies them to the cache, emits the appropriate
                 * cache-derived signals, and finally invokes the
                 * command's @ref MediaIOCommand::markCompleted hook so
                 * the @ref MediaIORequest fulfills its promise.
                 *
                 * Order is fixed: cache update → signal emission →
                 * request resolution.  @c .then() callbacks always
                 * observe up-to-date cached state.
                 *
                 * Backends never call this — only strategies do.
                 */
                void completeCommand(MediaIOCommand::Ptr cmd);

                // ---- Port containers (populated by subclass executeCmd
                //      hooks via @ref CommandMediaIO::addPortGroup etc.) ----
                //
                // Lives on @ref MediaIO so cached-state queries
                // (@ref source, @ref sink, @ref portGroup) are part of
                // the abstract interface, but mutated only by the
                // command-handling subclasses through friend access.

                List<MediaIOSource *>    _sources;
                List<MediaIOSink *>      _sinks;
                List<MediaIOPortGroup *> _portGroups;

        private:
                friend class CommandMediaIO;
                friend class MediaIOPort;
                friend class MediaIOSink;
                friend class MediaIOSource;
                friend class MediaIOPortGroup;
                friend class MediaIOReadCache;

                /**
                 * @brief Resets cached descriptor state to the not-open defaults.
                 *
                 * Called from the close finalize step.  Also clears
                 * the @c _closing flag and the pending read/write
                 * counters.  Not thread-safe against concurrent
                 * user-thread reads of the cached accessors — callers
                 * of @ref close that fire-and-forget must wait for
                 * @ref closedSignal before touching those accessors.
                 */
                void resetClosedState();

                /**
                 * @brief Walks a Frame and returns its total payload size.
                 *
                 * Sums every Image plane buffer's logical size plus every
                 * Audio buffer's logical size.  Used by the telemetry
                 * layer to feed RateTracker::record() when a read or
                 * write completes.  Skips invalid entries rather than
                 * dereferencing them.
                 *
                 * @param frame The frame to measure.
                 * @return The total payload size in bytes.
                 */
                static int64_t frameByteSize(const Frame::Ptr &frame);

                /**
                 * @brief Populates the standard MediaIOStats keys.
                 *
                 * Called by @ref completeCommand for stats-query
                 * commands after the backend task has contributed
                 * its own (backend-specific) fields.  Writes
                 * @c BytesPerSecond and @c FramesPerSecond from the
                 * per-group RateTrackers, copies @c FramesDropped,
                 * @c FramesRepeated and @c FramesLate from the
                 * per-group atomic counters, and reports the
                 * strand backlog as @c PendingOperations.
                 *
                 * The framework-managed fields are authoritative:
                 * if a backend had previously populated the same
                 * keys they are overwritten.  Backend-specific keys
                 * set by the task are left untouched.
                 *
                 * @param stats The stats object to populate.
                 */
                void populateStandardStats(MediaIOStats &stats) const;

                Config                 _config;
                // _open / _closing are read from arbitrary threads
                // (e.g. the EventLoop thread in
                // @ref MediaIOReadCache::submitOneLocked) while the
                // worker strand writes them through
                // @ref completeCommand and @ref resetClosedState.
                // Atomic loads / stores keep those reads safe without
                // forcing isOpen() / isClosing() through the strand.
                Atomic<bool>           _open;
                Atomic<bool>           _closing;


                // Container-level cached state, written from
                // @ref completeCommand on whatever thread the strategy
                // chose, read by the user thread.  Per-port cached state
                // (current frame, step, frame count, canSeek) lives on
                // @ref MediaIOPortGroup; per-port format shapes live on
                // @ref MediaIOSource / @ref MediaIOSink.
                MediaDesc   _mediaDesc;
                AudioDesc   _audioDesc;
                Metadata    _metadata;
                FrameRate   _frameRate;
                TimeStamp   _originTime;
                SeekMode    _defaultSeekMode = SeekExact;

                // Pre-open settings.  Backends consume these on the open
                // command (cmd.pendingMediaDesc / pendingAudioDesc /
                // pendingMetadata) so the planner / pipeline can hand
                // upstream shape to a sink or transform before its ports
                // exist.
                MediaDesc _pendingMediaDesc;
                AudioDesc _pendingAudioDesc;
                Metadata  _pendingMetadata;
                List<int> _pendingVideoTracks;
                List<int> _pendingAudioTracks;
};

PROMEKI_NAMESPACE_END
