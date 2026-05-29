/**
 * @file      st2110trafficcalc.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/st2110trafficcalc.h>

#include <promeki/rtppayloadrawvideo.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// True for any of the ST 2110-20 4:2:0 sampling structures.  4:2:0
// pgroups span two image rows, so each emitted SRD covers a row pair
// (§6.2.5) — the packet-count model needs to know this to walk the
// raster in row bands of two.
bool isSampling420(const St2110Sampling &s) {
        return s == St2110Sampling::YCbCr420 || s == St2110Sampling::CLYCbCr420 ||
               s == St2110Sampling::ICtCp420;
}

// Ceiling division for non-negative integers.
int64_t ceilDiv(int64_t num, int64_t den) {
        if (den <= 0) return 0;
        return (num + den - 1) / den;
}

// Resolves the ST 2110-21 §6.3 active-line ratio for a format.  The
// numerator / denominator pair (1080/1125, 487/525, ...) is what the
// standard's gapped-PRS flow model uses, which is not always the
// raster's own active/total geometry: every progressive format
// (720p / 1080p / 2160p / 4320p) shares the fixed 1080/1125 ratio
// (§6.3.2), while interlaced and PsF formats pick the §6.3.3
// Table 1 ratio for their line system.
void resolveActiveLines(const VideoFormat &format, bool progressive,
                        int &activeLines, int &totalLines) {
        if (progressive) {
                // §6.3.2 — fixed for all non-PsF progressive formats.
                activeLines = 1080;
                totalLines = 1125;
                return;
        }
        // §6.3.3 Table 1 — interlaced and PsF, by line system.  The
        // raster height selects the system: 486 → 525-line,
        // 576 → 625-line, everything else → 1125-line.
        const uint32_t h = format.raster().height();
        if (h <= 486) {
                activeLines = 487;
                totalLines = 525;
        } else if (h <= 576) {
                activeLines = 576;
                totalLines = 625;
        } else {
                activeLines = 1080;
                totalLines = 1125;
        }
}

} // namespace

St2110TrafficCalc::PacketModel St2110TrafficCalc::defaultModel() {
        return PacketModel();
}

St2110TrafficCalc::Result
St2110TrafficCalc::compute(const VideoFormat &format, const St2110Sampling &sampling,
                           const St2110Depth &depth, const RtpSenderType &senderType,
                           const PacketModel &model) {
        Result r;
        r.videoFormat = format;
        r.sampling = sampling;
        r.depth = depth;
        r.senderType = senderType;
        r.packetModel = model;

        // ---- Validate inputs --------------------------------------
        if (!format.isValid()) {
                r.error = Error::Invalid;
                return r;
        }
        if (!sampling.isValid() || !depth.isValid()) {
                r.error = Error::Invalid;
                return r;
        }
        if (senderType != RtpSenderType::TypeN && senderType != RtpSenderType::TypeNL &&
            senderType != RtpSenderType::TypeW) {
                // The §7.1 model is only defined for the three concrete
                // sender classes; Auto / Unknown cannot be costed.
                r.error = Error::Invalid;
                return r;
        }

        const St2110Video::Pgroup pg = St2110Video::pgroup(sampling, depth);
        if (pg.octets == 0 || pg.pixels == 0) {
                // (sampling, depth) is not a combination ST 2110-20 defines.
                r.error = Error::FormatMismatch;
                return r;
        }
        r.pgroup = pg;

        // ---- Raster + timing --------------------------------------
        r.width = format.raster().width();
        r.height = format.raster().height();
        r.scanMode = format.videoScanMode();
        r.progressive = (r.scanMode == VideoScanMode::Progressive);
        r.frameRate = format.frameRate();
        r.frameInterval = r.frameRate.frameDuration();
        if (!r.frameInterval.isValid() || r.frameInterval.nanoseconds() <= 0) {
                r.error = Error::Invalid;
                return r;
        }

        resolveActiveLines(format, r.progressive, r.activeLines, r.totalLines);
        r.activeRatio = St2110Tx::activeRatio(r.activeLines, r.totalLines);

        // ---- Packetization model ----------------------------------
        // RTP payload budget = MTU minus IP + UDP + RTP framing.
        const int framing = model.ipHeaderBytes + model.udpHeaderBytes + model.rtpHeaderBytes;
        const int rtpPayloadBudget = model.mtu - framing;
        const int perPacketOverhead =
                static_cast<int>(RtpPayloadRawVideo::ExtSeqSize + RtpPayloadRawVideo::SrdHeaderSize);
        if (rtpPayloadBudget <= perPacketOverhead + static_cast<int>(pg.octets)) {
                // MTU too small to carry the ESN + one SRD header + a
                // single pgroup of sample data.
                r.error = Error::OutOfRange;
                return r;
        }

        // An SRD row band: progressive 4:4:4 / 4:2:2 / Key map one
        // image row per SRD; 4:2:0 pairs two image rows per SRD
        // (§6.2.5).
        r.rowsPerSrd = isSampling420(sampling) ? 2 : 1;
        const int64_t pixelsPerBand = static_cast<int64_t>(r.width) * r.rowsPerSrd;
        const int64_t pgroupsPerBand = ceilDiv(pixelsPerBand, static_cast<int64_t>(pg.pixels));
        r.octetsPerLine = pgroupsPerBand * static_cast<int64_t>(pg.octets);
        const int64_t bands =
                (r.rowsPerSrd == 2) ? ceilDiv(r.height, 2) : static_cast<int64_t>(r.height);
        r.activeOctetsPerFrame = r.octetsPerLine * bands;

        int64_t nPackets = 0;
        if (model.packingMode == St2110PackingMode::Bpm) {
                // Block Packing Mode — fixed-size payloads that are an
                // integer multiple of 180 octets (§6.3.3).  Model the
                // frame as contiguous pgroup data packed into those
                // fixed payloads.
                const size_t bpmPayload =
                        RtpPayloadRawVideo::bpmTargetPayloadSize(static_cast<size_t>(rtpPayloadBudget));
                if (bpmPayload <= static_cast<size_t>(perPacketOverhead) + pg.octets) {
                        r.error = Error::OutOfRange;
                        return r;
                }
                const int64_t usable =
                        (static_cast<int64_t>(bpmPayload - perPacketOverhead) /
                         static_cast<int64_t>(pg.octets)) *
                        static_cast<int64_t>(pg.octets);
                r.payloadDataPerPacket = static_cast<int>(usable);
                nPackets = ceilDiv(r.activeOctetsPerFrame, usable);
                r.packetsPerLine = static_cast<int>(ceilDiv(r.octetsPerLine, usable));
                r.packetSizeBytes = framing + static_cast<int>(bpmPayload);
        } else {
                // General Packing Mode — line-aligned single-SRD
                // packets.  Each row band fragments into whole-pgroup
                // packets.
                const int64_t usable =
                        (static_cast<int64_t>(rtpPayloadBudget - perPacketOverhead) /
                         static_cast<int64_t>(pg.octets)) *
                        static_cast<int64_t>(pg.octets);
                r.payloadDataPerPacket = static_cast<int>(usable);
                r.packetsPerLine = static_cast<int>(ceilDiv(r.octetsPerLine, usable));
                nPackets = static_cast<int64_t>(r.packetsPerLine) * bands;
                const int64_t dataThisPacket = usable < r.octetsPerLine ? usable : r.octetsPerLine;
                r.packetSizeBytes = framing + perPacketOverhead + static_cast<int>(dataThisPacket);
        }
        if (nPackets <= 0) {
                r.error = Error::OutOfRange;
                return r;
        }
        r.packetsPerFrame = static_cast<int>(nPackets);

        // ---- Rates ------------------------------------------------
        const double frameSec = r.frameInterval.toSecondsDouble();
        r.packetRate = static_cast<double>(r.packetsPerFrame) / frameSec;
        r.mediaRateBps = static_cast<double>(r.activeOctetsPerFrame) * 8.0 / frameSec;
        const int64_t wireBytesPerFrame =
                r.activeOctetsPerFrame +
                static_cast<int64_t>(r.packetsPerFrame) * (framing + perPacketOverhead);
        r.wireRateBps = static_cast<double>(wireBytesPerFrame) * 8.0 / frameSec;
        r.beta = St2110Tx::Beta;
        // T_DRAIN = (T_FRAME / N_PACKETS) × (1 / β) — §6.6.1.
        const double tDrainNs = (static_cast<double>(r.frameInterval.nanoseconds()) /
                                 static_cast<double>(r.packetsPerFrame)) /
                                St2110Tx::Beta;
        r.tDrain = Duration::fromNanoseconds(static_cast<int64_t>(tDrainNs));

        // ---- Packet Read Schedule ---------------------------------
        r.trsGapped = St2110Tx::trsGapped(r.packetsPerFrame, r.frameInterval, r.activeRatio);
        r.trsLinear = St2110Tx::trsLinear(r.packetsPerFrame, r.frameInterval);
        // Type N employs the gapped PRS (§7.1.1); NL and W use the
        // linear PRS (§7.1.2 / §7.1.3).
        r.trs = (senderType == RtpSenderType::TypeN) ? r.trsGapped : r.trsLinear;

        // ---- TRO_DEFAULT (delegated, 2022 form) -------------------
        r.troDefault = St2110Tx::troDefault(senderType, r.packetsPerFrame, r.frameInterval,
                                            r.activeRatio, model.mtu);
        r.troffMicroseconds = r.troDefault.isValid() ? r.troDefault.microseconds() : 0;

        // ---- Virtual Receiver Buffer / burst ----------------------
        if (senderType == RtpSenderType::TypeW) {
                r.vrxFull = St2110Tx::vrxFullWideBytes(r.packetsPerFrame, model.mtu, r.frameInterval);
                r.cmax = St2110Tx::cmaxWidePackets(r.packetsPerFrame, r.frameInterval);
        } else {
                r.vrxFull = St2110Tx::vrxFullNarrowBytes(r.packetsPerFrame, model.mtu, r.frameInterval);
                r.cmax = St2110Tx::cmaxNarrowPackets(r.packetsPerFrame, r.activeRatio, r.frameInterval);
        }

        // Derived, informative sender-side timing tolerances.  The
        // standard specifies no numeric jitter range (§7.2.1 leaves
        // receiver tolerance to the implementer); these are the
        // emission-timing windows the §6.6 traffic model bounds.
        // VRX timing window = VRX_FULL × T_RS uses the read schedule
        // T_RS for this sender type (gapped for N, linear for NL/W).
        if (r.trs.isValid()) {
                r.vrxTimingWindow =
                        Duration::fromNanoseconds(r.vrxFull * r.trs.nanoseconds());
        }
        if (r.tDrain.isValid()) {
                r.cmaxBurstWindow = Duration::fromNanoseconds(
                        static_cast<int64_t>(r.cmax) * r.tDrain.nanoseconds());
        }

        // ---- SDP signalling ---------------------------------------
        r.tpParam = St2110Tx::tpFmtpValue(senderType);

        r.error = Error::Ok;
        return r;
}

St2110TrafficCalc::Result
St2110TrafficCalc::compute(const VideoFormat &format, const PixelFormat &pixelFormat,
                           const RtpSenderType &senderType, const PacketModel &model) {
        const St2110Video::PixelFormatBridge bridge = St2110Video::bridgeForPixelFormat(pixelFormat);
        if (!bridge.sampling.isValid() || !bridge.depth.isValid()) {
                Result r;
                r.videoFormat = format;
                r.senderType = senderType;
                r.packetModel = model;
                r.error = Error::FormatMismatch;
                return r;
        }
        return compute(format, bridge.sampling, bridge.depth, senderType, model);
}

String St2110TrafficCalc::Result::toString() const {
        if (!isValid()) {
                return String::sprintf("St2110TrafficCalc: invalid (%s)", error.name().cstr());
        }

        const double trsGappedUs = trsGapped.isValid()
                                           ? static_cast<double>(trsGapped.nanoseconds()) / 1000.0
                                           : 0.0;
        const double trsLinearUs = trsLinear.isValid()
                                           ? static_cast<double>(trsLinear.nanoseconds()) / 1000.0
                                           : 0.0;
        const double troUs =
                troDefault.isValid() ? static_cast<double>(troDefault.nanoseconds()) / 1000.0 : 0.0;
        const double tDrainNs =
                tDrain.isValid() ? static_cast<double>(tDrain.nanoseconds()) : 0.0;
        const double tFrameUs = static_cast<double>(frameInterval.nanoseconds()) / 1000.0;
        const double vrxWinUs = vrxTimingWindow.isValid()
                                        ? static_cast<double>(vrxTimingWindow.nanoseconds()) / 1000.0
                                        : 0.0;
        const double cmaxWinUs = cmaxBurstWindow.isValid()
                                         ? static_cast<double>(cmaxBurstWindow.nanoseconds()) / 1000.0
                                         : 0.0;

        String s;
        s += String::sprintf("SMPTE ST 2110-21 Traffic Shaping\n");
        s += String::sprintf("  Video format       : %s  (%ux%u %s)\n",
                             videoFormat.toString().cstr(), width, height,
                             progressive ? "progressive" : "interlaced/PsF");
        s += String::sprintf("  Sampling / depth   : %s / %s\n", sampling.valueName().cstr(),
                             depth.valueName().cstr());
        s += String::sprintf("  Sender type        : %s  (TP=%s)\n", senderType.valueName().cstr(),
                             tpParam.isEmpty() ? "-" : tpParam.cstr());
        s += String::sprintf("  Packing / MTU      : %s / %d octets\n",
                             packetModel.packingMode.valueName().cstr(), packetModel.mtu);
        s += String::sprintf("\n");
        s += String::sprintf("  T_FRAME            : %.3f us\n", tFrameUs);
        s += String::sprintf("  R_ACTIVE           : %.5f  (%d/%d)\n", activeRatio, activeLines,
                             totalLines);
        s += String::sprintf("  pgroup             : %zu octets / %zu px\n", pgroup.octets,
                             pgroup.pixels);
        s += String::sprintf("  N_PACKETS          : %d  (%d pkt/line x %s)\n", packetsPerFrame,
                             packetsPerLine,
                             rowsPerSrd == 2 ? "row-pairs" : "lines");
        s += String::sprintf("  Payload/packet     : %d octets (typ. packet %d octets)\n",
                             payloadDataPerPacket, packetSizeBytes);
        s += String::sprintf("  R_NOMINAL          : %.1f packets/s\n", packetRate);
        s += String::sprintf("  Media rate         : %.3f Mbit/s\n", mediaRateBps / 1e6);
        s += String::sprintf("  Wire rate (approx) : %.3f Mbit/s\n", wireRateBps / 1e6);
        s += String::sprintf("  beta               : %.2f\n", beta);
        s += String::sprintf("  T_DRAIN            : %.1f ns\n", tDrainNs);
        s += String::sprintf("\n");
        s += String::sprintf("  T_RS (gapped)      : %.4f us\n", trsGappedUs);
        s += String::sprintf("  T_RS (linear)      : %.4f us\n", trsLinearUs);
        s += String::sprintf("  TRO_DEFAULT        : %.3f us  (TROFF=%lld)\n", troUs,
                             static_cast<long long>(troffMicroseconds));
        s += String::sprintf("  VRX_FULL           : %lld packets\n",
                             static_cast<long long>(vrxFull));
        s += String::sprintf("  C_MAX              : %d packets\n", cmax);
        s += String::sprintf("  VRX timing window  : %.3f us  (derived: VRX_FULL x T_RS)\n",
                             vrxWinUs);
        s += String::sprintf("  C_MAX burst window : %.3f us  (derived: C_MAX x T_DRAIN)\n",
                             cmaxWinUs);
        return s;
}

PROMEKI_NAMESPACE_END
