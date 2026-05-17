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
        d.desc = "SMPTE 2016-3 Active Format Description";
        d.category = AncCategory::Aspect;
        d.canonicalTransport = AncTransport::St291;
        d.st291Did = 0x41;
        d.st291Sdid = 0x05;
        return d;
}

static AncFormat::Data makeBarData() {
        AncFormat::Data d;
        d.id = AncFormat::BarData;
        d.name = "BarData";
        d.desc = "SMPTE 2016-3 Bar Data";
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

// ---------------------------------------------------------------------------
// Registry storage
// ---------------------------------------------------------------------------

struct AncFormatRegistry {
                Map<AncFormat::ID, AncFormat::Data> entries;
                Map<String, AncFormat::ID>          nameMap;

                AncFormatRegistry() {
                        add(makeInvalid());
                        add(makeCea708());
                        add(makeCea608());
                        add(makeAfd());
                        add(makeBarData());
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
                }

                void add(AncFormat::Data &&d) {
                        if (d.id != AncFormat::Invalid) nameMap[d.name] = d.id;
                        entries[d.id] = std::move(d);
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
        auto &reg = registry();
        if (data.id != Invalid) reg.nameMap[data.name] = data.id;
        reg.entries[data.id] = std::move(data);
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
        auto  &reg = registry();
        IDList ret;
        for (const auto &[id, data] : reg.entries) {
                if (id == Invalid) continue;
                if (data.category == category) ret.pushToBack(id);
        }
        return ret;
}

AncFormat::IDList AncFormat::registeredIDsForTransport(const AncTransport &transport) {
        auto  &reg = registry();
        IDList ret;
        for (const auto &[id, data] : reg.entries) {
                if (id == Invalid) continue;
                bool matches = data.canonicalTransport == transport;
                if (!matches) {
                        if (transport == AncTransport::St291) {
                                matches = data.st291Did != 0;
                        } else if (transport == AncTransport::HdmiInfoFrame) {
                                matches = data.hdmiInfoFrameType != 0;
                        } else if (transport == AncTransport::MpegTsPrivate) {
                                matches = data.mpegTsTableId != 0;
                        }
                }
                if (matches) ret.pushToBack(id);
        }
        return ret;
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
