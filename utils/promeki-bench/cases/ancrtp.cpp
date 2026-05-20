/**
 * @file      ancrtp.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * RFC 8331 / ST 2110-40 ancillary-data hot-path benchmark cases for
 * promeki-bench.  The cases here drive @ref RtpPayloadAnc::packAncFrame
 * and @ref RtpPayloadAnc::unpackAncPackets over a representative
 * per-frame ANC load and report the per-channel throughput.
 *
 * ### BenchParams keys read by this suite
 *
 * | Key                | Type | Default | Description                                   |
 * |--------------------|------|---------|-----------------------------------------------|
 * | `ancrtp.packets`   | int  | 20      | ANC packets per simulated video frame         |
 * | `ancrtp.mtu`       | int  | 1200    | Max RTP payload size (forces fragmentation)   |
 *
 * The default load (~20 packets/frame) is a representative ST 2110-40
 * typical: a handful of captions / HDR / ATC / AFD per HD60 frame.
 * Each iteration of the hot loop is one frame's worth of pack-or-unpack;
 * @ref BenchmarkState::itemsProcessed reports total packets so the
 * runner's items/sec column scales linearly with channels-per-core
 * when divided by the per-channel packet rate.
 */

#include "cases.h"
#include "../benchparams.h"

#include <promeki/config.h>

#if PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_NETWORK

#include <cstdint>

#include <promeki/ancformat.h>
#include <promeki/ancpacket.h>
#include <promeki/benchmarkrunner.h>
#include <promeki/list.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/st291packet.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

        namespace {

                // Returns one synthetic ST 291 packet of the given format
                // with a fixed-shape UDW body the size depends on the
                // chosen format.  The packets that ride this benchmark
                // are wire-shape-only — we don't run real codecs since the
                // measurement target is the RFC 8331 carriage layer.
                St291Packet makePacket(AncFormat::ID id, uint16_t line, size_t udwBytes,
                                       uint8_t seed) {
                        List<uint16_t> udw;
                        udw.reserve(udwBytes);
                        for (size_t i = 0; i < udwBytes; ++i) {
                                udw.pushToBack(static_cast<uint16_t>((seed + i * 17u) & 0xFFu));
                        }
                        return St291Packet::build(AncFormat(id), udw, line,
                                                  /*hOffset*/ St291Packet::UnspecifiedHOffset,
                                                  /*fieldB*/ false,
                                                  /*cBit*/ false,
                                                  /*streamNum*/ 0);
                }

                // Builds the per-frame ANC load.  The mix is
                // representative of a typical ST 2110-40 production
                // stream: a handful of captions + HDR + AFD + ATC, plus
                // padding entries to reach the target packet count.
                AncPacket::List buildFrameLoad(size_t targetCount) {
                        // Each tuple is (format, UDW byte count, line).
                        // Sizes match the canonical wire shape of each
                        // codec (CEA-708 CDP ~30 bytes, AFD = 8 UDWs,
                        // ATC = 16 UDWs, HDR static = 28 UDWs).
                        struct PktShape {
                                        AncFormat::ID id;
                                        size_t        udwBytes;
                                        uint16_t      line;
                        };
                        const PktShape mix[] = {
                                {AncFormat::Cea708, 30, 9},
                                {AncFormat::Afd, 8, 11},
                                {AncFormat::AtcLtc, 16, 13},
                                {AncFormat::HdrStatic2086, 28, 14},
                                {AncFormat::Cea708, 30, 15},
                                {AncFormat::Afd, 8, 16},
                                {AncFormat::AtcVitc1, 16, 17},
                                {AncFormat::HdrStatic2086, 28, 18},
                                {AncFormat::Cea708, 30, 19},
                                {AncFormat::Cea708, 30, 20},
                        };
                        const size_t mixCount = sizeof(mix) / sizeof(mix[0]);

                        AncPacket::List out;
                        out.reserve(targetCount);
                        for (size_t i = 0; i < targetCount; ++i) {
                                const PktShape &s = mix[i % mixCount];
                                // Vary line per cycle so the §C11 sort
                                // does non-trivial work and a packet
                                // never lands on the exact same line twice.
                                const uint16_t line = static_cast<uint16_t>(s.line + (i / mixCount) * 30u);
                                St291Packet    p = makePacket(s.id, line, s.udwBytes,
                                                              static_cast<uint8_t>(i * 11u));
                                out.pushToBack(p.packet());
                        }
                        return out;
                }

                int paramPackets() {
                        return benchParams().getInt(String("ancrtp.packets"), 20);
                }

                int paramMtu() {
                        return benchParams().getInt(String("ancrtp.mtu"), 1200);
                }

                // ------------------------------------------------------------------
                // Pack benchmark — TX hot path
                // ------------------------------------------------------------------
                void benchPack(BenchmarkState &state) {
                        const size_t  packetsPerFrame = static_cast<size_t>(paramPackets());
                        const size_t  mtu = static_cast<size_t>(paramMtu());
                        RtpPayloadAnc payload;
                        payload.setMaxPayloadSize(mtu);

                        AncPacket::List frame = buildFrameLoad(packetsPerFrame);

                        // Warm the path once; first iteration pulls in
                        // the AncFormat registry, the RtpPacket pool, etc.
                        (void)payload.packAncFrame(frame, 0u);

                        for (auto _ : state) {
                                (void)_;
                                RtpPacket::List rtp = payload.packAncFrame(frame, 0u);
                                if (rtp.isEmpty()) {
                                        // Defensive: should not happen on
                                        // a non-empty input.
                                        state.setCounter(String("invalid"), 1.0);
                                }
                        }

                        state.setItemsProcessed(state.iterations() * packetsPerFrame);
                        state.setCounter(String("packets_per_frame"),
                                         static_cast<double>(packetsPerFrame));
                        state.setCounter(String("mtu_bytes"), static_cast<double>(mtu));
                        // Channels at 60 fps = items_per_sec / (packets * 60).
                        state.setCounter(String("hd60_channels"),
                                         0.0);  // placeholder; runner derives from
                                                // items_per_sec — left here so the
                                                // JSON schema always has the field.
                        state.setLabel(String("pack ") + String::number((int)packetsPerFrame) +
                                       " pkts/frame, MTU=" + String::number((int)mtu));
                }

                // ------------------------------------------------------------------
                // Unpack benchmark — RX hot path
                // ------------------------------------------------------------------
                void benchUnpack(BenchmarkState &state) {
                        const size_t  packetsPerFrame = static_cast<size_t>(paramPackets());
                        const size_t  mtu = static_cast<size_t>(paramMtu());
                        RtpPayloadAnc payload;
                        payload.setMaxPayloadSize(mtu);

                        AncPacket::List frame = buildFrameLoad(packetsPerFrame);
                        // Build the on-wire RTP frame once; the hot loop
                        // exercises only the unpack path.
                        RtpPacket::List rtpFrame = payload.packAncFrame(frame, 0u);
                        if (rtpFrame.isEmpty()) {
                                state.setCounter(String("invalid"), 1.0);
                                for (auto _ : state) (void)_;
                                return;
                        }

                        AncPacket::List out;
                        // Warmup.
                        (void)payload.unpackAncPackets(rtpFrame, out);
                        out.clear();

                        for (auto _ : state) {
                                (void)_;
                                out.clear();
                                Error err = payload.unpackAncPackets(rtpFrame, out);
                                if (err.isError() || out.size() != packetsPerFrame) {
                                        state.setCounter(String("invalid"), 1.0);
                                }
                        }

                        state.setItemsProcessed(state.iterations() * packetsPerFrame);
                        state.setCounter(String("packets_per_frame"),
                                         static_cast<double>(packetsPerFrame));
                        state.setCounter(String("mtu_bytes"), static_cast<double>(mtu));
                        state.setCounter(String("rtp_packets_per_frame"),
                                         static_cast<double>(rtpFrame.size()));
                        state.setLabel(String("unpack ") + String::number((int)packetsPerFrame) +
                                       " pkts/frame, MTU=" + String::number((int)mtu));
                }

                // ------------------------------------------------------------------
                // Roundtrip benchmark — full TX→RX hot path (what a relay /
                // transcode pipeline pays per frame end to end)
                // ------------------------------------------------------------------
                void benchRoundtrip(BenchmarkState &state) {
                        const size_t  packetsPerFrame = static_cast<size_t>(paramPackets());
                        const size_t  mtu = static_cast<size_t>(paramMtu());
                        RtpPayloadAnc payload;
                        payload.setMaxPayloadSize(mtu);

                        AncPacket::List frame = buildFrameLoad(packetsPerFrame);

                        // Warmup.
                        {
                                RtpPacket::List w = payload.packAncFrame(frame, 0u);
                                AncPacket::List o;
                                (void)payload.unpackAncPackets(w, o);
                        }

                        AncPacket::List out;
                        for (auto _ : state) {
                                (void)_;
                                RtpPacket::List rtp = payload.packAncFrame(frame, 0u);
                                out.clear();
                                Error err = payload.unpackAncPackets(rtp, out);
                                if (err.isError() || out.size() != packetsPerFrame) {
                                        state.setCounter(String("invalid"), 1.0);
                                }
                        }

                        state.setItemsProcessed(state.iterations() * packetsPerFrame);
                        state.setCounter(String("packets_per_frame"),
                                         static_cast<double>(packetsPerFrame));
                        state.setCounter(String("mtu_bytes"), static_cast<double>(mtu));
                        state.setLabel(String("roundtrip ") + String::number((int)packetsPerFrame) +
                                       " pkts/frame, MTU=" + String::number((int)mtu));
                }

        } // namespace

        void registerAncRtpCases() {
                BenchmarkRunner::registerCase(BenchmarkCase(
                        String("ancrtp"), String("pack_hd60"),
                        String("Pack a ~20-packet ST 2110-40 frame load via RtpPayloadAnc::packAncFrame"),
                        benchPack));
                BenchmarkRunner::registerCase(BenchmarkCase(
                        String("ancrtp"), String("unpack_hd60"),
                        String("Unpack an RFC 8331 ANC frame back into AncPacket::List"),
                        benchUnpack));
                BenchmarkRunner::registerCase(BenchmarkCase(
                        String("ancrtp"), String("roundtrip_hd60"),
                        String("Full TX+RX hot path: pack then immediately unpack one frame"),
                        benchRoundtrip));
        }

        String ancRtpParamHelp() {
                return String(
                        "ancrtp suite parameters:\n"
                        "  ancrtp.packets=<int>     ANC packets per simulated frame (default: 20)\n"
                        "  ancrtp.mtu=<int>         Max RTP payload size in bytes (default: 1200)\n"
                        "\n"
                        "  The default load (~20 packets/frame) matches an ST 2110-40 typical HD60\n"
                        "  production stream.  Each measured iteration is one frame's worth of\n"
                        "  pack-or-unpack work; divide items_per_sec by packets/frame to get the\n"
                        "  upper bound on frames/sec/core, and divide that by 60 for HD60 channel\n"
                        "  count at this CPU.\n");
        }

} // namespace benchutil
PROMEKI_NAMESPACE_END

#else // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_NETWORK

PROMEKI_NAMESPACE_BEGIN
namespace benchutil {

        void registerAncRtpCases() {
                // proav or network disabled — nothing to register.
        }

        String ancRtpParamHelp() {
                return String("ancrtp suite parameters: (disabled — built without PROMEKI_ENABLE_PROAV "
                              "or PROMEKI_ENABLE_NETWORK)\n");
        }

} // namespace benchutil
PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_NETWORK
