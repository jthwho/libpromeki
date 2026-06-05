/**
 * @file      ancformat.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/enums_anc.h>
#include <promeki/result.h>
#include <promeki/error.h>
#include <promeki/datatype.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief First-class identifier for an ancillary-data format family.
 * @ingroup proav
 *
 * Uses the @ref typeregistry "TypeRegistry pattern": a lightweight
 * inline wrapper around an immutable @ref Data record, identified by
 * an integer @ref ID.  Well-known formats are provided as named
 * enumerators; applications can register additional formats at runtime
 * via @ref registerType and @ref registerData.  Mirrors the shape of
 * @ref PixelFormat and @ref AudioFormat exactly.
 *
 * An @c AncFormat answers the logical question "what kind of ancillary
 * data is this?" (CEA-708 closed captions, AFD, ATC LTC timecode,
 * SCTE-104 splice markers, …) independent of the wire transport the
 * payload is currently riding on — the latter is captured by
 * @ref AncTransport on @ref AncPacket.  An @c AncFormat is therefore
 * preserved across translation: an @c AncPacket carrying
 * @c AncFormat::Cea708 keeps the same @c AncFormat whether it is on
 * @c AncTransport::St291, @c AncTransport::NdiXml, or any other
 * registered transport.
 *
 * @par Per-transport identity keys
 *
 * Each well-known format carries one or more transport-specific
 * identity bytes — the ST 291 DID/SDID pair, the HDMI InfoFrame type
 * byte, the MPEG-TS table_id — that the registry uses to recognise a
 * raw wire packet on its canonical transport.  The
 * @ref fromSt291DidSdid, @ref fromHdmiInfoFrameType, and
 * @ref fromMpegTsTableId static helpers perform that lookup; backends
 * use them on the capture path to label a freshly received packet
 * with the matching @c AncFormat.
 *
 * @par Wildcard SDID lookup
 *
 * Some formats span an SDID range under a single DID — SMPTE 2020
 * Dolby-metadata uses DID 0x45 with SDIDs 0x01-0x09 to carry different
 * sub-flavours, but the library represents them as one
 * @c Smpte2020Audio format ID.  Such formats register with
 * @c st291Sdid == 0; @ref fromSt291DidSdid then matches first by
 * exact (DID, SDID) and falls back to (DID, 0) when no exact entry
 * exists.  The actual SDID byte is recovered from the packet's wire
 * data when a caller needs the sub-flavour discriminator.
 *
 * @par Naming convention
 *
 * C++ identifiers and string names use a short CamelCase tag for the
 * format (e.g. @c "Cea708", @c "Afd", @c "AtcLtc",
 * @c "HdrStatic2086").  Vendor-specific HDMI InfoFrames sharing
 * type 0x81 are discriminated by their 3-byte OUI; the registry's
 * @ref fromHdmiInfoFrameType returns the catch-all
 * @c VendorInfoFrame and codecs that decode the OUI promote to the
 * specific ID via @c AncFormat(id) at parse time.
 *
 * @par Example
 * @code
 * AncFormat fmt(AncFormat::Cea708);
 * if(fmt.isValid()) {
 *         printf("%s on %s\n", fmt.name().cstr(),
 *                              fmt.canonicalTransport().name().cstr());
 * }
 *
 * AncFormat afmt = AncFormat::fromSt291DidSdid(0x60, 0x60);
 * assert(afmt.id() == AncFormat::AtcLtc);
 * @endcode
 *
 * @par Thread Safety
 * Fully thread-safe.  The handle wraps an integer ID and is safe to
 * share by value across threads.  Registrations are expected at
 * static-init time and the registry is internally synchronized;
 * thereafter @ref registeredIDs and the @c from* lookups are
 * lock-free.
 *
 * @see AncTransport, AncCategory, AncPacket, AncTranslator,
 *      @ref typeregistry "TypeRegistry Pattern"
 */
class AncFormat {
        public:
                PROMEKI_DATATYPE(AncFormat, DataTypeAncFormat, 1)

                /** @brief Writes the registered format name as a tagged String. */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads the registered format name and looks it up. */
                template <uint32_t V> static Result<AncFormat> readFromStream(DataStream &s);

                /**
                 * @brief Identifies an ancillary-data format family.
                 *
                 * Well-known formats have named enumerators.  User-defined
                 * formats obtain IDs from @ref registerType starting at
                 * @c UserDefined.
                 */
                enum ID {
                        Invalid = 0,             ///< Default / uninitialised.
                        Cea708 = 1,              ///< SMPTE 334-2 CDP carrying CEA-708 closed captions (DID 0x61 / SDID 0x01).  Modern workflows for both 608 and 708 captions use this format — CEA-608 data rides via the CDP's cc_data triples with cc_type=0/1 (F1/F2).
                        Cea608 = 2,              ///< SMPTE 334-1 raw line-21 byte format (DID 0x61 / SDID 0x02).  Parsed / built over the typed @ref Cea608Packet value (see @c anccodec_cea608.cpp).  Legacy SDI/NTSC carriage; modern broadcast workflows use the @c Cea708 (CDP) path for both 608 and 708 captions.
                        Afd = 3,                 ///< SMPTE 2016-3 Active Format Description (with Bar Data per Table 1).
                        PanScan = 4,             ///< SMPTE 2016-4 Pan-Scan Information.
                        Scte104 = 5,             ///< SCTE-104 splice-information signal.
                        Scte35 = 6,              ///< SCTE-35 splice_info_section (MPEG-TS).
                        AtcLtc = 7,              ///< SMPTE 12M-2 ATC LTC (shared DID=0x60/SDID=0x60; DBB1=0x00 discriminates).
                        AtcVitc1 = 8,            ///< SMPTE 12M-2 ATC VITC 1 (shared DID=0x60/SDID=0x60; DBB1=0x01 discriminates).
                        AtcVitc2 = 9,            ///< SMPTE 12M-2 ATC VITC 2 (shared DID=0x60/SDID=0x60; DBB1=0x02 discriminates).
                        Smpte2020Audio = 10,     ///< SMPTE 2020 Dolby metadata family (DID 0x45, SDIDs 0x01–0x09).
                        HdrStatic2086 = 11,      ///< SMPTE 2086 static HDR mastering metadata.
                        HdrDynamic2094_40 = 12,  ///< SMPTE ST 2094-40 dynamic HDR (HDR10+).
                        DvRpu = 13,              ///< Dolby Vision RPU metadata.
                        AviInfoFrame = 14,       ///< HDMI AVI InfoFrame (CEA-861).
                        AudioInfoFrame = 15,     ///< HDMI Audio InfoFrame.
                        SpdInfoFrame = 16,       ///< HDMI SPD (Source Product Description) InfoFrame.
                        VendorInfoFrame = 17,    ///< HDMI Vendor-Specific InfoFrame (OUI-agnostic catch-all).
                        Klv0601 = 18,            ///< MISB ST 0601 KLV (geolocation, sensor data).
                        Vpid = 19,               ///< SMPTE ST 352 Video Payload Identifier (DID 0x41, SDID 0x01).
                        PacketForDeletion = 20,  ///< ST 291-1 §6.3 Packet-Marked-for-Deletion (DID 0x80, Type-1).
                        Op47Sdp = 21,            ///< RDD 8 / OP-47 Subtitling Distribution Packet (DID 0x43, SDID 0x02).
                        Op47Multipack = 22,      ///< RDD 8 / OP-47 multipacket header (DID 0x43, SDID 0x03 — §4.2(iii) "203h includes parity").
                        AtcHfrtc = 23,           ///< SMPTE 12-3 ATC HFRTC (DID 0x60, SDID 0x61; DBB1 0x80..0x8F is the bitstream number).
                        VbiSt2031 = 24,          ///< SMPTE ST 2031 DVB-VBI / SCTE-VBI ancillary data (DID 0x41, SDID 0x08).
                        HdrDynamic2094_10 = 25,  ///< SMPTE ST 2094-10 dynamic HDR (Dolby DM) in ST 2108-1 Frame Type 2.
                        UserDefined = 1024       ///< First ID available for user-registered formats.
                };

                /** @brief List of AncFormat IDs. */
                using IDList = ::promeki::List<ID>;

                /**
                 * @brief Immutable descriptor for an ancillary-data format.
                 *
                 * Populated by the library for well-known formats, or by
                 * applications via @ref registerData for custom formats.
                 * Captures format-family identity (name, description,
                 * category) plus the per-transport identity bytes the
                 * registry uses to recognise raw packets on each transport.
                 */
                struct Data {
                                ID          id = Invalid; ///< Unique format identifier.
                                String      name;         ///< Short canonical name (e.g. @c "Cea708").
                                String      desc;         ///< Human-readable description.
                                AncCategory category;     ///< Broad content classification (Captions, Timecode, ...).
                                AncTransport canonicalTransport; ///< Primary "where this format lives" wire transport.
                                uint8_t      st291Did = 0;       ///< ST 291 DID (0 = no ST 291 carriage).
                                uint8_t st291Sdid = 0;           ///< ST 291 SDID (0 with non-zero @c st291Did =
                                                                 ///< wildcard SDID match — see class doc).
                                ///< @brief Concrete ST 291 SDID byte list for wildcard-SDID
                                ///<        formats.
                                ///<
                                ///< When the format registers with @c st291Sdid == 0 to
                                ///< absorb an SDID range under a single DID (e.g.
                                ///< @c Smpte2020Audio under DID 0x45 SDIDs 0x01-0x09),
                                ///< this list enumerates the concrete SDIDs the format
                                ///< covers so SDP fmtp emission and similar consumers can
                                ///< expand the wildcard into explicit pairs.  Empty for
                                ///< non-wildcard formats — use @ref st291Sdid in that
                                ///< case.
                                ::promeki::List<uint8_t> st291SdidRange;
                                uint8_t hdmiInfoFrameType = 0;   ///< HDMI InfoFrame type byte (0 = no HDMI carriage).
                                uint8_t mpegTsTableId = 0; ///< MPEG-TS private-section table_id (0 = no MPEG-TS carriage).
                };

                // -- Registry ----------------------------------------------

                /**
                 * @brief Allocates and returns a unique ID for a user-defined format.
                 * @return A unique ID at @c UserDefined or higher.
                 */
                static ID registerType();

                /**
                 * @brief Registers a Data record in the registry.
                 *
                 * After this call, constructing an @c AncFormat from
                 * @c data.id resolves to the registered data.
                 *
                 * @param data The populated Data struct with @c id set
                 *             to a value from @ref registerType or one of
                 *             the well-known enumerators.
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns the list of every registered format's ID.
                 * @return IDs of every registered format, excluding @c Invalid.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Returns IDs of every registered format in @p category.
                 * @param category The category to filter by.
                 * @return Matching IDs in registration order.  Empty when
                 *         no registered format matches.
                 */
                static IDList registeredIDsForCategory(const AncCategory &category);

                /**
                 * @brief Returns IDs of every registered format with a
                 *        non-zero key for @p transport (i.e. that can ride
                 *        natively on it).
                 *
                 * Used by sinks and SDP emit paths that need to enumerate
                 * formats permissible on a specific wire transport.  The
                 * canonical-transport hint is also honoured: a format
                 * whose @c canonicalTransport matches @p transport is
                 * always included even if no per-transport key byte is
                 * registered for it (the format simply has no DID/SDID-
                 * style identity on that transport, but the canonical
                 * association still counts).
                 *
                 * @param transport The transport to filter by.
                 * @return Matching IDs.  Empty when no registered format
                 *         carries on the transport.
                 */
                static IDList registeredIDsForTransport(const AncTransport &transport);

                /**
                 * @brief Looks up an @c AncFormat by registered name.
                 * @param name The format name to search for.
                 * @return The matching format on success, or
                 *         @c Error::IdNotFound if not registered.
                 */
                static Result<AncFormat> fromName(const String &name);

                /**
                 * @brief Alias of @ref fromName matching the
                 *        @c Result<T> @c fromString convention used
                 *        across the library.
                 *
                 * Exposed so the @ref DataType registry's concept-based
                 * @c ops.fromString detector picks AncFormat up
                 * automatically, alongside every other typereg wrapper.
                 */
                static Result<AncFormat> fromString(const String &name) { return fromName(name); }

                /**
                 * @brief Returns the ID for @p name, or @c Invalid when unknown.
                 *
                 * Convenience non-Result variant of @ref fromName for
                 * call sites that prefer a bare ID lookup.
                 */
                static ID idFromName(const String &name);

                /**
                 * @brief Looks up the @c AncFormat for a raw ST 291 (DID,
                 *        SDID) pair.
                 *
                 * Matches by exact (DID, SDID) first.  If no exact entry
                 * exists, falls back to a wildcard match where a
                 * registered format with @c st291Sdid == 0 and matching
                 * @c st291Did absorbs every SDID under that DID
                 * (e.g. @c Smpte2020Audio across SDIDs 0x01-0x09).  Used
                 * by capture paths to label a freshly received ST 291
                 * packet with the matching @c AncFormat.
                 *
                 * @param did  The DID byte from the packet.
                 * @param sdid The SDID byte from the packet.
                 * @param udw  Optional pointer to the packet's user-data
                 *             words.  When supplied, families that share a
                 *             single (DID, SDID) but discriminate their
                 *             flavour in the payload are refined to the
                 *             correct format: the ST 12-2 ATC trio
                 *             (@c AtcLtc / @c AtcVitc1 / @c AtcVitc2, all
                 *             on 0x60/0x60) is resolved from its DBB1
                 *             payload-type byte (UDWs 1..8 bit 3,
                 *             LSB-first) instead of anchoring to the
                 *             lowest-ID @c AtcLtc.  Pass @c nullptr (the
                 *             default) when only the DID/SDID is available
                 *             — the result then matches the legacy
                 *             two-argument behaviour.
                 * @return The matching format, or an invalid @c AncFormat
                 *         when the (DID, SDID) pair has no registered
                 *         mapping.
                 */
                static AncFormat fromSt291DidSdid(uint8_t did, uint8_t sdid,
                                                  const List<uint16_t> *udw = nullptr);

                /**
                 * @brief Looks up the @c AncFormat for an HDMI InfoFrame
                 *        type byte.
                 *
                 * Returns the format registered for @p type.  Type 0x81
                 * (Vendor-Specific) returns the OUI-agnostic
                 * @c VendorInfoFrame catch-all; consumers that decode the
                 * OUI and want a specific ID
                 * (@c HdrDynamic2094_40 / @c DvRpu / …) call
                 * @c AncFormat(id) explicitly after parsing the OUI.
                 *
                 * @param type The InfoFrame type byte.
                 * @return The matching format, or invalid when the type
                 *         is not registered.
                 */
                static AncFormat fromHdmiInfoFrameType(uint8_t type);

                /**
                 * @brief OUI-aware HDMI InfoFrame format lookup.
                 *
                 * Layer on top of @ref fromHdmiInfoFrameType that
                 * inspects the 24-bit OUI carried in the Vendor-Specific
                 * InfoFrame payload (type 0x81) and promotes the result
                 * to the specific format the OUI identifies:
                 *
                 *  - @c 0x00D046 → @c AncFormat::HdrDynamic2094_40
                 *    (HDR10+ / Samsung-led consortium).
                 *  - @c 0x00903E → @c AncFormat::DvRpu (Dolby Vision).
                 *  - All other OUIs (or non-vendor InfoFrame types) →
                 *    same result as @ref fromHdmiInfoFrameType (catches
                 *    @c AviInfoFrame / @c SpdInfoFrame / @c AudioInfoFrame
                 *    / @c VendorInfoFrame fallthrough).
                 *
                 * Capture backends call this on each ingested HDMI
                 * InfoFrame so the resulting @c AncPacket carries the
                 * correct format identity before reaching the
                 * @c AncTranslator dispatch path.
                 *
                 * @param type The InfoFrame type byte.
                 * @param oui  The 24-bit OUI (Vendor-Specific only;
                 *             ignored for other types).  Pass 0 when
                 *             the type byte is non-vendor.
                 * @return The promoted format.
                 */
                static AncFormat fromHdmiInfoFrame(uint8_t type, uint32_t oui);

                /**
                 * @brief Looks up the @c AncFormat for an MPEG-TS
                 *        private-section table_id.
                 *
                 * @param tableId The first byte of an MPEG-TS private
                 *                section.
                 * @return The matching format, or invalid when the
                 *         table_id is not registered.
                 */
                static AncFormat fromMpegTsTableId(uint8_t tableId);

                // -- Construction / accessors ------------------------------

                /** @brief Constructs an @c AncFormat from an ID (default @c Invalid). */
                inline AncFormat(ID id = Invalid);

                /**
                 * @brief Constructs by registered name.
                 * @throws Nothing.  Resolves to @c Invalid when the name
                 *         is not registered.
                 */
                explicit AncFormat(const String &name);

                /** @brief Returns true when this wrapper references a registered, non-invalid format. */
                bool isValid() const { return d != nullptr && d->id != Invalid; }

                /** @brief Returns the unique ID. */
                ID id() const { return d->id; }

                /** @brief Returns the format's short registered name. */
                const String &name() const { return d->name; }

                /** @brief Returns the format's human-readable description. */
                const String &desc() const { return d->desc; }

                /** @brief Returns the format's broad content category. */
                const AncCategory &category() const { return d->category; }

                /** @brief Returns the format's primary wire transport. */
                const AncTransport &canonicalTransport() const { return d->canonicalTransport; }

                /** @brief Returns the ST 291 DID byte (0 = no ST 291 carriage). */
                uint8_t st291Did() const { return d->st291Did; }

                /** @brief Returns the ST 291 SDID byte (0 = wildcard when @ref st291Did is non-zero, else no carriage). */
                uint8_t st291Sdid() const { return d->st291Sdid; }

                /**
                 * @brief Returns the list of concrete ST 291 SDID bytes
                 *        this format covers on its canonical ST 291
                 *        carriage.
                 *
                 * For a format that registers with a non-zero
                 * @ref st291Sdid this returns @c {st291Sdid()}.  For a
                 * wildcard-SDID format (registers with
                 * @c st291Sdid == 0 to absorb an SDID range under one
                 * DID — e.g. @c Smpte2020Audio across SDIDs 0x01-0x09
                 * under DID 0x45) this returns the registered concrete
                 * range, never the @c 0 sentinel.  Used by SDP fmtp
                 * emission so the wildcard expands into explicit
                 * @c DID_SDID entries — emitting @c SDID=0x00 verbatim
                 * collides with RFC 8331's Type-1 ANC packet sentinel
                 * and so cannot be used as a real per-packet SDID.
                 *
                 * @return Concrete SDID list (one entry for ordinary
                 *         formats, the full range for wildcards).
                 *         Empty when the format has no ST 291 carriage
                 *         (@ref st291Did == 0).
                 */
                ::promeki::List<uint8_t> st291ConcreteSdids() const {
                        ::promeki::List<uint8_t> out;
                        if (d == nullptr || d->st291Did == 0) return out;
                        if (!d->st291SdidRange.isEmpty()) {
                                out = d->st291SdidRange;
                                return out;
                        }
                        if (d->st291Sdid != 0) out.pushToBack(d->st291Sdid);
                        return out;
                }

                /** @brief Returns the HDMI InfoFrame type byte (0 = no HDMI carriage). */
                uint8_t hdmiInfoFrameType() const { return d->hdmiInfoFrameType; }

                /** @brief Returns the MPEG-TS private-section table_id (0 = no MPEG-TS carriage). */
                uint8_t mpegTsTableId() const { return d->mpegTsTableId; }

                /** @brief Returns the underlying immutable Data pointer. */
                const Data *data() const { return d; }

                /** @brief Returns the canonical string form — same as @ref name. */
                const String &toString() const { return d->name; }

                // -- Comparison --------------------------------------------

                /** @brief Equality compares the underlying Data pointer. */
                bool operator==(const AncFormat &o) const { return d == o.d; }

                /** @brief Inequality. */
                bool operator!=(const AncFormat &o) const { return d != o.d; }

        private:
                const Data        *d = nullptr;
                static const Data *lookupData(ID id);
};

inline AncFormat::AncFormat(ID id) : d(lookupData(id)) {}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV