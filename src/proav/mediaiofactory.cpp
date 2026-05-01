/**
 * @file      mediaiofactory.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaiofactory.h>

#include <promeki/logger.h>
#include <promeki/mediaio.h>
#include <promeki/variantspec.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MediaIOFactory)

namespace {

// Process-wide registry of every MediaIOFactory ever constructed via
// PROMEKI_REGISTER_MEDIAIO_FACTORY.  Lifetime matches the running
// process; entries are owned by the registry (allocated by the macro,
// deleted by the @ref RegistryHolder destructor at process exit so
// valgrind doesn't report them as leaked).
struct RegistryHolder {
                List<MediaIOFactory *> factories;

                ~RegistryHolder() {
                        // Delete in reverse order — matches ctor
                        // ordering for backends with internal cross-
                        // references (none today, but keeps the
                        // lifetime story trivially correct).
                        for (size_t i = factories.size(); i > 0; --i) {
                                delete factories[i - 1];
                        }
                }
};

RegistryHolder &registryHolder() {
        static RegistryHolder h;
        return h;
}

} // namespace

int MediaIOFactory::registerFactory(MediaIOFactory *factory) {
        if (factory == nullptr) return -1;
        List<MediaIOFactory *> &list = registryHolder().factories;
        const int               ret = static_cast<int>(list.size());
        list.pushToBack(factory);
        promekiDebug("Registered MediaIOFactory '%s'", factory->name().cstr());
        return ret;
}

const List<MediaIOFactory *> &MediaIOFactory::registeredFactories() {
        return registryHolder().factories;
}

const MediaIOFactory *MediaIOFactory::findByName(const String &name) {
        for (const MediaIOFactory *f : registeredFactories()) {
                if (f != nullptr && f->name() == name) return f;
        }
        return nullptr;
}

const MediaIOFactory *MediaIOFactory::findByExtension(const String &extension) {
        if (extension.isEmpty()) return nullptr;
        // Strip a leading dot so callers can pass either "mp4" or
        // ".mp4" — most call sites already strip but the registry is
        // a permissive layer on top.
        String needle = extension;
        if (!needle.isEmpty() && needle[0] == '.') needle = needle.substr(1);
        needle = needle.toLower();
        for (const MediaIOFactory *f : registeredFactories()) {
                if (f == nullptr) continue;
                for (const String &ext : f->extensions()) {
                        if (ext.toLower() == needle) return f;
                }
        }
        return nullptr;
}

const MediaIOFactory *MediaIOFactory::findByScheme(const String &scheme) {
        if (scheme.isEmpty()) return nullptr;
        // Compare case-insensitively on both sides per RFC 3986 —
        // schemes are case-insensitive.  The Url parser already
        // lowercases, but a registration could be in any case.
        const String needle = scheme.toLower();
        for (const MediaIOFactory *f : registeredFactories()) {
                if (f == nullptr) continue;
                for (const String &s : f->schemes()) {
                        if (s.toLower() == needle) return f;
                }
        }
        return nullptr;
}

const MediaIOFactory *MediaIOFactory::findForPath(const String &path) {
        if (path.isEmpty()) return nullptr;
        for (const MediaIOFactory *f : registeredFactories()) {
                if (f != nullptr && f->canHandlePath(path)) return f;
        }
        return nullptr;
}

// ============================================================================
// Registry-introspection convenience statics
// ============================================================================

MediaIOFactory::Config MediaIOFactory::defaultConfig(const String &typeName) {
        const MediaIOFactory *factory = findByName(typeName);
        if (factory == nullptr) return Config();
        Config cfg;
        cfg.setValidation(SpecValidation::None);
        Config::SpecMap specs = factory->configSpecs();
        for (auto it = specs.cbegin(); it != specs.cend(); ++it) {
                const Variant &def = it->second.defaultValue();
                if (def.isValid()) cfg.set(it->first, def);
        }
        cfg.setValidation(SpecValidation::Warn);
        cfg.set(MediaConfig::Type, typeName);
        return cfg;
}

MediaIOFactory::Config::SpecMap MediaIOFactory::configSpecs(const String &typeName) {
        const MediaIOFactory *factory = findByName(typeName);
        return factory != nullptr ? factory->configSpecs() : Config::SpecMap();
}

Metadata MediaIOFactory::defaultMetadata(const String &typeName) {
        const MediaIOFactory *factory = findByName(typeName);
        return factory != nullptr ? factory->defaultMetadata() : Metadata();
}

StringList MediaIOFactory::enumerate(const String &typeName) {
        const MediaIOFactory *factory = findByName(typeName);
        return factory != nullptr ? factory->enumerate() : StringList();
}

List<MediaDesc> MediaIOFactory::queryDevice(const String &typeName, const Config &config) {
        const MediaIOFactory *factory = findByName(typeName);
        return factory != nullptr ? factory->queryDevice(config) : List<MediaDesc>();
}

void MediaIOFactory::printDeviceInfo(const String &typeName, const Config &config) {
        const MediaIOFactory *factory = findByName(typeName);
        if (factory != nullptr) factory->printDeviceInfo(config);
}

StringList MediaIOFactory::unknownConfigKeys(const String &typeName, const Config &cfg) {
        // Detection is intentionally spec-driven: pull the backend's
        // spec map once, and let VariantDatabase::unknownKeys fall back
        // to the global MediaConfig spec registry for common keys like
        // Filename / Type / Name / Uuid.  No backend-specific knowledge
        // is hard-coded here — a brand-new backend gets key validation
        // for free the moment it publishes a configSpecs override.
        Config::SpecMap specs = configSpecs(typeName);
        return cfg.unknownKeys(specs);
}

Error MediaIOFactory::validateConfigKeys(const String &typeName, const Config &cfg, ConfigValidation mode,
                                         const String &contextLabel) {
        StringList unknown = unknownConfigKeys(typeName, cfg);
        if (unknown.isEmpty()) return Error::Ok;

        // One log line per unknown key so a caller's grep-friendly log
        // shows each typo individually.  The contextLabel lets the
        // caller embed its own scope (e.g. "mediaplay: input[TPG]")
        // without the framework having to know anything about the
        // caller — MediaIOFactory stays caller-agnostic.
        const char *modeTag = (mode == ConfigValidation::Strict) ? "rejecting" : "ignoring";
        for (size_t i = 0; i < unknown.size(); ++i) {
                if (contextLabel.isEmpty()) {
                        promekiWarn("MediaIO[%s]: unknown config key '%s' (%s)", typeName.cstr(), unknown[i].cstr(),
                                    modeTag);
                } else {
                        promekiWarn("%s: MediaIO[%s]: unknown config key '%s' (%s)", contextLabel.cstr(),
                                    typeName.cstr(), unknown[i].cstr(), modeTag);
                }
        }
        return (mode == ConfigValidation::Strict) ? Error::InvalidArgument : Error::Ok;
}

PROMEKI_NAMESPACE_END
