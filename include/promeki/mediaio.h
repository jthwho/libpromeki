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
 * @param ClassName The MediaIO subclass to register.
 */
#define PROMEKI_REGISTER_MEDIAIO(ClassName) \
        [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_mediaio_, PROMEKI_UNIQUE_ID) = \
                MediaIO::registerFormat(ClassName::formatDesc());

PROMEKI_NAMESPACE_BEGIN

class IODevice;

/**
 * @brief Abstract base class for media I/O backends.
 * @ingroup proav
 *
 * MediaIO provides a uniform interface for reading and writing media
 * content (video frames, audio, metadata) from container files, image
 * sequences, and hardware video I/O devices.  All MediaIO subclasses
 * derive from ObjectBase.
 *
 * Backends register themselves via PROMEKI_REGISTER_MEDIAIO and are
 * instantiated through the config-driven factory create() or the
 * convenience helpers createForFileRead() / createForFileWrite().
 *
 * readFrame() fills a synchronized Frame containing all video and audio
 * for the current position.  Seeking is optional; backends that support
 * it override canSeek() and seekToFrame().
 *
 * @par Lifecycle
 *
 * The base class centralizes open/close bookkeeping.  open() checks
 * state, calls onOpen(), and sets the mode on success.  close() calls
 * onClose() and resets the mode.  Backends override onOpen() and
 * onClose() instead of open()/close().
 *
 * Each backend destructor must call close() if still open, because
 * by the time ~MediaIO() runs the derived members are already
 * destroyed.  The base destructor asserts !isOpen() in debug builds
 * to catch missing cleanup.
 *
 * @par Step control
 *
 * The step property (default 1) controls how readFrame() advances
 * after each read.  Set to -1 for reverse, 2 for 2x forward, 0 to
 * re-read the same position indefinitely.  Backends override
 * setStep() to react to direction or speed changes (e.g. switching
 * a hardware device to reverse mode, or adjusting cache prefetch
 * direction).
 *
 * @par Frame count semantics
 *
 * frameCount() returns:
 * - A positive value for the total number of frames available (readers)
 *   or frames written so far (writers).
 * - 0 for zero frames.
 * - FrameCountUnknown (-1) when the length is not yet known.
 * - FrameCountInfinite (-2) for unbounded sources (generators, live).
 * - FrameCountError (-3) when the count is unavailable due to error.
 *
 * currentFrame() returns the number of frames read or written so far,
 * starting at 0.  seekToFrame() sets it to the target position.
 *
 * @code
 * MediaIO *io = MediaIO::createForFileRead("clip.mxf");
 * if(io) {
 *         Error err = io->open(MediaIO::Reader);
 *         if(err.isOk()) {
 *                 Frame frame;
 *                 while(io->readFrame(frame).isOk()) {
 *                         // process frame
 *                 }
 *                 io->close();
 *         }
 *         delete io;
 * }
 * @endcode
 */
class MediaIO : public ObjectBase {
        PROMEKI_OBJECT(MediaIO, ObjectBase)
        public:
                /** @brief Configuration database type for MediaIO instances. */
                using Config = VariantDatabase<MediaIO>;

                /** @brief Shorthand for Config::ID. */
                using ConfigID = Config::ID;

                /** @brief Open mode for the media resource. */
                enum Mode {
                        NotOpen = 0, ///< @brief Resource is not open.
                        Reader,      ///< @brief Open for reading.
                        Writer       ///< @brief Open for writing.
                };

                /** @brief Frame count is not yet known (e.g. file not fully scanned). */
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
                 *
                 * Each backend provides a static formatDesc() method returning
                 * one of these.  The factory functions use the registry to match
                 * a request to the right backend.
                 */
                struct FormatDesc {
                        String      name;        ///< @brief Backend name (e.g. "MXF", "DeckLink").
                        String      description; ///< @brief Human-readable description.
                        StringList  extensions;  ///< @brief Supported file extensions (no dots). Empty for devices.
                        bool        canRead;     ///< @brief Whether the backend supports reading.
                        bool        canWrite;    ///< @brief Whether the backend supports writing.
                        /** @brief Factory function that creates a new instance of the backend. */
                        std::function<MediaIO *(ObjectBase *)> create;
                        /** @brief Returns the default configuration for this backend. */
                        std::function<Config ()> defaultConfig;
                        /**
                         * @brief Optional content probe via IODevice.
                         *
                         * When provided, the factory calls this to verify that
                         * a device's content can be handled by this backend.
                         * The device is seeked to position 0 before each call.
                         * Used by createForFileRead() as a content-based
                         * fallback when no extension matches.  May be null for
                         * backends that cannot probe (e.g. headerless formats,
                         * generators).
                         *
                         * @param device An open, seekable IODevice positioned at 0.
                         * @return true if this backend can handle the content.
                         */
                        std::function<bool(IODevice *device)> canHandleDevice;
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
                 *
                 * Looks up the backend by name and calls its defaultConfig
                 * function.  Returns an empty Config if the name is not found.
                 *
                 * @param typeName The registered backend name (e.g. "TPG").
                 * @return A Config populated with default values.
                 *
                 * @par Example
                 * @code
                 * MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
                 * cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);
                 * MediaIO *io = MediaIO::create(cfg);
                 * @endcode
                 */
                static Config defaultConfig(const String &typeName);

                /**
                 * @brief Creates a MediaIO instance from a configuration.
                 *
                 * Looks up the backend by the ConfigType key in the config.
                 * If no type is present but ConfigFilename is, the backend is
                 * inferred from the file extension.
                 *
                 * @param config The configuration describing the desired backend and its options.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr if no matching backend was found.
                 */
                static MediaIO *create(const Config &config, ObjectBase *parent = nullptr);

                /**
                 * @brief Creates a MediaIO reader for the given filename.
                 *
                 * Convenience helper that builds a config from the filename,
                 * infers the backend from the file extension, and verifies
                 * that the backend supports reading.
                 *
                 * @param filename The path to the media file.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr on failure.
                 */
                static MediaIO *createForFileRead(const String &filename, ObjectBase *parent = nullptr);

                /**
                 * @brief Creates a MediaIO writer for the given filename.
                 *
                 * Convenience helper that builds a config from the filename,
                 * infers the backend from the file extension, and verifies
                 * that the backend supports writing.
                 *
                 * @param filename The path to the media file.
                 * @param parent Optional parent object.
                 * @return A new MediaIO instance, or nullptr on failure.
                 */
                static MediaIO *createForFileWrite(const String &filename, ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a MediaIO with an optional parent.
                 * @param parent The parent object, or nullptr.
                 */
                MediaIO(ObjectBase *parent = nullptr) : ObjectBase(parent) {}

                /**
                 * @brief Destructor.  Asserts that the resource is closed.
                 *
                 * Backends must call close() in their own destructor since
                 * derived members are destroyed before ~MediaIO() runs.
                 */
                ~MediaIO() override;

                /**
                 * @brief Returns the configuration used to create this instance.
                 * @return A const reference to the config.
                 */
                const Config &config() const {
                        return _config;
                }

                /**
                 * @brief Returns true if the resource is open.
                 * @return true if mode() is not NotOpen.
                 */
                bool isOpen() const {
                        return _mode != NotOpen;
                }

                /**
                 * @brief Returns the current open mode.
                 * @return The Mode value.
                 */
                Mode mode() const {
                        return _mode;
                }

                /**
                 * @brief Opens the media resource.
                 *
                 * Checks lifecycle state, delegates to onOpen(), and sets the
                 * mode on success.  Do not override — implement onOpen() instead.
                 *
                 * @param mode Reader or Writer.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error open(Mode mode);

                /**
                 * @brief Closes the media resource.
                 *
                 * Delegates to onClose() and resets the mode.  Do not override —
                 * implement onClose() instead.
                 *
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error close();

                // ---- Descriptors ----

                /**
                 * @brief Returns the media description.
                 *
                 * Valid after open() for readers.  Returns an invalid MediaDesc
                 * by default.
                 *
                 * @return The media description.
                 */
                virtual MediaDesc mediaDesc() const;

                /**
                 * @brief Sets the media description for writing.
                 *
                 * Call before open() for writers.
                 *
                 * @param desc The media description.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error setMediaDesc(const MediaDesc &desc);

                /**
                 * @brief Returns the frame rate.
                 *
                 * Defaults to mediaDesc().frameRate().  Override for backends
                 * that know their frame rate independent of the media descriptor.
                 *
                 * @return The frame rate.
                 */
                virtual FrameRate frameRate() const;

                /**
                 * @brief Returns the primary audio description.
                 *
                 * Returns the first entry from mediaDesc().audioList(), or an
                 * invalid AudioDesc if none.  Override for backends that manage
                 * audio description independently.
                 *
                 * @return The primary audio description.
                 */
                virtual AudioDesc audioDesc() const;

                /**
                 * @brief Sets the audio description for writing.
                 *
                 * Convenience for setting the primary audio stream.  The default
                 * implementation is a no-op returning NotSupported.
                 *
                 * @param desc The audio description.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error setAudioDesc(const AudioDesc &desc);

                /**
                 * @brief Returns the container-level metadata.
                 *
                 * Valid after open() for readers.  Returns empty Metadata by
                 * default.
                 *
                 * @return The metadata.
                 */
                virtual Metadata metadata() const;

                /**
                 * @brief Sets the container-level metadata for writing.
                 *
                 * Call before open() for writers.
                 *
                 * @param meta The metadata.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error setMetadata(const Metadata &meta);

                // ---- Frame I/O ----

                /**
                 * @brief Reads the next synchronized frame.
                 *
                 * Checks that the resource is open in Reader mode, then
                 * delegates to onReadFrame().  Do not override — implement
                 * onReadFrame() instead.
                 *
                 * @param frame The Frame to fill.
                 * @return Error::Ok on success, Error::EndOfFile at end, or an error.
                 */
                Error readFrame(Frame &frame);

                /**
                 * @brief Writes a frame to the media resource.
                 *
                 * Checks that the resource is open in Writer mode, then
                 * delegates to onWriteFrame().  Do not override — implement
                 * onWriteFrame() instead.
                 *
                 * @param frame The Frame containing video and audio to write.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error writeFrame(const Frame &frame);

                // ---- Navigation ----

                /**
                 * @brief Returns true if this backend supports seeking.
                 * @return true if seekToFrame() is available. Default: false.
                 */
                virtual bool canSeek() const;

                /**
                 * @brief Seeks to the given frame number.
                 *
                 * Only valid when canSeek() returns true.
                 *
                 * @param frameNumber The zero-based frame number to seek to.
                 * @return Error::Ok on success, or Error::IllegalSeek if not supported.
                 */
                virtual Error seekToFrame(int64_t frameNumber);

                /**
                 * @brief Returns the total number of frames.
                 *
                 * For readers, returns the total frames available.
                 * For writers, returns the number of frames written so far.
                 * Returns a negative sentinel (FrameCountUnknown, FrameCountInfinite,
                 * FrameCountError) for special cases.  0 means zero frames.
                 *
                 * @return The frame count or a negative sentinel.
                 */
                virtual int64_t frameCount() const;

                /**
                 * @brief Returns the number of frames read or written so far.
                 *
                 * Starts at 0, incremented after each successful readFrame()
                 * or writeFrame().  seekToFrame() sets it to the target.
                 *
                 * @return The frame counter.
                 */
                virtual uint64_t currentFrame() const;

                /**
                 * @brief Returns the current step increment.
                 *
                 * Controls how readFrame() advances the position after each
                 * read.  Defaults to 1 (forward playback).
                 *
                 * @return The step value.
                 */
                int step() const {
                        return _step;
                }

                /**
                 * @brief Sets the step increment.
                 *
                 * Controls how readFrame() advances the position.  Common values:
                 * - 1: normal forward playback (default).
                 * - -1: reverse playback.
                 * - 2: 2x fast-forward.
                 * - 0: re-read the same frame (hold).
                 *
                 * Override this to react to direction or speed changes
                 * (e.g. switching a hardware device to reverse mode, or
                 * adjusting cache prefetch direction).
                 *
                 * @param val The new step value.
                 */
                virtual void setStep(int val) {
                        _step = val;
                        return;
                }

                /** @brief Emitted when an error occurs. @signal */
                PROMEKI_SIGNAL(errorOccurred, Error);

                /**
                 * @brief Sets the configuration.
                 *
                 * Called by the factory before returning the new instance,
                 * or by external code that constructs a MediaIO directly.
                 * @param config The configuration to store.
                 */
                void setConfig(const Config &config) {
                        _config = config;
                        return;
                }

        protected:
                /**
                 * @brief Backend-specific open implementation.
                 *
                 * Called by open() after lifecycle checks pass.  The mode
                 * is already validated (not NotOpen, not already open).
                 * Return Error::Ok on success.
                 *
                 * @param mode Reader or Writer.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error onOpen(Mode mode);

                /**
                 * @brief Backend-specific close implementation.
                 *
                 * Called by close() before the mode is reset.
                 *
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error onClose();

                /**
                 * @brief Backend-specific read implementation.
                 *
                 * Called by readFrame() after verifying the resource is open
                 * in Reader mode.
                 *
                 * @param frame The Frame to fill.
                 * @return Error::Ok on success, Error::EndOfFile at end, or an error.
                 */
                virtual Error onReadFrame(Frame &frame);

                /**
                 * @brief Backend-specific write implementation.
                 *
                 * Called by writeFrame() after verifying the resource is open
                 * in Writer mode.
                 *
                 * @param frame The Frame containing video and audio to write.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error onWriteFrame(const Frame &frame);

        private:
                Config          _config;
                Mode            _mode = NotOpen;
                int             _step = 1;
};

PROMEKI_NAMESPACE_END
