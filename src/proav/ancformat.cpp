/**
 * @file      ancformat.cpp
 * @copyright Jason Howard. All rights reserved.
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
        // In-band control packet — categorised under @c Control (added
        // in P2-21) so callers iterating @c registeredIDsForCategory
        // by content type (Captions / Timecode / …) do not surface it.
        d.category = AncCategory::Control;
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
        d.desc = "RDD 8 / OP-47 VANC Multipacket (companion of the SDP, carries SDP + WSS + other inner ANC)";
        d.category = AncCategory::Subtitles;
        d.canonicalTransport = AncTransport::St291;
        // RDD 8:2008 §4.2(iii): "DID and SDID values 143h and 203h,
        // respectively (includes parity)" → 8-bit DID/SDID 0x43/0x03.
        d.st291Did = 0x43;
        d.st291Sdid = 0x03;
        return d;
}

static AncFormat::Data makeVbiSt2031() {
        AncFormat::Data d;
        d.id = AncFormat::VbiSt2031;
        d.name = "VbiSt2031";
        d.desc = "SMPTE ST 2031 DVB-VBI / SCTE-VBI ancillary data (ETSI EN 301 775 / ANSI/SCTE 127)";
        d.category = AncCategory::Vbi;
        d.canonicalTransport = AncTransport::St291;
        // ST 2031:2015 §5: "The DID word shall be set to the value 41h.
        // The SDID word shall be set to the value of 08h."
        d.st291Did = 0x41;
        d.st291Sdid = 0x08;
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
                // F9 hot-path lookup indexes for AncFormat::fromSt291DidSdid.
                // RTP unpack calls that function once per ANC record (~9 k/s
                // on HD-60, double on 4K60), and the linear-scan
                // implementation was O(N) over the entire registry.  The
                // exact-pair map keys on (did << 8) | sdid; the wildcard
                // map keys on did alone for families that register
                // st291Sdid == 0 (Smpte2020Audio, PacketForDeletion).
                // Insertion is first-write-wins for both maps so the
                // (0x60, 0x60) ATC trio anchors to AtcLtc (the first
                // registered) just like the linear scan did.
                Map<uint16_t, AncFormat::ID> byDidSdid;
                Map<uint8_t, AncFormat::ID>  byDidWildcard;

                AncFormatRegistry() {
                        add(makeInvalid());
                        add(makeCea708());
                        add(makeCea608());
                        add(makeAfd());
                        add(makePanScan());
                        add(makeScte104());
                        add(makeScte35());
                        // ST 12-2:2014 §5: every ATC packet (LTC, VITC1,
                        // VITC2) uses DID=0x60 / SDID=0x60.  The DBB1
                        // byte (UDWs 1..8 b3, LSB-first) discriminates
                        // the flavour; the ATC codec stamps that on the
                        // returned @c AncAtc::payloadType.  All three
                        // AncFormat IDs are kept so callers can drive
                        // the build path with an explicit flavour.  The
                        // bare (DID,SDID) → ID lookup anchors to
                        // @c AtcLtc (lowest ID wins the wildcard match);
                        // capture paths that hand @ref fromSt291DidSdid
                        // the UDW payload get the real flavour resolved
                        // from DBB1 (see @c atcFormatFromDbb1).
                        add(makeAtc(AncFormat::AtcLtc, "AtcLtc", "SMPTE 12M-2 Ancillary Timecode — LTC", 0x60));
                        add(makeAtc(AncFormat::AtcVitc1, "AtcVitc1", "SMPTE 12M-2 Ancillary Timecode — VITC 1", 0x60));
                        add(makeAtc(AncFormat::AtcVitc2, "AtcVitc2", "SMPTE 12M-2 Ancillary Timecode — VITC 2", 0x60));
                        // ST 12-3 ATC_HFRTC: distinct SDID=0x61 (vs the
                        // 0x60 shared by the ST 12-2 trio above).  DBB1
                        // ranges 0x80..0x8F where the low nibble is the
                        // bitstream number (per ST 12-3 §10.1, multiple
                        // bitstream numbers can coexist per frame to
                        // carry parallel timecode sources).  Discriminator
                        // by full DBB1 / DBB2 happens in the codec.
                        add(makeAtc(AncFormat::AtcHfrtc, "AtcHfrtc",
                                    "SMPTE ST 12-3 ATC HFRTC (high-frame-rate timecode)", 0x61));
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
                        // F8 additions: OP-47 SDP family, ST 2031 VBI,
                        // ST 2094-10 dynamic HDR.  No `CcfSt2106`
                        // registration here — ST 2106:2016 is
                        // "Non-PCM Audio in AES3 / DTS Type17",
                        // not a caption flag, and defines no DID/SDID.
                        add(makeOp47Sdp());
                        add(makeOp47Multipack());
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
                        const uint8_t        sdid = d.st291Sdid;
                        const uint8_t        hdmiType = d.hdmiInfoFrameType;
                        const uint8_t        tsTableId = d.mpegTsTableId;
                        entries[id] = std::move(d);
                        byCategory[category.value()].pushToBack(id);
                        // F9 hot-path index for fromSt291DidSdid.  First
                        // write wins so the (DID,SDID) collision case
                        // (ATC trio at 0x60/0x60) anchors to the
                        // first-registered ID — matches the linear scan's
                        // historical behaviour.  Wildcards (sdid == 0)
                        // populate the byDid wildcard map instead.
                        if (did != 0) {
                                if (sdid == 0) {
                                        if (!byDidWildcard.contains(did)) {
                                                byDidWildcard[did] = id;
                                        }
                                } else {
                                        const uint16_t key = static_cast<uint16_t>(
                                                (static_cast<uint16_t>(did) << 8) |
                                                static_cast<uint16_t>(sdid));
                                        if (!byDidSdid.contains(key)) {
                                                byDidSdid[key] = id;
                                        }
                                }
                        }
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

// ST 12-2:2014 §5 / Table 2: the LTC / VITC1 / VITC2 flavours all share
// DID=0x60 / SDID=0x60 and are discriminated only by the DBB1
// payload-type byte, carried in bit 3 of UDWs 1..8 (LSB-first):
// 0x00=ATC_LTC, 0x01=ATC_VITC1, 0x02=ATC_VITC2.  Recovers the matching
// AncFormat::ID from a captured ATC packet's user-data words so a
// receiver labels VITC1 / VITC2 correctly instead of anchoring to the
// lowest-ID AtcLtc that the (DID,SDID) lookup resolves to.
static AncFormat::ID atcFormatFromDbb1(const List<uint16_t> &udw) {
        if (udw.size() < 8) return AncFormat::AtcLtc;
        uint8_t dbb1 = 0;
        for (size_t i = 0; i < 8; ++i) {
                if ((udw[i] & 0x08u) != 0) dbb1 = static_cast<uint8_t>(dbb1 | (1u << i));
        }
        switch (dbb1) {
                case 0x01: return AncFormat::AtcVitc1;
                case 0x02: return AncFormat::AtcVitc2;
                default:   return AncFormat::AtcLtc; // 0x00 = LTC; reserved values → LTC
        }
}

AncFormat AncFormat::fromSt291DidSdid(uint8_t did, uint8_t sdid, const List<uint16_t> *udw) {
        if (did == 0) return AncFormat(Invalid);
        // F9 hot-path lookup: O(1) probes against the byDidSdid /
        // byDidWildcard indexes maintained by AncFormatRegistry::add.
        // The exact-pair probe wins when (DID,SDID) names a registered
        // format directly; otherwise we fall back to the per-DID
        // wildcard family (Smpte2020Audio at DID 0x45, PacketForDeletion
        // at DID 0x80).  The previous linear scan over reg.entries was
        // O(N) per call — ~30 ANC packets per HD-60 frame turned into
        // ~9 k registry walks per channel per second, dominating the
        // RX hot path.
        auto          &reg = registry();
        const uint16_t key = static_cast<uint16_t>(
                (static_cast<uint16_t>(did) << 8) | static_cast<uint16_t>(sdid));
        ID   resolved = Invalid;
        auto exactIt  = reg.byDidSdid.find(key);
        if (exactIt != reg.byDidSdid.end()) {
                resolved = exactIt->second;
        } else {
                auto wildcardIt = reg.byDidWildcard.find(did);
                if (wildcardIt != reg.byDidWildcard.end()) resolved = wildcardIt->second;
        }
        if (resolved == Invalid) return AncFormat(Invalid);

        // The (0x60,0x60) ATC slot resolves to the lowest-ID AtcLtc for
        // all three flavours; when the caller supplies the payload, refine
        // to the real flavour from the DBB1 byte.
        if (udw != nullptr && did == 0x60 && sdid == 0x60 &&
            (resolved == AtcLtc || resolved == AtcVitc1 || resolved == AtcVitc2)) {
                resolved = atcFormatFromDbb1(*udw);
        }
        return AncFormat(resolved);
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

AncFormat AncFormat::fromHdmiInfoFrame(uint8_t type, uint32_t oui) {
        // Non-vendor InfoFrame types resolve identically to
        // @ref fromHdmiInfoFrameType regardless of OUI.
        if (type != 0x81) return fromHdmiInfoFrameType(type);
        // Vendor-Specific InfoFrame — promote on OUI.
        constexpr uint32_t kOuiHdr10Plus = 0x00D046u;  // HDR10+ / Samsung
        constexpr uint32_t kOuiDolby     = 0x00903Eu;  // Dolby Vision
        switch (oui & 0xFFFFFFu) {
                case kOuiHdr10Plus: return AncFormat(HdrDynamic2094_40);
                case kOuiDolby:     return AncFormat(DvRpu);
                default:            return AncFormat(VendorInfoFrame);
        }
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
