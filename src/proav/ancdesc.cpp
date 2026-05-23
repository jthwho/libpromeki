/**
 * @file      ancdesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ancdesc.h>

#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/sdpsession.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

AncDesc::AncDesc() : _d(SharedPtr<Impl>::create()) {}

AncDesc::AncDesc(const Size2Du32 &raster, const VideoScanMode &scanMode, const FrameRate &frameRate)
    : _d(SharedPtr<Impl>::create()) {
        Impl *impl = _d.modify();
        impl->sourceRaster = raster;
        impl->scanMode = scanMode;
        impl->frameRate = frameRate;
}

// ---------------------------------------------------------------------------
// Accessors / mutators
// ---------------------------------------------------------------------------

const Size2Du32 &AncDesc::sourceRaster() const { return _d->sourceRaster; }

void AncDesc::setSourceRaster(const Size2Du32 &raster) { _d.modify()->sourceRaster = raster; }

const VideoScanMode &AncDesc::scanMode() const { return _d->scanMode; }

void AncDesc::setScanMode(const VideoScanMode &scanMode) { _d.modify()->scanMode = scanMode; }

const FrameRate &AncDesc::frameRate() const { return _d->frameRate; }

void AncDesc::setFrameRate(const FrameRate &frameRate) { _d.modify()->frameRate = frameRate; }

const AncFormat::IDList &AncDesc::allowedFormats() const { return _d->allowedFormats; }

void AncDesc::setAllowedFormats(AncFormat::IDList ids) { _d.modify()->allowedFormats = std::move(ids); }

const ::promeki::List<AncCategory> &AncDesc::allowedCategories() const { return _d->allowedCategories; }

void AncDesc::setAllowedCategories(::promeki::List<AncCategory> categories) {
        _d.modify()->allowedCategories = std::move(categories);
}

const Metadata &AncDesc::metadata() const { return _d->metadata; }

Metadata &AncDesc::metadata() { return _d.modify()->metadata; }

void AncDesc::setMetadata(Metadata m) { _d.modify()->metadata = std::move(m); }

int AncDesc::pairedVideoStreamIndex() const { return _d->pairedVideoStreamIndex; }

void AncDesc::setPairedVideoStreamIndex(int index) { _d.modify()->pairedVideoStreamIndex = index; }

int AncDesc::pairedAudioStreamIndex() const { return _d->pairedAudioStreamIndex; }

void AncDesc::setPairedAudioStreamIndex(int index) { _d.modify()->pairedAudioStreamIndex = index; }

uint32_t AncDesc::troff() const { return _d->troff; }

void AncDesc::setTroff(uint32_t ticks) { _d.modify()->troff = ticks; }

uint8_t AncDesc::vpidCode() const { return _d->vpidCode; }

void AncDesc::setVpidCode(uint8_t code) { _d.modify()->vpidCode = code; }

AncTransmissionModel AncDesc::transmissionModel() const { return _d->transmissionModel; }

void AncDesc::setTransmissionModel(const AncTransmissionModel &tm) {
        _d.modify()->transmissionModel = tm;
}

// ---------------------------------------------------------------------------
// Predicates
// ---------------------------------------------------------------------------

bool AncDesc::isValid() const {
        const bool hasRaster = _d->sourceRaster.width() > 0 && _d->sourceRaster.height() > 0;
        const bool hasScanMode = _d->scanMode != VideoScanMode::Unknown;
        if (hasRaster && hasScanMode) return true;
        return !_d->allowedFormats.isEmpty() || !_d->allowedCategories.isEmpty();
}

bool AncDesc::acceptsFormat(const AncFormat &fmt) const {
        if (!_d->allowedFormats.isEmpty()) {
                bool found = false;
                for (auto id : _d->allowedFormats) {
                        if (id == fmt.id()) {
                                found = true;
                                break;
                        }
                }
                if (!found) return false;
        }
        if (!_d->allowedCategories.isEmpty()) {
                bool found = false;
                for (const auto &cat : _d->allowedCategories) {
                        if (cat == fmt.category()) {
                                found = true;
                                break;
                        }
                }
                if (!found) return false;
        }
        return true;
}

bool AncDesc::formatEquals(const AncDesc &other) const {
        if (_d.ptr() == other._d.ptr()) return true;
        if (!(_d->sourceRaster == other._d->sourceRaster)) return false;
        if (!(_d->scanMode == other._d->scanMode)) return false;
        if (!(_d->frameRate == other._d->frameRate)) return false;
        if (_d->allowedFormats.size() != other._d->allowedFormats.size()) return false;
        for (size_t i = 0; i < _d->allowedFormats.size(); ++i) {
                if (_d->allowedFormats.at(i) != other._d->allowedFormats.at(i)) return false;
        }
        if (_d->allowedCategories.size() != other._d->allowedCategories.size()) return false;
        for (size_t i = 0; i < _d->allowedCategories.size(); ++i) {
                if (!(_d->allowedCategories.at(i) == other._d->allowedCategories.at(i))) return false;
        }
        if (_d->pairedVideoStreamIndex != other._d->pairedVideoStreamIndex) return false;
        if (_d->pairedAudioStreamIndex != other._d->pairedAudioStreamIndex) return false;
        if (_d->troff != other._d->troff) return false;
        if (_d->vpidCode != other._d->vpidCode) return false;
        if (!(_d->transmissionModel == other._d->transmissionModel)) return false;
        return true;
}

bool AncDesc::operator==(const AncDesc &other) const {
        if (_d.ptr() == other._d.ptr()) return true;
        return formatEquals(other) && _d->metadata == other._d->metadata;
}

// ---------------------------------------------------------------------------
// DataStream serialization
// ---------------------------------------------------------------------------

DataStream &operator<<(DataStream &stream, const AncDesc &desc) {
        // v2 (2026-05-19): appended troff + vpidCode for ST 2110-40
        // §7 SDP fmtp emission.  v1 readers see no trailing fields;
        // v2 readers fall back to the defaults (0, 0) when reading
        // a v1 stream.
        //
        // v3 (2026-05-21): appended transmissionModel for ST 2110-40
        // §6 LLTM / CTM signalling.  v2 readers ignore the trailing
        // field; v3 readers fall back to
        // AncTransmissionModel::Unsignalled when reading a v2 (or
        // earlier) stream.
        stream.beginFrame(DataTypeAncDesc, 3);
        stream << desc.sourceRaster();
        stream << desc.scanMode();
        stream << desc.frameRate();
        const AncFormat::IDList &fmts = desc.allowedFormats();
        stream << static_cast<uint32_t>(fmts.size());
        for (auto id : fmts) stream << static_cast<int32_t>(id);
        const ::promeki::List<AncCategory> &cats = desc.allowedCategories();
        stream << static_cast<uint32_t>(cats.size());
        for (const auto &cat : cats) stream << cat;
        stream << static_cast<int32_t>(desc.pairedVideoStreamIndex());
        stream << static_cast<int32_t>(desc.pairedAudioStreamIndex());
        stream << desc.metadata();
        // v2 trailer.
        stream << static_cast<uint32_t>(desc.troff());
        stream << static_cast<uint8_t>(desc.vpidCode());
        // v3 trailer.
        stream << desc.transmissionModel();
        stream.endFrame();
        return stream;
}

DataStream &operator>>(DataStream &stream, AncDesc &desc) {
        uint16_t version = 0;
        if (!stream.readFrame(DataTypeAncDesc, /*maxVersion=*/3, &version)) {
                desc = AncDesc();
                return stream;
        }
        Size2Du32         raster;
        VideoScanMode     scanMode;
        FrameRate         frameRate;
        uint32_t          fmtCount = 0;
        uint32_t          catCount = 0;
        int32_t           pairedVideo = -1;
        int32_t           pairedAudio = -1;
        Metadata          meta;
        AncFormat::IDList fmts;
        stream >> raster >> scanMode >> frameRate >> fmtCount;
        fmts.reserve(fmtCount);
        for (uint32_t i = 0; i < fmtCount; ++i) {
                int32_t v = 0;
                stream >> v;
                fmts.pushToBack(static_cast<AncFormat::ID>(v));
        }
        stream >> catCount;
        ::promeki::List<AncCategory> cats;
        cats.reserve(catCount);
        for (uint32_t i = 0; i < catCount; ++i) {
                AncCategory cat;
                stream >> cat;
                cats.pushToBack(cat);
        }
        stream >> pairedVideo;
        stream >> pairedAudio;
        stream >> meta;
        uint32_t troff = 0;
        uint8_t  vpidCode = 0;
        if (version >= 2) {
                stream >> troff >> vpidCode;
        }
        AncTransmissionModel tm = AncTransmissionModel::Unsignalled;
        if (version >= 3) {
                stream >> tm;
        }
        if (stream.status() != DataStream::Ok) {
                desc = AncDesc();
                return stream;
        }
        desc = AncDesc();
        desc.setSourceRaster(raster);
        desc.setScanMode(scanMode);
        desc.setFrameRate(frameRate);
        desc.setAllowedFormats(std::move(fmts));
        desc.setAllowedCategories(std::move(cats));
        desc.setPairedVideoStreamIndex(pairedVideo);
        desc.setPairedAudioStreamIndex(pairedAudio);
        desc.setMetadata(std::move(meta));
        desc.setTroff(troff);
        desc.setVpidCode(vpidCode);
        desc.setTransmissionModel(tm);
        return stream;
}

// ---------------------------------------------------------------------------
// SDP serialization helpers (RFC 8331 §6.2)
//
// The DID_SDID parameter list cannot be read through
// @ref SdpMediaDescription::fmtpParameters because RFC 8331 emits one
// @c DID_SDID= entry per pair (multiple keys with the same name, ;-
// separated), and the map-based fmtpParameters collapses duplicates.
// We hand-parse the raw fmtp attribute string instead.
// ---------------------------------------------------------------------------

namespace {

        // Parses a numeric byte literal: either a 0xHH hex form (case-
        // insensitive) or a plain decimal.  Returns @c true on success
        // and writes the parsed value to @p out; returns @c false on
        // malformed input.
        bool parseByteLiteral(const String &raw, uint8_t &out) {
                String tok = raw.trim();
                if (tok.isEmpty()) return false;
                int    base = 10;
                size_t off = 0;
                if (tok.length() > 2 && tok.charAt(0) == '0' &&
                    (tok.charAt(1) == 'x' || tok.charAt(1) == 'X')) {
                        base = 16;
                        off = 2;
                }
                if (off >= tok.length()) return false;
                long long value = 0;
                for (size_t i = off; i < tok.length(); ++i) {
                        const char c = static_cast<char>(tok.charAt(i).codepoint());
                        int        digit = -1;
                        if (c >= '0' && c <= '9') digit = c - '0';
                        else if (base == 16 && c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
                        else if (base == 16 && c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
                        if (digit < 0 || digit >= base) return false;
                        value = value * base + digit;
                        if (value > 0xFF) return false;
                }
                out = static_cast<uint8_t>(value);
                return true;
        }

        // Parses a single "DID_SDID={D,S}" entry from @p entry and
        // returns the parsed DID/SDID on success.  Returns @c false
        // on malformed input.
        bool parseOneDidSdid(const String &entry, uint8_t &did, uint8_t &sdid) {
                String s = entry.trim();
                if (!s.startsWith(String("DID_SDID"))) return false;
                const size_t eq = s.find('=');
                if (eq == String::npos) return false;
                s = s.substr(eq + 1).trim();
                if (!s.startsWith('{') || !s.endsWith(String("}"))) return false;
                String       inner = s.substr(1, s.length() - 2);
                const size_t comma = inner.find(',');
                if (comma == String::npos) return false;
                String didStr = inner.substr(0, comma);
                String sdidStr = inner.substr(comma + 1);
                return parseByteLiteral(didStr, did) && parseByteLiteral(sdidStr, sdid);
        }

        // Splits a raw fmtp attribute value into its space-separated
        // payload-type prefix and the @c ;-separated parameter list.
        // SDP @c a=fmtp:<pt> @c <params> is stored on the
        // SdpMediaDescription as @c "<pt> <params>"; this strips the
        // prefix and returns the parameter portion.
        String stripFmtpPayloadType(const String &raw) {
                const size_t sp = raw.find(' ');
                if (sp == String::npos) return String();
                return raw.substr(sp + 1);
        }

        // Splits @p params on @c ; , trimming each entry and skipping
        // empties.  Each entry is one fmtp parameter (key=value).
        StringList splitFmtpEntries(const String &params) {
                StringList   out;
                size_t       start = 0;
                const size_t n = params.length();
                for (size_t i = 0; i <= n; ++i) {
                        if (i == n || params.charAt(i) == ';') {
                                if (i > start) {
                                        String tok = params.substr(start, i - start).trim();
                                        if (!tok.isEmpty()) out.pushToBack(tok);
                                }
                                start = i + 1;
                        }
                }
                return out;
        }

        // Returns the set of @ref AncFormat IDs whose St291 representation
        // is honored by @p desc: when @ref AncDesc::allowedFormats is
        // empty, every registered St291 format; otherwise the
        // intersection of @ref AncDesc::allowedFormats with the
        // St291-registered set.
        //
        // ST 2110-40 §5.2.1 forbids embedded-audio metadata on this
        // transport ("audio metadata is part of the AES67 / -30 audio
        // stream"), so AudioMetadata-category formats are stripped
        // here with a warning.  The packetizer carries an equivalent
        // egress-side filter (rtppayloadanc.cpp orderedSt291Indices),
        // but doing it here too keeps the SDP from advertising
        // DID_SDID pairs the wire path would refuse to emit.
        AncFormat::IDList selectSt291Formats(const AncDesc &desc) {
                const AncFormat::IDList  all = AncFormat::registeredIDsForTransport(AncTransport::St291);
                const AncFormat::IDList &allowed = desc.allowedFormats();
                AncFormat::IDList        out;
                if (allowed.isEmpty()) {
                        out.reserve(all.size());
                        for (auto id : all) {
                                const AncFormat fmt(id);
                                if (fmt.category() == AncCategory::AudioMetadata) {
                                        promekiWarn("AncDesc::toSdp: stripping %s from "
                                                    "smpte291 SDP fmtp (§5.2.1 forbids "
                                                    "audio metadata on ST 2110-40)",
                                                    fmt.name().cstr());
                                        continue;
                                }
                                out.pushToBack(id);
                        }
                        return out;
                }
                for (auto id : allowed) {
                        bool isSt291 = false;
                        for (auto valid : all) {
                                if (id == valid) {
                                        isSt291 = true;
                                        break;
                                }
                        }
                        if (!isSt291) continue;
                        const AncFormat fmt(id);
                        if (fmt.category() == AncCategory::AudioMetadata) {
                                promekiWarn("AncDesc::toSdp: stripping %s from "
                                            "smpte291 SDP fmtp (§5.2.1 forbids audio "
                                            "metadata on ST 2110-40)",
                                            fmt.name().cstr());
                                continue;
                        }
                        out.pushToBack(id);
                }
                return out;
        }

        // Formats one byte as @c 0xHH (lowercase, zero-padded).
        String byteHex(uint8_t v) {
                static const char digits[] = "0123456789abcdef";
                char              buf[5] = {'0', 'x', digits[(v >> 4) & 0xF], digits[v & 0xF], '\0'};
                return String(buf);
        }

} // namespace

// ---------------------------------------------------------------------------
// SDP round-trip
// ---------------------------------------------------------------------------

AncDesc AncDesc::fromSdp(const SdpMediaDescription &md) {
        if (md.mediaType() != "video") return AncDesc();
        const SdpMediaDescription::RtpMap rm = md.rtpMap();
        if (!rm.valid) return AncDesc();
        if (rm.encoding != "smpte291") return AncDesc();

        AncDesc      out;
        const String fmtpRaw = md.attribute(String("fmtp"));
        if (fmtpRaw.isEmpty()) return out;
        const String     params = stripFmtpPayloadType(fmtpRaw);
        const StringList entries = splitFmtpEntries(params);

        AncFormat::IDList allowed;
        uint32_t          troffVal = 0;
        uint8_t           vpidCodeVal = 0;
        AncTransmissionModel tmVal = AncTransmissionModel::Unsignalled;
        for (const String &entry : entries) {
                if (entry.startsWith(String("DID_SDID"))) {
                        uint8_t did = 0, sdid = 0;
                        if (!parseOneDidSdid(entry, did, sdid)) continue;
                        const AncFormat fmt = AncFormat::fromSt291DidSdid(did, sdid);
                        // Push the resolved ID — invalid pairs come through as
                        // AncFormat::Invalid so the application can still see
                        // there was a wire-side request for an unknown DID/SDID.
                        // Dedupe so multiple SDIDs that collapse onto a single
                        // wildcard format (e.g. SMPTE 2020's 0x01–0x09 → one
                        // Smpte2020Audio ID) appear once.
                        const AncFormat::ID id = fmt.id();
                        bool already = false;
                        for (auto x : allowed) {
                                if (x == id) {
                                        already = true;
                                        break;
                                }
                        }
                        if (!already) allowed.pushToBack(id);
                        continue;
                }
                if (entry.startsWith(String("TROFF"))) {
                        const size_t eq = entry.find('=');
                        if (eq == String::npos) continue;
                        // TROFF is signed 32-bit per ST 2110-40; the
                        // library normalises into unsigned 32-bit 90 kHz
                        // ticks (callers convert from signed at the API
                        // boundary if they need direction).  We parse via
                        // String::toInt and reject negatives.
                        Error err;
                        const int n = entry.substr(eq + 1).trim().toInt(&err);
                        if (err.isOk() && n >= 0) {
                                troffVal = static_cast<uint32_t>(n);
                        }
                        continue;
                }
                if (entry.startsWith(String("VPID_Code"))) {
                        const size_t eq = entry.find('=');
                        if (eq == String::npos) continue;
                        Error     err;
                        const int n = entry.substr(eq + 1).trim().toInt(&err);
                        if (err.isOk() && n >= 0 && n <= 0xFF) {
                                vpidCodeVal = static_cast<uint8_t>(n);
                        }
                        continue;
                }
                if (entry.startsWith(String("TM="))) {
                        // ST 2110-40 §6 / §7: TM=LLTM | CTM.  An
                        // unknown / malformed value leaves @c tmVal at
                        // Unsignalled — receivers shouldn't trust a TM
                        // they can't interpret.  SSN parsing is
                        // intentionally not enforced: a receiver
                        // accepts either edition's SSN string and
                        // keys off TM alone.  The prefix match
                        // includes the '=' so it doesn't shadow
                        // @c TROFF.
                        const String val = entry.substr(3).trim();
                        if (val == "LLTM") {
                                tmVal = AncTransmissionModel::Lltm;
                        } else if (val == "CTM") {
                                tmVal = AncTransmissionModel::Ctm;
                        }
                        continue;
                }
        }
        out.setAllowedFormats(std::move(allowed));
        out.setTroff(troffVal);
        out.setVpidCode(vpidCodeVal);
        out.setTransmissionModel(tmVal);
        return out;
}

SdpMediaDescription AncDesc::toSdp(uint8_t payloadType) const {
        SdpMediaDescription md;
        // RFC 8331 §3.1 / ST 2110-40 §7: smpte291 ANC streams use
        // m=video (not m=application).
        md.setMediaType("video");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(payloadType);

        const String ptStr = String::number(payloadType);
        md.setAttribute("rtpmap", ptStr + String(" smpte291/90000"));

        String fmtp;
        const auto append = [&](const String &entry) {
                if (!fmtp.isEmpty()) fmtp += String(";");
                fmtp += entry;
        };

        // ST 2110-40 §7 mandatory parameters.  The :2023 revision
        // tightens the SSN/TM coupling rule:
        //
        //   - Sender that signals @c TM SHALL set @c SSN=ST2110-40:2023.
        //   - Sender that omits @c TM (the implicit CTM case) SHALL
        //     set @c SSN=ST2110-40:2018 and SHALL NOT emit @c TM.
        //
        // The default @ref AncTransmissionModel::Unsignalled keeps
        // @c SSN=:2018 with no @c TM line — the implicit-CTM form.
        // An explicit @c Lltm / @c Ctm bumps @c SSN to :2023 and
        // emits the matching @c TM= line.  LLTM scheduling itself
        // is the application's responsibility (it requires a
        // per-packet-deadline @c PacketScheduler — Phase D1's
        // @c TxTimePacketScheduler); the descriptor only carries the
        // SDP signalling.
        const AncTransmissionModel tm = transmissionModel();
        if (tm == AncTransmissionModel::Lltm) {
                append(String("SSN=ST2110-40:2023"));
                append(String("TM=LLTM"));
        } else if (tm == AncTransmissionModel::Ctm) {
                append(String("SSN=ST2110-40:2023"));
                append(String("TM=CTM"));
        } else {
                append(String("SSN=ST2110-40:2018"));
        }

        // exactframerate (ST 2110-40 §7) — required so ANC RTP
        // timestamps can be aligned to the paired video frame timing.
        // Emitted as <num>/<den> for non-integer rates and <num> for
        // integer rates (the SDP-conventional shape).
        if (frameRate().isValid()) {
                String fr;
                if (frameRate().denominator() == 1u) {
                        fr = String::number(frameRate().numerator());
                } else {
                        fr = String::number(frameRate().numerator()) + String("/") +
                             String::number(frameRate().denominator());
                }
                append(String("exactframerate=") + fr);
        }

        // TROFF (ST 2110-40 §7) — RTP timestamp offset in 90 kHz
        // ticks.  Emit only when non-zero: a receiver reading no
        // TROFF parameter assumes the default 0 offset, matching the
        // ST 2110-40 §7 contract.
        if (troff() != 0u) {
                append(String("TROFF=") + String::number(static_cast<int64_t>(troff())));
        }

        // VPID_Code (RFC 8331 §3.1) — the ST 352 VPID byte-1 (data
        // stream class) of the paired video stream.  Emit only when
        // declared (non-zero) so receivers that don't know the
        // VPID don't see a spurious "= 0" parameter.
        if (vpidCode() != 0u) {
                append(String("VPID_Code=") + String::number(static_cast<int>(vpidCode())));
        }

        // DID_SDID parameter list — one entry per concrete (DID, SDID)
        // pair this descriptor advertises.
        const AncFormat::IDList ids = selectSt291Formats(*this);
        for (size_t i = 0; i < ids.size(); ++i) {
                const AncFormat fmt(ids.at(i));
                if (!fmt.isValid() || fmt.st291Did() == 0) continue;
                // Wildcard-SDID formats (e.g. Smpte2020Audio) expand
                // into one DID_SDID entry per concrete SDID — emitting
                // SDID=0x00 directly would collide with the Type-1
                // sentinel per RFC 8331 §3.1.
                const ::promeki::List<uint8_t> sdids = fmt.st291ConcreteSdids();
                for (uint8_t sdid : sdids) {
                        append(String("DID_SDID={") + byteHex(fmt.st291Did()) +
                               String(",") + byteHex(sdid) + String("}"));
                }
        }

        if (!fmtp.isEmpty()) {
                md.setAttribute("fmtp", ptStr + String(" ") + fmtp);
        }
        return md;
}

PROMEKI_NAMESPACE_END
