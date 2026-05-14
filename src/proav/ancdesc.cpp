/**
 * @file      ancdesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ancdesc.h>

#include <promeki/datastream.h>
#include <promeki/error.h>
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
        stream.beginFrame(DataStream::TypeAncDesc, 1);
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
        stream.endFrame();
        return stream;
}

DataStream &operator>>(DataStream &stream, AncDesc &desc) {
        if (!stream.readFrame(DataStream::TypeAncDesc)) {
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
        AncFormat::IDList selectSt291Formats(const AncDesc &desc) {
                const AncFormat::IDList  all = AncFormat::registeredIDsForTransport(AncTransport::St291);
                const AncFormat::IDList &allowed = desc.allowedFormats();
                if (allowed.isEmpty()) return all;
                AncFormat::IDList out;
                for (auto id : allowed) {
                        for (auto valid : all) {
                                if (id == valid) {
                                        out.pushToBack(id);
                                        break;
                                }
                        }
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
        if (md.mediaType() != "application") return AncDesc();
        const SdpMediaDescription::RtpMap rm = md.rtpMap();
        if (!rm.valid) return AncDesc();
        if (rm.encoding != "smpte291") return AncDesc();

        AncDesc      out;
        const String fmtpRaw = md.attribute(String("fmtp"));
        if (fmtpRaw.isEmpty()) return out;
        const String     params = stripFmtpPayloadType(fmtpRaw);
        const StringList entries = splitFmtpEntries(params);

        AncFormat::IDList allowed;
        for (const String &entry : entries) {
                if (!entry.startsWith(String("DID_SDID"))) continue;
                uint8_t did = 0, sdid = 0;
                if (!parseOneDidSdid(entry, did, sdid)) continue;
                const AncFormat fmt = AncFormat::fromSt291DidSdid(did, sdid);
                // Push the resolved ID — invalid pairs come through as
                // AncFormat::Invalid so the application can still see
                // there was a wire-side request for an unknown DID/SDID.
                allowed.pushToBack(fmt.id());
        }
        out.setAllowedFormats(std::move(allowed));
        return out;
}

SdpMediaDescription AncDesc::toSdp(uint8_t payloadType) const {
        SdpMediaDescription md;
        md.setMediaType("application");
        md.setProtocol("RTP/AVP");
        md.addPayloadType(payloadType);

        const String ptStr = String::number(payloadType);
        md.setAttribute("rtpmap", ptStr + String(" smpte291/90000"));

        const AncFormat::IDList ids = selectSt291Formats(*this);
        if (ids.isEmpty()) return md;

        String fmtp;
        for (size_t i = 0; i < ids.size(); ++i) {
                const AncFormat fmt(ids.at(i));
                if (!fmt.isValid() || fmt.st291Did() == 0) continue;
                if (!fmtp.isEmpty()) fmtp += String(";");
                fmtp += String("DID_SDID={") + byteHex(fmt.st291Did()) + String(",") +
                        byteHex(fmt.st291Sdid()) + String("}");
        }
        if (!fmtp.isEmpty()) {
                md.setAttribute("fmtp", ptStr + String(" ") + fmtp);
        }
        return md;
}

PROMEKI_NAMESPACE_END
