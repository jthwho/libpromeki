/**
 * @file      main.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * promeki-pcap — offline packet-capture decoder.  Reads .pcap /
 * .pcapng files, demultiplexes Ethernet / IP / UDP, and decodes
 * SMPTE ST 2110-40 ancillary data with optional SDP-driven flow
 * labelling.  A thin driver over @ref promeki::PcapReader,
 * @ref promeki::PacketDemux, and @ref promeki::PcapFlowRouter.
 */

#include <cstdio>
#include <cstdlib>

#include <promeki/ancdetails.h>
#include <promeki/ancformat.h>
#include <promeki/ansistream.h>
#include <promeki/ancpacket.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/anctranslator.h>
#include <promeki/cmdlineparser.h>
#include <promeki/duration.h>
#include <promeki/enum.h>
#include <promeki/error.h>
#include <promeki/hexdump.h>
#include <promeki/json.h>
#include <promeki/map.h>
#include <promeki/pcapflowrouter.h>
#include <promeki/pcapreader.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/sdpsession.h>
#include <promeki/st291packet.h>
#include <promeki/string.h>
#include <promeki/textstream.h>
#include <promeki/variantspec.h>

using namespace promeki;

namespace {

// ---- ANSI colorization -----------------------------------------------
//
// Output styling follows mediaplay's model: stdout is auto-detected for
// ANSI support (TTY- and NO_COLOR-aware via
// AnsiStream::stdoutSupportsANSI) and the --nocolor flag forces it off.
// Rather than route every line through an AnsiStream, the color codes
// are emitted as inline SGR strings (mirroring
// Logger::defaultConsoleFormatter) so the existing printf-based output
// keeps working: each helper wraps its argument in a select-graphic-
// rendition pair, or returns it untouched when color is disabled.
namespace clr {

bool enabled = false;

String paint(const char *sgr, const String &s) {
        if(!enabled) return s;
        return String("\033[") + sgr + "m" + s + "\033[0m";
}

String header(const String &s) { return paint("1", s); }    // bold
String time(const String &s)   { return paint("0;36", s); } // cyan
String dst(const String &s)    { return paint("0;32", s); } // green
String ssrc(const String &s)   { return paint("0;33", s); } // yellow
String fmt(const String &s)    { return paint("1;32", s); } // bold green
String field(const String &s)  { return paint("0;33", s); } // yellow
String dim(const String &s)    { return paint("2", s); }    // faint
String good(const String &s)   { return paint("0;32", s); } // green
String warn(const String &s)   { return paint("1;33", s); } // bold yellow
String bad(const String &s)    { return paint("1;31", s); } // bold red

} // namespace clr

void printUsage(const CmdLineParser &parser) {
        std::printf("promeki-pcap — offline pcap / pcapng decoder (SMPTE ST 2110-40 ANC)\n\n");
        std::printf("Usage:\n");
        std::printf("  promeki-pcap info  <file>\n");
        std::printf("  promeki-pcap flows <file> [--sdp <file>] [--anc <host:port>]... [--cfg <Key:Value>]...\n");
        std::printf("  promeki-pcap anc   <file> (--sdp <file> | --anc <host:port[/pt]>...)\n");
        std::printf("                            [--type <name>]... [--cfg <Key:Value>]... [--hexdump] [--json]\n\n");
        std::printf("Subcommands:\n");
        std::printf("  info   Container summary: format, byte order, link types, record + byte counts.\n");
        std::printf("  flows  Auto-discovered (or SDP-labelled) RTP flow table, with per-flow RTP health\n");
        std::printf("         (loss / duplicate / reorder / jitter).  Warn on jitter above a threshold with\n");
        std::printf("         --cfg RtpJitterWarnThreshold:10ms.\n");
        std::printf("  anc    Decode ST 2110-40 ancillary data.  Label the ANC flow with --sdp,\n");
        std::printf("         or name it directly with one or more --anc <host:port[/pt]> when you have no SDP.\n");
        std::printf("         Filter to specific ANC formats with one or more --type <name> (e.g. Atc, Cea708).\n");
        std::printf("         Feed parser context with --cfg <Key:Value> (e.g. --cfg AtcParseRateHint:30 to\n");
        std::printf("         supply the frame rate the ATC timecode parser needs, or --cfg RtpJitterWarnThreshold:10ms to\n");
        std::printf("         warn on RTP jitter); --cfg list shows all keys.\n\n");
        std::printf("Options:\n");
        const StringList usage = parser.generateUsage();
        for(size_t i = 0; i < usage.size(); ++i) std::printf("  %s\n", usage[i].cstr());
}

String hex32(uint32_t v) {
        return String("0x") + String::number(v, 16, 8, '0');
}

// Format a capture DateTime with full nanosecond precision so packets
// across streams can be timed up against each other.  Floors to the
// whole second (avoiding to_time_t rounding) and appends the fraction.
String fullTimestamp(const DateTime &dt) {
        if(!dt.isValid()) return String("n/a");
        const int64_t ns = dt.nanoseconds();
        int64_t secs = ns / 1000000000LL;
        int64_t frac = ns - secs * 1000000000LL;
        if(frac < 0) {
                secs -= 1;
                frac += 1000000000LL;
        }
        return DateTime(static_cast<time_t>(secs)).toString("%F %T") +
                "." + String::number(frac, 10, 9, '0');
}

// Print every registered AncTranslateConfig key with its type / range /
// enum-value details and description (the `--cfg list` action).
int listTranslateConfig() {
        AncTranslateConfig::SpecMap specs;
        const Map<uint64_t, VariantSpec> raw = AncTranslateConfig::registeredSpecs();
        for(auto it = raw.cbegin(); it != raw.cend(); ++it) {
                specs.insert(AncTranslateConfig::ID::fromId(it->first), it->second);
        }
        std::printf("AncTranslateConfig keys (set with --cfg Key:Value):\n\n");
        TextStream ts(stdout);
        AncTranslateConfig::writeSpecMapHelp(ts, specs);
        ts << endl;

        // Spell out the allowed values for the enum-typed keys.
        bool wroteHeader = false;
        for(auto it = specs.cbegin(); it != specs.cend(); ++it) {
                const VariantSpec &spec = it->second;
                if(!spec.hasEnumType()) continue;
                if(!wroteHeader) {
                        std::printf("Enum values:\n");
                        wroteHeader = true;
                }
                const Enum::Type      type = spec.enumType();
                const Enum::ValueList vals = Enum::values(type);
                std::printf("  %-24s ", it->first.name().cstr());
                for(size_t i = 0; i < vals.size(); ++i) {
                        std::printf("%s%s", i == 0 ? "" : ", ", vals[i].first().cstr());
                }
                std::printf("\n");
        }
        if(wroteHeader) std::printf("\n");
        return 0;
}

// Apply --cfg Key:Value pairs to an AncTranslateConfig, parsing each
// value against the key's registered VariantSpec (so e.g.
// "AtcParseRateHint:30" lands as a uint32_t).  Returns false (after
// printing an error) on a malformed pair, unknown key, or parse failure.
bool buildTranslateConfig(const StringList &specs, AncTranslateConfig &cfg) {
        for(size_t i = 0; i < specs.size(); ++i) {
                const String &kv = specs[i];
                const size_t colon = kv.find(':');
                if(colon == String::npos || colon == 0) {
                        std::fprintf(stderr, "error: bad --cfg '%s' (expected Key:Value)\n", kv.cstr());
                        return false;
                }
                const String key = kv.left(colon);
                const String val = kv.mid(colon + 1);
                const AncTranslateConfig::ID id(key);
                const VariantSpec *spec = AncTranslateConfig::spec(id);
                if(spec == nullptr) {
                        std::fprintf(stderr, "error: unknown --cfg key '%s'\n", key.cstr());
                        return false;
                }
                Error   pe;
                Variant value = spec->parseString(val, &pe);
                if(pe.isError()) {
                        std::fprintf(stderr, "error: --cfg %s:%s — could not parse as %s\n", key.cstr(), val.cstr(),
                                     spec->typeName().cstr());
                        return false;
                }
                cfg.set(id, std::move(value));
        }
        return true;
}

// True if `name` is wanted given the (possibly empty) --type filter.
bool typeWanted(const StringList &filters, const String &name) {
        if(filters.isEmpty()) return true;
        const String lname = name.toLower();
        for(size_t i = 0; i < filters.size(); ++i) {
                if(filters[i].toLower() == lname) return true;
        }
        return false;
}

// "PcapFlowKind::Anc" -> "Anc" for compact table output.
String kindShort(const PcapFlowKind &kind) {
        const String s = kind.toString();
        const size_t p = s.rfind(':');
        return p == String::npos ? s : s.mid(p + 1);
}

// Apply each --anc <host:port[/pt]> spec as a manually-designated ANC
// flow.  An optional "/pt" suffix pins the RTP payload type; without it
// the flow accepts any PT.  Returns false (after printing an error) on a
// malformed spec.
bool applyAncFlows(PcapFlowRouter &router, const StringList &specs) {
        for(size_t i = 0; i < specs.size(); ++i) {
                String hostPort = specs[i];
                int pt = -1;
                const size_t slash = hostPort.find('/');
                if(slash != String::npos) {
                        const String ptStr = hostPort.mid(slash + 1);
                        hostPort = hostPort.left(slash);
                        Error ptErr;
                        const int parsed = ptStr.toInt(&ptErr);
                        if(ptErr.isError() || parsed < 0 || parsed > 127) {
                                std::fprintf(stderr, "error: bad payload type in --anc '%s' (expected 0-127)\n",
                                             specs[i].cstr());
                                return false;
                        }
                        pt = parsed;
                }
                auto [sa, e] = SocketAddress::fromString(hostPort);
                if(e.isError()) {
                        std::fprintf(stderr, "error: bad --anc '%s' (expected host:port[/pt])\n", specs[i].cstr());
                        return false;
                }
                router.addAncFlow(sa, pt);
        }
        return true;
}

// ---- info ------------------------------------------------------------

int cmdInfo(const String &path) {
        PcapReader reader;
        const Error err = reader.openFile(path);
        if(err.isError()) {
                std::fprintf(stderr, "error: cannot open '%s': %s\n", path.cstr(), err.desc().cstr());
                return 1;
        }
        std::printf("File:        %s\n", path.cstr());
        std::printf("Format:      %s\n", reader.format().toString().cstr());
        std::printf("Byte order:  %s\n", reader.byteOrder().toString().cstr());
        std::printf("Link type:   %s\n", reader.linkType().toString().cstr());
        std::printf("Interfaces:  %zu\n", reader.interfaceCount());
        std::printf("Snap length: %u\n", reader.snapLength());

        uint64_t records = 0;
        uint64_t bytes = 0;
        DateTime first;
        DateTime last;
        for(;;) {
                auto [rec, rerr] = reader.next();
                if(rerr == Error::EndOfFile) break;
                if(rerr.isError()) {
                        std::printf("(stopped: %s)\n", rerr.desc().cstr());
                        break;
                }
                ++records;
                bytes += rec.capturedLength();
                if(rec.captureTime.isValid()) {
                        if(!first.isValid()) first = rec.captureTime;
                        last = rec.captureTime;
                }
        }
        std::printf("Records:     %llu\n", static_cast<unsigned long long>(records));
        std::printf("Captured:    %llu bytes\n", static_cast<unsigned long long>(bytes));
        if(first.isValid()) {
                std::printf("First:       %s\n", clr::time(fullTimestamp(first)).cstr());
                std::printf("Last:        %s\n", clr::time(fullTimestamp(last)).cstr());
        }
        return 0;
}

// ---- RTP anomalies ---------------------------------------------------

// True for anomalies that warrant a Warning (vs the lower-severity
// Info-level reorder / duplicate notes).
bool rtpAnomalyIsWarning(const PcapFlowRouter::RtpAnomaly &a) {
        using K = PcapFlowRouter::RtpAnomaly::Kind;
        return a.kind != K::Reorder && a.kind != K::Duplicate;
}

// Stable wire name for an anomaly kind (JSON output).
String rtpAnomalyKindName(PcapFlowRouter::RtpAnomaly::Kind k) {
        using K = PcapFlowRouter::RtpAnomaly::Kind;
        switch(k) {
                case K::SsrcChange: return String("ssrcChange");
                case K::PayloadTypeChange: return String("payloadTypeChange");
                case K::PacketLoss: return String("packetLoss");
                case K::Reorder: return String("reorder");
                case K::Duplicate: return String("duplicate");
                case K::TimestampRegression: return String("timestampRegression");
                case K::JitterExceeded: return String("jitterExceeded");
        }
        return String("unknown");
}

// Human-readable one-liner, prefixed with its severity tag to match the
// "[Warning] …" / "[Info] …" style of the ANC detail issues.
String rtpAnomalyText(const PcapFlowRouter::RtpAnomaly &a) {
        using K = PcapFlowRouter::RtpAnomaly::Kind;
        const String tag  = rtpAnomalyIsWarning(a) ? String("[Warning] ") : String("[Info] ");
        const String dst  = a.dst.toString();
        const String ssrc = hex32(a.ssrc);
        switch(a.kind) {
                case K::SsrcChange:
                        return tag + "RTP SSRC changed on " + dst + ": " + hex32(a.previous) + " -> " + ssrc;
                case K::PayloadTypeChange:
                        return tag + "RTP payload type changed on " + dst + " (ssrc " + ssrc + "): was PT " +
                               String::number(a.previous);
                case K::PacketLoss:
                        return tag + "RTP packet loss on " + dst + " (ssrc " + ssrc + "): " +
                               String::number(a.count) + (a.count == 1 ? " packet missing" : " packets missing");
                case K::TimestampRegression:
                        return tag + "RTP timestamp regressed on " + dst + " (ssrc " + ssrc + "): was " +
                               String::number(a.previous);
                case K::Reorder:
                        return tag + "RTP reorder on " + dst + " (ssrc " + ssrc + ")";
                case K::Duplicate:
                        return tag + "RTP duplicate on " + dst + " (ssrc " + ssrc + ")";
                case K::JitterExceeded:
                        return tag + "RTP jitter on " + dst + " (ssrc " + ssrc + "): " +
                               a.jitter.toScaledString() + " over the configured threshold";
        }
        return tag + "RTP anomaly on " + dst;
}

// JSON form of an anomaly for the `anc --json` output.
JsonObject rtpAnomalyJson(const PcapFlowRouter::RtpAnomaly &a) {
        JsonObject o;
        o.set("kind", rtpAnomalyKindName(a.kind));
        o.set("dst", a.dst.toString());
        o.set("ssrc", static_cast<uint64_t>(a.ssrc));
        o.set("previous", static_cast<uint64_t>(a.previous));
        o.set("lost", static_cast<uint64_t>(a.count));
        if(a.kind == PcapFlowRouter::RtpAnomaly::Kind::JitterExceeded) {
                o.set("jitterNs", static_cast<int64_t>(a.jitter.nanoseconds()));
                o.set("jitter", a.jitter.toScaledString());
        }
        if(a.captureTime.isValid()) o.set("captureTime", fullTimestamp(a.captureTime));
        return o;
}

// ---- flows -----------------------------------------------------------

int cmdFlows(const String &path, const String &sdpPath, const StringList &ancSpecs, const StringList &cfgSpecs) {
        AncTranslateConfig cfg;
        if(!buildTranslateConfig(cfgSpecs, cfg)) return 2;
        const Duration jitterWarn = cfg.getAs<Duration>(AncTranslateConfig::RtpJitterWarnThreshold, Duration::zero());

        PcapFlowRouter router;
        router.setJitterWarnThreshold(jitterWarn);
        if(!sdpPath.isEmpty()) {
                auto [sdp, serr] = SdpSession::fromFile(sdpPath);
                if(serr.isError()) {
                        std::fprintf(stderr, "error: cannot read SDP '%s': %s\n", sdpPath.cstr(), serr.desc().cstr());
                        return 1;
                }
                router.setSdp(sdp);
        }
        if(!applyAncFlows(router, ancSpecs)) return 2;

        // Tally the discrete cross-row events (SSRC / payload-type changes)
        // per destination — those spawn new flow rows, so a per-destination
        // count is the clearest way to surface them.
        struct DiscreteTally {
                uint64_t ssrcChanges = 0;
                uint64_t ptChanges = 0;
                uint64_t jitterExcursions = 0;
        };
        Map<String, DiscreteTally> discrete;
        router.onRtpAnomaly([&](const PcapFlowRouter::RtpAnomaly &a) {
                using K = PcapFlowRouter::RtpAnomaly::Kind;
                if(a.kind == K::SsrcChange) discrete[a.dst.toString()].ssrcChanges++;
                else if(a.kind == K::PayloadTypeChange) discrete[a.dst.toString()].ptChanges++;
                else if(a.kind == K::JitterExceeded) discrete[a.dst.toString()].jitterExcursions++;
        });

        const Error err = router.processFile(path);
        if(err.isError()) {
                std::fprintf(stderr, "error: cannot process '%s': %s\n", path.cstr(), err.desc().cstr());
                return 1;
        }
        const List<PcapFlowRouter::FlowStat> &stats = router.flowStats();
        const String header = String::sprintf("%-26s %-12s %4s %-7s %9s %12s %7s %6s %6s %10s", "DESTINATION", "SSRC",
                                              "PT", "KIND", "PACKETS", "BYTES", "LOST", "DUP", "REORD", "JITTER");
        std::printf("%s\n", clr::header(header).cstr());
        for(const PcapFlowRouter::FlowStat &s : stats) {
                // Pad each colored column to width *before* wrapping it in
                // SGR codes — escape bytes would otherwise count toward the
                // printf field width and break the table alignment.
                const String dstCol  = clr::dst(String::sprintf("%-26s", s.dst.toString().cstr()));
                const String ssrcCol = clr::ssrc(String::sprintf("%-12s", hex32(s.ssrc).cstr()));
                String       lostCol = String::sprintf("%7llu", static_cast<unsigned long long>(s.lostPackets));
                if(s.lostPackets > 0) lostCol = clr::bad(lostCol);
                // Jitter as an auto-scaled Duration ("1.50 ms"); flagged red
                // when it crosses the configured warning threshold.
                String jitterCol = String::sprintf("%10s", s.maxJitter.toScaledString().cstr());
                if(jitterWarn.isValid() && jitterWarn.nanoseconds() > 0 &&
                   s.maxJitter.nanoseconds() > jitterWarn.nanoseconds()) {
                        jitterCol = clr::bad(jitterCol);
                }
                std::printf("%s %s %4u %-7s %9llu %12llu %s %6llu %6llu %s\n", dstCol.cstr(), ssrcCol.cstr(),
                            static_cast<unsigned>(s.payloadType), kindShort(s.kind).cstr(),
                            static_cast<unsigned long long>(s.packets), static_cast<unsigned long long>(s.bytes),
                            lostCol.cstr(), static_cast<unsigned long long>(s.duplicatePackets),
                            static_cast<unsigned long long>(s.reorderedPackets), jitterCol.cstr());
        }
        if(stats.isEmpty()) std::printf("(no RTP flows found)\n");

        // RTP anomaly summary: the discrete per-destination events plus any
        // per-flow timestamp regressions (the column-friendly quality counts
        // above already cover loss / duplicate / reorder / jitter).
        StringList notes;
        for(auto it = discrete.cbegin(); it != discrete.cend(); ++it) {
                String n;
                if(it->second.ssrcChanges > 0) {
                        n += String::number(it->second.ssrcChanges) + " SSRC change" +
                             (it->second.ssrcChanges == 1 ? "" : "s");
                }
                if(it->second.ptChanges > 0) {
                        if(!n.isEmpty()) n += ", ";
                        n += String::number(it->second.ptChanges) + " payload-type change" +
                             (it->second.ptChanges == 1 ? "" : "s");
                }
                if(it->second.jitterExcursions > 0) {
                        if(!n.isEmpty()) n += ", ";
                        n += String::number(it->second.jitterExcursions) + " jitter excursion" +
                             (it->second.jitterExcursions == 1 ? "" : "s");
                }
                if(!n.isEmpty()) notes.pushToBack(String("  ") + it->first + "  " + n);
        }
        for(const PcapFlowRouter::FlowStat &s : stats) {
                if(s.timestampRegressions > 0) {
                        notes.pushToBack(String("  ") + s.dst.toString() + " (ssrc " + hex32(s.ssrc) + ")  " +
                                         String::number(s.timestampRegressions) + " timestamp regression" +
                                         (s.timestampRegressions == 1 ? "" : "s"));
                }
        }
        if(!notes.isEmpty()) {
                std::printf("\n%s\n", clr::warn("RTP anomalies:").cstr());
                for(const String &n : notes) std::printf("%s\n", clr::warn(n).cstr());
        }
        return 0;
}

// ---- anc -------------------------------------------------------------

// Resolved ANC format name for a packet (prefers the packet's own
// AncFormat, falls back to DID/SDID resolution).
String packetFormatName(const AncPacket &p) {
        if(p.format().isValid()) return p.format().name();
        auto [sp, e] = St291Packet::from(p);
        if(e.isOk()) return AncFormat::fromSt291DidSdid(sp.did(), sp.sdid()).name();
        return String("Unknown");
}

// Build a JSON object describing one decoded ANC packet.
JsonObject ancPacketJson(const AncTranslator &tr, const AncPacket &pkt, bool hexdump) {
        JsonObject o;
        auto [sp, e] = St291Packet::from(pkt);
        if(e.isOk()) {
                o.set("did", static_cast<unsigned int>(sp.did()));
                o.set("sdid", static_cast<unsigned int>(sp.sdid()));
                o.set("checksum", static_cast<unsigned int>(sp.checksum()));
                o.set("computedChecksum", static_cast<unsigned int>(sp.computedChecksum()));
                o.set("checksumValid", sp.checksumValid());
        }
        o.set("format", packetFormatName(pkt));
        o.set("line", static_cast<unsigned int>(pkt.st291Line()));
        o.set("hOffset", static_cast<unsigned int>(pkt.st291HOffset()));
        o.set("streamNum", static_cast<unsigned int>(pkt.st291StreamNum()));
        o.set("dataBytes", static_cast<uint64_t>(pkt.data().size()));
        // Optional space-separated hex of the raw ST 291 payload bytes,
        // emitted as a single string (no address / ASCII columns).
        if(hexdump) {
                o.set("hex", HexDump()
                                     .setShowAddress(false)
                                     .setShowAscii(false)
                                     .setBytesPerLine(pkt.data().size() < 1 ? 1 : pkt.data().size())
                                     .build(pkt.data())
                                     .trim());
        }
        // Full human-readable analysis: a `details` object carrying the
        // fully-decoded `lines` array plus a `issues` array of
        // severity-tagged warnings / errors.
        o.set("details", tr.details(pkt).toJson());
        return o;
}

// Per-flow running state so each ANC frame can report how far it sits
// from the previous frame on the same SSRC — both in wall-clock capture
// time and in RTP-timestamp ticks (the 90 kHz ANC media clock).
struct AncFlowTiming {
        DateTime prevCapture;
        uint32_t prevRtp = 0;
        bool     haveRtp = false;
        // Fingerprint of the previous ATC packet seen on this SSRC, used to
        // flag a frozen time address (consecutive byte-identical packets).
        String   prevAtcHex;
        bool     havePrevAtc = false;
};

// Pretty inter-frame deltas for the ANC header line.  `dWall` is the
// capture-time gap; `dRtp` is the RTP-timestamp advance translated to
// wall-clock via the ANC 90 kHz clock.  Either is "n/a" on the first
// frame (or an invalid capture time).  Both use the library's auto-scaled
// Duration formatting (e.g. "33.3 ms").
void ancFrameDeltas(AncFlowTiming &state, const PcapFlowRouter::RoutedAncFrame &f, String &dWall, String &dRtp) {
        dWall = String("n/a");
        dRtp  = String("n/a");
        if(f.captureTime.isValid() && state.prevCapture.isValid()) {
                dWall = Duration::fromNanoseconds(f.captureTime.nanoseconds() - state.prevCapture.nanoseconds())
                                .toScaledString();
        }
        if(state.haveRtp) {
                // Unsigned subtraction folds the 32-bit wraparound into the
                // natural forward delta.
                const uint32_t ticks = f.anc.rtpTimestamp - state.prevRtp;
                dRtp = Duration::fromSamples(static_cast<int64_t>(ticks), RtpPayloadAnc::ClockRate).toScaledString();
        }
        if(f.captureTime.isValid()) state.prevCapture = f.captureTime;
        state.prevRtp  = f.anc.rtpTimestamp;
        state.haveRtp  = true;
}

// Print an AncDetails as a compact indented block.  The decoded
// "Name = Value" field lines are packed several to a row (separated by
// " | ", wrapped to a target width) to make better use of horizontal
// space; each issue keeps its own row, prefixed with its severity (e.g.
// "[Warning] …") so warnings and errors stay easy to spot.
void printAncDetails(const AncDetails &d, const char *indent) {
        constexpr size_t kWrapWidth = 100;
        const size_t     indentLen = String(indent).size();
        const String     sep(" | ");

        String row;
        auto   flush = [&]() {
                if(!row.isEmpty()) {
                        std::printf("%s%s\n", indent, row.cstr());
                        row = String();
                }
        };
        for(const String &line : d.lines()) {
                if(row.isEmpty()) {
                        row = line;
                } else if(indentLen + row.size() + sep.size() + line.size() <= kWrapWidth) {
                        row += sep;
                        row += line;
                } else {
                        flush();
                        row = line;
                }
        }
        flush();

        for(const AncDetails::Issue &iss : d.issues()) {
                const String tag = String("[") + iss.severity.valueName() + "] " + iss.message;
                String       line = tag;
                if(iss.severity == AncDetailSeverity::Error) {
                        line = clr::bad(tag);
                } else if(iss.severity == AncDetailSeverity::Warning) {
                        line = clr::warn(tag);
                }
                std::printf("%s%s\n", indent, line.cstr());
        }
}

void printAncFrameText(const AncTranslator &tr, const StringList &filters, AncFlowTiming &state, uint64_t &frameIndex,
                       const PcapFlowRouter::RoutedAncFrame &f, bool hexdump) {
        // Select the packets that pass the --type filter (all of them if no
        // filter was given); skip the frame entirely if none match.
        List<size_t> shown;
        for(size_t i = 0; i < f.anc.packets.size(); ++i) {
                if(typeWanted(filters, packetFormatName(f.anc.packets[i]))) shown.pushToBack(i);
        }
        if(shown.isEmpty() && !filters.isEmpty()) return;

        String dWall;
        String dRtp;
        ancFrameDeltas(state, f, dWall, dRtp);
        std::printf("%s (+%s) ANC %llu dst=%s ssrc=%s rtpts=%u (%s) +%s pkts=%d%s\n",
                    clr::time(fullTimestamp(f.captureTime)).cstr(), dWall.cstr(),
                    static_cast<unsigned long long>(frameIndex++), clr::dst(f.dst.toString()).cstr(),
                    clr::ssrc(hex32(f.ssrc)).cstr(), f.anc.rtpTimestamp, hex32(f.anc.rtpTimestamp).cstr(),
                    dRtp.cstr(), f.anc.packetCount,
                    f.anc.keepAlive ? clr::dim(" (keep-alive)").cstr() : "");
        for(size_t k = 0; k < shown.size(); ++k) {
                const AncPacket &p = f.anc.packets[shown[k]];
                const String     fmtName = packetFormatName(p);
                auto [sp, e] = St291Packet::from(p);
                String didSdid("DID/SDID=?");
                // ST 291 checksum word (10-bit), rendered alongside the
                // decoded fields so it can be checked against the wire.  A
                // mismatch with the value recomputed over DID/SDID/DC/UDW
                // (§6.4) shows both "stored!=computed" and is flagged red.
                String csStr;
                if(e.isOk()) {
                        didSdid = String("DID=0x") + String::number(sp.did(), 16, 2, '0') + " SDID=0x" +
                                  String::number(sp.sdid(), 16, 2, '0');
                        const String csHex = String("0x") + String::number(sp.checksum(), 16, 3, '0');
                        if(sp.checksumValid()) {
                                csStr = String(" cs=") + clr::good(csHex);
                        } else {
                                const String comp = String("0x") + String::number(sp.computedChecksum(), 16, 3, '0');
                                csStr = String(" cs=") + clr::bad(csHex + "!=" + comp);
                        }
                }
                std::printf("  [%zu] %s line=%u hoff=%u stream=%u fmt=%s%s bytes=%zu\n", shown[k],
                            clr::field(didSdid).cstr(), static_cast<unsigned>(p.st291Line()),
                            static_cast<unsigned>(p.st291HOffset()), static_cast<unsigned>(p.st291StreamNum()),
                            clr::fmt(fmtName).cstr(), csStr.cstr(), p.data().size());
                // Full analysis of the packet: every decoded field on its own
                // line plus any severity-tagged warnings / errors.
                printAncDetails(tr.details(p), "        ");
                // Flag a frozen ATC time address: a conformant ATC stream
                // always advances either the time address or the field-mark
                // bit between frames, so consecutive byte-identical packets on
                // one SSRC mean the sender is re-transmitting a stale
                // timecode.  (A field-marked HFR pair differs in the
                // field-mark bit, so it is not flagged.)
                if(fmtName.left(3) == String("Atc")) {
                        const String hex = p.data().toHex();
                        if(state.havePrevAtc && hex == state.prevAtcHex) {
                                std::printf("        %s\n",
                                            clr::warn("[Warning] duplicate ATC packet — byte-identical to the "
                                                      "previous on this flow (time address not advancing)")
                                                    .cstr());
                        }
                        state.prevAtcHex   = hex;
                        state.havePrevAtc = true;
                }
                // Optional raw hex of the ST 291 packet payload (DID, SDID,
                // DataCount, the user-data words and checksum) so the decoded
                // fields can be checked against the wire bytes.
                if(hexdump) {
                        const String dump = HexDump().setIndent("        ").build(p.data());
                        std::fputs(dump.cstr(), stdout);
                }
        }
}

int cmdAnc(const String &path, const String &sdpPath, const StringList &ancSpecs, const StringList &typeFilters,
           const StringList &cfgSpecs, bool asJson, bool hexdump) {
        if(sdpPath.isEmpty() && ancSpecs.isEmpty()) {
                std::fprintf(stderr, "error: 'anc' needs --sdp or at least one --anc <host:port> to label the ANC flow\n");
                return 2;
        }

        AncTranslateConfig cfg;
        if(!buildTranslateConfig(cfgSpecs, cfg)) return 2;

        PcapFlowRouter router;
        router.setJitterWarnThreshold(
                cfg.getAs<Duration>(AncTranslateConfig::RtpJitterWarnThreshold, Duration::zero()));
        if(!sdpPath.isEmpty()) {
                auto [sdp, serr] = SdpSession::fromFile(sdpPath);
                if(serr.isError()) {
                        std::fprintf(stderr, "error: cannot read SDP '%s': %s\n", sdpPath.cstr(), serr.desc().cstr());
                        return 1;
                }
                router.setSdp(sdp);
        }
        if(!applyAncFlows(router, ancSpecs)) return 2;

        AncTranslator translator(cfg);
        JsonArray frames;
        JsonArray anomalies;                     // collected RTP anomalies (JSON output)
        Map<uint32_t, AncFlowTiming> timing;     // per-SSRC inter-frame delta state
        uint64_t                     frameIndex = 0; // running id of each printed/emitted ANC frame

        // RTP-level warnings for the flow(s) we're decoding.  These fire in
        // capture order during processing, so in text mode they print inline
        // between the surrounding ANC frames; in JSON mode they collect into a
        // sibling array so stdout stays valid JSON.  Anomalies on other flows
        // (e.g. an unlabelled video stream in the same capture) are skipped.
        router.onRtpAnomaly([&](const PcapFlowRouter::RtpAnomaly &a) {
                if(a.flowKind != PcapFlowKind::Anc) return;
                if(asJson) {
                        anomalies.append(rtpAnomalyJson(a));
                        return;
                }
                const String line = fullTimestamp(a.captureTime) + "  " + rtpAnomalyText(a);
                std::printf("%s\n", (rtpAnomalyIsWarning(a) ? clr::warn(line) : clr::dim(line)).cstr());
        });

        router.onAncFrame([&](const PcapFlowRouter::RoutedAncFrame &f) {
                if(asJson) {
                        JsonArray pkts;
                        for(size_t i = 0; i < f.anc.packets.size(); ++i) {
                                if(typeWanted(typeFilters, packetFormatName(f.anc.packets[i]))) {
                                        pkts.append(ancPacketJson(translator, f.anc.packets[i], hexdump));
                                }
                        }
                        if(pkts.size() == 0 && !typeFilters.isEmpty()) return; // nothing of interest
                        String dWall;
                        String dRtp;
                        ancFrameDeltas(timing[f.ssrc], f, dWall, dRtp);
                        JsonObject fo;
                        fo.set("index", static_cast<uint64_t>(frameIndex++));
                        fo.set("dst", f.dst.toString());
                        fo.set("ssrc", static_cast<uint64_t>(f.ssrc));
                        fo.set("rtpTimestamp", static_cast<uint64_t>(f.anc.rtpTimestamp));
                        fo.set("rtpDelta", dRtp);
                        fo.set("captureDelta", dWall);
                        if(f.captureTime.isValid()) fo.set("captureTime", fullTimestamp(f.captureTime));
                        fo.set("keepAlive", f.anc.keepAlive);
                        fo.set("packets", pkts);
                        frames.append(fo);
                } else {
                        printAncFrameText(translator, typeFilters, timing[f.ssrc], frameIndex, f, hexdump);
                }
        });

        const Error err = router.processFile(path);
        if(err.isError()) {
                std::fprintf(stderr, "error: cannot process '%s': %s\n", path.cstr(), err.desc().cstr());
                return 1;
        }
        if(asJson) {
                JsonObject root;
                root.set("frames", frames);
                root.set("rtpAnomalies", anomalies);
                std::printf("%s\n", root.toString(2).cstr());
        }
        return 0;
}

} // namespace

int main(int argc, char *argv[]) {
        String sdpPath;
        StringList ancSpecs;
        StringList typeFilters;
        StringList cfgSpecs;
        bool asJson = false;
        bool hexdump = false;
        bool showHelp = false;
        bool noColor = false;

        CmdLineParser parser;
        parser.registerOptions({
                {'h', "help", "Show this help and exit",
                 CmdLineParser::OptionCallback([&]() { showHelp = true; return 0; })},
                {0, "sdp", "SDP file describing the capture's flows (labels ANC / audio / video)",
                 CmdLineParser::OptionStringCallback([&](const String &s) { sdpPath = s; return 0; })},
                {0, "anc", "Treat <host:port[/pt]> as an ANC flow (no SDP needed); repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) { ancSpecs.pushToBack(s); return 0; })},
                {0, "type", "Only dump ANC packets of this format (e.g. Atc, Cea708, Afd); repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) { typeFilters.pushToBack(s); return 0; })},
                {0, "cfg", "Set an AncTranslateConfig key (Key:Value, e.g. AtcParseRateHint:30); 'list' shows all keys; repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) { cfgSpecs.pushToBack(s); return 0; })},
                {0, "json", "Emit JSON instead of human-readable text (anc subcommand)",
                 CmdLineParser::OptionCallback([&]() { asJson = true; return 0; })},
                {0, "hexdump", "Also dump each ANC packet's raw ST 291 payload bytes as a hex dump (anc subcommand)",
                 CmdLineParser::OptionCallback([&]() { hexdump = true; return 0; })},
                {0, "nocolor", "Disable ANSI color output (color is otherwise auto-enabled on a color-capable TTY)",
                 CmdLineParser::OptionCallback([&]() { noColor = true; return 0; })},
        });

        if(parser.parseMain(argc, argv) != 0) {
                printUsage(parser);
                return 2;
        }
        if(showHelp) {
                printUsage(parser);
                return 0;
        }

        // Color is on by default when stdout is a color-capable TTY
        // (AnsiStream::stdoutSupportsANSI honors NO_COLOR and pipe / file
        // redirects); --nocolor forces it off unconditionally.
        clr::enabled = !noColor && AnsiStream::stdoutSupportsANSI();
        if(cfgSpecs.contains(String("list"))) return listTranslateConfig();
        if(parser.argCount() < 2) {
                std::fprintf(stderr, "error: a subcommand and a capture file are required\n\n");
                printUsage(parser);
                return 2;
        }

        const String subcommand = parser.arg(0);
        const String path = parser.arg(1);

        if(subcommand == String("info")) return cmdInfo(path);
        if(subcommand == String("flows")) return cmdFlows(path, sdpPath, ancSpecs, cfgSpecs);
        if(subcommand == String("anc")) return cmdAnc(path, sdpPath, ancSpecs, typeFilters, cfgSpecs, asJson, hexdump);

        std::fprintf(stderr, "error: unknown subcommand '%s'\n\n", subcommand.cstr());
        printUsage(parser);
        return 2;
}
