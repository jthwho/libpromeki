/**
 * @file      anctranslator.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Implements the four registries that back @ref AncTranslator's
 * parse / parseGroup / build / translate dispatch.  Registrations are
 * expected at static-init time; each map is guarded by an internal
 * Mutex so registrations across TUs and the read path stay race-free.
 */

#include <promeki/anctranslator.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/result.h>
#include <promeki/util.h>

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

        // Process-wide registry singleton.  One Mutex covers all four maps —
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

        Map<ParserKey, AncTranslator::MultiParserFn> &multiParserRegistry() {
                static Map<ParserKey, AncTranslator::MultiParserFn> r;
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

        // Sync-policy registry is keyed on AncFormat::ID alone — no transport
        // dimension, since frame-rate-conversion semantics are
        // transport-independent.
        Map<AncFormat::ID, AncTranslator::SyncPolicyFn> &syncPolicyRegistry() {
                static Map<AncFormat::ID, AncTranslator::SyncPolicyFn> r;
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

        AncTranslator::MultiParserFn findMultiParser(AncFormat::ID format, const AncTransport &src) {
                ParserKey            key{format, src.value()};
                Mutex::Locker        lock(registryMutex());
                auto                &r = multiParserRegistry();
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

        AncTranslator::SyncPolicyFn findSyncPolicy(AncFormat::ID format) {
                Mutex::Locker  lock(registryMutex());
                auto          &r = syncPolicyRegistry();
                auto           it = r.find(format);
                if (it == r.end()) return nullptr;
                return it->second;
        }

} // namespace

// ============================================================================
// Registration
// ============================================================================

// ============================================================================
// Registry-collision handling
// ============================================================================
//
// Two distinct cases we want to handle differently:
//
//  1. **Same key, same fn pointer.**  Idempotent re-registration —
//     legitimate when a library is linked twice through a static-init
//     graph or when test fixtures re-install their stubs.  The
//     dispatcher's final state is identical either way, so we treat
//     this as a silent no-op.
//
//  2. **Same key, different fn pointer.**  Programming error: two
//     TUs disagree about which codec owns the (format, transport)
//     slot, and link order silently picks one.  Loud failure is the
//     right policy:
//
//      - DEBUG / DevRelease builds (@c PROMEKI_DEBUG_ENABLE defined)
//        @c PROMEKI_ASSERT(false) — the assert throws and unwinds out
//        of the static-init runtime as @c std::terminate, crashing
//        the process loudly so the build never ships with silent
//        codec replacement.
//      - Plain Release builds keep "warn + last-wins" so a misbehaving
//        downstream deployment is not bricked by an upstream sanity
//        check; ops see the warn in the log and fix it before it
//        matters.

namespace {
template <typename Map, typename Key, typename Fn>
[[maybe_unused]] inline bool checkRegistryCollision(Map &r, const Key &key, Fn fn, const char *what,
                                                    const String &detail) {
        if (!r.contains(key)) return false;
        if (r.value(key) == fn) {
                // Idempotent — same fn at the same key.  No-op.
                return true;
        }
        promekiWarn("AncTranslator: re-registering %s with a different fn — %s",
                    what, detail.cstr());
#ifdef PROMEKI_DEBUG_ENABLE
        PROMEKI_ASSERT(false /* AncTranslator registry collision (see warn above) */);
#endif
        return false;
}
} // namespace

void AncTranslator::registerParser(AncFormat::ID format, const AncTransport &src, ParserFn fn) {
        if (fn == nullptr) return;
        ParserKey      key{format, src.value()};
        Mutex::Locker  lock(registryMutex());
        auto          &r = parserRegistry();
        if (checkRegistryCollision(r, key, fn, "parser",
                                   String::sprintf("format=%d transport=%s",
                                                    static_cast<int>(format),
                                                    src.valueName().cstr()))) {
                return;
        }
        r.insert(key, fn);
}

void AncTranslator::registerMultiParser(AncFormat::ID format, const AncTransport &src, MultiParserFn fn) {
        if (fn == nullptr) return;
        ParserKey      key{format, src.value()};
        Mutex::Locker  lock(registryMutex());
        auto          &r = multiParserRegistry();
        if (checkRegistryCollision(r, key, fn, "multi-parser",
                                   String::sprintf("format=%d transport=%s",
                                                    static_cast<int>(format),
                                                    src.valueName().cstr()))) {
                return;
        }
        r.insert(key, fn);
}

void AncTranslator::registerBuilder(AncFormat::ID format, const AncTransport &dst, BuilderFn fn) {
        if (fn == nullptr) return;
        BuilderKey    key{format, dst.value()};
        Mutex::Locker lock(registryMutex());
        auto         &r = builderRegistry();
        if (checkRegistryCollision(r, key, fn, "builder",
                                   String::sprintf("format=%d transport=%s",
                                                    static_cast<int>(format),
                                                    dst.valueName().cstr()))) {
                return;
        }
        r.insert(key, fn);
}

void AncTranslator::registerTranslator(AncFormat::ID format, const AncTransport &src, const AncTransport &dst,
                                        TranslatorFn fn) {
        if (fn == nullptr) return;
        TranslatorKey key{format, src.value(), dst.value()};
        Mutex::Locker lock(registryMutex());
        auto         &r = translatorRegistry();
        if (checkRegistryCollision(r, key, fn, "translator",
                                   String::sprintf("format=%d src=%s dst=%s",
                                                    static_cast<int>(format),
                                                    src.valueName().cstr(),
                                                    dst.valueName().cstr()))) {
                return;
        }
        r.insert(key, fn);
}

void AncTranslator::registerSyncPolicy(AncFormat::ID format, SyncPolicyFn fn) {
        if (fn == nullptr) return;
        Mutex::Locker  lock(registryMutex());
        auto          &r = syncPolicyRegistry();
        if (checkRegistryCollision(r, format, fn, "sync policy",
                                   String::sprintf("format=%d", static_cast<int>(format)))) {
                return;
        }
        r.insert(format, fn);
}

// ============================================================================
// Dispatch
// ============================================================================

AncTranslator::ParseResult AncTranslator::parse(const AncPacket &pkt) const {
        const AncFormat::ID fmtId = pkt.format().id();
        // Multi-parsers take precedence on a single-packet entry point — the
        // wire packet may be one segment of an N-packet message, but the
        // codec is the only thing that knows whether N == 1 or N > 1.
        // Wrap the single packet in a one-element list and dispatch.
        if (MultiParserFn mfn = findMultiParser(fmtId, pkt.transport()); mfn != nullptr) {
                AncPacket::List pkts;
                pkts.pushToBack(pkt);
                return mfn(pkts, _cfg);
        }
        ParserFn fn = findParser(fmtId, pkt.transport());
        if (fn == nullptr) return makeError<Variant>(Error::NotSupported);
        return fn(pkt, _cfg);
}

AncTranslator::ParseResult AncTranslator::parseGroup(const AncPacket::List &pkts) const {
        if (pkts.isEmpty()) return makeError<Variant>(Error::InvalidArgument);
        const AncFormat::ID fmtId = pkts.front().format().id();
        const AncTransport &xport = pkts.front().transport();
        if (MultiParserFn mfn = findMultiParser(fmtId, xport); mfn != nullptr) {
                return mfn(pkts, _cfg);
        }
        // No multi-parser — single-packet ParserFn only handles size-1 lists.
        if (pkts.size() != 1) return makeError<Variant>(Error::NotSupported);
        ParserFn fn = findParser(fmtId, xport);
        if (fn == nullptr) return makeError<Variant>(Error::NotSupported);
        return fn(pkts.front(), _cfg);
}

AncTranslator::PacketsResult AncTranslator::build(const Variant &v, const AncFormat &fmt,
                                                   const AncTransport &target) const {
        BuilderFn fn = findBuilder(fmt.id(), target);
        if (fn == nullptr) {
                return makeError<AncPacket::List>(Error::NotSupported);
        }
        return fn(v, _cfg);
}

AncTranslator::PacketsResult AncTranslator::translate(const AncPacket &pkt,
                                                       const AncTransport &target) const {
        // Identity short-circuit: same transport in and out, return in a
        // one-element list.
        if (pkt.transport() == target) {
                AncPacket::List out;
                out.pushToBack(pkt);
                return makeResult<AncPacket::List>(std::move(out));
        }

        const AncFormat::ID fmtId = pkt.format().id();

        // Prefer a registered direct translator.
        if (TranslatorFn direct = findTranslator(fmtId, pkt.transport(), target); direct != nullptr) {
                return direct(pkt, target, _cfg);
        }

        // Composed path: parse on src, build on dst.  Note: this path only
        // works for single-packet messages — multi-packet codecs must
        // either register a direct translator, or callers must use
        // parseGroup + build directly.
        ParseResult parsed = parse(pkt);
        if (parsed.second().isError()) return makeError<AncPacket::List>(parsed.second());
        BuilderFn b = findBuilder(fmtId, target);
        if (b == nullptr) return makeError<AncPacket::List>(Error::NotSupported);
        return b(parsed.first(), _cfg);
}

AncTranslator::PacketsResult AncTranslator::applySyncPolicy(const AncPacket     &pkt,
                                                             FrameSyncDisposition disposition,
                                                             uint8_t              repeatIndex) const {
        const AncFormat::ID fmtId = pkt.format().id();
        if (SyncPolicyFn fn = findSyncPolicy(fmtId); fn != nullptr) {
                return fn(pkt, disposition, repeatIndex, _cfg);
        }
        // Default fallback: copy through on Play / Repeat, drop on Drop.
        // No log here — AncFrameSync handles once-per-format warning when
        // it dispatches.  Keeping this layer silent lets callers use the
        // method as a pure transform without log noise.
        AncPacket::List out;
        if (disposition.kind() != FrameSyncDisposition::Drop) {
                out.pushToBack(pkt);
        }
        return makeResult<AncPacket::List>(std::move(out));
}

// ============================================================================
// Static capability queries
// ============================================================================

bool AncTranslator::hasParser(const AncFormat &format, const AncTransport &src) {
        return findParser(format.id(), src) != nullptr ||
               findMultiParser(format.id(), src) != nullptr;
}

bool AncTranslator::hasMultiParser(const AncFormat &format, const AncTransport &src) {
        return findMultiParser(format.id(), src) != nullptr;
}

bool AncTranslator::hasBuilder(const AncFormat &format, const AncTransport &dst) {
        return findBuilder(format.id(), dst) != nullptr;
}

bool AncTranslator::hasDirectTranslator(const AncFormat &format, const AncTransport &src,
                                         const AncTransport &dst) {
        return findTranslator(format.id(), src, dst) != nullptr;
}

bool AncTranslator::hasSyncPolicy(const AncFormat &format) {
        return findSyncPolicy(format.id()) != nullptr;
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
        {
                auto &r = parserRegistry();
                for (auto it = r.cbegin(); it != r.cend(); ++it) {
                        if (it->first.format == id) out.pushToBack(AncTransport(it->first.srcTransport));
                }
        }
        {
                auto &r = multiParserRegistry();
                for (auto it = r.cbegin(); it != r.cend(); ++it) {
                        if (it->first.format == id) {
                                AncTransport xport(it->first.srcTransport);
                                // Deduplicate with single-parser entries.
                                bool already = false;
                                for (const auto &x : out) {
                                        if (x.value() == xport.value()) {
                                                already = true;
                                                break;
                                        }
                                }
                                if (!already) out.pushToBack(xport);
                        }
                }
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
