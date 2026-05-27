/**
 * @file      anctranslator.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/enums_anc.h>
#include <promeki/framesyncdisposition.h>
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
 * AncTranslator::ParseResult r = t.parse(myPacket);
 *
 * // Translate to a different transport.  Falls back to parse+build
 * // when no direct translator is registered.
 * AncTranslator::PacketsResult r2 = t.translate(myPacket, AncTransport::NdiXml);
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
                // -- Return-type aliases --------------------------------------

                /**
                 * @brief Result of a parse / multi-parse dispatch — a typed
                 *        @ref Variant carrying the format's
                 *        application-level payload.
                 */
                using ParseResult = Result<Variant>;

                /**
                 * @brief Result of a build / translate / sync-policy dispatch
                 *        — a list of zero-or-more @ref AncPacket on the
                 *        target transport.
                 *
                 * The common case is a one-element list.  Codecs that split
                 * a single logical message across multiple ANC packets
                 * (e.g. SMPTE ST 2108-2 HDR10+) return the full split
                 * sequence; sync policies return an empty list on
                 * @c Drop.
                 */
                using PacketsResult = Result<AncPacket::List>;

                // -- Handler signatures ---------------------------------------

                /** @brief Parser: decode one packet's wire bytes into a typed Variant. */
                using ParserFn = ParseResult (*)(const AncPacket &, const AncTranslateConfig &);

                /**
                 * @brief Multi-packet parser: decode a sequence of packets
                 *        that together carry one logical message into a
                 *        single typed Variant.
                 *
                 * Used by codecs whose wire format splits a single
                 * application-level message across multiple ANC packets
                 * — SMPTE ST 2108-2 HDR/WCG KLV is the canonical example,
                 * with the per-packet @c Packet Count byte incrementing
                 * across consecutive ANC packets and the per-frame
                 * Message Length declared in the first packet's UDW.
                 *
                 * The framework groups packets sharing
                 * @c (format, transport) within an @ref AncPayload, then
                 * dispatches them as a single list to this callback.
                 * Codecs that only ever see one packet per message
                 * register a regular @ref ParserFn instead.
                 */
                using MultiParserFn = ParseResult (*)(const AncPacket::List &,
                                                       const AncTranslateConfig &);

                /**
                 * @brief Builder: encode a typed Variant onto a target
                 *        transport as one or more ANC packets.
                 *
                 * Returns a list so codecs that legitimately need to
                 * emit multiple ANC packets for one logical message
                 * (SMPTE ST 2108-2 HDR10+, where the Message can exceed
                 * a single ST 291 packet's 255-byte UDW capacity and
                 * is split across consecutive packets with incrementing
                 * @c Packet Count) can do so cleanly.  The common case
                 * is a one-element list.
                 */
                using BuilderFn = PacketsResult (*)(const Variant &, const AncTranslateConfig &);

                /**
                 * @brief Direct translator: wire-to-wire conversion that
                 *        bypasses the parse / build round-trip.
                 *
                 * Optional optimisation path; @c AncTranslator::translate
                 * falls back to the composed @c parse() → @c build()
                 * chain when no direct translator is registered.
                 *
                 * Returns a list for symmetry with @ref BuilderFn —
                 * one inbound packet may legitimately fan out to many
                 * outbound packets on transports that fragment messages.
                 */
                using TranslatorFn = PacketsResult (*)(const AncPacket &, AncTransport,
                                                        const AncTranslateConfig &);

                /**
                 * @brief Frame-sync policy: transform one ANC packet according
                 *        to a per-frame @ref FrameSyncDisposition.
                 *
                 * Called by @ref AncFrameSync when a pipeline stage performs
                 * frame-rate conversion or absorbs genlock drift.  The
                 * registered policy decides what happens to the packet under
                 * each disposition — copy through, drop, mutate (e.g. ATC
                 * timecode increment, CEA-708 framing-only CDP), or stash
                 * and forward — and returns 0..N output packets:
                 *
                 *  - **Empty list** = drop the packet from the output frame.
                 *  - **One packet**  = the policy's transformed packet.
                 *  - **>1 packet**   = split / promote — used by codecs that
                 *                      need to fan out a single inbound packet
                 *                      across multiple outbound packets, or by
                 *                      the SCTE-104 stash-and-forward path on
                 *                      @c Drop.
                 *
                 * @c repeatIndex is @c 0 for @c Play and the first slot of a
                 * @c Repeat run, then increments @c 1, @c 2, … for subsequent
                 * slots within the same @c Repeat.  Codecs use it to advance
                 * sequence-counter-like state (CEA-708 CDP @c sequence_counter,
                 * ATC timecode) across the run.  The argument is ignored on
                 * @c Drop.
                 *
                 * The key for this registry is just @c AncFormat::ID — there
                 * is no transport dimension, since frame-rate-conversion
                 * semantics are transport-independent.
                 */
                using SyncPolicyFn = PacketsResult (*)(const AncPacket &,
                                                        FrameSyncDisposition,
                                                        uint8_t repeatIndex,
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
                ParseResult parse(const AncPacket &pkt) const;

                /**
                 * @brief Decodes a sequence of related packets carrying
                 *        one logical message into a typed Variant.
                 *
                 * For codecs with a registered @ref MultiParserFn (e.g.
                 * SMPTE ST 2108-2 HDR10+ where one message splits across
                 * incrementing-@c PacketCount packets) this dispatches
                 * the entire list to the multi-parser.  For codecs with
                 * only a single-packet @ref ParserFn registered, this is
                 * equivalent to calling @ref parse on @p pkts.front()
                 * when @p pkts has exactly one entry; multi-element
                 * lists return @c Error::NotSupported in that case.
                 *
                 * The format / transport are taken from
                 * @c pkts.front() — callers are expected to have
                 * pre-grouped packets by @c (format, transport).
                 *
                 * @param pkts Non-empty packet list belonging to one
                 *             logical message.
                 */
                ParseResult parseGroup(const AncPacket::List &pkts) const;

                /**
                 * @brief Encodes a typed Variant onto the requested transport.
                 *
                 * Dispatches to the registered builder for @c (fmt,
                 * target) using the held config.  The returned list
                 * usually contains a single packet; codecs that split
                 * a message across multiple ANC packets (SMPTE ST
                 * 2108-2 HDR10+) return the full split sequence.
                 *
                 * @param v      The typed Variant carrying the format's
                 *               application-level payload (e.g.
                 *               @c Cea708Cdp, @c Timecode).
                 * @param fmt    The ANC format.
                 * @param target The target wire transport.
                 * @return       The produced packet list on success, or
                 *               @c Error::NotSupported when no builder
                 *               is registered.
                 */
                PacketsResult build(const Variant &v, const AncFormat &fmt,
                                     const AncTransport &target) const;

                /**
                 * @brief Translates a packet onto the requested transport.
                 *
                 * Identity-short-circuits when @c pkt.transport() ==
                 * @p target (returns the input packet in a one-element
                 * list).  Otherwise prefers a registered direct
                 * translator for @c (pkt.format(), pkt.transport(),
                 * target); on miss, falls back to the composed
                 * @c parse(pkt) → @c build(variant, pkt.format(),
                 * target) chain.
                 *
                 * @param pkt    The packet to translate.
                 * @param target The target wire transport.
                 * @return       The translated packet list on success,
                 *               or @c Error::NotSupported when no path
                 *               exists.
                 */
                PacketsResult translate(const AncPacket &pkt, const AncTransport &target) const;

                /**
                 * @brief Applies a frame-sync disposition to one ANC packet.
                 *
                 * Dispatches to the registered @ref SyncPolicyFn for the
                 * packet's format using the held config.  When no policy is
                 * registered for the format, applies the default fallback:
                 * @c Drop returns an empty list, @c Play and @c Repeat
                 * return the packet unchanged.  The default fallback is
                 * silent at this layer — @ref AncFrameSync handles
                 * once-per-format warning when it dispatches.
                 *
                 * @param pkt          The packet to transform.
                 * @param disposition  The frame-sync disposition for this slot.
                 * @param repeatIndex  @c 0 for @c Play and the first slot of
                 *                     a @c Repeat run; @c 1, @c 2, … for
                 *                     subsequent @c Repeat slots.  Ignored on
                 *                     @c Drop.
                 * @return The transformed packet list (empty list = drop).
                 */
                PacketsResult applySyncPolicy(const AncPacket     &pkt,
                                               FrameSyncDisposition disposition,
                                               uint8_t              repeatIndex) const;

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
                 * @brief Registers a multi-packet parser for @c (format, src).
                 *
                 * Multi-parsers take precedence over single-packet
                 * parsers on the same key when both are registered.
                 * Same registration policy as @ref registerParser.
                 *
                 * @param format ID of the ANC format the parser handles.
                 * @param src    Source wire transport.
                 * @param fn     Free function pointer; must be non-null.
                 */
                static void registerMultiParser(AncFormat::ID format, const AncTransport &src,
                                                 MultiParserFn fn);

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

                /**
                 * @brief Registers a frame-sync policy for @p format.
                 *
                 * Same registration policy as @ref registerParser, but the
                 * key is one-dimensional (no transport) — frame-rate-conversion
                 * semantics are transport-independent.
                 *
                 * @param format ID of the ANC format the policy handles.
                 * @param fn     Free function pointer; must be non-null.
                 */
                static void registerSyncPolicy(AncFormat::ID format, SyncPolicyFn fn);

                // -- Static capability queries -------------------------------

                /**
                 * @brief True when a parser is registered for @c (format, src).
                 *
                 * Returns true for either a single-packet @ref ParserFn
                 * registration or a @ref MultiParserFn registration.
                 */
                static bool hasParser(const AncFormat &format, const AncTransport &src);

                /** @brief True when a multi-packet parser is registered for @c (format, src). */
                static bool hasMultiParser(const AncFormat &format, const AncTransport &src);

                /** @brief True when a builder is registered for @c (format, dst). */
                static bool hasBuilder(const AncFormat &format, const AncTransport &dst);

                /** @brief True when a direct translator is registered for @c (format, src, dst). */
                static bool hasDirectTranslator(const AncFormat &format, const AncTransport &src,
                                                 const AncTransport &dst);

                /** @brief True when a frame-sync policy is registered for @p format. */
                static bool hasSyncPolicy(const AncFormat &format);

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
 * @brief Registers a free function as an ANC multi-packet parser at
 *        static-init time.
 * @ingroup proav
 *
 * Mirror of @ref PROMEKI_REGISTER_ANC_PARSER for the
 * @c MultiParserFn signature.  Used by codecs whose wire format may
 * split one message across multiple ANC packets (e.g. SMPTE ST 2108-2
 * HDR10+).
 */
#define PROMEKI_REGISTER_ANC_MULTI_PARSER(Tag, Format, Transport, Fn)                                                  \
        namespace {                                                                                                    \
                struct AncMultiParserRegistrar_##Tag {                                                                 \
                                AncMultiParserRegistrar_##Tag() {                                                      \
                                        ::promeki::AncTranslator::registerMultiParser(                                 \
                                                ::promeki::AncFormat::Format, Transport, Fn);                          \
                                }                                                                                      \
                };                                                                                                     \
                static AncMultiParserRegistrar_##Tag s_anc_multi_parser_registrar_##Tag;                               \
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

/**
 * @brief Registers a free function as an ANC frame-sync policy at static-init.
 * @ingroup proav
 *
 * Mirror of @ref PROMEKI_REGISTER_ANC_PARSER for the frame-sync policy
 * registry.  No transport argument because the registry is keyed only
 * on @c AncFormat::ID — frame-rate-conversion semantics are
 * transport-independent.
 *
 * @param Tag    Unique suffix (typically <Format>).
 * @param Format The @c AncFormat::ID enumerator.
 * @param Fn     Free function with @c AncTranslator::SyncPolicyFn signature.
 */
#define PROMEKI_REGISTER_ANC_SYNC_POLICY(Tag, Format, Fn)                                                              \
        namespace {                                                                                                    \
                struct AncSyncPolicyRegistrar_##Tag {                                                                  \
                                AncSyncPolicyRegistrar_##Tag() {                                                       \
                                        ::promeki::AncTranslator::registerSyncPolicy(::promeki::AncFormat::Format,     \
                                                                                      Fn);                             \
                                }                                                                                      \
                };                                                                                                     \
                static AncSyncPolicyRegistrar_##Tag s_anc_sync_policy_registrar_##Tag;                                 \
        }

#endif // PROMEKI_ENABLE_PROAV
