/**
 * @file      ancdesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/ancdesc.h>

#include <promeki/error.h>
#include <promeki/sdpsession.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

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
                StringList out;
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
                const AncFormat::IDList all = AncFormat::registeredIDsForTransport(AncTransport::St291);
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
        const String       params = stripFmtpPayloadType(fmtpRaw);
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
