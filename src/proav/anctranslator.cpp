/**
 * @file      anctranslator.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Implements the three registries that back @ref AncTranslator's
 * parse / build / translate dispatch.  Registrations are expected at
 * static-init time; each map is guarded by an internal Mutex so
 * registrations across TUs and the read path stay race-free.
 */

#include <promeki/anctranslator.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // ----------------------------------------------------------------------
        // Registry keys.  We can't use AncTransport directly because Enum has no
        // operator<; we project to its int value, which is stable for a given
        // build.  Tiny structs keep the call sites readable.
        // ----------------------------------------------------------------------

        struct ParserKey {
                        AncFormat::ID format;
                        int           srcTransport;

                        bool operator<(const ParserKey &o) const {
                                if (format != o.format) return format < o.format;
                                return srcTransport < o.srcTransport;
                        }
        };

        struct BuilderKey {
                        AncFormat::ID format;
                        int           dstTransport;

                        bool operator<(const BuilderKey &o) const {
                                if (format != o.format) return format < o.format;
                                return dstTransport < o.dstTransport;
                        }
        };

        struct TranslatorKey {
                        AncFormat::ID format;
                        int           srcTransport;
                        int           dstTransport;

                        bool operator<(const TranslatorKey &o) const {
                                if (format != o.format) return format < o.format;
                                if (srcTransport != o.srcTransport) return srcTransport < o.srcTransport;
                                return dstTransport < o.dstTransport;
                        }
        };

        // Process-wide registry singleton.  One Mutex covers all three maps —
        // registrations happen at static-init and dispatch traffic is light
        // relative to the cost of an ANC translate, so contention is a
        // non-issue.
        Mutex &registryMutex() {
                static Mutex m;
                return m;
        }

        Map<ParserKey, AncTranslator::ParserFn> &parserRegistry() {
                static Map<ParserKey, AncTranslator::ParserFn> r;
                return r;
        }

        Map<BuilderKey, AncTranslator::BuilderFn> &builderRegistry() {
                static Map<BuilderKey, AncTranslator::BuilderFn> r;
                return r;
        }

        Map<TranslatorKey, AncTranslator::TranslatorFn> &translatorRegistry() {
                static Map<TranslatorKey, AncTranslator::TranslatorFn> r;
                return r;
        }

        // Snapshot lookups: copy the function pointer out under the lock, then
        // call without holding it.  Keeps the lock window minimal and matches
        // MediaPayload's createEmpty pattern.

        AncTranslator::ParserFn findParser(AncFormat::ID format, const AncTransport &src) {
                ParserKey            key{format, src.value()};
                Mutex::Locker        lock(registryMutex());
                auto                &r = parserRegistry();
                auto                 it = r.find(key);
                if (it == r.end()) return nullptr;
                return it->second;
        }

        AncTranslator::BuilderFn findBuilder(AncFormat::ID format, const AncTransport &dst) {
                BuilderKey            key{format, dst.value()};
                Mutex::Locker         lock(registryMutex());
                auto                 &r = builderRegistry();
                auto                  it = r.find(key);
                if (it == r.end()) return nullptr;
                return it->second;
        }

        AncTranslator::TranslatorFn findTranslator(AncFormat::ID format, const AncTransport &src,
                                                    const AncTransport &dst) {
                TranslatorKey          key{format, src.value(), dst.value()};
                Mutex::Locker          lock(registryMutex());
                auto                  &r = translatorRegistry();
                auto                   it = r.find(key);
                if (it == r.end()) return nullptr;
                return it->second;
        }

} // namespace

// ============================================================================
// Registration
// ============================================================================

void AncTranslator::registerParser(AncFormat::ID format, const AncTransport &src, ParserFn fn) {
        if (fn == nullptr) return;
        ParserKey      key{format, src.value()};
        Mutex::Locker  lock(registryMutex());
        auto          &r = parserRegistry();
        if (r.contains(key)) {
                promekiWarn("AncTranslator: re-registering parser for format=%d transport=%s",
                            static_cast<int>(format), src.valueName().cstr());
        }
        r.insert(key, fn);
}

void AncTranslator::registerBuilder(AncFormat::ID format, const AncTransport &dst, BuilderFn fn) {
        if (fn == nullptr) return;
        BuilderKey    key{format, dst.value()};
        Mutex::Locker lock(registryMutex());
        auto         &r = builderRegistry();
        if (r.contains(key)) {
                promekiWarn("AncTranslator: re-registering builder for format=%d transport=%s",
                            static_cast<int>(format), dst.valueName().cstr());
        }
        r.insert(key, fn);
}

void AncTranslator::registerTranslator(AncFormat::ID format, const AncTransport &src, const AncTransport &dst,
                                        TranslatorFn fn) {
        if (fn == nullptr) return;
        TranslatorKey key{format, src.value(), dst.value()};
        Mutex::Locker lock(registryMutex());
        auto         &r = translatorRegistry();
        if (r.contains(key)) {
                promekiWarn("AncTranslator: re-registering translator for format=%d src=%s dst=%s",
                            static_cast<int>(format), src.valueName().cstr(), dst.valueName().cstr());
        }
        r.insert(key, fn);
}

// ============================================================================
// Dispatch
// ============================================================================

Result<Variant> AncTranslator::parse(const AncPacket &pkt) const {
        ParserFn fn = findParser(pkt.format().id(), pkt.transport());
        if (fn == nullptr) {
                return makeError<Variant>(Error::NotSupported);
        }
        return fn(pkt, _cfg);
}

Result<AncPacket> AncTranslator::build(const Variant &v, const AncFormat &fmt,
                                        const AncTransport &target) const {
        BuilderFn fn = findBuilder(fmt.id(), target);
        if (fn == nullptr) {
                return makeError<AncPacket>(Error::NotSupported);
        }
        return fn(v, _cfg);
}

Result<AncPacket> AncTranslator::translate(const AncPacket &pkt, const AncTransport &target) const {
        // Identity short-circuit: same transport in and out, return as-is.
        if (pkt.transport() == target) {
                return makeResult<AncPacket>(pkt);
        }

        const AncFormat::ID fmtId = pkt.format().id();

        // Prefer a registered direct translator.
        if (TranslatorFn direct = findTranslator(fmtId, pkt.transport(), target); direct != nullptr) {
                return direct(pkt, target, _cfg);
        }

        // Composed path: parse on src, build on dst.
        ParserFn p = findParser(fmtId, pkt.transport());
        if (p == nullptr) return makeError<AncPacket>(Error::NotSupported);
        BuilderFn b = findBuilder(fmtId, target);
        if (b == nullptr) return makeError<AncPacket>(Error::NotSupported);

        Result<Variant> parsed = p(pkt, _cfg);
        if (parsed.second().isError()) return makeError<AncPacket>(parsed.second());
        return b(parsed.first(), _cfg);
}

// ============================================================================
// Static capability queries
// ============================================================================

bool AncTranslator::hasParser(const AncFormat &format, const AncTransport &src) {
        return findParser(format.id(), src) != nullptr;
}

bool AncTranslator::hasBuilder(const AncFormat &format, const AncTransport &dst) {
        return findBuilder(format.id(), dst) != nullptr;
}

bool AncTranslator::hasDirectTranslator(const AncFormat &format, const AncTransport &src,
                                         const AncTransport &dst) {
        return findTranslator(format.id(), src, dst) != nullptr;
}

bool AncTranslator::canTranslate(const AncFormat &format, const AncTransport &src, const AncTransport &dst) {
        if (src == dst) return true;
        if (hasDirectTranslator(format, src, dst)) return true;
        return hasParser(format, src) && hasBuilder(format, dst);
}

List<AncTransport> AncTranslator::registeredParserTransports(const AncFormat &format) {
        List<AncTransport> out;
        AncFormat::ID      id = format.id();
        Mutex::Locker      lock(registryMutex());
        auto              &r = parserRegistry();
        for (auto it = r.cbegin(); it != r.cend(); ++it) {
                if (it->first.format == id) out.pushToBack(AncTransport(it->first.srcTransport));
        }
        return out;
}

List<AncTransport> AncTranslator::registeredBuilderTransports(const AncFormat &format) {
        List<AncTransport> out;
        AncFormat::ID      id = format.id();
        Mutex::Locker      lock(registryMutex());
        auto              &r = builderRegistry();
        for (auto it = r.cbegin(); it != r.cend(); ++it) {
                if (it->first.format == id) out.pushToBack(AncTransport(it->first.dstTransport));
        }
        return out;
}

PROMEKI_NAMESPACE_END
