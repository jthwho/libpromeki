/**
 * @file      libraryoptions.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/libraryoptions.h>
#include <promeki/env.h>
#include <promeki/regex.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {
        const String envPrefix = "PROMEKI_OPT_";
} // namespace

LibraryOptions &LibraryOptions::instance() {
        static LibraryOptions opts = []() {
                LibraryOptions db;
                // Touch each static ID to force registration, then
                // populate from their spec defaults.
                const ID ids[] = {CrashHandler,
                                  CoreDumps,
                                  CrashLogDir,
                                  CaptureEnvironment,
                                  TempDir,
                                  IpcDir,
                                  TerminationSignalHandler,
                                  SignalDoubleTapExit};
                for (const ID &id : ids) {
                        const VariantSpec *s = spec(id);
                        if (s != nullptr) {
                                const Variant &def = s->defaultValue();
                                if (def.isValid()) db.set(id, def);
                        }
                }
                return db;
        }();
        return opts;
}

void LibraryOptions::loadFromEnvironment() {
        Map<String, String> vars = Env::list(RegEx("^PROMEKI_OPT_"));
        vars.forEach([&](const String &envName, const String &value) {
                String optName = envName.mid(envPrefix.length());
                ID     id = ID::find(optName);
                if (!id.isValid()) {
                        promekiWarn("LibraryOptions: unknown option '%s' "
                                    "(from %s)",
                                    optName.cstr(), envName.cstr());
                        return;
                }
                const VariantSpec *s = spec(id);
                if (s == nullptr) {
                        promekiWarn("LibraryOptions: no spec for '%s'", optName.cstr());
                        return;
                }
                Error   err;
                Variant v = s->parseString(value, &err);
                if (err.isError()) {
                        promekiWarn("LibraryOptions: failed to parse '%s=%s': %s", optName.cstr(), value.cstr(),
                                    err.desc().cstr());
                        return;
                }
                set(id, std::move(v));
        });
}

PROMEKI_NAMESPACE_END
