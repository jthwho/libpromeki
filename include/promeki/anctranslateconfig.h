/**
 * @file      anctranslateconfig.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/variantdatabase.h>
#include <promeki/enums.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/metadata.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Held configuration bundle for an @ref AncTranslator session.
 * @ingroup proav
 *
 * Thin subclass of @c VariantDatabase&lt;"AncTranslateConfig"&gt;
 * that names every well-known knob driving parser / builder /
 * translator behaviour.  Following the @ref MediaConfig pattern,
 * each well-known key is declared via @ref PROMEKI_DECLARE_ID with
 * a @ref VariantSpec carrying the accepted type and default value;
 * client code references the key constants directly so misspellings
 * fail to compile.
 *
 * Two key families live here:
 *
 *  - **Universal knobs** (@c Fidelity, @c Checksum,
 *    @c OnUnsupported, @c AllowLossy) that every handler may
 *    consult regardless of transport.
 *  - **Per-transport build-time inputs** grouped by transport
 *    namespace (@c St291::BuildLine, @c NdiXml::Namespace, …)
 *    that codecs producing packets on the named transport use to
 *    shape the output.  These are distinct from
 *    @ref AncMeta "AncMeta::<Transport>::<Key>" sidecar values,
 *    which are *read-time* descriptors of an already-captured
 *    packet; the config values are *build-time* defaults a codec
 *    consults when synthesising a fresh packet.  A codec writes
 *    the build-time value into the packet's @c AncMeta sidecar so
 *    the produced packet can be round-tripped back through the
 *    pipeline.
 *
 * @par Held by @ref AncTranslator
 *
 * @c AncTranslateConfig is constructed once per logical session
 * (typically per sink-emit loop) and passed at @c AncTranslator
 * construction time.  The translator threads it into every parser /
 * builder / direct-translator call it dispatches; per-call public
 * APIs do not take a config so application code does not have to
 * thread the bundle by hand.
 *
 * @par String / DataStream round-trip
 *
 * The class delegates JSON round-trip to the inherited @c Metadata
 * machinery (the @ref toString / @ref fromString helpers below
 * forward to the base @c VariantDatabase JSON conversion).  Binary
 * round-trip uses the template @c DataStream operators inherited
 * from @c VariantDatabase.  The whole bundle therefore rides
 * through MediaConfig, DataStream, and CLI tooling as one value
 * without any custom serialization.
 *
 * @par Example
 * @code
 * AncTranslateConfig cfg;
 * cfg.set(AncTranslateConfig::Checksum,
 *         AncChecksumPolicy::AlwaysRecompute);
 * cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(11));
 * AncTranslator t(cfg);
 * AncTranslator::PacketsResult r = t.build(Variant(myTimecode),
 *                                          AncFormat(AncFormat::AtcLtc),
 *                                          AncTransport::St291);
 * @endcode
 *
 * @par Thread Safety
 * Mirrors @c VariantDatabase: the @c SharedPtr-backed storage is
 * safe to copy across threads, but mutating one handle while
 * another thread reads or writes the same handle is undefined
 * behaviour.  Build the config on one thread, then copy it into
 * each translator session that needs it.
 *
 * @see AncTranslator, AncFidelity, AncChecksumPolicy,
 *      AncOnUnsupported, AncMeta, MediaConfig
 */
class AncTranslateConfig : public VariantDatabase<"AncTranslateConfig"> {
        public:
                /** @brief Base class alias. */
                using Base = VariantDatabase<"AncTranslateConfig">;

                using Base::Base;

                // ============================================================
                // Universal knobs
                // ============================================================

                /// @brief Enum @ref AncFidelity — output verbosity for
                /// translators emitting on transports with multiple valid
                /// representations (XML / JSON / AMF text forms mostly).
                ///
                /// @note Currently declared but not consumed by any
                ///       in-tree codec.  Wired up once the multi-form
                ///       text codecs land (NDI XML, RTMP AMF).
                PROMEKI_DECLARE_ID(Fidelity,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setEnumType(AncFidelity::Type)
                                           .setDefault(AncFidelity(AncFidelity::Default))
                                           .setDescription("Output verbosity for translators that have multiple valid output forms."));

                /// @brief Enum @ref AncChecksumPolicy — how
                /// ST 291-emitting codecs handle the per-packet checksum.
                PROMEKI_DECLARE_ID(Checksum,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setEnumType(AncChecksumPolicy::Type)
                                           .setDefault(AncChecksumPolicy(AncChecksumPolicy::PreserveOrRecompute))
                                           .setDescription("ST 291 checksum policy for builder / translator outputs."));

                /// @brief Enum @ref AncOnUnsupported — what to do when
                /// the input cannot be represented on the target transport.
                ///
                /// @note Currently declared but not consumed by any
                ///       in-tree codec.  Wired up once cross-transport
                ///       translation paths surface lossy-fallback cases.
                PROMEKI_DECLARE_ID(OnUnsupported,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setEnumType(AncOnUnsupported::Type)
                                           .setDefault(AncOnUnsupported(AncOnUnsupported::BestEffort))
                                           .setDescription("Behaviour when the input cannot be represented on the target transport."));

                /// @brief bool — opt-in for translations that may have
                /// to truncate or downsample (e.g. CEA-708 service-block
                /// subset for legacy targets).  Defaults to @c false so
                /// lossless paths are the only fallback unless the
                /// caller explicitly accepts loss.
                ///
                /// @note Currently declared but not consumed by any
                ///       in-tree codec.  Wired up once a lossy codec
                ///       path lands.
                PROMEKI_DECLARE_ID(AllowLossy,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Permit lossy translations (truncation / downsampling) when no lossless path exists."));

                // ============================================================
                // Per-transport build-time inputs
                // ============================================================
                //
                // Naming pattern: <Transport><Field>.  The dotted-namespace
                // form the devplan sketches (`St291::BuildLine`) is not
                // representable as a single C++ identifier in a database
                // that takes a flat name list; we keep the wire name
                // dotted (`St291.BuildLine`) for nice JSON output while
                // the C++ identifier collapses the dot.

                /// @brief uint16_t — VANC line number to place a built
                /// ST 291 packet on when the source did not specify one
                /// (e.g. when translating from a non-ST 291 transport).
                /// Codec writes the value into @c AncPacket::st291Line()
                /// on the produced packet.
                ///
                /// Default is @c 0x7FE — the RFC 8331 §2.2 / RP 168
                /// "anywhere from two lines after the switching point
                /// to the last line before active video" sentinel,
                /// which ST 2110-40 §5.2.2 recommends when no exact
                /// VANC line is known.  See
                /// @ref St291Packet::UnspecifiedLine.
                PROMEKI_DECLARE_ID(St291BuildLine,
                                   VariantSpec()
                                           .setType(DataTypeUInt16)
                                           .setDefault(uint16_t(0x7FE))
                                           .setDescription("VANC line number for built ST 291 packets (0x7FE = "
                                                           "RP 168 sentinel: anywhere after the switching point)."));

                /// @brief bool — F-bit value for built ST 291 packets;
                /// stamped onto @c AncPacket::st291FieldB().
                PROMEKI_DECLARE_ID(St291FieldB,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("F-bit for built ST 291 packets (field-2 indicator)."));

                /// @brief bool — C-bit (Y-vs-C stream selector) for built
                /// ST 291 packets; stamped onto @c AncPacket::st291CBit().
                ///
                /// Default is @c false (Y stream).  Most ANC formats are
                /// Y-only on the wire by spec mandate: ST 12-2 §8.2.1
                /// (ATC), ST 2016-3 §5 (AFD/Bar Data), ST 2108-1 §5 (HDR
                /// static), ST 2110-40 §5.2.2 (RFC 8331 carriage).
                /// Codecs honour the knob verbatim — a caller that flips
                /// this to @c true for an Y-only format is responsible
                /// for whatever interop pain that produces.
                PROMEKI_DECLARE_ID(St291BuildCBit,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("C-bit (Y=false / C=true) for built ST 291 packets."));

                /// @brief uint8_t — F-bit value for the ST 2110-40 §5.5
                /// ANC_Count=0 keep-alive RTP packet, using the wire
                /// codes of @c RtpPayloadAnc::FieldIndication
                /// (@c 0=Progressive, @c 1=Invalid/reserved,
                /// @c 2=InterlacedField1, @c 3=InterlacedField2).
                /// Plumbed into @c RtpPayloadAnc::setKeepAliveField at
                /// session construction.  Default is @c 0 (Progressive,
                /// RFC 8331 §2.1).  Kept as a primitive instead of a
                /// TypedEnum to avoid pulling the @c rtppayloadanc.h
                /// header into every translator-config consumer; the
                /// caller casts the byte through the enum class at the
                /// setter boundary.
                PROMEKI_DECLARE_ID(St291KeepAliveField,
                                   VariantSpec()
                                           .setType(DataTypeUInt8)
                                           .setDefault(uint8_t(0))
                                           .setDescription("F-bit for the ST 2110-40 §5.5 ANC_Count=0 keep-alive RTP packet."));

                /// @brief String — XML namespace prefix for built NDI
                /// metadata frames.  Empty (default) lets the codec pick.
                ///
                /// @note Currently declared but not consumed — no NDI
                ///       XML codec has landed yet.
                PROMEKI_DECLARE_ID(NdiXmlNamespace,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("XML namespace prefix preference for built NDI metadata frames."));

                /// @brief uint32_t — OUI to stamp when building a
                /// vendor-specific HDMI InfoFrame from a typed Variant.
                /// 0 (default) lets the codec choose the standard OUI
                /// (HDR10+ for @c HdrDynamic2094_40, Dolby for @c DvRpu,
                /// etc.).
                PROMEKI_DECLARE_ID(HdmiInfoFrameOui,
                                   VariantSpec()
                                           .setType(DataTypeUInt32)
                                           .setDefault(uint32_t(0))
                                           .setDescription("OUI to stamp into built vendor-specific HDMI InfoFrames (0 = codec default)."));

                /// @brief String — override for AMF0 script-tag name
                /// when building RTMP @c onMetaData / @c onCaptionInfo /
                /// @c onCuePoint outputs.  Empty (default) resolves per
                /// format (Cea708 → @c onCaptionInfo,
                /// Scte104 → @c onCuePoint, …).
                ///
                /// @note Currently declared but not consumed — no RTMP
                ///       AMF codec has landed yet.
                PROMEKI_DECLARE_ID(RtmpAmfObjectName,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("AMF0 script-tag name override for built RTMP outputs (empty = codec default)."));

                /// @brief uint32_t — Image width (pixels) for the
                /// ST 2094-40 §9.2 Window 0 rectangle.  When zero
                /// (default), the @c HdrDynamic2094_40 codec emits the
                /// legacy @c (0xFFFF, 0xFFFF) sentinel and logs a warn
                /// — that sentinel is not spec-conformant under §9.2
                /// ("Window 0 shall cover all pixels in an image"), so
                /// callers that have the dimensions should always
                /// stamp them here.
                PROMEKI_DECLARE_ID(HdrDynamicImageWidth,
                                   VariantSpec()
                                           .setType(DataTypeUInt32)
                                           .setDefault(uint32_t(0))
                                           .setDescription("Image width (pixels) for ST 2094-40 §9.2 Window 0 (0 = unknown, emits sentinel)."));

                /// @brief uint32_t — Image height (pixels) for the
                /// ST 2094-40 §9.2 Window 0 rectangle.  Companion of
                /// @ref HdrDynamicImageWidth.
                PROMEKI_DECLARE_ID(HdrDynamicImageHeight,
                                   VariantSpec()
                                           .setType(DataTypeUInt32)
                                           .setDefault(uint32_t(0))
                                           .setDescription("Image height (pixels) for ST 2094-40 §9.2 Window 0 (0 = unknown, emits sentinel)."));

                /// @brief uint32_t — ATC parse-time frame-rate hint
                /// (24 / 25 / 30; 0 = no hint).  The 8 time-code bytes
                /// in an ST 12-2 packet don't disambiguate 24 / 25 /
                /// 30-NDF — only the wire's DF bit narrows 30 → 29.97-DF.
                /// When a caller knows the paired video's rate, supplying
                /// it here lets the parser stamp the right
                /// @c Timecode::Mode (NDF24 / NDF25 / NDF30 / DF30) on
                /// the returned value.  Unknown / unsupported values are
                /// silently ignored and the parser falls back to the
                /// NDF30 / DF30 default.
                PROMEKI_DECLARE_ID(AtcParseRateHint,
                                   VariantSpec()
                                           .setType(DataTypeUInt32)
                                           .setDefault(uint32_t(0))
                                           .setDescription("ATC parse-time frame-rate hint (24/25/30; 0 = no hint)."));

                // ============================================================
                // String round-trip (JSON)
                // ============================================================

                /**
                 * @brief Returns the configured ST 291 checksum policy.
                 *
                 * Convenience accessor for the @ref Checksum key — codec
                 * parsers forward the result to
                 * @c St291Packet::from(pkt, policy) /
                 * @c RtpPayloadAnc::unpackAncPackets(buf, out, policy).
                 * Falls back to @c PreserveOrRecompute (the spec default
                 * per audit Q6) when the key is absent.
                 */
                AncChecksumPolicy checksumPolicy() const {
                        if (!contains(Checksum)) {
                                return AncChecksumPolicy(AncChecksumPolicy::PreserveOrRecompute);
                        }
                        Enum e = getAs<Enum>(Checksum);
                        return AncChecksumPolicy(e.value());
                }

                /**
                 * @brief Returns the configured F-bit byte for
                 *        ST 2110-40 §5.5 keep-alive RTP packets.
                 *
                 * Convenience accessor for the @ref St291KeepAliveField
                 * key — @c RtpAncPacketizerThread plumbs the result
                 * into @c RtpPayloadAnc::setKeepAliveField after
                 * casting through @c RtpPayloadAnc::FieldIndication at
                 * the call site.  Falls back to @c 0 (Progressive) when
                 * the key is absent.
                 */
                uint8_t keepAliveFieldByte() const {
                        return getAs<uint8_t>(St291KeepAliveField, uint8_t(0));
                }

                /**
                 * @brief Serializes this config as a JSON document.
                 *
                 * Equivalent to @c Metadata::toString applied to the
                 * backing @c VariantDatabase entries.  Used by
                 * @c MediaConfig keys, log messages, and CLI tooling
                 * that needs a one-line string form.
                 *
                 * @param indent JSON indentation (0 = compact, default).
                 * @return The encoded JSON document.
                 */
                String toString(unsigned int indent = 0) const {
                        return toJson().toString(indent);
                }

                /**
                 * @brief Parses a JSON-encoded config document.
                 *
                 * Inverse of @ref toString.  Malformed JSON sets
                 * @p err to @ref Error::ParseFailed and returns a
                 * default-constructed config.
                 *
                 * @param str The JSON document to parse.
                 * @param err Optional error output.
                 * @return The reconstructed config (default-constructed
                 *         on parse error).
                 */
                static AncTranslateConfig fromString(const String &str, Error *err = nullptr) {
                        Error      parseErr;
                        JsonObject json = JsonObject::parse(str, &parseErr);
                        if (parseErr.isError()) {
                                if (err) *err = parseErr;
                                return AncTranslateConfig();
                        }
                        // Mirror Metadata::fromJson: walk the object and feed
                        // each entry through the spec-aware coercion path so
                        // string-encoded rich types (Enum, Url, …) land back
                        // as their proper Variant kind.
                        AncTranslateConfig cfg;
                        bool               good = true;
                        json.forEach([&cfg, &good](const String &key, const Variant &val) {
                                if (!val.isValid()) {
                                        good = false;
                                        return;
                                }
                                Error e = cfg.setFromJson(ID(key), val);
                                if (e.isError()) good = false;
                        });
                        if (err) *err = good ? Error::Ok : Error::Invalid;
                        return cfg;
                }
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
