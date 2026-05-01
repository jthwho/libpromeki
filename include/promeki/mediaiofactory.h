/**
 * @file      mediaiofactory.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/url.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;
class MediaIO;
class ObjectBase;

/**
 * @brief Macro to register a @ref MediaIOFactory at static initialization.
 * @ingroup mediaio_user
 *
 * Allocates a single process-lifetime instance of @p FactoryClass and
 * registers it with @ref MediaIOFactory::registerFactory.
 *
 * @param FactoryClass A concrete @ref MediaIOFactory subclass with a
 *                     default constructor.
 */
#define PROMEKI_REGISTER_MEDIAIO_FACTORY(FactoryClass)                                                                 \
        [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_mediaio_factory_, PROMEKI_UNIQUE_ID) =                    \
                MediaIOFactory::registerFactory(new FactoryClass());

/**
 * @brief Polymorphic factory that creates @ref MediaIO instances for a
 *        given backend identity.
 * @ingroup mediaio_user
 *
 * Each backend declares one concrete @c XxxFactory subclass that
 * returns identity, role flags, config specs, discovery surfaces, and
 * constructs the matching @ref MediaIO instance from a
 * @ref MediaConfig.  Lookup is name- / extension- / scheme- /
 * path-based (see the @c findBy* statics).
 *
 * @par Lifetime
 * Factories are owned by the registry and live for the entire process.
 * The single instance per backend is allocated by
 * @ref PROMEKI_REGISTER_MEDIAIO_FACTORY at static init time and
 * deleted at process exit.
 *
 * @par Thread safety
 * Factory implementations must be thread-safe — multiple threads may
 * query the registry concurrently.  Practically every override is a
 * pure function returning a constant, so this is trivially satisfied
 * by stateless subclasses.
 */
class MediaIOFactory {
        public:
                /** @brief Configuration database type. */
                using Config = MediaConfig;

                /** @brief Virtual destructor for polymorphic ownership. */
                virtual ~MediaIOFactory() = default;

                // ---- Identity ----

                /** @brief Backend name (e.g. "TPG", "MXF").  Required. */
                virtual String name() const = 0;

                /** @brief Human-readable label.  Default: same as @ref name. */
                virtual String displayName() const { return name(); }

                /** @brief Human-readable description.  Default: empty. */
                virtual String description() const { return String(); }

                // ---- Discovery surfaces ----

                /** @brief Supported file extensions (no leading dot). */
                virtual StringList extensions() const { return StringList(); }

                /** @brief Claimed URL schemes (lowercase, no trailing colon). */
                virtual StringList schemes() const { return StringList(); }

                /**
                 * @brief Returns true if this backend can handle @p path.
                 *
                 * Used to route device-style filesystem paths (e.g.
                 * @c "/dev/video0") to the right backend before the
                 * extension-based dispatcher runs.  Default returns
                 * @c false.
                 */
                virtual bool canHandlePath(const String &path) const {
                        (void)path;
                        return false;
                }

                /**
                 * @brief Returns true if this backend can handle the
                 *        content of an open IODevice.
                 *
                 * Used as a content-based probe when neither the URL
                 * scheme nor the file extension uniquely identifies
                 * the backend.  Default returns @c false.
                 */
                virtual bool canHandleDevice(IODevice *device) const {
                        (void)device;
                        return false;
                }

                /**
                 * @brief Lists available instances for device-style backends.
                 *
                 * For capture cards / video cameras, returns the locator
                 * strings (suitable for use as @c MediaConfig::Filename)
                 * for each available device.  Default returns empty.
                 */
                virtual StringList enumerate() const { return StringList(); }

                // ---- Role flags ----

                /** @brief True if the backend can act as a source. */
                virtual bool canBeSource() const { return false; }

                /** @brief True if the backend can act as a sink. */
                virtual bool canBeSink() const { return false; }

                /** @brief True if the backend can act as a transform (source + sink). */
                virtual bool canBeTransform() const { return false; }

                // ---- Configuration ----

                /**
                 * @brief Returns the config specs for this backend.
                 *
                 * Lists every @ref MediaConfig::ID the backend
                 * understands paired with a @ref VariantSpec carrying
                 * defaults, accepted types, ranges, and descriptions.
                 */
                virtual Config::SpecMap configSpecs() const { return Config::SpecMap(); }

                /**
                 * @brief Returns the metadata schema this backend honors.
                 *
                 * Lists every @ref Metadata::ID the backend consumes,
                 * pre-populated with empty / default values.
                 */
                virtual Metadata defaultMetadata() const { return Metadata(); }

                /**
                 * @brief Translates a parsed URL into a @ref MediaConfig.
                 *
                 * Called when @ref MediaIO::createFromUrl dispatches to
                 * this backend via its registered @ref schemes.
                 * Backends populate @p outConfig from @p url's
                 * authority and path; query parameters are applied
                 * automatically by the framework.  Default returns
                 * @c Error::NotSupported.
                 */
                virtual Error urlToConfig(const Url &url, Config *outConfig) const {
                        (void)url;
                        (void)outConfig;
                        return Error::NotSupported;
                }

                /**
                 * @brief Optional bridge declaration for transform backends.
                 *
                 * Returns @c true if an instance can convert from
                 * @p from to @p to.  When @c true the implementation
                 * populates @p outConfig with the @ref MediaConfig the
                 * planner should use and @p outCost with the unitless
                 * conversion cost (lower = higher quality).  Default
                 * returns @c false.
                 */
                virtual bool bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig,
                                    int *outCost) const {
                        (void)from;
                        (void)to;
                        (void)outConfig;
                        (void)outCost;
                        return false;
                }

                // ---- Device introspection ----

                /**
                 * @brief Returns every supported configuration the
                 *        device can be opened with.
                 *
                 * Backends representing hardware devices implement
                 * this so callers can discover what the device offers
                 * without opening it.  @p config carries at least the
                 * device locator.  Default returns empty.
                 */
                virtual List<MediaDesc> queryDevice(const Config &config) const {
                        (void)config;
                        return List<MediaDesc>();
                }

                /**
                 * @brief Writes backend-specific device details to stdout.
                 *
                 * Called by CLI tools in probe / help modes.  Default
                 * is a no-op.
                 */
                virtual void printDeviceInfo(const Config &config) const { (void)config; }

                // ---- Construction ----

                /**
                 * @brief Constructs a fresh @ref MediaIO instance.
                 *
                 * The returned object is fully wired but not yet open
                 * — callers invoke @ref MediaIO::open afterward.  Must
                 * never return @c nullptr unless the backend genuinely
                 * cannot allocate state to open later (e.g. an unknown
                 * sub-format selected by @p config).  Caller takes
                 * ownership.
                 *
                 * @param config The backend configuration; the factory
                 *               passes @p config along to the new
                 *               instance via @ref MediaIO::setConfig.
                 * @param parent Optional ObjectBase parent.
                 * @return The newly constructed instance, or nullptr.
                 */
                virtual MediaIO *create(const Config &config, ObjectBase *parent = nullptr) const = 0;

                // ---- Static registry ----

                /**
                 * @brief Registers @p factory in the process-wide registry.
                 *
                 * Takes ownership of @p factory; the framework deletes
                 * it at process exit.  Returns the index assigned in
                 * the registry list.  Intended to be called only
                 * from @ref PROMEKI_REGISTER_MEDIAIO_FACTORY.
                 */
                static int registerFactory(MediaIOFactory *factory);

                /** @brief Returns the list of all registered factories. */
                static const List<MediaIOFactory *> &registeredFactories();

                /**
                 * @brief Finds the factory whose @ref name matches @p name.
                 * @return The matching factory, or nullptr.
                 */
                static const MediaIOFactory *findByName(const String &name);

                /**
                 * @brief Finds the first factory that claims @p extension.
                 *
                 * @p extension is matched case-insensitively against
                 * each registered factory's @ref extensions list.  The
                 * leading dot, if any, is stripped.  Returns nullptr
                 * if no factory claims the extension.
                 */
                static const MediaIOFactory *findByExtension(const String &extension);

                /**
                 * @brief Finds the first factory that claims URL scheme
                 *        @p scheme.
                 *
                 * Match is case-insensitive on both sides per RFC 3986.
                 * Returns nullptr when no factory claims the scheme.
                 */
                static const MediaIOFactory *findByScheme(const String &scheme);

                /**
                 * @brief Finds the first factory whose @ref canHandlePath
                 *        returns true for @p path.
                 *
                 * Returns nullptr if no factory claims the path.
                 */
                static const MediaIOFactory *findForPath(const String &path);

                // ---- Registry-introspection convenience statics ----
                //
                // Each of the following is a thin wrapper around
                // findByName(typeName)->...; they exist so callers can
                // query backend metadata by string name without first
                // resolving the factory pointer themselves.  All return
                // an empty / default value when the named backend is
                // not registered.

                /**
                 * @brief Returns the default configuration for the named backend.
                 *
                 * Builds a @ref MediaConfig by extracting the default
                 * value from each @ref VariantSpec in the backend's
                 * config spec map.  Sets @ref MediaConfig::Type to
                 * @p typeName so the resulting Config round-trips
                 * through @ref MediaIO::create.
                 *
                 * @param typeName The registered backend name (e.g. "TPG").
                 * @return A Config populated with default values; empty
                 *         when the backend is not registered.
                 */
                static Config defaultConfig(const String &typeName);

                /**
                 * @brief Returns the config specs for the named backend.
                 *
                 * @param typeName The registered backend name.
                 * @return A SpecMap, or an empty map if the backend is
                 *         not found.
                 */
                static Config::SpecMap configSpecs(const String &typeName);

                /**
                 * @brief Returns the metadata schema the named backend honors.
                 *
                 * @param typeName The registered backend name.
                 * @return A @ref Metadata listing the honored keys; empty
                 *         when the backend is not registered.
                 */
                static Metadata defaultMetadata(const String &typeName);

                /**
                 * @brief Lists available instances for the named backend.
                 *
                 * For device-style backends (capture cards, video
                 * cameras), returns the locator strings (e.g.
                 * @c "video0", @c "video1") that can be used as
                 * @c MediaConfig::Filename.
                 *
                 * @param typeName The registered backend name.
                 * @return A list of available instance locators; empty
                 *         when the backend is unknown or does not
                 *         support enumeration.
                 */
                static StringList enumerate(const String &typeName);

                /**
                 * @brief Queries a device for its supported configurations.
                 *
                 * @param typeName The registered backend name.
                 * @param config   Config carrying the device locator.
                 * @return A list of supported MediaDesc configurations;
                 *         empty when the backend does not implement
                 *         the query.
                 */
                static List<MediaDesc> queryDevice(const String &typeName, const Config &config);

                /**
                 * @brief Prints backend-specific device info to stdout.
                 *
                 * No-op when the backend is unknown or does not
                 * implement the callback.
                 */
                static void printDeviceInfo(const String &typeName, const Config &config);

                /**
                 * @brief Policy for @ref validateConfigKeys.
                 *
                 * Controls what @ref validateConfigKeys does when it
                 * finds a config key that has no spec registered —
                 * either in the named backend's spec map or in the
                 * global @ref MediaConfig spec registry.  The detection
                 * logic itself (@ref unknownConfigKeys) is unaffected
                 * by this enum.
                 */
                enum class ConfigValidation {
                        Lenient, ///< @brief Log each unknown key as a warning and return @c Error::Ok.
                        Strict   ///< @brief Log each unknown key as a warning and return @c Error::InvalidArgument.
                };

                /**
                 * @brief Returns the names of config keys in @p cfg that are
                 *        not recognized by the backend or the global registry.
                 *
                 * Walks @p cfg's stored keys and filters out any that
                 * have a @ref VariantSpec — either in the backend's
                 * spec map or in the global @ref MediaConfig spec
                 * registry.  The result is the set of leftover key
                 * names, sorted lexicographically for stable logging.
                 *
                 * Pure detection primitive.  Callers that want a
                 * ready-made error policy should use
                 * @ref validateConfigKeys instead.
                 *
                 * @param typeName The registered backend name.  When the
                 *                 backend is unknown or has no spec map,
                 *                 only the global registry is consulted.
                 * @param cfg      The configuration to check.
                 * @return The sorted list of unrecognized key names; an
                 *         empty list when every key is recognized.
                 */
                static StringList unknownConfigKeys(const String &typeName, const Config &cfg);

                /**
                 * @brief Validates a config's key set against the registered specs.
                 *
                 * Calls @ref unknownConfigKeys and then applies the
                 * error policy selected by @p mode:
                 *  - @ref ConfigValidation::Lenient — emit a warning
                 *    for each unknown key and return @c Error::Ok.
                 *  - @ref ConfigValidation::Strict — emit a warning
                 *    for each unknown key and return
                 *    @c Error::InvalidArgument.
                 *
                 * Warnings are emitted via @c promekiWarn and include
                 * the optional @p contextLabel so callers can make each
                 * line self-identifying (e.g. @c "mediaplay:
                 * input[TPG]").
                 *
                 * @param typeName     The registered backend name.
                 * @param cfg          The configuration to check.
                 * @param mode         The error policy to apply.
                 * @param contextLabel Optional prefix for log messages.
                 * @return @c Error::Ok or @c Error::InvalidArgument per
                 *         @p mode.  Always @c Error::Ok when there are
                 *         no unknown keys.
                 */
                static Error validateConfigKeys(const String &typeName, const Config &cfg, ConfigValidation mode,
                                                const String &contextLabel = String());
};

PROMEKI_NAMESPACE_END
