/**
 * @file      ancformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ancformat.h>
#include <promeki/datastream.h>
#include <promeki/atomic.h>
#include <promeki/map.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered formats
// ---------------------------------------------------------------------------

static Atomic<int> _nextType{AncFormat::UserDefined};

AncFormat::ID AncFormat::registerType() { return static_cast<ID>(_nextType.fetchAndAdd(1)); }

// ---------------------------------------------------------------------------
// Built-in Data factories
// ---------------------------------------------------------------------------

static AncFormat::Data makeInvalid() {
        AncFormat::Data d;
        d.id = AncFormat::Invalid;
        d.name = "Invalid";
        d.desc = "Invalid ancillary-data format";
        d.category = AncCategory::Unknown;
        d.canonicalTransport = AncTransport::Invalid;
        return d;
}

static AncFormat::Data makeCea708() {
        AncFormat::Data d;
        d.id = AncFormat::Cea708;
        d.name = "Cea708";
        d.desc = "SMPTE 334-2 Caption Distribution Packet carrying CEA-708 closed captions";
        d.category = AncCategory::Captions;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x61;
        d.st291Sdid = 0x01;
        return d;
}

static AncFormat::Data makeCea608() {
        AncFormat::Data d;
        d.id = AncFormat::Cea608;
        d.name = "Cea608";
        d.desc = "SMPTE 334-1 CEA-608 line-21 closed captions";
        d.category = AncCategory::Captions;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x61;
        d.st291Sdid = 0x02;
        return d;
}

static AncFormat::Data makeAfd() {
        AncFormat::Data d;
        d.id = AncFormat::Afd;
        d.name = "Afd";
        // ST 2016-3 §4 defines a single combined packet at DID 0x41 /
        // SDID 0x05 that carries both the AFD code (UDW 1) and the
        // letterbox / pillarbox Bar Data (UDWs 4-8); there is no
        // separate Bar Data ANC packet in ST 2016-3.  SDID 0x06 is
        // ST 2016-4 Pan-Scan (see makePanScan).
        d.desc = "SMPTE 2016-3 Active Format Description and Bar Data";
        d.category = AncCategory::Aspect;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x41;
        d.st291Sdid = 0x05;
        return d;
}

static AncFormat::Data makePanScan() {
        AncFormat::Data d;
        d.id = AncFormat::PanScan;
        d.name = "PanScan";
        // Per ST 2016-3 Annex C road map: SDID 0x06 under DID 0x41 is
        // registered to ST 2016-4 Pan-Scan information, not Bar Data
        // (which rides in the combined AFD packet at SDID 0x05).
        d.desc = "SMPTE 2016-4 Pan-Scan Information";
        d.category = AncCategory::Aspect;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x41;
        d.st291Sdid = 0x06;
        return d;
}

static AncFormat::Data makeScte104() {
        AncFormat::Data d;
        d.id = AncFormat::Scte104;
        d.name = "Scte104";
        d.desc = "SCTE-104 splice-information signal";
        d.category = AncCategory::Splice;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x41;
        d.st291Sdid = 0x07;
        return d;
}

static AncFormat::Data makeScte35() {
        AncFormat::Data d;
        d.id = AncFormat::Scte35;
        d.name = "Scte35";
        d.desc = "SCTE-35 splice_info_section in MPEG-TS private data";
        d.category = AncCategory::Splice;
        d.canonicalTransport = AncTransport::MpegTsPrivate;
        d.mpegTsTableId = 0xFC;
        return d;
}

static AncFormat::Data makeAtc(AncFormat::ID id, const char *name, const char *desc, uint8_t sdid) {
        AncFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.category = AncCategory::Timecode;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x60;
        d.st291Sdid = sdid;
        return d;
}

static AncFormat::Data makeSmpte2020Audio() {
        AncFormat::Data d;
        d.id = AncFormat::Smpte2020Audio;
        d.name = "Smpte2020Audio";
        d.desc = "SMPTE 2020 audio (Dolby) metadata, SDIDs 0x01-0x09 under DID 0x45";
        d.category = AncCategory::AudioMetadata;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x45;
        // Wildcard SDID: matches every SDID under DID 0x45 unless a
        // more specific entry is registered.  The actual sub-flavour
        // SDID is recovered from the packet's wire bytes.
        d.st291Sdid = 0;
        // Concrete SDID enumeration (SMPTE 2020-1 §6 sub-flavours):
        // 0x01 (Linear PCM 48k 16-ch program 1), 0x02-0x09 (additional
        // programs / Dolby E variants).  SDP fmtp emission iterates
        // this list so receivers see explicit DID_SDID entries instead
        // of the (DID, 0x00) sentinel that collides with RFC 8331's
        // Type-1 ANC packet labelling.
        d.st291SdidRange = {0x01, 0x02, 0x03, 0x04, 0x05,
                            0x06, 0x07, 0x08, 0x09};
        return d;
}

static AncFormat::Data makeHdrStatic2086() {
        AncFormat::Data d;
        d.id = AncFormat::HdrStatic2086;
        d.name = "HdrStatic2086";
        d.desc = "SMPTE 2086 static HDR mastering display metadata";
        d.category = AncCategory::Hdr;
        d.canonicalTransport = AncTransport::HdmiInfoFrame;
        d.hdmiInfoFrameType = 0x87; // DRM InfoFrame.
        d.st291Did = 0x41;          // SMPTE ST 2108-1 HDR/WCG Metadata Ancillary Data Packet.
        d.st291Sdid = 0x0C;
        return d;
}

static AncFormat::Data makeHdrDynamic2094_40() {
        AncFormat::Data d;
        d.id = AncFormat::HdrDynamic2094_40;
        d.name = "HdrDynamic2094_40";
        d.desc = "SMPTE ST 2094-40 dynamic HDR metadata (HDR10+)";
        d.category = AncCategory::Hdr;
        d.canonicalTransport = AncTransport::HdmiInfoFrame;
        d.hdmiInfoFrameType = 0x81; // Vendor-Specific (HDR10+ OUI discriminator).
        d.st291Did = 0x41;          // SMPTE ST 2108-2 HDR/WCG KLV Metadata Ancillary Data Packet.
        d.st291Sdid = 0x0D;
        return d;
}

static AncFormat::Data makeDvRpu() {
        AncFormat::Data d;
        d.id = AncFormat::DvRpu;
        d.name = "DvRpu";
        d.desc = "Dolby Vision RPU metadata";
        d.category = AncCategory::Hdr;
        d.canonicalTransport = AncTransport::HdmiInfoFrame;
        d.hdmiInfoFrameType = 0x81; // Vendor-Specific (Dolby OUI discriminator).
        return d;
}

static AncFormat::Data makeAviInfoFrame() {
        AncFormat::Data d;
        d.id = AncFormat::AviInfoFrame;
        d.name = "AviInfoFrame";
        d.desc = "HDMI AVI InfoFrame (CEA-861) — colourimetry, aspect, scan info";
        d.category = AncCategory::Display;
        d.canonicalTransport = AncTransport::HdmiInfoFrame;
        d.hdmiInfoFrameType = 0x82;
        return d;
}

static AncFormat::Data makeAudioInfoFrame() {
        AncFormat::Data d;
        d.id = AncFormat::AudioInfoFrame;
        d.name = "AudioInfoFrame";
        d.desc = "HDMI Audio InfoFrame (CEA-861)";
        d.category = AncCategory::AudioMetadata;
        d.canonicalTransport = AncTransport::HdmiInfoFrame;
        d.hdmiInfoFrameType = 0x84;
        return d;
}

static AncFormat::Data makeSpdInfoFrame() {
        AncFormat::Data d;
        d.id = AncFormat::SpdInfoFrame;
        d.name = "SpdInfoFrame";
        d.desc = "HDMI Source Product Description InfoFrame";
        d.category = AncCategory::Display;
        d.canonicalTransport = AncTransport::HdmiInfoFrame;
        d.hdmiInfoFrameType = 0x83;
        return d;
}

static AncFormat::Data makeVendorInfoFrame() {
        AncFormat::Data d;
        d.id = AncFormat::VendorInfoFrame;
        d.name = "VendorInfoFrame";
        d.desc = "HDMI Vendor-Specific InfoFrame (OUI-agnostic catch-all)";
        d.category = AncCategory::Display;
        d.canonicalTransport = AncTransport::HdmiInfoFrame;
        d.hdmiInfoFrameType = 0x81;
        return d;
}

static AncFormat::Data makeKlv0601() {
        AncFormat::Data d;
        d.id = AncFormat::Klv0601;
        d.name = "Klv0601";
        d.desc = "MISB ST 0601 KLV geolocation / sensor metadata";
        d.category = AncCategory::Geolocation;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x44;
        d.st291Sdid = 0x04;
        return d;
}

static AncFormat::Data makeVpid() {
        AncFormat::Data d;
        d.id = AncFormat::Vpid;
        d.name = "Vpid";
        d.desc = "SMPTE ST 352 Video Payload Identifier";
        d.category = AncCategory::PayloadId;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x41;
        d.st291Sdid = 0x01;
        return d;
}

static AncFormat::Data makePacketForDeletion() {
        AncFormat::Data d;
        d.id = AncFormat::PacketForDeletion;
        d.name = "PacketForDeletion";
        d.desc = "SMPTE ST 291-1 §6.3 Packet-Marked-for-Deletion (Type-1, DID 0x80)";
        // No good fit in the current AncCategory taxonomy — this is an
        // in-band control packet, not content metadata.  None of the
        // categories added in F8 (Subtitles / Klv / Sei / Vbi) cover
        // control-plane packets either; we keep "Unknown" so this
        // entry stays out of category filters that select on
        // Captions / Timecode / etc.
        d.category = AncCategory::Unknown;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x80;
        // Type-1 packet — word 1 carries DBN, not SDID.  Register as a
        // wildcard so the (0x80, anyDBN) lookup falls back here, and
        // enumerate {0x00} as the concrete SDID so SDP fmtp emission
        // produces the RFC 8331 §3.1 sentinel ({DID, 0x00}) for the
        // Type-1 packet.
        d.st291Sdid = 0;
        d.st291SdidRange = {0x00};
        return d;
}

static AncFormat::Data makeOp47Sdp() {
        AncFormat::Data d;
        d.id = AncFormat::Op47Sdp;
        d.name = "Op47Sdp";
        d.desc = "RDD 8 / OP-47 Subtitling Distribution Packet (multi-packet subtitling carriage)";
        d.category = AncCategory::Subtitles;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x43;
        d.st291Sdid = 0x02;
        return d;
}

static AncFormat::Data makeOp47Multipack() {
        AncFormat::Data d;
        d.id = AncFormat::Op47Multipack;
        d.name = "Op47Multipack";
        d.desc = "RDD 8 / OP-47 multipacket header (companion of the SDP)";
        d.category = AncCategory::Subtitles;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x43;
        d.st291Sdid = 0x01;
        return d;
}

static AncFormat::Data makeCcfSt2106() {
        AncFormat::Data d;
        d.id = AncFormat::CcfSt2106;
        d.name = "CcfSt2106";
        d.desc = "SMPTE ST 2106 Caption Compatible Flag";
        d.category = AncCategory::Captions;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x41;
        d.st291Sdid = 0x14;
        return d;
}

static AncFormat::Data makeVbiSt2031() {
        AncFormat::Data d;
        d.id = AncFormat::VbiSt2031;
        d.name = "VbiSt2031";
        d.desc = "SMPTE ST 2031 NTSC VBI / line-21 carriage in HD-SDI";
        d.category = AncCategory::Vbi;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x60;
        d.st291Sdid = 0x01;
        return d;
}

static AncFormat::Data makeHdrDynamic2094_10() {
        AncFormat::Data d;
        d.id = AncFormat::HdrDynamic2094_10;
        d.name = "HdrDynamic2094_10";
        // ST 2108-1 §5.3.4 carries Frame Type 2 (ST 2094-10 dynamic
        // metadata / Dolby DM) inside the same DID 0x41 / SDID 0x0C
        // packet as Frame Type 1 (the HdrStatic2086 ST 2086 mastering
        // payload).  We deliberately do NOT register a (DID,SDID)
        // lookup key here — the (0x41, 0x0C) pair already resolves to
        // HdrStatic2086, and the ST 2108-1 codec is responsible for
        // promoting the format ID to HdrDynamic2094_10 (or the other
        // ST 2108-1 Frame Types) once it inspects the Frame Type
        // byte.  The registration is name-addressable only; callers
        // that already know they have Frame Type 2 can construct the
        // AncFormat directly.
        d.desc = "SMPTE ST 2094-10 dynamic HDR (Dolby DM) carried in ST 2108-1 Frame Type 2";
        d.category = AncCategory::Hdr;
        d.canonicalTransport = AncTransport::St291;
        return d;
}

// ---------------------------------------------------------------------------
// Registry storage
// ---------------------------------------------------------------------------

struct AncFormatRegistry {
                Map<AncFormat::ID, AncFormat::Data> entries;
                Map<String, AncFormat::ID>          nameMap;
                // F9.2: indexed views maintained on every add() so the
                // by-category / by-transport queries don't rescan the
                // whole table on each call.  Keys are the underlying
                // Enum integer value (AncCategory / AncTransport derive
                // from TypedEnum so their identity is an int).
                // Insertion order is preserved because each list only
                // ever grows.
                Map<int, AncFormat::IDList> byCategory;
                Map<int, AncFormat::IDList> byTransport;

                AncFormatRegistry() {
                        add(makeInvalid());
                        add(makeCea708());
                        add(makeCea608());
                        add(makeAfd());
                        add(makePanScan());
                        add(makeScte104());
                        add(makeScte35());
                        add(makeAtc(AncFormat::AtcLtc, "AtcLtc", "SMPTE 12M-2 Ancillary Timecode — LTC", 0x60));
                        add(makeAtc(AncFormat::AtcVitc1, "AtcVitc1", "SMPTE 12M-2 Ancillary Timecode — VITC 1", 0x61));
                        add(makeAtc(AncFormat::AtcVitc2, "AtcVitc2", "SMPTE 12M-2 Ancillary Timecode — VITC 2", 0x62));
                        add(makeSmpte2020Audio());
                        add(makeHdrStatic2086());
                        add(makeHdrDynamic2094_40());
                        add(makeDvRpu());
                        add(makeAviInfoFrame());
                        add(makeAudioInfoFrame());
                        add(makeSpdInfoFrame());
                        add(makeVendorInfoFrame());
                        add(makeKlv0601());
                        add(makeVpid());
                        add(makePacketForDeletion());
                        // F8 additions: OP-47 SDP family, ST 2106 CCF,
                        // ST 2031 VBI, ST 2094-10 dynamic HDR.
                        add(makeOp47Sdp());
                        add(makeOp47Multipack());
                        add(makeCcfSt2106());
                        add(makeVbiSt2031());
                        add(makeHdrDynamic2094_10());
                }

                void add(AncFormat::Data &&d) {
                        if (d.id == AncFormat::Invalid) {
                                entries[d.id] = std::move(d);
                                return;
                        }
                        nameMap[d.name] = d.id;
                        const AncFormat::ID  id = d.id;
                        const AncCategory    category = d.category;
                        const AncTransport   canonical = d.canonicalTransport;
                        const uint8_t        did = d.st291Did;
                        const uint8_t        hdmiType = d.hdmiInfoFrameType;
                        const uint8_t        tsTableId = d.mpegTsTableId;
                        entries[id] = std::move(d);
                        byCategory[category.value()].pushToBack(id);
                        // Transport view honours canonicalTransport and
                        // any non-zero per-transport key byte (the same
                        // policy registeredIDsForTransport used to apply
                        // by full scan).  Multi-transport formats (e.g.
                        // HdrStatic2086 on HdmiInfoFrame canonical with
                        // St291 carriage too) appear under every match.
                        appendTransport(canonical.value(), id);
                        if (canonical != AncTransport::St291 && did != 0) {
                                appendTransport(AncTransport::St291.value(), id);
                        }
                        if (canonical != AncTransport::HdmiInfoFrame && hdmiType != 0) {
                                appendTransport(AncTransport::HdmiInfoFrame.value(), id);
                        }
                        if (canonical != AncTransport::MpegTsPrivate && tsTableId != 0) {
                                appendTransport(AncTransport::MpegTsPrivate.value(), id);
                        }
                }

                void appendTransport(int t, AncFormat::ID id) {
                        AncFormat::IDList &list = byTransport[t];
                        for (auto existing : list) {
                                if (existing == id) return;
                        }
                        list.pushToBack(id);
                }
};

static AncFormatRegistry &registry() {
        static AncFormatRegistry reg;
        return reg;
}

const AncFormat::Data *AncFormat::lookupData(ID id) {
        auto &reg = registry();
        auto  it = reg.entries.find(id);
        if (it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

void AncFormat::registerData(Data &&data) {
        // Route through AncFormatRegistry::add so the indexed views
        // (byCategory / byTransport) stay in sync with runtime
        // registrations.
        registry().add(std::move(data));
}

AncFormat::IDList AncFormat::registeredIDs() {
        auto  &reg = registry();
        IDList ret;
        for (const auto &[id, data] : reg.entries) {
                if (id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

AncFormat::IDList AncFormat::registeredIDsForCategory(const AncCategory &category) {
        auto &reg = registry();
        auto  it = reg.byCategory.find(category.value());
        if (it == reg.byCategory.end()) return IDList();
        return it->second;
}

AncFormat::IDList AncFormat::registeredIDsForTransport(const AncTransport &transport) {
        auto &reg = registry();
        auto  it = reg.byTransport.find(transport.value());
        if (it == reg.byTransport.end()) return IDList();
        return it->second;
}

Result<AncFormat> AncFormat::fromName(const String &name) {
        auto &reg = registry();
        auto  it = reg.nameMap.find(name);
        if (it == reg.nameMap.end()) return makeError<AncFormat>(Error::IdNotFound);
        return makeResult(AncFormat(it->second));
}

AncFormat::ID AncFormat::idFromName(const String &name) {
        auto &reg = registry();
        auto  it = reg.nameMap.find(name);
        return it == reg.nameMap.end() ? Invalid : it->second;
}

AncFormat::AncFormat(const String &name) : d(lookupData(idFromName(name))) {}

AncFormat AncFormat::fromSt291DidSdid(uint8_t did, uint8_t sdid) {
        if (did == 0) return AncFormat(Invalid);
        auto         &reg = registry();
        const Data   *wildcard = nullptr;
        for (const auto &[id, data] : reg.entries) {
                if (id == Invalid) continue;
                if (data.st291Did != did) continue;
                if (data.st291Sdid == sdid) return AncFormat(id);
                // Wildcard SDID-0 entries absorb any SDID under their
                // matching DID; remember the first one we see and only
                // return it after an exhaustive exact-match search fails.
                if (data.st291Sdid == 0 && wildcard == nullptr) wildcard = &data;
        }
        return wildcard != nullptr ? AncFormat(wildcard->id) : AncFormat(Invalid);
}

AncFormat AncFormat::fromHdmiInfoFrameType(uint8_t type) {
        if (type == 0) return AncFormat(Invalid);
        auto &reg = registry();
        // Type 0x81 is shared by VendorInfoFrame, HdrDynamic2094_40,
        // and DvRpu — the latter two require an OUI inspection that
        // happens in the codec.  The registry returns the catch-all
        // VendorInfoFrame so an OUI-blind caller still gets a valid
        // format, and OUI-aware callers promote via AncFormat(id)
        // explicitly.
        if (type == 0x81) return AncFormat(VendorInfoFrame);
        for (const auto &[id, data] : reg.entries) {
                if (id == Invalid) continue;
                if (data.hdmiInfoFrameType == type) return AncFormat(id);
        }
        return AncFormat(Invalid);
}

AncFormat AncFormat::fromMpegTsTableId(uint8_t tableId) {
        if (tableId == 0) return AncFormat(Invalid);
        auto &reg = registry();
        for (const auto &[id, data] : reg.entries) {
                if (id == Invalid) continue;
                if (data.mpegTsTableId == tableId) return AncFormat(id);
        }
        return AncFormat(Invalid);
}

// ============================================================================
// DataStream wire format (v1: tagged String holding the registered name).
// ============================================================================

Error AncFormat::writeToStream(DataStream &s) const {
        s << String(name());
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<AncFormat> AncFormat::readFromStream<1>(DataStream &s) {
        String str;
        s >> str;
        if (s.status() != DataStream::Ok) return makeError<AncFormat>(s.toError());
        return AncFormat::fromString(str);
}

PROMEKI_NAMESPACE_END
