/**
 * @file      mediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/frame.h>
#include <promeki/videodesc.h>
#include <promeki/metadata.h>
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
 *
 * @par Config-driven creation
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaIO::ConfigType, "MXF");
 * cfg.set(MediaIO::ConfigFilename, "/path/to/clip.mxf");
 * MediaIO *io = MediaIO::create(cfg);
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
                 *
                 * @par Example
                 * @code
                 * MediaIO::Config cfg;
                 * cfg.set(MediaIO::ConfigType, "DPXSequence");
                 * cfg.set(MediaIO::ConfigFilename, "/path/to/frame.%06d.dpx");
                 * MediaIO *io = MediaIO::create(cfg);
                 * @endcode
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
                 *
                 * @par Example
                 * @code
                 * MediaIO *io = MediaIO::createForFileRead("clip.mxf");
                 * if(io) {
                 *         io->open(MediaIO::Reader);
                 *         // ...
                 * }
                 * @endcode
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
                 *
                 * @par Example
                 * @code
                 * MediaIO *io = MediaIO::createForFileWrite("output.mxf");
                 * if(io) {
                 *         io->setVideoDesc(myDesc);
                 *         io->open(MediaIO::Writer);
                 *         // ...
                 * }
                 * @endcode
                 */
                static MediaIO *createForFileWrite(const String &filename, ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a MediaIO with an optional parent.
                 * @param parent The parent object, or nullptr.
                 */
                MediaIO(ObjectBase *parent = nullptr) : ObjectBase(parent) {}

                /** @brief Virtual destructor. */
                virtual ~MediaIO();

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
                 * For readers, media description is available after a successful
                 * open.  For writers, setVideoDesc() and setMetadata() should be
                 * called before open().
                 *
                 * @param mode Reader or Writer.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error open(Mode mode);

                /**
                 * @brief Closes the media resource.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error close();

                /**
                 * @brief Returns the video description of the media.
                 *
                 * Valid after open() for readers.  Returns an invalid VideoDesc
                 * by default.
                 *
                 * @return The video description.
                 */
                virtual VideoDesc videoDesc() const;

                /**
                 * @brief Sets the video description for writing.
                 *
                 * Call before open() for writers.
                 *
                 * @param desc The video description.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error setVideoDesc(const VideoDesc &desc);

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

                /**
                 * @brief Reads the next synchronized frame.
                 *
                 * Fills @p frame with all video and audio content for the
                 * current position.  Returns Error::EndOfFile when no more
                 * frames are available.
                 *
                 * @param frame The Frame to fill.
                 * @return Error::Ok on success, Error::EndOfFile at end, or an error.
                 */
                virtual Error readFrame(Frame &frame);

                /**
                 * @brief Writes a frame to the media resource.
                 *
                 * @param frame The Frame containing video and audio to write.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error writeFrame(const Frame &frame);

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
                virtual Error seekToFrame(uint64_t frameNumber);

                /**
                 * @brief Returns the total number of frames in the media.
                 *
                 * May return 0 for live or unknown-length sources.
                 *
                 * @return The frame count.
                 */
                virtual uint64_t frameCount() const;

                /**
                 * @brief Returns the current frame position.
                 * @return The zero-based frame number of the next frame to be read or written.
                 */
                virtual uint64_t currentFrame() const;

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
                 * @brief Sets the open mode.
                 *
                 * Call from subclass open() and close() implementations.
                 * @param mode The mode to set.
                 */
                void setMode(Mode mode) {
                        _mode = mode;
                        return;
                }

        private:
                Config          _config;
                Mode            _mode = NotOpen;
};

PROMEKI_NAMESPACE_END
