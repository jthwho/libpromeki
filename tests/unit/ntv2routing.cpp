/**
 * @file      ntv2routing.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NTV2

#include <doctest/doctest.h>
#include <promeki/enums.h>
#include <promeki/list.h>
#include <promeki/ntv2routing.h>

#include <ntv2enums.h>
#include <ntv2signalrouter.h>
#include <ntv2utils.h>

using namespace promeki;

namespace {

        // Convenience to confirm a connection list contains a pair
        // built from AJA's own crosspoint helpers — anchors the
        // test against the SDK's idea of which IDs go together
        // rather than re-deriving the bit layouts in the test.
        bool containsConnection(const Ntv2Routing::ConnectionList &conns, uint32_t in, uint32_t out) {
                for (size_t i = 0; i < conns.size(); ++i) {
                        if (conns[i].input == in && conns[i].output == out) return true;
                }
                return false;
        }

        // Helpers that mirror AJA's GetXxx... functions returning the
        // raw uint32_t crosspoint id for comparison.
        uint32_t fbInputXpt(int ch1Based, bool binput = false) {
                return static_cast<uint32_t>(::GetFrameStoreInputXptFromChannel(
                        static_cast<NTV2Channel>(ch1Based - 1), binput));
        }
        uint32_t fbOutputXpt(int ch1Based, bool rgb = false, bool is425 = false) {
                return static_cast<uint32_t>(::GetFrameStoreOutputXptFromChannel(
                        static_cast<NTV2Channel>(ch1Based - 1), rgb, is425));
        }
        uint32_t inputSourceXpt(int sdi1Based) {
                NTV2InputSource src =
                        ::NTV2ChannelToInputSource(static_cast<NTV2Channel>(sdi1Based - 1), NTV2_IOKINDS_SDI);
                return static_cast<uint32_t>(::GetInputSourceOutputXpt(src, false, false, 0));
        }
        uint32_t sdiOutputInputXpt(int sdi1Based) {
                return static_cast<uint32_t>(::GetSDIOutputInputXpt(
                        static_cast<NTV2Channel>(sdi1Based - 1), false));
        }
        uint32_t tsiMuxInputXpt(int mux1Based, bool linkB) {
                return static_cast<uint32_t>(::GetTSIMuxInputXptFromChannel(
                        static_cast<NTV2Channel>(mux1Based - 1), linkB));
        }
        uint32_t tsiMuxOutputXpt(int mux1Based, bool linkB, bool rgb = false) {
                return static_cast<uint32_t>(::GetTSIMuxOutputXptFromChannel(
                        static_cast<NTV2Channel>(mux1Based - 1), linkB, rgb));
        }

} // namespace

TEST_CASE("Ntv2Routing: single-link source produces one FB <- SDIIn connection") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(
                SdiLinkStandard::SL_3GA, /*channel=*/1, /*startPort=*/1,
                /*can12g=*/true, /*fbRgb=*/false);
        CHECK(c.size() == 1);
        CHECK(containsConnection(c, fbInputXpt(1), inputSourceXpt(1)));
}

TEST_CASE("Ntv2Routing: SL_HD at channel 3 uses framestore 3 and SDI port 3") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(
                SdiLinkStandard::SL_HD, /*channel=*/3, /*startPort=*/3,
                /*can12g=*/true, /*fbRgb=*/false);
        CHECK(c.size() == 1);
        CHECK(containsConnection(c, fbInputXpt(3), inputSourceXpt(3)));
        // And not the channel-1 crosspoints — proves the offset
        // propagates through the helper.
        CHECK(!containsConnection(c, fbInputXpt(1), inputSourceXpt(1)));
}

TEST_CASE("Ntv2Routing: Auto link standard defaults to single-link") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(
                SdiLinkStandard::Auto, /*channel=*/1, /*startPort=*/1,
                /*can12g=*/false, /*fbRgb=*/false);
        CHECK(c.size() == 1);
        CHECK(containsConnection(c, fbInputXpt(1), inputSourceXpt(1)));
}

TEST_CASE("Ntv2Routing: SL_12G follows the single-link path on 12G-routing cards") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(
                SdiLinkStandard::SL_12G, /*channel=*/1, /*startPort=*/1,
                /*can12g=*/true, /*fbRgb=*/false);
        CHECK(c.size() == 1);
        CHECK(containsConnection(c, fbInputXpt(1), inputSourceXpt(1)));
}

TEST_CASE("Ntv2Routing: SL_12G falls back to 2SI on cards without 12G routing") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(
                SdiLinkStandard::SL_12G, /*channel=*/1, /*startPort=*/1,
                /*can12g=*/false, /*fbRgb=*/false);
        // 4 quadrant paths × 2 connections (TSIMux <- SDIIn,
        // FB <- TSIMux) = 8 connections total.
        CHECK(c.size() == 8);
}

TEST_CASE("Ntv2Routing: QL_3G_SQD produces four parallel FB <- SDIIn pairs") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(
                SdiLinkStandard::QL_3G_SQD, /*channel=*/1, /*startPort=*/1,
                /*can12g=*/false, /*fbRgb=*/false);
        CHECK(c.size() == 4);
        for (int q = 0; q < 4; ++q) {
                CHECK(containsConnection(c, fbInputXpt(1 + q), inputSourceXpt(1 + q)));
        }
}

TEST_CASE("Ntv2Routing: QL_3G_2SI produces TSIMux + FB connections per quadrant") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(
                SdiLinkStandard::QL_3G_2SI, /*channel=*/1, /*startPort=*/1,
                /*can12g=*/false, /*fbRgb=*/false);
        CHECK(c.size() == 8);
        // Quadrant 0: TSIMux1 LinkA <- SDIIn1, FB1 (Binput=false) <- TSIMux1 LinkA
        CHECK(containsConnection(c, tsiMuxInputXpt(1, false), inputSourceXpt(1)));
        CHECK(containsConnection(c, fbInputXpt(1, /*Binput=*/false), tsiMuxOutputXpt(1, false)));
        // Quadrant 3: TSIMux2 LinkB <- SDIIn4, FB2 (Binput=true) <- TSIMux2 LinkB
        CHECK(containsConnection(c, tsiMuxInputXpt(2, true), inputSourceXpt(4)));
        CHECK(containsConnection(c, fbInputXpt(2, /*Binput=*/true), tsiMuxOutputXpt(2, true)));
}

TEST_CASE("Ntv2Routing: sink-side single-link maps SDIOut <- FB") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiOutputConnections(
                SdiLinkStandard::SL_3GA, /*channel=*/2, /*startPort=*/2,
                /*can12g=*/true, /*fbRgb=*/false);
        CHECK(c.size() == 1);
        CHECK(containsConnection(c, sdiOutputInputXpt(2), fbOutputXpt(2)));
}

TEST_CASE("Ntv2Routing: sink-side QL_3G_2SI produces four SDIOut paths through TSI mux") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiOutputConnections(
                SdiLinkStandard::QL_3G_2SI, /*channel=*/1, /*startPort=*/1,
                /*can12g=*/false, /*fbRgb=*/false);
        CHECK(c.size() == 8);
        CHECK(containsConnection(c, sdiOutputInputXpt(1), tsiMuxOutputXpt(1, false)));
        CHECK(containsConnection(c, sdiOutputInputXpt(4), tsiMuxOutputXpt(2, true)));
}

TEST_CASE("Ntv2Routing: dual-link returns an empty list (not yet implemented)") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(
                SdiLinkStandard::DL_3G, /*channel=*/1, /*startPort=*/1,
                /*can12g=*/false, /*fbRgb=*/false);
        CHECK(c.isEmpty());
}

TEST_CASE("Ntv2Routing: SL_24G is reserved and returns an empty list") {
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(
                SdiLinkStandard::SL_24G, /*channel=*/1, /*startPort=*/1,
                /*can12g=*/true, /*fbRgb=*/false);
        CHECK(c.isEmpty());
}

TEST_CASE("Ntv2Routing: source single-link inserts CSC when FB is RGB and wire is YUV") {
        Ntv2Routing::Config cfg;
        cfg.standard         = SdiLinkStandard::SL_3GA;
        cfg.channelStart     = 1;
        cfg.portStart        = 1;
        cfg.framebufferRgb   = true;
        cfg.signalRgb        = false;
        cfg.allowOnBoardCsc = true;

        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(cfg);
        // Two connections: SDIIn -> CSC, CSC -> FB.
        CHECK(c.size() == 2);
        // CSC-in (Y) receives the SDIIn (YCbCr wire) output crosspoint.
        CHECK(containsConnection(c, static_cast<uint32_t>(::GetCSCInputXptFromChannel(NTV2_CHANNEL1, false)),
                                 inputSourceXpt(1)));
        // FB-in receives the CSC's RGB output crosspoint.
        CHECK(containsConnection(c, fbInputXpt(1),
                                 static_cast<uint32_t>(::GetCSCOutputXptFromChannel(NTV2_CHANNEL1, false,
                                                                                    /*rgb=*/true))));
}

TEST_CASE("Ntv2Routing: sink single-link inserts CSC when FB is RGB and wire is YUV") {
        Ntv2Routing::Config cfg;
        cfg.standard         = SdiLinkStandard::SL_3GA;
        cfg.channelStart     = 1;
        cfg.portStart        = 1;
        cfg.framebufferRgb   = true;
        cfg.signalRgb        = false;
        cfg.allowOnBoardCsc = true;

        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiOutputConnections(cfg);
        // Two connections: FB -> CSC, CSC -> SDIOut.
        CHECK(c.size() == 2);
        // CSC-in receives the FB's RGB output crosspoint.
        CHECK(containsConnection(c,
                                 static_cast<uint32_t>(::GetCSCInputXptFromChannel(NTV2_CHANNEL1, false)),
                                 fbOutputXpt(1, /*rgb=*/true)));
        // SDIOut-in receives the CSC's YCbCr output crosspoint.
        CHECK(containsConnection(c, sdiOutputInputXpt(1),
                                 static_cast<uint32_t>(::GetCSCOutputXptFromChannel(NTV2_CHANNEL1, false,
                                                                                    /*rgb=*/false))));
}

TEST_CASE("Ntv2Routing: CSC stays out of the path when allowOnBoardCsc is false") {
        // Same RGB-FB / YUV-wire mismatch as the prior case, but with
        // CSC disabled — the helper still emits a connection list (one
        // FB <- SDIIn pair) without the CSC pair, leaving any colour-
        // family mismatch for the user / planner to resolve.
        Ntv2Routing::Config cfg;
        cfg.standard         = SdiLinkStandard::SL_3GA;
        cfg.channelStart     = 1;
        cfg.portStart        = 1;
        cfg.framebufferRgb   = true;
        cfg.signalRgb        = false;
        cfg.allowOnBoardCsc = false;

        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(cfg);
        CHECK(c.size() == 1);
        CHECK(containsConnection(c, fbInputXpt(1), inputSourceXpt(1)));
}

TEST_CASE("Ntv2Routing: same-family routing never inserts CSC even when allowOnBoardCsc is true") {
        Ntv2Routing::Config cfg;
        cfg.standard         = SdiLinkStandard::SL_3GA;
        cfg.channelStart     = 1;
        cfg.portStart        = 1;
        cfg.framebufferRgb   = false;
        cfg.signalRgb        = false;
        cfg.allowOnBoardCsc = true;

        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(cfg);
        CHECK(c.size() == 1);
        CHECK(containsConnection(c, fbInputXpt(1), inputSourceXpt(1)));
}

TEST_CASE("Ntv2Routing: QL_3G_SQD with CSC inserts four per-quadrant CSCs") {
        Ntv2Routing::Config cfg;
        cfg.standard         = SdiLinkStandard::QL_3G_SQD;
        cfg.channelStart     = 1;
        cfg.portStart        = 1;
        cfg.framebufferRgb   = true;
        cfg.signalRgb        = false;
        cfg.allowOnBoardCsc = true;

        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiOutputConnections(cfg);
        // Each of four quadrants: FB-out -> CSC-in, SDIOut-in <- CSC-out.
        CHECK(c.size() == 8);
        for (int q = 0; q < 4; ++q) {
                const auto cscCh = static_cast<NTV2Channel>(q);
                CHECK(containsConnection(c, static_cast<uint32_t>(::GetCSCInputXptFromChannel(cscCh, false)),
                                         fbOutputXpt(1 + q, /*rgb=*/true)));
                CHECK(containsConnection(c, sdiOutputInputXpt(1 + q),
                                         static_cast<uint32_t>(::GetCSCOutputXptFromChannel(cscCh, false, false))));
        }
}

TEST_CASE("Ntv2Routing: single-link sink with mirrorPortStarts fans one FB to several SDI outs") {
        Ntv2Routing::Config cfg;
        cfg.standard         = SdiLinkStandard::SL_3GA;
        cfg.channelStart     = 1;
        cfg.portStart        = 1;
        cfg.mirrorPortStarts = {2, 3};

        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiOutputConnections(cfg);
        // Primary + 2 mirrors = 3 connections, each from the same
        // framestore-output crosspoint into a distinct SDIOut input.
        CHECK(c.size() == 3);
        const uint32_t fbOut = fbOutputXpt(1);
        CHECK(containsConnection(c, sdiOutputInputXpt(1), fbOut));
        CHECK(containsConnection(c, sdiOutputInputXpt(2), fbOut));
        CHECK(containsConnection(c, sdiOutputInputXpt(3), fbOut));
}

TEST_CASE("Ntv2Routing: empty mirrorPortStarts list is the same as the no-fanout case") {
        Ntv2Routing::Config cfg;
        cfg.standard         = SdiLinkStandard::SL_3GA;
        cfg.channelStart     = 1;
        cfg.portStart        = 1;
        // mirrorPortStarts intentionally empty.

        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiOutputConnections(cfg);
        CHECK(c.size() == 1);
        CHECK(containsConnection(c, sdiOutputInputXpt(1), fbOutputXpt(1)));
}

TEST_CASE("Ntv2Routing: source dispatch ignores mirrorPortStarts (input fanout is meaningless)") {
        Ntv2Routing::Config cfg;
        cfg.standard         = SdiLinkStandard::SL_3GA;
        cfg.channelStart     = 1;
        cfg.portStart        = 1;
        cfg.mirrorPortStarts = {2, 3};

        // Source side: one SDIIn feeds the framestore; mirroring an
        // input doesn't make sense, so the helper drops the
        // mirrorPortStarts and emits just the primary's connections.
        Ntv2Routing::ConnectionList c = Ntv2Routing::sdiInputConnections(cfg);
        CHECK(c.size() == 1);
        CHECK(containsConnection(c, fbInputXpt(1), inputSourceXpt(1)));
}

TEST_CASE("Ntv2Routing: needsTsi/needsSquares reflect the link standard") {
        // 2SI always needs TSI mode programmed.
        CHECK(Ntv2Routing::needsTsi(SdiLinkStandard::QL_3G_2SI, /*can12g=*/false) == true);
        CHECK(Ntv2Routing::needsTsi(SdiLinkStandard::QL_3G_2SI, /*can12g=*/true) == true);
        // 12G needs TSI either way — 12g-routing cards still need
        // the framestore in TSI mode.
        CHECK(Ntv2Routing::needsTsi(SdiLinkStandard::SL_12G, /*can12g=*/false) == true);
        CHECK(Ntv2Routing::needsTsi(SdiLinkStandard::SL_12G, /*can12g=*/true) == true);
        // Quad Squares wants the dedicated bit, not TSI.
        CHECK(Ntv2Routing::needsTsi(SdiLinkStandard::QL_3G_SQD, /*can12g=*/false) == false);
        CHECK(Ntv2Routing::needsSquares(SdiLinkStandard::QL_3G_SQD) == true);
        // Single-link wants neither.
        CHECK(Ntv2Routing::needsTsi(SdiLinkStandard::SL_3GA, /*can12g=*/true) == false);
        CHECK(Ntv2Routing::needsSquares(SdiLinkStandard::SL_3GA) == false);
}

#endif // PROMEKI_ENABLE_NTV2
