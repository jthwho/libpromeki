/**
 * @file      ntv2routing.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/ntv2routing.h>

#include <ntv2enums.h>
#include <ntv2signalrouter.h>
#include <ntv2utils.h>

PROMEKI_NAMESPACE_BEGIN

namespace Ntv2Routing {

namespace {

        // Convenience for building a Connection from AJA SDK enum values.
        Connection conn(NTV2InputXptID in, NTV2OutputXptID out) {
                return Connection{static_cast<uint32_t>(in), static_cast<uint32_t>(out)};
        }

        // 1-based → NTV2Channel enum cast.  Returns NTV2_CHANNEL1 for
        // out-of-range arguments so a malformed call still yields a
        // deterministic crosspoint id (the caller still gets an empty
        // list when the link standard is invalid).
        NTV2Channel chFromIndex(int idx1Based) {
                if (idx1Based < 1) idx1Based = 1;
                if (idx1Based > NTV2_MAX_NUM_CHANNELS) idx1Based = NTV2_MAX_NUM_CHANNELS;
                return static_cast<NTV2Channel>(idx1Based - 1);
        }

        // Returns true when the helper should bridge the FB / wire
        // colour-family mismatch via on-board CSC.  When false, the
        // caller has either disabled the on-board CSC or the FB and
        // wire are already in the same family.
        bool wantsCsc(const Config &c) {
                return c.allowOnBoardCsc && (c.framebufferRgb != c.signalRgb);
        }

        // Returns the SDI-input crosspoint that hands the wire bytes
        // off to the routing fabric.  Handles the YUV vs RGB choice
        // so the CSC + non-CSC paths share one helper.
        NTV2OutputXptID sdiInputXpt(int sdi1Based, bool wireRgb) {
                const NTV2Channel sdiCh = chFromIndex(sdi1Based);
                const NTV2InputSource src = ::NTV2ChannelToInputSource(sdiCh, NTV2_IOKINDS_SDI);
                return ::GetInputSourceOutputXpt(src, /*DS2*/ false, /*RGB*/ wireRgb, /*quadrant*/ 0);
        }

        // -----------------------------------------------------------
        // Source-mode helpers
        // -----------------------------------------------------------

        ConnectionList sourceSingleLink(const Config &c) {
                ConnectionList out;
                const NTV2Channel fbCh  = chFromIndex(c.channelStart);
                const NTV2OutputXptID wireXpt = sdiInputXpt(c.portStart, c.signalRgb);
                if (wantsCsc(c)) {
                        // CSC channel 1:1-matches the framestore channel.
                        const NTV2Channel cscCh = fbCh;
                        // CSC in <- SDI wire (wire colour family).
                        out.pushToBack(conn(::GetCSCInputXptFromChannel(cscCh, /*KeyInput*/ false),
                                            wireXpt));
                        // FB in <- CSC out (framestore colour family).
                        out.pushToBack(conn(::GetFrameStoreInputXptFromChannel(fbCh, false),
                                            ::GetCSCOutputXptFromChannel(cscCh, /*key*/ false,
                                                                         /*rgb*/ c.framebufferRgb)));
                } else {
                        out.pushToBack(conn(::GetFrameStoreInputXptFromChannel(fbCh, false), wireXpt));
                }
                return out;
        }

        ConnectionList sourceQuadSquares(const Config &c) {
                ConnectionList out;
                for (int path = 0; path < 4; ++path) {
                        const NTV2Channel    fbCh = chFromIndex(c.channelStart + path);
                        const NTV2OutputXptID wireXpt = sdiInputXpt(c.portStart + path, c.signalRgb);
                        if (wantsCsc(c)) {
                                const NTV2Channel cscCh = fbCh;
                                out.pushToBack(conn(::GetCSCInputXptFromChannel(cscCh, false),
                                                    wireXpt));
                                out.pushToBack(conn(::GetFrameStoreInputXptFromChannel(fbCh, false),
                                                    ::GetCSCOutputXptFromChannel(cscCh, false,
                                                                                 c.framebufferRgb)));
                        } else {
                                out.pushToBack(conn(::GetFrameStoreInputXptFromChannel(fbCh, false),
                                                    wireXpt));
                        }
                }
                return out;
        }

        // Quad-link 2SI source.  When CSC is in play the per-quadrant
        // CSC sits between the SDI input and the TSI mux: SDIIn ->
        // CSC -> TSIMux -> FrameStore.  Otherwise the SDIIn feeds the
        // TSI mux directly.
        ConnectionList sourceQuadTsi(const Config &c) {
                ConnectionList out;
                for (int path = 0; path < 4; ++path) {
                        const NTV2Channel muxCh  = chFromIndex(c.channelStart + path / 2);
                        const NTV2Channel fbCh   = chFromIndex(c.channelStart + path / 2);
                        const bool        linkB  = (path & 1) != 0;
                        const NTV2OutputXptID wire = sdiInputXpt(c.portStart + path, c.signalRgb);
                        NTV2OutputXptID muxFeed = wire;
                        if (wantsCsc(c)) {
                                const NTV2Channel cscCh = chFromIndex(c.channelStart + path);
                                out.pushToBack(conn(::GetCSCInputXptFromChannel(cscCh, false), wire));
                                muxFeed = ::GetCSCOutputXptFromChannel(cscCh, false, c.framebufferRgb);
                        }
                        out.pushToBack(conn(::GetTSIMuxInputXptFromChannel(muxCh, linkB), muxFeed));
                        out.pushToBack(conn(::GetFrameStoreInputXptFromChannel(fbCh, /*Binput*/ linkB),
                                            ::GetTSIMuxOutputXptFromChannel(muxCh, linkB,
                                                                            /*RGB*/ c.framebufferRgb)));
                }
                return out;
        }

        ConnectionList source12g(const Config &c) {
                if (c.can12gRouting) return sourceSingleLink(c);
                return sourceQuadTsi(c);
        }

        // -----------------------------------------------------------
        // Sink-mode helpers — inverse of the above.
        // -----------------------------------------------------------

        // Returns the SDI-output input crosspoint (where the wire
        // leaves the routing fabric).
        NTV2InputXptID sdiOutputInXpt(int sdi1Based) {
                return ::GetSDIOutputInputXpt(chFromIndex(sdi1Based), /*DS2*/ false);
        }

        // Returns the framestore-output crosspoint (where the
        // framestore feeds the routing fabric).  Sink-side CSC
        // expects 2SI (Is425=true) when the FB feeds a TSI mux,
        // false otherwise.
        NTV2OutputXptID fbOutputXpt(int fb1Based, bool rgb, bool is425) {
                return ::GetFrameStoreOutputXptFromChannel(chFromIndex(fb1Based), rgb, is425);
        }

        ConnectionList sinkSingleLink(const Config &c) {
                ConnectionList out;
                const NTV2InputXptID  sdiOut = sdiOutputInXpt(c.portStart);
                const NTV2OutputXptID fbOut  = fbOutputXpt(c.channelStart, c.framebufferRgb,
                                                           /*Is425*/ false);
                if (wantsCsc(c)) {
                        const NTV2Channel cscCh = chFromIndex(c.channelStart);
                        // CSC in <- FB out (framestore colour family).
                        out.pushToBack(conn(::GetCSCInputXptFromChannel(cscCh, false), fbOut));
                        // SDIOut in <- CSC out (wire colour family).
                        out.pushToBack(conn(sdiOut, ::GetCSCOutputXptFromChannel(cscCh, false,
                                                                                 c.signalRgb)));
                } else {
                        out.pushToBack(conn(sdiOut, fbOut));
                }
                return out;
        }

        ConnectionList sinkQuadSquares(const Config &c) {
                ConnectionList out;
                for (int path = 0; path < 4; ++path) {
                        const NTV2InputXptID  sdiOut = sdiOutputInXpt(c.portStart + path);
                        const NTV2OutputXptID fbOut  = fbOutputXpt(c.channelStart + path,
                                                                   c.framebufferRgb, false);
                        if (wantsCsc(c)) {
                                const NTV2Channel cscCh = chFromIndex(c.channelStart + path);
                                out.pushToBack(conn(::GetCSCInputXptFromChannel(cscCh, false), fbOut));
                                out.pushToBack(conn(sdiOut, ::GetCSCOutputXptFromChannel(
                                                                    cscCh, false, c.signalRgb)));
                        } else {
                                out.pushToBack(conn(sdiOut, fbOut));
                        }
                }
                return out;
        }

        ConnectionList sinkQuadTsi(const Config &c) {
                ConnectionList out;
                for (int path = 0; path < 4; ++path) {
                        const NTV2Channel muxCh  = chFromIndex(c.channelStart + path / 2);
                        const NTV2Channel fbCh   = chFromIndex(c.channelStart + path / 2);
                        const bool        linkB  = (path & 1) != 0;
                        const NTV2InputXptID sdiOut = sdiOutputInXpt(c.portStart + path);
                        out.pushToBack(conn(::GetTSIMuxInputXptFromChannel(muxCh, linkB),
                                            ::GetFrameStoreOutputXptFromChannel(fbCh, c.framebufferRgb,
                                                                                /*Is425*/ true)));
                        const NTV2OutputXptID muxOut =
                                ::GetTSIMuxOutputXptFromChannel(muxCh, linkB, c.framebufferRgb);
                        if (wantsCsc(c)) {
                                const NTV2Channel cscCh = chFromIndex(c.channelStart + path);
                                out.pushToBack(conn(::GetCSCInputXptFromChannel(cscCh, false), muxOut));
                                out.pushToBack(conn(sdiOut,
                                                    ::GetCSCOutputXptFromChannel(cscCh, false,
                                                                                 c.signalRgb)));
                        } else {
                                out.pushToBack(conn(sdiOut, muxOut));
                        }
                }
                return out;
        }

        ConnectionList sink12g(const Config &c) {
                if (c.can12gRouting) return sinkSingleLink(c);
                return sinkQuadTsi(c);
        }

        bool isPlainSingleLink(const SdiLinkStandard &s) {
                return s == SdiLinkStandard::SL_HD || s == SdiLinkStandard::SL_3GA
                       || s == SdiLinkStandard::SL_3GB || s == SdiLinkStandard::SL_SD
                       || s == SdiLinkStandard::SL_6G;
        }

        ConnectionList dispatchSource(const Config &c) {
                if (c.standard == SdiLinkStandard::Auto || isPlainSingleLink(c.standard)) {
                        return sourceSingleLink(c);
                }
                if (c.standard == SdiLinkStandard::SL_12G)    return source12g(c);
                if (c.standard == SdiLinkStandard::QL_3G_SQD) return sourceQuadSquares(c);
                if (c.standard == SdiLinkStandard::QL_3G_2SI) return sourceQuadTsi(c);
                return ConnectionList();
        }

        ConnectionList dispatchSinkOne(const Config &c) {
                if (c.standard == SdiLinkStandard::Auto || isPlainSingleLink(c.standard)) {
                        return sinkSingleLink(c);
                }
                if (c.standard == SdiLinkStandard::SL_12G)    return sink12g(c);
                if (c.standard == SdiLinkStandard::QL_3G_SQD) return sinkQuadSquares(c);
                if (c.standard == SdiLinkStandard::QL_3G_2SI) return sinkQuadTsi(c);
                return ConnectionList();
        }

        // Walks the primary group + any mirror groups, dispatching
        // one routing pass per group with portStart pointing into the
        // appropriate destination.  All passes share the same
        // framestore (channelStart), so the connection lists fan out
        // from one source crosspoint to N input crosspoints — that's
        // exactly the AJA crosspoint fabric's native fanout semantic.
        ConnectionList dispatchSink(const Config &c) {
                ConnectionList primary = dispatchSinkOne(c);
                if (primary.isEmpty()) return primary;
                if (c.mirrorPortStarts.isEmpty()) return primary;
                ConnectionList out = primary;
                for (size_t i = 0; i < c.mirrorPortStarts.size(); ++i) {
                        Config mirror   = c;
                        mirror.portStart = c.mirrorPortStarts.at(i);
                        mirror.mirrorPortStarts.clear(); // avoid infinite recursion
                        const ConnectionList add = dispatchSinkOne(mirror);
                        for (size_t j = 0; j < add.size(); ++j) out.pushToBack(add.at(j));
                }
                return out;
        }

} // namespace

ConnectionList sdiInputConnections(const Config &cfg) { return dispatchSource(cfg); }

ConnectionList sdiOutputConnections(const Config &cfg) { return dispatchSink(cfg); }

ConnectionList sdiInputConnections(const SdiLinkStandard &standard, int channel, int startPort,
                                   bool can12gRouting, bool framebufferRgb) {
        Config c;
        c.standard         = standard;
        c.channelStart     = channel;
        c.portStart        = startPort;
        c.can12gRouting    = can12gRouting;
        c.framebufferRgb   = framebufferRgb;
        c.signalRgb        = false;
        c.allowOnBoardCsc  = true;
        return dispatchSource(c);
}

ConnectionList sdiOutputConnections(const SdiLinkStandard &standard, int channel, int startPort,
                                    bool can12gRouting, bool framebufferRgb) {
        Config c;
        c.standard         = standard;
        c.channelStart     = channel;
        c.portStart        = startPort;
        c.can12gRouting    = can12gRouting;
        c.framebufferRgb   = framebufferRgb;
        c.signalRgb        = false;
        c.allowOnBoardCsc  = true;
        return dispatchSink(c);
}

bool needsTsi(const SdiLinkStandard &standard, bool can12gRouting) {
        if (standard == SdiLinkStandard::QL_3G_2SI) return true;
        if (standard == SdiLinkStandard::SL_12G && !can12gRouting) return true;
        if (standard == SdiLinkStandard::SL_12G && can12gRouting) return true;
        return false;
}

bool needsSquares(const SdiLinkStandard &standard) {
        return standard == SdiLinkStandard::QL_3G_SQD;
}

} // namespace Ntv2Routing

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
