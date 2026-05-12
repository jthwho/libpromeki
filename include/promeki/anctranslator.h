/**
 * @file      anctranslator.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/enums.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/variant.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Stateful session that decodes, encodes, and translates
 *        @ref AncPacket between transports via registered handlers.
 * @ingroup proav
 *
 * A single @c AncTranslator instance holds an
 * @ref AncTranslateConfig and dispatches every call through three
 * process-wide registries:
 *
 *  - **Parsers** — @c (AncFormat, AncTransport src) ⇒ typed
 *    @ref Variant.  Decodes wire bytes into a typed value
 *    (`Cea708Cdp`, `Timecode`, `Scte104Message`, …).
 *  - **Builders** — @c (AncFormat, AncTransport dst) ⇒
 *    @ref AncPacket.  Encodes a typed Variant onto a target
 *    transport.
 *  - **Direct translators** — @c (AncFormat, AncTransport src,
 *    AncTransport dst) ⇒ @ref AncPacket.  Optional optimisation
 *    when wire-to-wire conversion can bypass the Variant
 *    round-trip.
 *
 * Registration is expected at static-init time (see the
 * @ref PROMEKI_REGISTER_ANC_PARSER, @ref PROMEKI_REGISTER_ANC_BUILDER,
 * and @ref PROMEKI_REGISTER_ANC_TRANSLATOR macros below); each
 * registry is guarded by an internal @c Mutex so registrations from
 * multiple translation units are race-free.  Lookups after static
 * init are read-mostly and rely on the same mutex's mutual
 * exclusion.
 *
 * @par Dispatch semantics
 *
 *  - @ref parse looks up the registered parser for the input
 *    packet's @c (format, transport) and returns
 *    @c Error::NotSupported when none is registered.
 *  - @ref build looks up the registered builder for the target
 *    @c (format, dst) and returns @c Error::NotSupported when none
 *    is registered.
 *  - @ref translate identity-short-circuits when
 *    @c pkt.transport() == target (returns the packet unchanged),
 *    otherwise prefers a registered direct translator for
 *    @c (format, src, dst), falling back to the composed
 *    @c parse() → @c build() chain.  Returns @c Error::NotSupported
 *    when no path exists.
 *
 * @par Held config
 *
 * The @c AncTranslateConfig held by value is threaded into every
 * handler call as a @c const&.  Per-call public APIs are config-free
 * so application code does not have to thread the bundle by hand.
 * Construct one translator per logical session
 * (per-sink-emit-loop is the canonical scope).
 *
 * @par Capability queries
 *
 * The static @c hasParser / @c hasBuilder / @c hasDirectTranslator /
 * @c canTranslate predicates do not require an instance — they read
 * the registries directly.  Used by sinks to enumerate what they can
 * actually emit before walking a payload.
 *
 * @par Example
 * @code
 * AncTranslateConfig cfg;
 * cfg.set(AncTranslateConfig::Checksum, AncChecksumPolicy::AlwaysRecompute);
 * AncTranslator t(cfg);
 *
 * // Decode an inbound packet to its typed Variant form.
 * Result<Variant> r = t.parse(myPacket);
 *
 * // Translate to a different transport.  Falls back to parse+build
 * // when no direct translator is registered.
 * Result<AncPacket> r2 = t.translate(myPacket, AncTransport::NdiXml);
 * @endcode
 *
 * @par Thread Safety
 * Registration and dispatch are internally synchronised; a
 * translator instance can therefore be shared across threads as
 * long as its held @c AncTranslateConfig is not mutated underneath
 * a concurrent caller.  Prefer constructing a fresh translator (or
 * copying an existing one) per emission strand.
 */
class AncTranslator {
        public:
                // -- Handler signatures ---------------------------------------

                /** @brief Parser: decode wire bytes into a typed Variant. */
                using ParserFn = Result<Variant> (*)(const AncPacket &, const AncTranslateConfig &);

                /** @brief Builder: encode a typed Variant onto a target transport. */
                using BuilderFn = Result<AncPacket> (*)(const Variant &, const AncTranslateConfig &);

                /**
                 * @brief Direct translator: wire-to-wire conversion that
                 *        bypasses the parse / build round-trip.
                 *
                 * Optional optimisation path; @c AncTranslator::translate
                 * falls back to the composed @c parse() → @c build()
                 * chain when no direct translator is registered.
                 */
                using TranslatorFn = Result<AncPacket> (*)(const AncPacket &, AncTransport,
                                                            const AncTranslateConfig &);

                // -- Construction --------------------------------------------

                /** @brief Constructs a translator with a default-constructed config. */
                AncTranslator() = default;

                /** @brief Constructs a translator holding the given config. */
                explicit AncTranslator(AncTranslateConfig cfg) : _cfg(std::move(cfg)) {}

                /** @brief Returns the held config. */
                const AncTranslateConfig &config() const { return _cfg; }

                /** @brief Replaces the held config. */
                void setConfig(AncTranslateConfig cfg) { _cfg = std::move(cfg); }

                // -- Dispatch -------------------------------------------------

                /**
                 * @brief Decodes a packet's wire bytes into a typed Variant.
                 *
                 * Dispatches to the registered parser for
                 * @c (pkt.format(), pkt.transport()) using the held
                 * config.
                 *
                 * @param pkt The packet to decode.
                 * @return The typed Variant on success, or
                 *         @c Error::NotSupported when no parser is
                 *         registered.
                 */
                Result<Variant> parse(const AncPacket &pkt) const;

                /**
                 * @brief Encodes a typed Variant onto the requested transport.
                 *
                 * Dispatches to the registered builder for @c (fmt,
                 * target) using the held config.
                 *
                 * @param v      The typed Variant carrying the format's
                 *               application-level payload (e.g.
                 *               @c Cea708Cdp, @c Timecode).
                 * @param fmt    The ANC format.
                 * @param target The target wire transport.
                 * @return       The produced packet on success, or
                 *               @c Error::NotSupported when no builder
                 *               is registered.
                 */
                Result<AncPacket> build(const Variant &v, const AncFormat &fmt,
                                        const AncTransport &target) const;

                /**
                 * @brief Translates a packet onto the requested transport.
                 *
                 * Identity-short-circuits when @c pkt.transport() ==
                 * @p target (returns @p pkt unchanged).  Otherwise
                 * prefers a registered direct translator for
                 * @c (pkt.format(), pkt.transport(), target); on miss,
                 * falls back to the composed @c parse(pkt) → @c
                 * build(variant, pkt.format(), target) chain.
                 *
                 * @param pkt    The packet to translate.
                 * @param target The target wire transport.
                 * @return       The translated packet on success, or
                 *               @c Error::NotSupported when no path
                 *               exists.
                 */
                Result<AncPacket> translate(const AncPacket &pkt, const AncTransport &target) const;

                // -- Static registration -------------------------------------

                /**
                 * @brief Registers a parser for @c (format, src).
                 *
                 * Re-registering an existing pair replaces the previous
                 * entry and logs a warning; the convention is "register
                 * only at static init."
                 *
                 * @param format ID of the ANC format the parser handles.
                 * @param src    Source wire transport the parser decodes from.
                 * @param fn     Free function pointer; must be non-null.
                 */
                static void registerParser(AncFormat::ID format, const AncTransport &src, ParserFn fn);

                /**
                 * @brief Registers a builder for @c (format, dst).
                 *
                 * Same registration policy as @ref registerParser.
                 *
                 * @param format ID of the ANC format the builder produces.
                 * @param dst    Target wire transport the builder emits onto.
                 * @param fn     Free function pointer; must be non-null.
                 */
                static void registerBuilder(AncFormat::ID format, const AncTransport &dst, BuilderFn fn);

                /**
                 * @brief Registers a direct translator for @c (format, src, dst).
                 *
                 * Same registration policy as @ref registerParser.
                 *
                 * @param format ID of the ANC format the translator handles.
                 * @param src    Source wire transport.
                 * @param dst    Target wire transport.
                 * @param fn     Free function pointer; must be non-null.
                 */
                static void registerTranslator(AncFormat::ID format, const AncTransport &src,
                                                const AncTransport &dst, TranslatorFn fn);

                // -- Static capability queries -------------------------------

                /** @brief True when a parser is registered for @c (format, src). */
                static bool hasParser(const AncFormat &format, const AncTransport &src);

                /** @brief True when a builder is registered for @c (format, dst). */
                static bool hasBuilder(const AncFormat &format, const AncTransport &dst);

                /** @brief True when a direct translator is registered for @c (format, src, dst). */
                static bool hasDirectTranslator(const AncFormat &format, const AncTransport &src,
                                                 const AncTransport &dst);

                /**
                 * @brief True when a path exists from @p src to @p dst for @p format.
                 *
                 * @c src == @p dst always returns true (identity
                 * short-circuit).  Otherwise the predicate reports
                 * whether @ref translate can emit on @p dst — either
                 * via a direct translator or via the composed
                 * parse+build path.
                 */
                static bool canTranslate(const AncFormat &format, const AncTransport &src,
                                          const AncTransport &dst);

                /** @brief Every source transport with a parser registered for @p format. */
                static List<AncTransport> registeredParserTransports(const AncFormat &format);

                /** @brief Every target transport with a builder registered for @p format. */
                static List<AncTransport> registeredBuilderTransports(const AncFormat &format);

        private:
                AncTranslateConfig _cfg;
};

PROMEKI_NAMESPACE_END

// ============================================================================
// Static-init registration macros
// ============================================================================

/**
 * @brief Registers a free function as an ANC parser at static-init time.
 * @ingroup proav
 *
 * Expands to a namespace-scope struct whose constructor calls
 * @ref AncTranslator::registerParser.  Place at the top level of a
 * codec @c .cpp file.
 *
 * @param Tag      A unique identifier suffix (typically <Format>_<Transport>).
 * @param Format   The @c AncFormat::ID enumerator.
 * @param Transport The source @c AncTransport static (e.g.
 *                  @c AncTransport::St291).
 * @param Fn       The free function with @c AncTranslator::ParserFn signature.
 */
#define PROMEKI_REGISTER_ANC_PARSER(Tag, Format, Transport, Fn)                                                        \
        namespace {                                                                                                    \
                struct AncParserRegistrar_##Tag {                                                                      \
                                AncParserRegistrar_##Tag() {                                                           \
                                        ::promeki::AncTranslator::registerParser(::promeki::AncFormat::Format,         \
                                                                                  Transport, Fn);                      \
                                }                                                                                      \
                };                                                                                                     \
                static AncParserRegistrar_##Tag s_anc_parser_registrar_##Tag;                                          \
        }

/**
 * @brief Registers a free function as an ANC builder at static-init time.
 * @ingroup proav
 *
 * Mirror of @ref PROMEKI_REGISTER_ANC_PARSER for the build direction.
 */
#define PROMEKI_REGISTER_ANC_BUILDER(Tag, Format, Transport, Fn)                                                       \
        namespace {                                                                                                    \
                struct AncBuilderRegistrar_##Tag {                                                                     \
                                AncBuilderRegistrar_##Tag() {                                                          \
                                        ::promeki::AncTranslator::registerBuilder(::promeki::AncFormat::Format,        \
                                                                                   Transport, Fn);                     \
                                }                                                                                      \
                };                                                                                                     \
                static AncBuilderRegistrar_##Tag s_anc_builder_registrar_##Tag;                                        \
        }

/**
 * @brief Registers a free function as a direct ANC translator at static-init.
 * @ingroup proav
 *
 * Mirror of @ref PROMEKI_REGISTER_ANC_PARSER for the wire-to-wire
 * optimisation path.
 *
 * @param Tag       Unique suffix (typically <Format>_<Src>_<Dst>).
 * @param Format    The @c AncFormat::ID enumerator.
 * @param Src       Source transport static.
 * @param Dst       Target transport static.
 * @param Fn        Free function with @c AncTranslator::TranslatorFn signature.
 */
#define PROMEKI_REGISTER_ANC_TRANSLATOR(Tag, Format, Src, Dst, Fn)                                                     \
        namespace {                                                                                                    \
                struct AncTranslatorRegistrar_##Tag {                                                                  \
                                AncTranslatorRegistrar_##Tag() {                                                       \
                                        ::promeki::AncTranslator::registerTranslator(::promeki::AncFormat::Format,     \
                                                                                      Src, Dst, Fn);                   \
                                }                                                                                      \
                };                                                                                                     \
                static AncTranslatorRegistrar_##Tag s_anc_translator_registrar_##Tag;                                  \
        }
